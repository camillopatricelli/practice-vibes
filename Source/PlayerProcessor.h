#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include "Stems.h"
#include "Take.h"

namespace pv
{

/** A decoded song, held in memory as stereo float. */
struct LoadedSong
{
    juce::AudioBuffer<float> audio;     // always 2 channels
    double sampleRate = 44100.0;
    juce::String name;

    /** Waveform cache: min/max of the mono mix per bucket, at several resolutions
        (finest first, each level 4x coarser), so any zoom level draws quickly and sharply.
    */
    struct PeakLevel
    {
        int samplesPerBucket = 0;
        std::vector<float> mins, maxs;
    };

    std::vector<PeakLevel> peakLevels;

    double getLengthSeconds() const     { return audio.getNumSamples() / sampleRate; }

    /** Builds peakLevels from the audio. */
    void buildPeaks();
};

/** Plays a song with independent speed and pitch (Signalsmith Stretch), and A-B looping.

    Lives in the audio graph as a stereo source. Control methods are called from
    the message thread; parameters are handed to the audio thread via atomics.
*/
class PlayerProcessor final : public juce::AudioProcessor
{
public:
    PlayerProcessor();
    ~PlayerProcessor() override;

    //==============================================================================
    using LoadCallback = std::function<void (const juce::String& errorOrEmpty)>;

    /** Decodes the file on a background thread, then makes it the current song. */
    void loadAsync (const juce::URL& file, LoadCallback onDone);

    /** Makes an already-decoded song current (message thread). */
    void setSong (std::shared_ptr<const LoadedSong>);

    std::shared_ptr<const LoadedSong> getSong() const  { return song; }
    bool hasSong() const                                { return song != nullptr; }

    void play();
    void pause();
    void stop();                                        // pause and go back to the start (or loop start)
    bool isPlaying() const noexcept                     { return playing.load(); }

    void seekToSeconds (double seconds);
    double getPositionSeconds() const noexcept          { return positionSeconds.load(); }
    double getLengthSeconds() const                     { return song != nullptr ? song->getLengthSeconds() : 0.0; }

    /** Playback speed, 1.0 = original. Pitch is not affected. */
    void setSpeed (double newSpeed)                     { speed = juce::jlimit (0.25, 2.0, newSpeed); }
    double getSpeed() const noexcept                    { return speed.load(); }

    /** Pitch shift in semitones (fractional allowed). Speed is not affected. */
    void setSemitones (double newSemitones)             { semitones = juce::jlimit (-12.0, 12.0, newSemitones); }
    double getSemitones() const noexcept                { return semitones.load(); }

    void setLoopSeconds (double startSeconds, double endSeconds);
    void setLoopEnabled (bool shouldLoop);
    bool isLoopEnabled() const noexcept                 { return loopEnabled.load(); }
    bool hasLoop() const noexcept                       { return loopEnd.load() > loopStart.load(); }
    double getLoopStartSeconds() const;
    double getLoopEndSeconds() const;

    std::atomic<float> gain { 1.0f };

    /** Where the listener is in the song, tied to a running count of rendered samples,
        so other processors in the same graph (the metronome) can line up with it. */
    struct Clock
    {
        juce::int64 samplesRendered = 0;    // samples processed since prepareToPlay, at the end of the last block
        double heardSeconds = 0.0;          // song position heard at that moment
        double speed = 1.0;
        bool playing = false;
    };

    /** Audio thread safe; returns false if the clock is being updated right now. */
    bool getClock (Clock&) const;

    //==============================================================================
    /** What the player does at the start of a block (reported to a SongBlockListener, e.g. the take recorder). */
    struct SongState
    {
        double heardSeconds = 0.0;          // song position heard from this block on
        bool playing = false;
        double speed = 1.0, semitones = 0.0;
        bool loopOn = false;
        double loopStart = 0.0, loopEnd = 0.0;
    };

    struct SongBlockListener
    {
        virtual ~SongBlockListener() = default;
        virtual void songBlock (const SongState&) noexcept = 0;    // audio thread
    };

    void setSongBlockListener (SongBlockListener* l)   { songListener = l; }

    //==============================================================================
    /** A recorded take replayed over the song: the take plays from start to end, and the song follows the take's
        timeline (position, speed, pitch, loop, pauses) as it was while recording. While a take is set, the
        song's own transport is idle: playTake() / pauseTake() / seekTake() drive both. */
    struct Take
    {
        LoadedSong audio;
        TakeTimeline timeline;
        double getLengthSeconds() const     { return audio.getLengthSeconds(); }
    };

    /** Decodes a take's audio file on the loader thread, then sets it (message thread callback). */
    void loadTakeAsync (const juce::URL& file, TakeTimeline, LoadCallback onDone);

    /** Message thread. nullptr removes the take (the song stays loaded, paused). Loading a song removes it too. */
    void setTake (std::shared_ptr<const Take>);
    bool hasTake() const noexcept                       { return take != nullptr; }
    std::shared_ptr<const Take> getTake() const         { return take; }

    void playTake();
    void pauseTake()                                    { takePlaying = false; }
    void seekTake (double seconds)                      { takeSeek = juce::jmax (0.0, seconds); takePosition = juce::jmax (0.0, seconds); }
    bool isTakePlaying() const noexcept                 { return takePlaying.load(); }
    double getTakePosition() const noexcept             { return takePosition.load(); }
    double getTakeLength() const                        { return take != nullptr ? take->getLengthSeconds() : 0.0; }

    std::atomic<float> takeGain { 1.0f };

    //==============================================================================
    /** A song's stems (see Stems.h), 16-bit in memory, at the song's sample rate. While any stem gain isn't 1, the
        player plays the stems mixed with those gains instead of the song (before speed / pitch: everything else
        works the same). */
    struct StemSet
    {
        int numSamples = 0;
        std::array<std::vector<juce::int16>, Stems::count * 2> samples;    // stem * 2 + channel
        std::array<float, Stems::count> scale {};                          // int16 -> float
    };

    /** Decodes a complete stems folder at `sampleRate` (any thread; a few seconds). */
    static std::shared_ptr<StemSet> loadStems (const juce::File& folder, double sampleRate, juce::String& error);

    /** Message thread. Ignored unless `forSong` is the song loaded now. nullptr removes them. Loading a song does too. */
    void setStems (std::shared_ptr<const StemSet>, const LoadedSong* forSong);
    bool hasStems() const noexcept                      { return stems != nullptr; }
    std::shared_ptr<const StemSet> getStems() const     { return stems; }

    /** Per stem (Stems::Index), 0..2; 1 = as separated. */
    std::array<std::atomic<float>, Stems::count> stemGains;

    /** Renders a take replayed over its song, as heard in take mode, into `out` (stereo, at the take's sample
        rate; scaled down if it would clip). Any thread, faster than real time. `cancel` may stop it early. */
    static juce::Result renderTakeMix (std::shared_ptr<const LoadedSong>, std::shared_ptr<const Take>, float songGain,
                                       float takeGain, juce::AudioBuffer<float>& out, double& sampleRate,
                                       const std::atomic<bool>* cancel = nullptr,
                                       std::shared_ptr<const StemSet> stems = nullptr,
                                       std::array<float, Stems::count> stemGains = { 1, 1, 1, 1, 1, 1 });

    /** The speed / pitch actually used (a replaying take's timeline overrides setSpeed / setSemitones). */
    double getEffectiveSpeed() const noexcept           { return effectiveSpeed.load(); }
    double getEffectiveSemitones() const noexcept       { return effectiveSemitones.load(); }

    //==============================================================================
    const juce::String getName() const override         { return "Song player"; }
    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override                    {}
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

private:
    juce::int64 secondsToSamples (double seconds) const;
    void renderBlock (juce::AudioBuffer<float>&);
    void mixTake (const Take&, juce::AudioBuffer<float>&);

    mutable juce::SpinLock clockLock;
    Clock clock;
    juce::int64 samplesRendered = 0;

    juce::AudioFormatManager formatManager;
    juce::ThreadPool loaderPool { 1 };

    // The song is swapped under this lock; the audio thread only ever try-locks it.
    juce::SpinLock songLock;
    std::shared_ptr<const LoadedSong> song;

    signalsmith::stretch::SignalsmithStretch<float> stretch;
    double deviceSampleRate = 48000.0;
    double inputRemainder = 0.0;
    juce::int64 readPosition = 0;
    float lastGain = 1.0f;
    double lastTransposeFactor = 0.0;

    std::atomic<bool> playing { false };
    std::atomic<juce::int64> seekTarget { -1 };
    std::atomic<double> speed { 1.0 }, semitones { 0.0 }, positionSeconds { 0.0 };
    std::atomic<bool> loopEnabled { false };
    std::atomic<juce::int64> loopStart { 0 }, loopEnd { 0 };

    // Stems (swapped under songLock, like the song)
    std::shared_ptr<const StemSet> stems;
    std::array<float, Stems::count> currentStemGains {};

    // Take replay (the take is swapped under songLock, like the song)
    std::shared_ptr<const Take> take;
    std::atomic<bool> takePlaying { false };
    std::atomic<double> takeSeek { -1.0 }, takePosition { 0.0 };
    double takePos = 0.0;               // audio thread: seconds = takeSamples / deviceSampleRate
    juce::int64 takeSamples = 0;
    int takeSegment = -1;
    float lastTakeGain = 1.0f;
    std::atomic<double> effectiveSpeed { 1.0 }, effectiveSemitones { 0.0 };
    bool renderedPlaying = false;       // the song was heard in the last block
    double heardSeconds = 0.0;          // like positionSeconds, but not clamped to the song (negative just after a seek to 0)
    std::atomic<SongBlockListener*> songListener { nullptr };

    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayerProcessor)
};

} // namespace pv
