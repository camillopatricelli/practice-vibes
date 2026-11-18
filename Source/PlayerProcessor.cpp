#include "PlayerProcessor.h"
#include "AacFile.h"
#include "MediaFoundationAudio.h"
#include "Localisation.h"

namespace pv
{

namespace
{
    /** Gives the stretcher `reader[channel][i]` access to the song, starting at `start`,
        wrapping around the loop region and returning silence past the end.
    */
    struct SongReader
    {
        const LoadedSong* song = nullptr;
        const PlayerProcessor::StemSet* stems = nullptr;   // set: play the stems mixed with stemGain instead
        std::array<float, Stems::count> stemGain {};       // includes the int16 scale
        juce::int64 start = 0;
        bool looping = false;
        juce::int64 loopStart = 0, loopEnd = 0;

        juce::int64 map (juce::int64 position) const noexcept
        {
            if (looping && position >= loopEnd)
                position = loopStart + (position - loopStart) % (loopEnd - loopStart);

            return position;
        }

        struct Channel
        {
            const SongReader& reader;
            const float* data;
            juce::int64 length;
            int channel;

            float operator[] (int index) const noexcept
            {
                const auto position = reader.map (reader.start + index);

                if (position < 0 || position >= length)
                    return 0.0f;

                if (reader.stems == nullptr)
                    return data[position];

                if (position >= reader.stems->numSamples)
                    return 0.0f;

                auto sum = 0.0f;

                for (int s = 0; s < Stems::count; ++s)
                    sum += reader.stemGain[(size_t) s] * (float) reader.stems->samples[(size_t) (s * 2 + channel)][(size_t) position];

                return sum;
            }
        };

        Channel operator[] (int channel) const noexcept
        {
            return { *this, song->audio.getReadPointer (channel), (juce::int64) song->audio.getNumSamples(), channel };
        }
    };

    std::shared_ptr<LoadedSong> decodeSong (juce::AudioFormatManager& formats, const juce::URL& url, juce::String& error)
    {
        const auto name = juce::URL::removeEscapeChars (url.getFileName());
        const auto extension = juce::File::createFileWithoutCheckingPath (name).getFileExtension().toLowerCase();

        // AAC / M4A on Windows: JUCE can't decode it, so use Media Foundation.
        if (MediaFoundationAudio::handlesExtension (extension) && url.isLocalFile())
        {
            auto song = std::make_shared<LoadedSong>();
            song->name = name;
            const auto result = MediaFoundationAudio::decodeFile (url.getLocalFile(), song->audio, song->sampleRate);

            if (result.failed())
            {
                error = tr ("can't decode this {format} file: {error}", extension.substring (1).toUpperCase(), result.getErrorMessage());
                return {};
            }

            song->buildPeaks();
            return song;
        }

        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));

        if (stream == nullptr)
        {
            error = tr ("could not open the file");
            return {};
        }

        // Pick the decoder by extension, like createReaderFor (File). Sniffing every format lets the lenient
        // MP3 decoder "succeed" on other formats (it played .m4a files as noise); only sniff unknown extensions.
        std::unique_ptr<juce::AudioFormatReader> reader;

        if (auto* format = extension.isNotEmpty() ? formats.findFormatForFileExtension (extension) : nullptr)
            reader.reset (format->createReaderFor (stream.release(), true));
        else
            reader.reset (formats.createReaderFor (std::move (stream)));

        if (reader == nullptr)
        {
            error = extension.isNotEmpty() && formats.findFormatForFileExtension (extension) == nullptr
                        ? tr ("unsupported audio format ({extension})", extension)
                        : tr ("unsupported or damaged audio file");
            return {};
        }

        const auto length = (int) juce::jmin<juce::int64> (reader->lengthInSamples, std::numeric_limits<int>::max());

        if (length <= 0)
        {
            error = tr ("the file contains no audio");
            return {};
        }

        auto song = std::make_shared<LoadedSong>();
        song->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
        song->name = name;
        song->audio.setSize (2, length);

        reader->read (&song->audio, 0, length, 0, true, true);

        if (reader->numChannels == 1)
            song->audio.copyFrom (1, 0, song->audio, 0, 0, length);

        song->buildPeaks();

        return song;
    }
}

//==============================================================================
void LoadedSong::buildPeaks()
{
    peakLevels.clear();

    const auto length = audio.getNumSamples();

    if (length <= 0)
        return;

    // Finest level straight from the audio.
    PeakLevel level;
    level.samplesPerBucket = 256;
    const auto numBuckets = (length + level.samplesPerBucket - 1) / level.samplesPerBucket;
    level.mins.resize ((size_t) numBuckets);
    level.maxs.resize ((size_t) numBuckets);

    const auto* left = audio.getReadPointer (0);
    const auto* right = audio.getReadPointer (1);

    for (int bucket = 0; bucket < numBuckets; ++bucket)
    {
        const auto begin = bucket * level.samplesPerBucket;
        const auto end = juce::jmin (length, begin + level.samplesPerBucket);
        auto lo = 0.0f, hi = 0.0f;

        for (int i = begin; i < end; ++i)
        {
            const auto mono = 0.5f * (left[i] + right[i]);
            lo = juce::jmin (lo, mono);
            hi = juce::jmax (hi, mono);
        }

        level.mins[(size_t) bucket] = lo;
        level.maxs[(size_t) bucket] = hi;
    }

    peakLevels.push_back (std::move (level));

    // Each further level combines 4 buckets of the previous one.
    while (peakLevels.back().mins.size() > 512)
    {
        const auto& previous = peakLevels.back();
        PeakLevel coarser;
        coarser.samplesPerBucket = previous.samplesPerBucket * 4;
        const auto count = (previous.mins.size() + 3) / 4;
        coarser.mins.resize (count);
        coarser.maxs.resize (count);

        for (size_t i = 0; i < count; ++i)
        {
            auto lo = 0.0f, hi = 0.0f;

            for (size_t j = i * 4; j < juce::jmin (previous.mins.size(), i * 4 + 4); ++j)
            {
                lo = juce::jmin (lo, previous.mins[j]);
                hi = juce::jmax (hi, previous.maxs[j]);
            }

            coarser.mins[i] = lo;
            coarser.maxs[i] = hi;
        }

        peakLevels.push_back (std::move (coarser));
    }
}

//==============================================================================
PlayerProcessor::PlayerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formatManager.registerBasicFormats();
    stretch.presetDefault (2, (float) deviceSampleRate);

    for (auto& g : stemGains)
        g = 1.0f;

    currentStemGains.fill (1.0f);
}

PlayerProcessor::~PlayerProcessor()
{
    alive.reset();
    loaderPool.removeAllJobs (true, 10000);
}

void PlayerProcessor::loadAsync (const juce::URL& file, LoadCallback onDone)
{
    std::weak_ptr<bool> weakAlive = alive;

    loaderPool.addJob ([this, weakAlive, file, done = std::move (onDone)]
    {
        juce::String error;
        auto decoded = decodeSong (formatManager, file, error);

        juce::MessageManager::callAsync ([this, weakAlive, decoded, error, done]
        {
            if (weakAlive.expired())
                return;

            if (decoded != nullptr)
            {
                setSong (decoded);
                juce::Logger::writeToLog ("Loaded song " + decoded->name + " ("
                                          + juce::String (decoded->getLengthSeconds(), 1) + " s, "
                                          + juce::String (decoded->sampleRate, 0) + " Hz)");
            }
            else
            {
                juce::Logger::writeToLog ("Could not load song: " + error);
            }

            if (done)
                done (error);
        });
    });
}

void PlayerProcessor::setSong (std::shared_ptr<const LoadedSong> newSong)
{
    playing = false;
    loopEnabled = false;
    loopStart = 0;
    loopEnd = 0;
    positionSeconds = 0.0;

    takePlaying = false;
    std::shared_ptr<const LoadedSong> old;
    std::shared_ptr<const Take> oldTake;
    std::shared_ptr<const StemSet> oldStems;

    {
        const juce::SpinLock::ScopedLockType lock (songLock);
        old = std::exchange (song, std::move (newSong));
        oldTake = std::exchange (take, nullptr);
        oldStems = std::exchange (stems, nullptr);
        seekTarget = 0;
    }

    // `old` is released here, on the message thread, never on the audio thread.
}

//==============================================================================
std::shared_ptr<PlayerProcessor::StemSet> PlayerProcessor::loadStems (const juce::File& folder, double sampleRate, juce::String& error)
{
    if (! Stems::isComplete (folder))
    {
        error = "no stems";
        return {};
    }

    // Per-stem gains the separator applied to fit a loud stem into the file.
    std::array<float, Stems::count> fileGain;
    fileGain.fill (1.0f);

    if (auto xml = juce::XmlDocument::parse (Stems::infoFile (folder)))
        for (auto* e : xml->getChildWithTagNameIterator ("STEM"))
            for (int s = 0; s < Stems::count; ++s)
                if (e->getStringAttribute ("name") == Stems::fileStem (s))
                    fileGain[(size_t) s] = (float) e->getDoubleAttribute ("gain", 1.0);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto set = std::make_shared<StemSet>();

    for (int s = 0; s < Stems::count; ++s)
    {
        auto decoded = decodeSong (formats, juce::URL (Stems::stemFile (folder, s)), error);

        if (decoded == nullptr)
            return {};

        auto audio = std::move (decoded->audio);

        if (decoded->sampleRate != sampleRate)
            audio = AacFile::resample (audio, decoded->sampleRate, sampleRate);

        const auto n = audio.getNumSamples();

        if (s == 0)
            set->numSamples = n;

        const auto length = juce::jmin (n, set->numSamples);
        const auto peak = juce::jmax (1.0e-6f, audio.getMagnitude (0, length));
        set->scale[(size_t) s] = peak * fileGain[(size_t) s] / 32767.0f;

        for (int c = 0; c < 2; ++c)
        {
            auto& dst = set->samples[(size_t) (s * 2 + c)];
            dst.assign ((size_t) set->numSamples, 0);
            const auto* src = audio.getReadPointer (juce::jmin (c, audio.getNumChannels() - 1));

            for (int i = 0; i < length; ++i)
                dst[(size_t) i] = (juce::int16) juce::roundToInt (src[i] / peak * 32767.0f);
        }
    }

    return set;
}

void PlayerProcessor::setStems (std::shared_ptr<const StemSet> newStems, const LoadedSong* forSong)
{
    std::shared_ptr<const StemSet> old;

    {
        const juce::SpinLock::ScopedLockType lock (songLock);

        if (song.get() != forSong && newStems != nullptr)
            return;

        old = std::exchange (stems, std::move (newStems));
    }
}

//==============================================================================
void PlayerProcessor::loadTakeAsync (const juce::URL& file, TakeTimeline timeline, LoadCallback onDone)
{
    std::weak_ptr<bool> weakAlive = alive;

    loaderPool.addJob ([this, weakAlive, file, timeline = std::move (timeline), done = std::move (onDone)]
    {
        juce::String error;
        auto decoded = decodeSong (formatManager, file, error);
        std::shared_ptr<Take> newTake;

        if (decoded != nullptr)
        {
            newTake = std::make_shared<Take>();
            newTake->audio = std::move (*decoded);
            newTake->timeline = timeline;
        }

        juce::MessageManager::callAsync ([this, weakAlive, newTake, error, done]
        {
            if (weakAlive.expired())
                return;

            if (newTake != nullptr)
                setTake (newTake);

            if (done)
                done (error);
        });
    });
}

void PlayerProcessor::setTake (std::shared_ptr<const Take> newTake)
{
    playing = false;
    takePlaying = false;
    takePosition = 0.0;
    takeSeek = 0.0;
    std::shared_ptr<const Take> old;

    {
        const juce::SpinLock::ScopedLockType lock (songLock);
        old = std::exchange (take, std::move (newTake));
    }
}

juce::Result PlayerProcessor::renderTakeMix (std::shared_ptr<const LoadedSong> song, std::shared_ptr<const Take> take,
                                             float songGain, float takeGain, juce::AudioBuffer<float>& out,
                                             double& sampleRate, const std::atomic<bool>* cancel,
                                             std::shared_ptr<const StemSet> stems, std::array<float, Stems::count> stemGains)
{
    if (song == nullptr || take == nullptr || take->audio.audio.getNumSamples() == 0)
        return juce::Result::fail (tr ("nothing to export"));

    constexpr int block = 512;
    sampleRate = take->audio.sampleRate;
    const auto total = take->audio.audio.getNumSamples();

    PlayerProcessor renderer;
    renderer.prepareToPlay (sampleRate, block);
    renderer.setSong (song);
    renderer.setTake (take);
    renderer.setStems (stems, song.get());

    for (int s = 0; s < Stems::count; ++s)
    {
        renderer.stemGains[(size_t) s] = stemGains[(size_t) s];
        renderer.currentStemGains[(size_t) s] = stemGains[(size_t) s];
    }

    renderer.gain = songGain;
    renderer.lastGain = songGain;
    renderer.takeGain = takeGain;
    renderer.lastTakeGain = takeGain;
    renderer.playTake();

    out.setSize (2, total);
    out.clear();
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;

    for (int done = 0; done < total; done += block)
    {
        if (cancel != nullptr && cancel->load())
            return juce::Result::fail ("cancelled");

        buffer.clear();
        renderer.processBlock (buffer, midi);
        const auto n = juce::jmin (block, total - done);

        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, done, buffer, ch, 0, n);
    }

    // Song and guitar together can go over full scale: turn the whole mix down rather than clip it.
    if (const auto peak = out.getMagnitude (0, total); peak > 0.98f)
        out.applyGain (0.98f / peak);

    return juce::Result::ok();
}

void PlayerProcessor::playTake()
{
    if (take == nullptr)
        return;

    // Start over at the end; otherwise line the song up again where the take is.
    takeSeek = getTakePosition() >= getTakeLength() - 0.05 ? 0.0 : getTakePosition();
    takePlaying = true;
}

//==============================================================================
juce::int64 PlayerProcessor::secondsToSamples (double seconds) const
{
    if (song == nullptr)
        return 0;

    return juce::jlimit<juce::int64> (0, song->audio.getNumSamples(), (juce::int64) (seconds * song->sampleRate));
}

void PlayerProcessor::play()
{
    if (song == nullptr)
        return;

    // Restart from the top if we had reached the end.
    if (! loopEnabled.load() && getPositionSeconds() >= getLengthSeconds() - 0.05)
        seekToSeconds (0.0);

    playing = true;
}

void PlayerProcessor::pause()
{
    playing = false;
}

void PlayerProcessor::stop()
{
    playing = false;
    seekToSeconds (loopEnabled.load() && hasLoop() ? getLoopStartSeconds() : 0.0);
}

void PlayerProcessor::seekToSeconds (double seconds)
{
    if (song == nullptr)
        return;

    seekTarget = secondsToSamples (seconds);
    positionSeconds = juce::jlimit (0.0, getLengthSeconds(), seconds);
}

void PlayerProcessor::setLoopSeconds (double startSeconds, double endSeconds)
{
    if (endSeconds < startSeconds)
        std::swap (startSeconds, endSeconds);

    const auto start = secondsToSamples (startSeconds);
    const auto end = secondsToSamples (endSeconds);

    // Store end first as "empty" so the audio thread never sees end < start.
    loopEnd = 0;
    loopStart = start;
    loopEnd = end;
}

void PlayerProcessor::setLoopEnabled (bool shouldLoop)
{
    loopEnabled = shouldLoop && hasLoop();

    // Jump into the loop if we're outside it.
    if (loopEnabled.load())
    {
        const auto position = getPositionSeconds();

        if (position < getLoopStartSeconds() || position >= getLoopEndSeconds())
            seekToSeconds (getLoopStartSeconds());
    }
}

double PlayerProcessor::getLoopStartSeconds() const
{
    return song != nullptr ? (double) loopStart.load() / song->sampleRate : 0.0;
}

double PlayerProcessor::getLoopEndSeconds() const
{
    return song != nullptr ? (double) loopEnd.load() / song->sampleRate : 0.0;
}

//==============================================================================
bool PlayerProcessor::getClock (Clock& out) const
{
    const juce::SpinLock::ScopedTryLockType lock (clockLock);

    if (! lock.isLocked())
        return false;

    out = clock;
    return true;
}

void PlayerProcessor::prepareToPlay (double sampleRate, int)
{
    deviceSampleRate = sampleRate > 0 ? sampleRate : 48000.0;
    samplesRendered = 0;
    stretch.presetDefault (2, (float) deviceSampleRate);
    lastTransposeFactor = 0.0;

    // Re-prime the stretcher at the current position.
    if (song != nullptr)
        seekTarget = secondsToSamples (getPositionSeconds());
}

void PlayerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    renderBlock (buffer);
    samplesRendered += buffer.getNumSamples();

    const juce::SpinLock::ScopedLockType lock (clockLock);
    clock.samplesRendered = samplesRendered;
    clock.heardSeconds = positionSeconds.load();
    clock.speed = speed.load();
    clock.playing = renderedPlaying;
}

void PlayerProcessor::renderBlock (juce::AudioBuffer<float>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    renderedPlaying = false;

    const juce::SpinLock::ScopedTryLockType lock (songLock);

    if (! lock.isLocked())
        return;

    if (song == nullptr || buffer.getNumChannels() < 2)
    {
        if (auto* listener = songListener.load())
            listener->songBlock ({});

        return;
    }

    const auto numSamples = buffer.getNumSamples();
    const auto length = (juce::int64) song->audio.getNumSamples();
    const auto rateRatio = song->sampleRate / deviceSampleRate;   // song samples per device sample at 1x

    // This block's settings: the user's, or a replaying take's timeline.
    auto speedNow = speed.load();
    auto semitonesNow = semitones.load();
    auto playNow = playing.load();
    auto loopOn = loopEnabled.load();
    auto loopStartNow = loopStart.load(), loopEndNow = loopEnd.load();
    auto target = seekTarget.exchange (-1);
    auto* takeNow = take.get();
    std::optional<double> replaySeekSeconds;     // a replaying take wants the song heard from here

    if (takeNow != nullptr)
    {
        playNow = false;
        auto resync = false;

        if (const auto s = takeSeek.exchange (-1.0); s >= 0.0)
        {
            takeSamples = (juce::int64) std::llround (juce::jmin (s, takeNow->getLengthSeconds()) * deviceSampleRate);
            takePos = (double) takeSamples / deviceSampleRate;
            resync = true;
        }

        if (takePlaying.load())
        {
            const auto& timeline = takeNow->timeline;

            if (const auto index = timeline.segmentAt (takePos); index >= 0)
            {
                const auto& segment = timeline.segments[(size_t) index];
                speedNow = segment.speed;
                semitonesNow = segment.semitones;
                playNow = segment.playing;
                loopOn = segment.loopOn;
                loopStartNow = secondsToSamples (segment.loopStart);
                loopEndNow = secondsToSamples (segment.loopEnd);

                // Seek when the take jumped, or a new segment starts somewhere else than where the song is.
                const auto wanted = timeline.songSecondsAt (takePos, index);

                if (resync || (index != takeSegment && std::abs (wanted - heardSeconds) > 0.02))
                    replaySeekSeconds = wanted;

                takeSegment = index;
            }
        }
        else
        {
            takeSegment = -1;

            if (resync)     // moved while paused: show the song where the take is
                replaySeekSeconds = takeNow->timeline.songSecondsAt (takePos);
        }
    }

    effectiveSpeed = speedNow;
    effectiveSemitones = semitonesNow;
    const auto playbackRate = speedNow * rateRatio;

    SongReader reader;
    reader.song = song.get();

    // Stems: gains glide (about 0.2 s from 0 to 1) so muting doesn't click.
    {
        auto allOne = true;

        for (int s = 0; s < Stems::count; ++s)
        {
            const auto goal = stems != nullptr ? stemGains[(size_t) s].load() : 1.0f;
            auto& g = currentStemGains[(size_t) s];
            const auto step = 0.05f * (float) numSamples / 512.0f;
            g = g < goal ? juce::jmin (goal, g + step) : juce::jmax (goal, g - step);
            allOne = allOne && g == 1.0f;

            if (stems != nullptr)
                reader.stemGain[(size_t) s] = g * stems->scale[(size_t) s];
        }

        if (stems != nullptr && ! allOne)
            reader.stems = stems.get();
    }
    reader.loopStart = loopStartNow;
    reader.loopEnd = loopEndNow;
    reader.looping = loopOn && reader.loopEnd - reader.loopStart > 256;

    // The stretcher preserves samples-per-cycle, so a sample-rate mismatch must be
    // corrected through the transpose factor as well as the read rate.
    const auto transposeFactor = rateRatio * std::pow (2.0, semitonesNow / 12.0);

    if (transposeFactor != lastTransposeFactor)
    {
        stretch.setTransposeFactor ((float) transposeFactor);
        lastTransposeFactor = transposeFactor;
    }

    // After a seek the stretcher's output lags its input by outputLatency() (see `delay` below): what is heard
    // at the start of the block is that much before the seek target.
    const auto seekLag = (juce::int64) std::llround (stretch.outputLatency() * playbackRate);

    if (replaySeekSeconds.has_value())
    {
        target = reader.map (juce::jmax<juce::int64> (0, (juce::int64) std::llround (*replaySeekSeconds * song->sampleRate) + seekLag));
        positionSeconds = juce::jmax (0.0, *replaySeekSeconds);
    }

    if (target >= 0)
        heardSeconds = replaySeekSeconds.has_value() ? *replaySeekSeconds : (double) (target - seekLag) / song->sampleRate;

    if (target >= 0)
    {
        stretch.reset();
        reader.start = target;
        const auto preroll = stretch.inputLatency();
        stretch.seek (reader, preroll, playbackRate);
        readPosition = reader.map (target + preroll);
        inputRemainder = 0.0;
    }

    if (auto* listener = songListener.load())
    {
        SongState state;
        state.heardSeconds = heardSeconds;
        state.playing = playNow;
        state.speed = speedNow;
        state.semitones = semitonesNow;
        state.loopOn = reader.looping;
        state.loopStart = (double) reader.loopStart / song->sampleRate;
        state.loopEnd = (double) reader.loopEnd / song->sampleRate;
        listener->songBlock (state);
    }

    if (playNow)
    {
        renderedPlaying = true;
        inputRemainder += numSamples * playbackRate;
        const auto inputSamples = (int) inputRemainder;
        inputRemainder -= inputSamples;

        reader.start = readPosition;
        stretch.process (reader, inputSamples, buffer.getArrayOfWritePointers(), numSamples);
        readPosition = reader.map (readPosition + inputSamples);

        const auto newGain = gain.load();
        buffer.applyGainRamp (0, numSamples, lastGain, newGain);
        lastGain = newGain;

        // What the listener hears now was read this many song samples ago.
        const auto delay = (juce::int64) (stretch.inputLatency() + stretch.outputLatency() * playbackRate);
        auto heard = readPosition - delay;

        if (reader.looping && heard < reader.loopStart && readPosition >= reader.loopStart)
            heard += reader.loopEnd - reader.loopStart;

        positionSeconds = (double) juce::jlimit<juce::int64> (0, length, heard) / song->sampleRate;
        heardSeconds = (double) heard / song->sampleRate;

        if (! reader.looping && readPosition >= length + delay)
        {
            positionSeconds = (double) length / song->sampleRate;

            if (takeNow == nullptr)
                playing = false;
            else
                renderedPlaying = false;
        }
    }

    if (takeNow != nullptr && takePlaying.load())
        mixTake (*takeNow, buffer);
}

void PlayerProcessor::mixTake (const Take& t, juce::AudioBuffer<float>& buffer)
{
    // The take plays at its own sample rate (linear interpolation if the device runs at another one).
    const auto& audio = t.audio.audio;
    const auto takeLength = audio.getNumSamples();
    const auto step = t.audio.sampleRate / deviceSampleRate;
    const auto numSamples = buffer.getNumSamples();
    const auto newGain = takeGain.load();
    const auto position = takePos * t.audio.sampleRate;

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* in = audio.getReadPointer (juce::jmin (ch, audio.getNumChannels() - 1));
        auto* out = buffer.getWritePointer (ch);
        auto p = position;

        for (int i = 0; i < numSamples; ++i, p += step)
        {
            const auto index = (int) p;

            if (index + 1 >= takeLength)
                break;

            const auto frac = (float) (p - index);
            const auto g = lastTakeGain + (newGain - lastTakeGain) * ((float) i / (float) numSamples);
            out[i] += g * (in[index] + frac * (in[index + 1] - in[index]));
        }
    }

    lastTakeGain = newGain;
    takeSamples += numSamples;
    takePos = (double) takeSamples / deviceSampleRate;

    if (takePos >= t.getLengthSeconds())
    {
        takePos = t.getLengthSeconds();
        takePlaying = false;
    }

    takePosition = takePos;
}

} // namespace pv
