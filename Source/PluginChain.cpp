#include "PluginChain.h"

namespace pv
{

using UpdateKind = juce::AudioProcessorGraph::UpdateKind;
using NodeID = juce::AudioProcessorGraph::NodeID;
using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

namespace
{
    std::pair<double, int> currentRateAndBlockSize (juce::AudioDeviceManager& dm)
    {
        if (auto* device = dm.getCurrentAudioDevice())
            return { device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples() };

        return { 48000.0, 512 };
    }

    /** Ask the plugin for stereo in/out on its main buses; keep its default if it refuses. */
    void configureBuses (juce::AudioPluginInstance& plugin)
    {
        plugin.enableAllBuses();

        auto layout = plugin.getBusesLayout();

        if (! layout.inputBuses.isEmpty())
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();

        if (! layout.outputBuses.isEmpty())
            layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();

        if (plugin.checkBusesLayoutSupported (layout))
            plugin.setBusesLayout (layout);
    }
}

//==============================================================================
PluginChain::PluginChain (juce::AudioDeviceManager& dm, juce::AudioPluginFormatManager& fm)
    : deviceManager (dm), formatManager (fm)
{
    midiInNode      = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::midiInputNode));
    audioInNode     = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioInputNode));
    audioOutNode    = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioOutputNode));
    inputStageNode  = graph.addNode (std::make_unique<GainMeterProcessor> ("Input stage", 1));
    outputStageNode = graph.addNode (std::make_unique<GainMeterProcessor> ("Instrument output", 2));
    masterNode      = graph.addNode (std::make_unique<GainMeterProcessor> ("Master", 2));

    // Start muted: on an iPad the built-in mic + speaker would otherwise howl.
    getOutputStage().muted = true;

    player.setProcessor (&graph);
    deviceManager.addAudioCallback (&deviceCallback);
    deviceManager.addChangeListener (this);

    rebuildConnections();
}

PluginChain::~PluginChain()
{
    alive.reset();
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&deviceCallback);
    player.setProcessor (nullptr);
    slots.clear();
    sources.clear();
    midiInNode = nullptr;
    looperBeforeNode = nullptr;
    looperAfterNode = nullptr;
    graph.clear();
}

//==============================================================================
juce::String PluginChain::getPluginName (int index) const
{
    if (juce::isPositiveAndBelow (index, getNumPlugins()))
        return slots[(size_t) index].description.name;

    return {};
}

juce::AudioPluginInstance* PluginChain::getPlugin (int index) const
{
    if (juce::isPositiveAndBelow (index, getNumPlugins()))
        return dynamic_cast<juce::AudioPluginInstance*> (slots[(size_t) index].node->getProcessor());

    return nullptr;
}

bool PluginChain::isInstrument (int index) const
{
    return juce::isPositiveAndBelow (index, getNumPlugins()) && slots[(size_t) index].instrument;
}

bool PluginChain::isInstrumentPlugin (const juce::AudioPluginInstance& plugin)
{
    return plugin.getPluginDescription().isInstrument
        || (plugin.acceptsMidi() && plugin.getMainBusNumInputChannels() == 0 && plugin.getMainBusNumOutputChannels() > 0);
}

void PluginChain::sendInstrumentMidi (const juce::MidiMessage& message)
{
    if (! instrumentTakesMidi.load() || ! audioRunning.load())
        return;

    // The collector places messages in the block by their timestamp (same clock as MIDI input).
    auto copy = message;
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    if (copy.getTimeStamp() <= 0.0 || copy.getTimeStamp() > now)
        copy.setTimeStamp (now);

    player.getMidiMessageCollector().addMessageToQueue (copy);
}

bool PluginChain::isBypassed (int index) const
{
    return juce::isPositiveAndBelow (index, getNumPlugins()) && slots[(size_t) index].node->isBypassed();
}

void PluginChain::setBypassed (int index, bool shouldBeBypassed)
{
    if (! juce::isPositiveAndBelow (index, getNumPlugins()))
        return;

    slots[(size_t) index].node->setBypassed (shouldBeBypassed);
    juce::Logger::writeToLog ((shouldBeBypassed ? "Bypassed " : "Enabled ") + getPluginName (index));
    notifyChange();
}

GainMeterProcessor& PluginChain::getInputStage() const
{
    return *static_cast<GainMeterProcessor*> (inputStageNode->getProcessor());
}

GainMeterProcessor& PluginChain::getOutputStage() const
{
    return *static_cast<GainMeterProcessor*> (outputStageNode->getProcessor());
}

GainMeterProcessor& PluginChain::getMasterStage() const
{
    return *static_cast<GainMeterProcessor*> (masterNode->getProcessor());
}

//==============================================================================
void PluginChain::loadPlugin (const juce::PluginDescription& description, NodeCallback callback)
{
    const auto [sampleRate, blockSize] = currentRateAndBlockSize (deviceManager);
    std::weak_ptr<bool> weakAlive = alive;

    juce::Logger::writeToLog ("Loading " + description.name + " (" + description.pluginFormatName + ")...");

    formatManager.createPluginInstanceAsync (description, sampleRate, blockSize,
        [this, weakAlive, cb = std::move (callback)] (std::unique_ptr<juce::AudioPluginInstance> instance,
                                                      const juce::String& error)
        {
            if (weakAlive.expired())
                return;

            if (instance == nullptr)
            {
                cb (nullptr, error.isNotEmpty() ? error : juce::String ("unknown error"));
                return;
            }

            configureBuses (*instance);


            auto fullDescription = instance->getPluginDescription();
            const auto ins = instance->getMainBusNumInputChannels();
            const auto outs = instance->getMainBusNumOutputChannels();
            const auto latency = instance->getLatencySamples();

            auto node = graph.addNode (std::move (instance), std::nullopt, UpdateKind::none);

            if (node == nullptr)
            {
                cb (nullptr, "the audio graph refused the plugin");
                return;
            }

            juce::Logger::writeToLog ("Loaded " + fullDescription.name
                                      + " [in " + juce::String (ins) + " ch, out " + juce::String (outs)
                                      + " ch, latency " + juce::String (latency) + " samples]");
            cb (node, {});
        });
}

void PluginChain::addPlugin (const juce::PluginDescription& description, LoadCallback onDone)
{
    loadPlugin (description, [this, name = description.name, done = std::move (onDone)] (NodePtr node, const juce::String& error)
    {
        if (node == nullptr)
            juce::Logger::writeToLog ("Could not load " + name + ": " + error);
        else
            addSlot (node);

        notifyChange();

        if (done)
            done (node == nullptr ? (error.isNotEmpty() ? error : juce::String ("failed")) : juce::String());
    });
}

void PluginChain::addPluginInstance (std::unique_ptr<juce::AudioPluginInstance> instance)
{
    if (instance == nullptr)
        return;

    configureBuses (*instance);

    if (auto node = graph.addNode (std::move (instance), std::nullopt, UpdateKind::none))
        addSlot (node);

    notifyChange();
}

void PluginChain::prepareForTests (int numDeviceIns, int numDeviceOuts, double sampleRate, int blockSize)
{
    deviceManager.removeAudioCallback (&deviceCallback);    // nothing else may drive the graph
    player.setProcessor (nullptr);
    graph.setPlayConfigDetails (numDeviceIns, numDeviceOuts, sampleRate, blockSize);
    graph.prepareToPlay (sampleRate, blockSize);
    rebuildConnections();
}

void PluginChain::processForTests (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    graph.processBlock (buffer, midi);
}

void PluginChain::addSlot (NodePtr node)
{
    auto* plugin = dynamic_cast<juce::AudioPluginInstance*> (node->getProcessor());
    Slot slot { node, plugin != nullptr ? plugin->getPluginDescription() : juce::PluginDescription(),
                plugin != nullptr && isInstrumentPlugin (*plugin) };

    if (slot.instrument)
    {
        if (hasInstrument())
        {
            juce::Logger::writeToLog ("Instrument " + slots.front().description.name + " replaced by " + slot.description.name);
            const auto oldID = slots.front().node->nodeID;
            slots.erase (slots.begin());
            graph.removeNode (oldID, UpdateKind::none);
        }

        slots.insert (slots.begin(), slot);
        lastAddedIndex = 0;
    }
    else
    {
        slots.push_back (slot);
        lastAddedIndex = getNumPlugins() - 1;
    }

    rebuildConnections();
}

void PluginChain::removePlugin (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPlugins()))
        return;

    const auto nodeID = slots[(size_t) index].node->nodeID;
    juce::Logger::writeToLog ("Removed " + getPluginName (index));

    slots.erase (slots.begin() + index);
    graph.removeNode (nodeID, UpdateKind::none);
    rebuildConnections();
    notifyChange();
}

void PluginChain::movePlugin (int index, int delta)
{
    const auto newIndex = index + delta;

    if (! juce::isPositiveAndBelow (index, getNumPlugins()) || ! juce::isPositiveAndBelow (newIndex, getNumPlugins())
        || isInstrument (index) || isInstrument (newIndex))
        return;     // the instrument stays on top

    std::swap (slots[(size_t) index], slots[(size_t) newIndex]);
    rebuildConnections();
    notifyChange();
}

void PluginChain::clear()
{
    for (auto& slot : slots)
        graph.removeNode (slot.node->nodeID, UpdateKind::none);

    slots.clear();
    rebuildConnections();
    notifyChange();
}

void PluginChain::setLooperNodes (std::unique_ptr<juce::AudioProcessor> beforeFx, std::unique_ptr<juce::AudioProcessor> afterFx)
{
    looperBeforeNode = graph.addNode (std::move (beforeFx), std::nullopt, UpdateKind::none);
    looperAfterNode = graph.addNode (std::move (afterFx), std::nullopt, UpdateKind::none);
    rebuildConnections();
}

int PluginChain::getPluginLatencySamples() const
{
    int total = 0;

    for (auto& slot : slots)
        if (! slot.instrument)
            total += juce::jmax (0, slot.node->getProcessor()->getLatencySamples());

    return total;
}

void PluginChain::setInputChannel (int channel)
{
    inputChannel = juce::jmax (0, channel);
    rebuildConnections();
}

//==============================================================================
void PluginChain::rebuildConnections()
{
    // Make sure the graph's I/O nodes reflect the current device's channel counts.
    graph.rebuild();

    for (auto& connection : graph.getConnections())
        graph.removeConnection (connection, UpdateKind::none);

    auto connect = [this] (NodeID source, int sourceChannel, NodeID dest, int destChannel)
    {
        graph.addConnection ({ { source, sourceChannel }, { dest, destChannel } }, UpdateKind::none);
    };

    const auto numDeviceIns = audioInNode->getProcessor()->getTotalNumOutputChannels();
    const auto numDeviceOuts = audioOutNode->getProcessor()->getTotalNumInputChannels();

    if (numDeviceIns > 0)
        connect (audioInNode->nodeID, juce::jlimit (0, numDeviceIns - 1, inputChannel), inputStageNode->nodeID, 0);

    auto previous = inputStageNode->nodeID;
    auto previousChannels = 1;

    if (looperBeforeNode != nullptr)
    {
        connect (inputStageNode->nodeID, 0, looperBeforeNode->nodeID, 0);
        previous = looperBeforeNode->nodeID;
    }

    // What feeds the next stage: the audio input, plus the instrument's sound (the graph sums them).
    std::vector<std::pair<NodeID, int>> heads { { previous, previousChannels } };
    auto connectHeads = [&] (NodeID dest, int numChannels)
    {
        for (int c = 0; c < numChannels; ++c)
            for (auto& [node, channels] : heads)
                connect (node, juce::jmin (c, channels - 1), dest, c);
    };

    auto takesMidi = false;

    for (auto& slot : slots)
    {
        auto* processor = slot.node->getProcessor();
        const auto ins = processor->getMainBusNumInputChannels();
        const auto outs = processor->getMainBusNumOutputChannels();

        if (slot.instrument)
        {
            if (processor->acceptsMidi())
            {
                connect (midiInNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex,
                         slot.node->nodeID, juce::AudioProcessorGraph::midiChannelIndex);
                takesMidi = true;
            }

            if (outs > 0)
                heads.push_back ({ slot.node->nodeID, outs });

            continue;
        }

        if (outs <= 0)
            continue; // nothing to pass on (e.g. a MIDI-only plugin)

        connectHeads (slot.node->nodeID, ins);
        heads = { { slot.node->nodeID, outs } };
    }

    instrumentTakesMidi = takesMidi;
    connectHeads (outputStageNode->nodeID, 2);

    for (int c = 0; c < 2; ++c)
        connect (outputStageNode->nodeID, c, masterNode->nodeID, c);

    if (looperAfterNode != nullptr)
    {
        // Recorded before the instrument output stage, so loops record even with monitoring off.
        connectHeads (looperAfterNode->nodeID, 2);

        for (int c = 0; c < 2; ++c)
            connect (looperAfterNode->nodeID, c, masterNode->nodeID, c);
    }

    for (auto& source : sources)
    {
        const auto outs = source->getProcessor()->getMainBusNumOutputChannels();

        for (int c = 0; c < 2 && outs > 0; ++c)
            connect (source->nodeID, juce::jmin (c, outs - 1), masterNode->nodeID, c);
    }

    for (int c = 0; c < juce::jmin (2, numDeviceOuts); ++c)
        connect (masterNode->nodeID, c, audioOutNode->nodeID, c);

    graph.rebuild();
}

void PluginChain::notifyChange()
{
    if (onChange)
        onChange();
}

void PluginChain::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The audio device changed (channels, sample rate...): re-wire.
    rebuildConnections();
    notifyChange();
}

//==============================================================================
std::unique_ptr<juce::XmlElement> PluginChain::createStateXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("PRACTICE_VIBES_CHAIN");
    xml->setAttribute ("version", 1);
    xml->setAttribute ("inputChannel", inputChannel);
    xml->setAttribute ("inputGain", (double) getInputStage().gain.load());
    xml->setAttribute ("outputGain", (double) getOutputStage().gain.load());

    for (auto& slot : slots)
    {
        auto* element = xml->createNewChildElement ("SLOT");
        element->setAttribute ("bypassed", slot.node->isBypassed());

        juce::MemoryBlock state;
        slot.node->getProcessor()->getStateInformation (state);
        element->setAttribute ("state", state.toBase64Encoding());

        if (auto description = slot.description.createXml())
            element->addChildElement (description.release());
    }

    return xml;
}

void PluginChain::restoreFromXml (const juce::XmlElement& xml, std::function<void()> onDone)
{
    if (! xml.hasTagName ("PRACTICE_VIBES_CHAIN"))
    {
        if (onDone)
            onDone();

        return;
    }

    clear();

    inputChannel = juce::jmax (0, xml.getIntAttribute ("inputChannel", 0));
    getInputStage().gain = (float) xml.getDoubleAttribute ("inputGain", 1.0);
    getOutputStage().gain = (float) xml.getDoubleAttribute ("outputGain", 1.0);

    auto pending = std::make_shared<std::vector<PendingSlot>>();

    for (auto* element : xml.getChildWithTagNameIterator ("SLOT"))
    {
        PendingSlot slot;

        auto* description = element->getChildByName ("PLUGIN");

        if (description == nullptr || ! slot.description.loadFromXml (*description))
            continue;

        slot.state.fromBase64Encoding (element->getStringAttribute ("state"));
        slot.bypassed = element->getBoolAttribute ("bypassed");
        pending->push_back (std::move (slot));
    }

    juce::Logger::writeToLog ("Restoring chain with " + juce::String ((int) pending->size()) + " plugin(s)");

    rebuildConnections();
    notifyChange();
    restoreNext (pending, 0, std::move (onDone));
}

void PluginChain::restoreNext (std::shared_ptr<std::vector<PendingSlot>> pending, size_t index, std::function<void()> onDone)
{
    if (index >= pending->size())
    {
        notifyChange();

        if (onDone)
            onDone();

        return;
    }

    loadPlugin ((*pending)[index].description, [this, pending, index, onDone] (NodePtr node, const juce::String& error)
    {
        auto& entry = (*pending)[index];

        if (node != nullptr)
        {
            addSlot (node);

            if (entry.state.getSize() > 0)
                node->getProcessor()->setStateInformation (entry.state.getData(), (int) entry.state.getSize());

            node->setBypassed (entry.bypassed);
        }
        else
        {
            juce::Logger::writeToLog ("Could not restore " + entry.description.name + ": " + error);
        }

        notifyChange();
        restoreNext (pending, index + 1, onDone);
    });
}

} // namespace pv
