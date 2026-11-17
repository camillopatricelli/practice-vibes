#include "AppEngine.h"
#include "AppPaths.h"

namespace pv
{

namespace
{
    // Our own attribute in the device-state XML; JUCE ignores unknown attributes.
    const juce::Identifier followAttribute { "pvFollowSystemOutput" };

    // ASIO has no "system default output": each driver is one interface, chosen on purpose.
    const juce::String asioTypeName { "ASIO" };

    juce::AudioIODeviceType* findDeviceType (juce::AudioDeviceManager& manager, const juce::String& name)
    {
        for (auto* type : manager.getAvailableDeviceTypes())
            if (type->getTypeName() == name)
                return type;

        return nullptr;
    }

    juce::String stateWithoutFollow (const juce::XmlElement* xml)
    {
        if (xml == nullptr)
            return {};

        juce::XmlElement copy (*xml);
        copy.removeAttribute (followAttribute);
        return copy.toString();
    }
}

AppEngine::AppEngine()
{
    juce::addDefaultFormatsToManager (formatManager);

    juce::StringArray formatNames;

    for (auto* format : formatManager.getFormats())
        formatNames.add (format->getName());

    juce::Logger::writeToLog ("Plugin formats: " + (formatNames.isEmpty() ? juce::String ("none") : formatNames.joinIntoString (", ")));

    chain = std::make_unique<PluginChain> (deviceManager, formatManager);
    player = chain->addSource (std::make_unique<PlayerProcessor>());
    metronome = chain->addSource (std::make_unique<MetronomeProcessor>());

    if (metronome != nullptr && player != nullptr)
        metronome->setPlayer (player);

    if (auto xml = juce::parseXML (getMetronomeFile()))
        metronome->applySettingsXml (*xml);

    savedMetronomeSettings = metronome->createSettingsXml()->toString();

    drums = chain->addSource (std::make_unique<DrumSequencer> (*metronome));

    if (auto xml = juce::parseXML (getDrumsFile()))
        drums->applyStateXml (*xml);

    savedDrumSettings = drums->createStateXml()->toString();

    tuner = std::make_unique<Tuner>();

    if (auto xml = juce::parseXML (getTunerFile()))
        tuner->fromXml (*xml);

    chain->getInputStage().tap = tuner.get();

    looper = std::make_unique<Looper> (*metronome);
    recorder = std::make_unique<Recorder>();
    looper->afterFxTap = recorder.get();
    player->setSongBlockListener (recorder.get());
    chain->setLooperNodes (looper->createBeforeFxNode(), looper->createAfterFxNode());
    chain->setBlockListener (this);
    chain->setPlayHead (&playHead);
    startTimerHz (2);
    catalogue = std::make_unique<PluginCatalogue> (formatManager);
    footswitch = std::make_unique<Footswitch>();

    if (auto xml = juce::parseXML (getFootswitchFile()))
        footswitch->fromXml (*xml);

    midi = std::make_unique<MidiMonitor> (deviceManager);

    footswitch->onChange = [this] { footswitch->toXml()->writeTo (getFootswitchFile()); };
    footswitch->perform = [this] (FootswitchAction action, double pressSeconds) { performFootswitchAction (action, pressSeconds); };

    midi->onMessage = [this] (const juce::MidiMessage& message, const juce::String& source)
    {
        footswitch->handleMidi (message);

        if (onMidiMessage)
            onMidiMessage (message, source);
    };
}

AppEngine::~AppEngine()
{
    stopTimer();
    alive.reset();
    deviceManager.removeChangeListener (this);
    midi.reset();
    footswitch.reset();
    catalogue.reset();
    player = nullptr;
    metronome = nullptr;
    drums = nullptr;
    chain.reset();      // stops audio and deletes the looper's nodes before the looper
    looper.reset();
    recorder.reset();
    deviceManager.closeAudioDevice();
}

void AppEngine::start()
{
    std::weak_ptr<bool> weakAlive = alive;

    juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio, [this, weakAlive] (bool granted)
    {
        if (weakAlive.expired())
            return;

        if (! granted)
            juce::Logger::writeToLog ("Audio input permission was NOT granted: the instrument input will be silent");

        auto savedState = juce::parseXML (getDeviceStateFile());
        juce::String savedAsioDriver;     // set when the saved setup is an ASIO driver

        if (savedState != nullptr)
        {
            followSystemOutput = savedState->getBoolAttribute (followAttribute, false);
            const auto savedType = savedState->getStringAttribute ("deviceType");

            if (savedType == asioTypeName)
            {
                // JUCE would fall back to another ASIO driver (or to no device at all) when the saved
                // one is missing: open the default Windows Audio device instead, and remember the ASIO choice.
                followSystemOutput = false;
                savedAsioDriver = savedState->getStringAttribute ("audioOutputDeviceName");

                if (savedAsioDriver.isEmpty())
                    savedAsioDriver = savedState->getStringAttribute ("audioDeviceName");

                bool driverFound = false;

                if (auto* asio = findDeviceType (deviceManager, asioTypeName))
                {
                    asio->scanForDevices();
                    driverFound = savedAsioDriver.isNotEmpty() && asio->getDeviceNames (false).contains (savedAsioDriver);
                }

                if (! driverFound)
                {
                    juce::Logger::writeToLog ("ASIO driver \"" + savedAsioDriver + "\" not found: using Windows Audio for now");
                    unavailableAsioState = std::move (savedState);
                    followSystemOutput = true;
                }
            }
            else if (followSystemOutput)
            {
                // The saved output name is stale when following: open today's default instead.
                if (auto* type = findDeviceType (deviceManager, savedType))
                {
                    type->scanForDevices();
                    const auto defaultName = type->getDeviceNames (false)[type->getDefaultDeviceIndex (false)];

                    if (defaultName.isNotEmpty())
                        savedState->setAttribute ("audioOutputDeviceName", defaultName);
                }
            }
        }

        auto error = deviceManager.initialise (granted ? 2 : 0, 2, savedState.get(), true);

        if (error.isNotEmpty())
            juce::Logger::writeToLog ("Audio device error: " + error);

        if (unavailableAsioState != nullptr)
        {
            // Keep the MIDI inputs the user had enabled with the ASIO setup.
            for (auto* midi : unavailableAsioState->getChildWithTagNameIterator ("MIDIINPUT"))
                for (const auto& info : juce::MidiInput::getAvailableDevices())
                    if (info.identifier == midi->getStringAttribute ("identifier"))
                        deviceManager.setMidiInputDeviceEnabled (info.identifier, true);

            const auto current = deviceManager.createStateXml();
            fallbackStateAtStart = stateWithoutFollow (current.get());
        }
        else if (savedAsioDriver.isNotEmpty()
                 && (deviceManager.getCurrentAudioDevice() == nullptr
                     || deviceManager.getCurrentAudioDevice()->getName() != savedAsioDriver))
        {
            // The driver is installed but wouldn't open (another app holds it, or the interface was unplugged
            // without the driver noticing), and JUCE fell back to another ASIO driver or to nothing.
            // Not "chosen": the saved state keeps the ASIO setup for the next start.
            juce::Logger::writeToLog ("Could not open the ASIO driver \"" + savedAsioDriver + "\": using Windows Audio for now");
            followSystemOutput = true;

            if (auto* first = deviceManager.getAvailableDeviceTypes().getFirst())
                deviceManager.setCurrentAudioDeviceType (first->getTypeName(), false);
        }

        lastOutputName = deviceManager.getAudioDeviceSetup().outputDeviceName;
        juce::Logger::writeToLog (followSystemOutput ? "Output follows the system default output"
                                                     : "Output fixed to the device chosen in the audio settings");
        deviceManager.addChangeListener (this);

        logAudioDeviceInfo();

        if (auto chainXml = juce::parseXML (getChainFile()))
            chain->restoreFromXml (*chainXml);
    });
}

void AppEngine::saveMetronomeIfChanged()
{
    // The Practice and Looper tabs both edit the metronome: one place notices and saves.
    const auto settings = metronome->createSettingsXml()->toString();

    if (settings != savedMetronomeSettings && settings.length() > 0)
        if (juce::File (getMetronomeFile()).replaceWithText (settings))
            savedMetronomeSettings = settings;

    // Drums: pattern, kit and mix (edited on the Looper tab), saved the same way.
    if (drums != nullptr)
    {
        const auto drumSettings = drums->createStateXml()->toString();

        if (drumSettings != savedDrumSettings && drumSettings.length() > 0)
            if (juce::File (getDrumsFile()).replaceWithText (drumSettings))
                savedDrumSettings = drumSettings;
    }
}

void AppEngine::saveState()
{
    if (onSaveState)
        onSaveState();

    saveMetronomeIfChanged();

    auto deviceXml = deviceManager.createStateXml();

    // Started on Windows Audio because the ASIO driver was missing, and the user hasn't changed anything since.
    if (unavailableAsioState != nullptr && stateWithoutFollow (deviceXml.get()) == fallbackStateAtStart)
        deviceXml = std::make_unique<juce::XmlElement> (*unavailableAsioState);

    if (deviceXml != nullptr)
    {
        if (deviceXml->getStringAttribute ("deviceType") != asioTypeName)
            deviceXml->setAttribute (followAttribute, followSystemOutput);

        deviceXml->writeTo (getDeviceStateFile());
    }

    if (auto chainXml = chain->createStateXml())
        if (chainXml->writeTo (getChainFile()))
            juce::Logger::writeToLog ("Saved chain to " + getChainFile().getFullPathName());
}

juce::String AppEngine::getSystemDefaultOutputName() const
{
    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        if (type->getTypeName() != asioTypeName)
            return type->getDeviceNames (false)[type->getDefaultDeviceIndex (false)];

    return {};
}

// Called (asynchronously) after any device change: the user editing the audio settings,
// a device appearing/disappearing, or the OS default output changing (JUCE's WASAPI type
// reports that as a device-list change, but keeps the old device open).
void AppEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    const auto defaultName = getSystemDefaultOutputName();
    const bool outputChanged = setup.outputDeviceName != lastOutputName;
    lastOutputName = setup.outputDeviceName;

    if (! followSystemOutput)
    {
        // Picking the current default (or JUCE falling back to it after the chosen
        // device was unplugged) goes back to following.
        if (outputChanged && defaultName.isNotEmpty() && setup.outputDeviceName == defaultName)
        {
            followSystemOutput = true;
            juce::Logger::writeToLog ("Output follows the system default output again");
        }

        return;
    }

    if (outputChanged && setup.outputDeviceName != defaultName)
    {
        followSystemOutput = false;
        juce::Logger::writeToLog ("Output fixed to " + setup.outputDeviceName + " (chosen in the audio settings)");
        return;
    }

    if (defaultName.isEmpty() || setup.outputDeviceName == defaultName)
        return;

    juce::Logger::writeToLog ("System default output changed: switching to " + defaultName);
    setup.outputDeviceName = defaultName;
    lastOutputName = defaultName;

    // Not "chosen": the saved state keeps meaning "follow the default".
    const auto error = deviceManager.setAudioDeviceSetup (setup, false);

    if (error.isNotEmpty())
        juce::Logger::writeToLog ("Could not switch output: " + error);
}

void AppEngine::beginBlock (juce::int64 blockStart, int numSamples)
{
    // Audio thread: the bar clock first, then what reads this block's bar lines.
    metronome->advanceClock (blockStart, numSamples);
    playHead.update (*metronome, blockStart);
    looper->beginBlock (blockStart, numSamples);
    recorder->beginBlock (blockStart, numSamples);
}

void AppEngine::performFootswitchAction (FootswitchAction action, double pressSeconds)
{
    using Kind = FootswitchAction::Kind;

    switch (action.kind)
    {
        case Kind::trackPress: looper->pressTrack (action.track, pressSeconds); break;
        case Kind::trackClear: looper->clearTrack (action.track); break;
        case Kind::trackMute:  looper->toggleMute (action.track); break;
        case Kind::undo:       looper->undo(); break;
        case Kind::muteAll:    looper->toggleMuteAll(); break;
        case Kind::clearAll:   looper->clearAll(); break;

        case Kind::metronome:
            if (metronome->isRunning())
                metronome->stop();
            else
                metronome->start();
            break;

        case Kind::bypass:
        {
            auto index = getBypassTarget ? getBypassTarget() : -1;

            if (! juce::isPositiveAndBelow (index, chain->getNumPlugins()))
                index = chain->getNumPlugins() > 0 ? 0 : -1;

            if (index >= 0)
                chain->setBypassed (index, ! chain->isBypassed (index));
            break;
        }

        case Kind::tuner:
            if (onToggleTuner)
                onToggleTuner();
            break;

        case Kind::record:
            if (onToggleRecording)
                onToggleRecording();
            break;

        case Kind::stem:
            if (onToggleStem)
                onToggleStem (action.track);
            break;

        case Kind::none:
            break;
    }
}

void AppEngine::saveTunerSettings()
{
    tuner->toXml()->writeTo (getTunerFile());
}

void AppEngine::timerCallback()
{
    int deviceLatency = 0;

    if (auto* device = deviceManager.getCurrentAudioDevice())
        deviceLatency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();

    looper->deviceLatencySamples = deviceLatency;
    looper->chainLatencySamples = chain->getPluginLatencySamples();
    saveMetronomeIfChanged();
}

void AppEngine::logAudioDeviceInfo()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        juce::Logger::writeToLog ("No audio device open");
        return;
    }

    auto describeChannels = [] (const juce::StringArray& names, const juce::BigInteger& active)
    {
        juce::StringArray result;

        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                result.add (names[i]);

        return result.isEmpty() ? juce::String ("none") : result.joinIntoString (", ");
    };

    juce::Logger::writeToLog ("Audio device: " + device->getTypeName() + " / " + device->getName()
                              + " | " + juce::String (device->getCurrentSampleRate(), 0) + " Hz"
                              + " | buffer " + juce::String (device->getCurrentBufferSizeSamples())
                              + " | latency in/out " + juce::String (device->getInputLatencyInSamples())
                              + "/" + juce::String (device->getOutputLatencyInSamples()) + " samples");
    juce::Logger::writeToLog ("  inputs: " + describeChannels (device->getInputChannelNames(), device->getActiveInputChannels()));
    juce::Logger::writeToLog ("  outputs: " + describeChannels (device->getOutputChannelNames(), device->getActiveOutputChannels()));
}

} // namespace pv
