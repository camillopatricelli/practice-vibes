#include "Recorder.h"

namespace pv
{

Recorder::Recorder (double secondsToBuffer)
    : bufferSeconds (secondsToBuffer)
{
    segments.reserve ((size_t) maxSegments);
}

Recorder::~Recorder()
{
    stop();
    writerThread.stopThread (2000);
}

bool Recorder::start (const juce::File& file, double sampleRate, int compensationSamples, juce::String& error)
{
    if (active.load())
    {
        error = "already recording";
        return false;
    }

    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

    if (stream == nullptr)
    {
        error = "can't write " + file.getFullPathName();
        return false;
    }

    juce::FlacAudioFormat flac;
    auto formatWriter = flac.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                                     .withNumChannels (2)
                                                                                     .withBitsPerSample (24));

    if (formatWriter == nullptr)
    {
        error = "can't create a FLAC file";
        file.deleteFile();
        return false;
    }

    if (! writerThread.isThreadRunning())
        writerThread.startThread();

    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (formatWriter.release(), writerThread,
                                                                                (int) (sampleRate * bufferSeconds));
    segments.clear();       // the audio thread doesn't touch them while there's no writer
    written = 0;
    dropped = false;
    rate = sampleRate;
    compensation = juce::jmax (0, compensationSamples);
    currentFile = file;

    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        writer = std::move (threaded);
    }

    ++generation;
    active = true;
    return true;
}

Recorder::Result Recorder::stop()
{
    Result result;

    if (! active.exchange (false) && writer == nullptr)
        return result;

    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> finished;

    {
        // The audio thread holds this lock while it uses the writer: once we have it, it's done with it.
        const juce::SpinLock::ScopedLockType lock (writerLock);
        finished = std::move (writer);
    }

    finished.reset();   // flushes the remaining audio and closes the file

    result.file = currentFile;
    result.seconds = getRecordedSeconds();
    result.timeline.segments = segments;
    return result;
}

//==============================================================================
void Recorder::beginBlock (juce::int64 start, int) noexcept
{
    blockStart = start;
    const auto gen = generation.load();
    blockActive = active.load();

    if (blockActive && gen != seenGeneration)
    {
        seenGeneration = gen;
        startTime = start;
        toSkip = compensation.load();
    }
}

void Recorder::captureGuitar (const float* left, const float* right, int numSamples) noexcept
{
    if (! blockActive)
        return;

    const juce::SpinLock::ScopedTryLockType lock (writerLock);

    if (! lock.isLocked() || writer == nullptr)
        return;

    auto offset = juce::jmin (toSkip, numSamples);
    toSkip -= offset;
    const auto count = numSamples - offset;

    if (count <= 0)
        return;

    const float* channels[] = { left + offset, (right != nullptr ? right : left) + offset };

    if (writer->write (channels, count))
        written += count;
    else
        dropped = true;     // the disk couldn't keep up: the take will have a gap
}

void Recorder::songBlock (const PlayerProcessor::SongState& state) noexcept
{
    if (! blockActive)
        return;

    // Same lock as the writer: once stop() has taken the writer away, the segments are no longer touched.
    const juce::SpinLock::ScopedTryLockType lock (writerLock);

    if (! lock.isLocked() || writer == nullptr || segments.size() >= (size_t) maxSegments)
        return;

    TakeSegment s;
    s.takeSeconds = (double) (blockStart - startTime) / juce::jmax (1.0, rate.load());
    s.songSeconds = state.heardSeconds;
    s.playing = state.playing;
    s.speed = state.speed;
    s.semitones = state.semitones;
    s.loopOn = state.loopOn;
    s.loopStart = state.loopOn ? state.loopStart : 0.0;
    s.loopEnd = state.loopOn ? state.loopEnd : 0.0;

    // Same settings: only a jump (a seek, or a loop wrap the timeline didn't predict) starts a new segment.
    if (! segments.empty())
    {
        const auto& last = segments.back();

        if (last.sameSettings (s) && std::abs (last.songSecondsAt (s.takeSeconds) - s.songSeconds) <= seekToleranceSeconds)
            return;
    }

    segments.push_back (s);     // capacity reserved in the constructor: never allocates
}

} // namespace pv
