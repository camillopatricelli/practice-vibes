#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "AudioTap.h"
#include "PlayerProcessor.h"
#include "Take.h"

namespace pv
{

/** Records the processed guitar (after the plugins, the same signal the looper's "after FX" mode records) to a
    FLAC file, plus a timeline of what the song player did meanwhile, so the take can later be replayed in step
    with the song (see PlayerProcessor::setTake).

    Timing: take time 0 is the block in which recording started. The guitar arrives `compensation` samples after
    the moment the player heard the song it was playing along to (device in + out latency + plugins + the
    looper's fine-tune), so the first `compensation` input samples are dropped: take sample k then lines up with
    the song as rendered at block start + k.

    start/stop: message thread. beginBlock/captureGuitar/songBlock: audio thread (lock-free, no allocation).
*/
class Recorder final : public PlayerProcessor::SongBlockListener,
                       public StereoTap
{
public:
    /** `bufferSeconds`: audio held in memory until the writer thread puts it on disk. */
    explicit Recorder (double bufferSeconds = 4.0);
    ~Recorder() override;

    /** Starts writing to `file` (FLAC, 24 bit, stereo). */
    bool start (const juce::File& file, double sampleRate, int compensationSamples, juce::String& error);

    struct Result
    {
        juce::File file;
        double seconds = 0.0;
        TakeTimeline timeline;
    };

    /** Stops and closes the file. */
    Result stop();

    /** Turns a recorded (FLAC) take into a small AAC .m4a file (any thread; takes a moment for long takes). */
    static juce::Result convertToAac (const juce::File& recorded, const juce::File& m4a, int kbps = takeKbps);

    static constexpr int takeKbps = 128;        // takes are for listening back, not for release: ~1 MB per minute

    bool isRecording() const noexcept                   { return active.load(); }
    double getRecordedSeconds() const noexcept          { return (double) written.load() / juce::jmax (1.0, rate.load()); }
    bool hasDroppedAudio() const noexcept               { return dropped.load(); }

    // Audio thread ----------------------------------------------------------------
    void beginBlock (juce::int64 blockStart, int numSamples) noexcept;
    void captureGuitar (const float* left, const float* right, int numSamples) noexcept override;
    void songBlock (const PlayerProcessor::SongState&) noexcept override;

    static constexpr int maxSegments = 8192;
    static constexpr double seekToleranceSeconds = 0.015;   // a jump bigger than this starts a new segment

private:
    juce::TimeSliceThread writerThread { "Take writer" };
    juce::SpinLock writerLock;                               // the audio thread only try-locks it
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::File currentFile;
    double bufferSeconds;

    std::atomic<bool> active { false };
    std::atomic<double> rate { 48000.0 };
    std::atomic<juce::int64> written { 0 };
    std::atomic<bool> dropped { false };
    std::atomic<int> compensation { 0 };

    // Audio thread state
    std::atomic<int> generation { 0 };                      // +1 per start(): the audio thread notices a new take
    int seenGeneration = 0;
    bool blockActive = false;
    juce::int64 blockStart = 0, startTime = 0;
    int toSkip = 0;
    std::vector<TakeSegment> segments;                      // capacity reserved once; filled on the audio thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Recorder)
};

} // namespace pv
