#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumKit.h"
#include "DrumPattern.h"
#include "MetronomeProcessor.h"

namespace pv
{

/** The built-in drum machine: plays a DrumPattern on the metronome's bar clock (same tempo, same
    bar lines, starts and stops with it). The pattern restarts with the looper's first loop, since
    the clock's bar count does (MetronomeProcessor::getClockBarIndex()).

    Pattern and mix live in atomics, so edits are heard at once and the audio thread never locks.
    The kit (audio) is built on a background thread and swapped in. A source in the chain's graph:
    it is mixed into the master, so the looper doesn't record it.
*/
class DrumSequencer final : public juce::AudioProcessor
{
public:
    explicit DrumSequencer (const MetronomeProcessor&);
    ~DrumSequencer() override;

    // Settings (UI -> audio)
    std::atomic<bool> enabled { false };
    std::atomic<float> gain { 0.8f };
    std::array<std::atomic<float>, numDrums> drumGains;
    std::array<std::atomic<bool>, numDrums> drumMuted;

    /** Message thread. Copies the pattern for the audio thread (cells change one by one). */
    void setPattern (const DrumPattern&);
    const DrumPattern& getPattern() const noexcept          { return pattern; }
    void setCell (int bar, int step, Drum, juce::uint8 level);

    /** Message thread: loads the kit for the device's sample rate in the background. */
    void setKitType (DrumKitType);
    DrumKitType getKitType() const noexcept                 { return kitType; }
    bool isKitReady() const noexcept                        { return activeKit.load() != nullptr; }

    static float defaultDrumGain (Drum);

    /** For the UI: the step just played (bar of the pattern, step in the bar), -1 when silent. */
    int getPlayingBar() const noexcept                      { return playingBar.load(); }
    int getPlayingStep() const noexcept                     { return playingStep.load(); }

    /** The pattern, kit and mix, for drums.xml and saved loop sessions. Message thread. */
    std::unique_ptr<juce::XmlElement> createStateXml() const;
    void applyStateXml (const juce::XmlElement&);

    //==============================================================================
    const juce::String getName() const override { return "Drums"; }
    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override            {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    double getTailLengthSeconds() const override                { return 0.0; }
    bool acceptsMidi() const override                           { return false; }
    bool producesMidi() const override                          { return false; }
    bool hasEditor() const override                             { return false; }
    juce::AudioProcessorEditor* createEditor() override         { return nullptr; }
    int getNumPrograms() override                               { return 1; }
    int getCurrentProgram() override                            { return 0; }
    void setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override            { return {}; }
    void changeProgramName (int, const juce::String&) override  {}
    void getStateInformation (juce::MemoryBlock&) override      {}
    void setStateInformation (const void*, int) override        {}

    /** Tests: builds the kit right now on this thread. */
    void loadKitNow (DrumKitType, double sampleRate);

private:
    struct Voice
    {
        const juce::AudioBuffer<float>* sample = nullptr;
        int position = 0, startOffset = 0;
        float gain = 1.0f, fade = 1.0f, fadeStep = 0.0f;
        int drum = 0;
    };

    void loadKit();
    void kitLoaded (std::unique_ptr<DrumSamples>);
    void trigger (const DrumSamples&, Drum, juce::uint8 level, int offset);
    void scheduleBar (const DrumSamples&, double barStart, double beatSamples, int clockBeats, juce::int64 barIndex,
                      juce::int64 blockStart, int numSamples);
    void render (juce::AudioBuffer<float>&);

    const MetronomeProcessor& metronome;

    // Message-thread copy, and its mirror for the audio thread.
    DrumPattern pattern;
    std::array<std::atomic<juce::uint8>, DrumPattern::maxBars * DrumPattern::maxSteps * numDrums> liveCells;
    std::atomic<int> liveBeats { 4 }, liveTicks { 4 }, liveBars { 1 };
    std::atomic<double> liveSwing { 0.5 };

    // Kits: owned on the message thread; the audio thread only reads activeKit. A replaced kit is kept
    // until the next swap, by then the audio thread has long moved on.
    DrumKitType kitType = DrumKitType::acoustic;
    std::unique_ptr<DrumSamples> currentKit, previousKit;
    std::atomic<DrumSamples*> activeKit { nullptr };
    std::atomic<double> preparedRate { 0.0 };
    double kitRate = 0.0;
    DrumKitType loadedType = DrumKitType::acoustic;
    bool loading = false;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    // Audio thread
    const DrumSamples* playingKit = nullptr;
    std::array<Voice, 48> voices;
    std::array<int, numDrums> roundRobin {};
    bool wasPlaying = false;
    float lastGain = 0.8f;
    std::atomic<int> playingBar { -1 }, playingStep { -1 };

    std::unique_ptr<juce::Timer> kitTimer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumSequencer)
};

} // namespace pv
