#include "DrumSequencer.h"

namespace pv
{

namespace
{
    struct CallbackTimer final : public juce::Timer
    {
        explicit CallbackTimer (std::function<void()> f) : callback (std::move (f)) {}
        void timerCallback() override   { callback(); }
        std::function<void()> callback;
    };

    size_t liveIndex (int bar, int step, int drum) noexcept
    {
        return (size_t) ((bar * DrumPattern::maxSteps + step) * numDrums + drum);
    }

    const char* kitName (DrumKitType type)  { return type == DrumKitType::electronic ? "electronic" : "acoustic"; }
}

DrumSequencer::DrumSequencer (const MetronomeProcessor& m)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      metronome (m)
{
    for (int d = 0; d < numDrums; ++d)
    {
        drumGains[(size_t) d] = defaultDrumGain ((Drum) d);
        drumMuted[(size_t) d] = false;
    }

    for (auto& cell : liveCells)
        cell = 0;

    setPattern (DrumPatterns::builtIn()[1]);

    // The device's sample rate is known only once audio runs: build the kit then (and again if it changes).
    kitTimer = std::make_unique<CallbackTimer> ([this]
    {
        const auto rate = preparedRate.load();

        if (! loading && rate > 0.0 && (rate != kitRate || loadedType != kitType || currentKit == nullptr))
            loadKit();
    });

    kitTimer->startTimer (300);
}

DrumSequencer::~DrumSequencer()
{
    kitTimer.reset();
    alive.reset();
}

float DrumSequencer::defaultDrumGain (Drum drum)
{
    switch (drum)
    {
        case Drum::kick:      return 1.0f;
        case Drum::snare:     return 0.85f;
        case Drum::closedHat: return 0.5f;
        case Drum::openHat:   return 0.45f;
        case Drum::ride:      return 0.5f;
        case Drum::crash:     return 0.5f;
        case Drum::tomHigh:   return 0.75f;
        case Drum::tomLow:    return 0.75f;
    }

    return 0.7f;
}

//==============================================================================
void DrumSequencer::setPattern (const DrumPattern& p)
{
    pattern = p;

    for (int bar = 0; bar < DrumPattern::maxBars; ++bar)
        for (int step = 0; step < DrumPattern::maxSteps; ++step)
            for (int d = 0; d < numDrums; ++d)
                liveCells[liveIndex (bar, step, d)] = pattern.get (bar, step, (Drum) d);

    liveBeats = pattern.beatsPerBar;
    liveTicks = pattern.ticks;
    liveSwing = pattern.swing;
    liveBars = pattern.bars;
}

void DrumSequencer::setCell (int bar, int step, Drum drum, juce::uint8 level)
{
    pattern.set (bar, step, drum, level);

    if (juce::isPositiveAndBelow (bar, DrumPattern::maxBars) && juce::isPositiveAndBelow (step, DrumPattern::maxSteps))
        liveCells[liveIndex (bar, step, (int) drum)] = pattern.get (bar, step, drum);
}

//==============================================================================
void DrumSequencer::setKitType (DrumKitType type)
{
    kitType = type;     // the timer loads it
}

void DrumSequencer::loadKit()
{
    loading = true;
    const auto type = kitType;
    const auto rate = preparedRate.load();
    std::weak_ptr<bool> weakAlive = alive;

    juce::Thread::launch ([this, type, rate, weakAlive]
    {
        const auto started = juce::Time::getMillisecondCounterHiRes();
        auto kit = std::make_unique<DrumSamples> (loadDrumKit (type, rate));
        const auto ms = juce::Time::getMillisecondCounterHiRes() - started;
        auto shared = std::make_shared<std::unique_ptr<DrumSamples>> (std::move (kit));

        juce::MessageManager::callAsync ([this, type, rate, weakAlive, shared, ms]
        {
            if (weakAlive.expired())
                return;

            loading = false;
            kitRate = rate;
            loadedType = type;

            if (*shared == nullptr || (*shared)->isEmpty())
            {
                juce::Logger::writeToLog (juce::String ("Drums: could not build the ") + kitName (type) + " kit");
                return;
            }

            juce::Logger::writeToLog (juce::String ("Drums: ") + kitName (type) + " kit ready at "
                                      + juce::String (rate, 0) + " Hz in " + juce::String (ms, 0) + " ms");
            kitLoaded (std::move (*shared));
        });
    });
}

void DrumSequencer::kitLoaded (std::unique_ptr<DrumSamples> kit)
{
    previousKit = std::move (currentKit);
    currentKit = std::move (kit);
    activeKit = currentKit.get();
}

void DrumSequencer::loadKitNow (DrumKitType type, double sampleRate)
{
    kitType = loadedType = type;
    kitRate = sampleRate;
    kitLoaded (std::make_unique<DrumSamples> (loadDrumKit (type, sampleRate)));
}

//==============================================================================
std::unique_ptr<juce::XmlElement> DrumSequencer::createStateXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("DRUMS");
    xml->setAttribute ("kit", kitName (kitType));
    xml->setAttribute ("gain", (double) gain.load());

    auto* mix = xml->createNewChildElement ("MIX");

    for (int d = 0; d < numDrums; ++d)
    {
        auto* row = mix->createNewChildElement ("DRUM");
        row->setAttribute ("index", d);
        row->setAttribute ("gain", (double) drumGains[(size_t) d].load());
        row->setAttribute ("muted", drumMuted[(size_t) d].load());
    }

    xml->addChildElement (pattern.toXml().release());
    return xml;
}

void DrumSequencer::applyStateXml (const juce::XmlElement& xml)
{
    setKitType (xml.getStringAttribute ("kit") == "electronic" ? DrumKitType::electronic : DrumKitType::acoustic);
    gain = (float) juce::jlimit (0.0, 1.5, xml.getDoubleAttribute ("gain", 0.8));

    if (auto* mix = xml.getChildByName ("MIX"))
    {
        for (auto* row : mix->getChildWithTagNameIterator ("DRUM"))
        {
            const auto d = row->getIntAttribute ("index", -1);

            if (juce::isPositiveAndBelow (d, numDrums))
            {
                drumGains[(size_t) d] = (float) juce::jlimit (0.0, 1.5, row->getDoubleAttribute ("gain", defaultDrumGain ((Drum) d)));
                drumMuted[(size_t) d] = row->getBoolAttribute ("muted", false);
            }
        }
    }

    if (auto* p = xml.getChildByName ("DRUM_PATTERN"))
        setPattern (DrumPattern::fromXml (*p));
}

//==============================================================================
void DrumSequencer::prepareToPlay (double sampleRate, int)
{
    preparedRate = sampleRate;
}

void DrumSequencer::trigger (const DrumSamples& kit, Drum drum, juce::uint8 level, int offset)
{
    const auto& layers = kit.hits[(size_t) drum];

    if (layers.empty())
        return;

    const auto n = (int) layers.size();
    const auto layer = level == DrumPattern::accent ? n - 1
                     : level == DrumPattern::normal ? (n >= 3 ? n - 2 : n - 1)
                                                    : 0;
    const auto& roundRobins = layers[(size_t) layer];
    auto& counter = roundRobin[(size_t) drum];
    const auto* sample = &roundRobins[(size_t) (counter++ % (int) roundRobins.size())];

    // A closed hi-hat stops a ringing open one, as on a real hi-hat.
    if (drum == Drum::closedHat)
        for (auto& v : voices)
            if (v.sample != nullptr && v.drum == (int) Drum::openHat && v.fadeStep == 0.0f)
                v.fadeStep = 1.0f / (float) (0.015 * getSampleRate());

    auto* voice = &voices[0];

    for (auto& v : voices)
    {
        if (v.sample == nullptr)
        {
            voice = &v;
            break;
        }

        if (v.position > voice->position)
            voice = &v;     // none free: steal the oldest
    }

    const auto levelGain = level == DrumPattern::accent ? 1.0f : level == DrumPattern::normal ? 0.9f : 0.75f;
    *voice = { sample, 0, offset, levelGain * drumGains[(size_t) drum].load(), 1.0f, 0.0f, (int) drum };
}

void DrumSequencer::scheduleBar (const DrumSamples& kit, double barStart, double beatSamples, int clockBeats,
                                 juce::int64 barIndex, juce::int64 blockStart, int numSamples)
{
    const auto bars = juce::jlimit (1, DrumPattern::maxBars, liveBars.load());
    const auto ticks = juce::jlimit (2, DrumPattern::maxTicks, liveTicks.load());
    const auto beats = juce::jmin (juce::jlimit (1, DrumPattern::maxBeats, liveBeats.load()), clockBeats);
    const auto swing = liveSwing.load();
    const auto bar = (int) (((barIndex % bars) + bars) % bars);
    const auto blockEnd = blockStart + numSamples;

    for (int step = 0; step < beats * ticks; ++step)
    {
        const auto t = barStart + (step / ticks + MetronomeProcessor::tickFraction (step % ticks, ticks, swing)) * beatSamples;
        const auto sample = (juce::int64) std::ceil (t - 1.0e-9);

        if (sample < blockStart || sample >= blockEnd)
            continue;

        playingBar = bar;
        playingStep = step;

        for (int d = 0; d < numDrums; ++d)
        {
            const auto level = liveCells[liveIndex (bar, step, d)].load();

            if (level != DrumPattern::off && ! drumMuted[(size_t) d].load())
                trigger (kit, (Drum) d, level, (int) (sample - blockStart));
        }
    }
}

void DrumSequencer::render (juce::AudioBuffer<float>& buffer)
{
    const auto numSamples = buffer.getNumSamples();
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    for (auto& v : voices)
    {
        if (v.sample == nullptr)
            continue;

        const auto* inL = v.sample->getReadPointer (0);
        const auto* inR = v.sample->getReadPointer (v.sample->getNumChannels() > 1 ? 1 : 0);
        const auto length = v.sample->getNumSamples();

        for (int i = v.startOffset; i < numSamples && v.position < length; ++i, ++v.position)
        {
            const auto g = v.gain * v.fade;
            left[i] += inL[v.position] * g;
            right[i] += inR[v.position] * g;

            if (v.fadeStep > 0.0f && (v.fade -= v.fadeStep) <= 0.0f)
            {
                v.position = length;
                break;
            }
        }

        v.startOffset = 0;

        if (v.position >= length)
            v.sample = nullptr;
    }
}

void DrumSequencer::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (buffer.getNumChannels() < 2)
        return;

    const auto* kit = activeKit.load();

    if (kit != playingKit)
    {
        for (auto& v : voices)
            v.sample = nullptr;     // their audio belongs to the old kit

        playingKit = kit;
    }

    const auto playing = enabled.load() && metronome.isClockRunning() && kit != nullptr;

    if (playing)
    {
        const auto blockStart = metronome.getClockBlockStart();
        const auto barStart = metronome.getClockBarStart();
        const auto beatSamples = metronome.getClockBeatSamples();
        const auto index = metronome.getClockBarIndex();

        // advanceClock() has already moved to the bar holding the end of this block: the block may
        // also hold the end of the previous bar.
        const auto previousBeats = metronome.getClockPreviousBarBeats();
        const auto previousBeatSamples = metronome.getClockPreviousBeatSamples();
        scheduleBar (*kit, barStart - previousBeats * previousBeatSamples, previousBeatSamples, previousBeats, index - 1,
                     blockStart, buffer.getNumSamples());
        scheduleBar (*kit, barStart, beatSamples, metronome.getClockBeatsPerBar(), index, blockStart, buffer.getNumSamples());
    }
    else
    {
        playingBar = -1;
        playingStep = -1;

        // Switched off: fade out what still rings (the metronome stopping lets it ring).
        if (! enabled.load())
            for (auto& v : voices)
                if (v.sample != nullptr && v.fadeStep == 0.0f)
                    v.fadeStep = 1.0f / (float) (0.03 * getSampleRate());
    }

    wasPlaying = playing;
    render (buffer);

    const auto g = gain.load();
    buffer.applyGainRamp (0, buffer.getNumSamples(), lastGain, g);
    lastGain = g;
}

} // namespace pv
