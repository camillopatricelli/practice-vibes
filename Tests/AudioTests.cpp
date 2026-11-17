// Offline checks of the song player, metronome and looper DSP. Run: PracticeVibesTests(.exe)
// Exit code 0 = all passed.

#include "../Source/BarClockPlayHead.h"
#include "../Source/ChordAnalyzer.h"
#include "../Source/DrumSequencer.h"
#include "../Source/Footswitch.h"
#include "../Source/LatencyCalibrator.h"
#include "../Source/Library.h"
#include "../Source/Links.h"
#include "../Source/LoopFiles.h"
#include "../Source/Looper.h"
#include "../Source/MediaFoundationAudio.h"
#include "../Source/MetronomeProcessor.h"
#include "../Source/PlayerProcessor.h"
#include "../Source/PluginChain.h"
#include "../Source/PracticeLog.h"
#include "../Source/PracticePlanner.h"
#include "../Source/Recorder.h"
#include "../Source/SessionPlans.h"
#include "../Source/StemMixer.h"
#include "../Source/StoragePanel.h"
#include "../Source/Stems.h"
#include "../Source/AacFile.h"
#include "../Source/Take.h"
#include "../Source/TopicShare.h"
#include "../Source/StudentGroups.h"
#include "../Source/Tuner.h"
#include "../Source/GainMeterProcessor.h"

#include <iostream>
#include <map>
#include <thread>

using namespace pv;

namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what)
    {
        std::cout << (condition ? "  PASS  " : "  FAIL  ") << what << std::endl;

        if (! condition)
            ++failures;
    }

    std::shared_ptr<LoadedSong> makeSine (double sampleRate, double frequency, double seconds)
    {
        auto song = std::make_shared<LoadedSong>();
        song->sampleRate = sampleRate;
        song->name = "sine";
        const auto length = (int) (sampleRate * seconds);
        song->audio.setSize (2, length);

        for (int i = 0; i < length; ++i)
        {
            const auto value = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / sampleRate);
            song->audio.setSample (0, i, value);
            song->audio.setSample (1, i, value);
        }

        return song;
    }

    /** Renders `seconds` of audio, returning only the final `measureSeconds`. */
    juce::AudioBuffer<float> render (juce::AudioProcessor& processor, double sampleRate, double seconds, double measureSeconds)
    {
        constexpr int blockSize = 512;
        const auto total = (int) (seconds * sampleRate);
        const auto keep = (int) (measureSeconds * sampleRate);
        juce::AudioBuffer<float> block (2, blockSize), kept (2, keep);
        juce::MidiBuffer midi;

        for (int done = 0; done < total; done += blockSize)
        {
            block.clear();
            processor.processBlock (block, midi);

            for (int i = 0; i < blockSize; ++i)
            {
                const auto index = done + i - (total - keep);

                if (index >= 0 && index < keep)
                    kept.setSample (0, index, block.getSample (0, i));
            }
        }

        return kept;
    }

    double estimateFrequency (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        int crossings = 0;
        const auto* data = buffer.getReadPointer (0);

        for (int i = 1; i < buffer.getNumSamples(); ++i)
            if (data[i - 1] <= 0.0f && data[i] > 0.0f)
                ++crossings;

        return crossings / (buffer.getNumSamples() / sampleRate);
    }

    int countClicks (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        int clicks = 0;
        int quietSamples = 1000000;
        const auto* data = buffer.getReadPointer (0);
        const auto minGap = (int) (0.05 * sampleRate);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (std::abs (data[i]) > 0.05f)
            {
                if (quietSamples > minGap)
                    ++clicks;

                quietSamples = 0;
            }
            else
            {
                ++quietSamples;
            }
        }

        return clicks;
    }

    void testPlayer()
    {
        std::cout << "Song player" << std::endl;
        constexpr double deviceRate = 48000.0;
        constexpr double songRate = 44100.0;   // deliberately different from the device

        {
            PlayerProcessor player;
            player.prepareToPlay (deviceRate, 512);
            player.setSong (makeSine (songRate, 440.0, 10.0));
            player.play();
            const auto out = render (player, deviceRate, 2.0, 1.0);
            const auto f = estimateFrequency (out, deviceRate);
            check (std::abs (f - 440.0) < 5.0, "pitch preserved across sample rates: " + juce::String (f, 1) + " Hz (want 440)");
            check (std::abs (player.getPositionSeconds() - 2.0) < 0.15, "position after 2 s at 1x: " + juce::String (player.getPositionSeconds(), 2) + " s");
        }

        {
            PlayerProcessor player;
            player.prepareToPlay (deviceRate, 512);
            player.setSong (makeSine (songRate, 440.0, 10.0));
            player.setSemitones (12.0);
            player.play();
            const auto f = estimateFrequency (render (player, deviceRate, 2.0, 1.0), deviceRate);
            check (std::abs (f - 880.0) < 10.0, "+12 semitones: " + juce::String (f, 1) + " Hz (want 880)");
        }

        {
            PlayerProcessor player;
            player.prepareToPlay (deviceRate, 512);
            player.setSong (makeSine (songRate, 440.0, 10.0));
            player.setSpeed (0.5);
            player.play();
            const auto out = render (player, deviceRate, 4.0, 1.0);
            const auto f = estimateFrequency (out, deviceRate);
            check (std::abs (f - 440.0) < 5.0, "half speed keeps pitch: " + juce::String (f, 1) + " Hz (want 440)");
            check (std::abs (player.getPositionSeconds() - 2.0) < 0.2, "half speed position after 4 s: " + juce::String (player.getPositionSeconds(), 2) + " s (want ~2)");
        }

        {
            PlayerProcessor player;
            player.prepareToPlay (deviceRate, 512);
            player.setSong (makeSine (songRate, 440.0, 10.0));
            player.setLoopSeconds (3.0, 4.0);
            player.setLoopEnabled (true);
            player.play();

            constexpr int blockSize = 512;
            juce::AudioBuffer<float> block (2, blockSize);
            juce::MidiBuffer midi;
            double minPos = 1.0e9, maxPos = -1.0;

            for (int done = 0; done < (int) (6.0 * deviceRate); done += blockSize)
            {
                player.processBlock (block, midi);

                if (done > (int) (0.5 * deviceRate))
                {
                    minPos = juce::jmin (minPos, player.getPositionSeconds());
                    maxPos = juce::jmax (maxPos, player.getPositionSeconds());
                }
            }

            check (minPos >= 2.9 && maxPos <= 4.1, "loop stays within 3-4 s: saw " + juce::String (minPos, 2) + " - " + juce::String (maxPos, 2));
            check (player.isPlaying(), "still playing while looping");
        }

        {
            PlayerProcessor player;
            player.prepareToPlay (deviceRate, 512);
            player.setSong (makeSine (songRate, 440.0, 1.0));
            player.play();
            render (player, deviceRate, 2.0, 0.1);
            check (! player.isPlaying(), "stops at the end of the song");
        }
    }

    void testFileLoading()
    {
        std::cout << "File loading" << std::endl;

        // Write a 2-second mono 44.1 kHz WAV to a temp file.
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-test-song.wav");
        file.deleteFile();

        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (44100.0)
                                                                                       .withNumChannels (1)
                                                                                       .withBitsPerSample (16));
            check (writer != nullptr, "test WAV writer created");

            if (writer == nullptr)
                return;

            auto source = makeSine (44100.0, 220.0, 2.0);
            writer->writeFromAudioSampleBuffer (source->audio, 0, source->audio.getNumSamples());
        }

        PlayerProcessor player;
        juce::String result = "pending";
        player.loadAsync (juce::URL (file), [&result] (const juce::String& error) { result = error; });

        for (int i = 0; i < 100 && result == "pending"; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        check (result.isEmpty(), "WAV file loads without error" + (result.isEmpty() ? juce::String() : ": " + result));

        if (auto song = player.getSong())
        {
            check (std::abs (song->getLengthSeconds() - 2.0) < 0.01, "length is 2 s: " + juce::String (song->getLengthSeconds(), 3));
            check (song->audio.getNumChannels() == 2, "mono file is played as stereo");
            check (std::abs (song->audio.getSample (1, 1000) - song->audio.getSample (0, 1000)) < 1.0e-6, "both channels identical");

            check (! song->peakLevels.empty(), "waveform cache built: " + juce::String ((int) song->peakLevels.size()) + " level(s)");

            if (! song->peakLevels.empty())
            {
                const auto& finest = song->peakLevels.front();
                const auto maxPeak = *std::max_element (finest.maxs.begin(), finest.maxs.end());
                const auto minPeak = *std::min_element (finest.mins.begin(), finest.mins.end());
                check (maxPeak > 0.45f && maxPeak < 0.55f && minPeak < -0.45f && minPeak > -0.55f,
                       "waveform cache min/max: " + juce::String (minPeak, 2) + " / " + juce::String (maxPeak, 2));

                const auto& coarsest = song->peakLevels.back();
                check (std::abs (*std::max_element (coarsest.maxs.begin(), coarsest.maxs.end()) - maxPeak) < 1.0e-6f,
                       "coarse levels keep the same peak");
            }
        }
        else
        {
            check (false, "song available after loading");
        }

        file.deleteFile();

        if (MediaFoundationAudio::isAvailable())
        {
            auto loadFile = [] (const juce::File& f, PlayerProcessor& p)
            {
                juce::String r = "pending";
                p.loadAsync (juce::URL (f), [&r] (const juce::String& error) { r = error; });

                for (int i = 0; i < 200 && r == "pending"; ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

                return r;
            };

            // A 3-second 440 Hz AAC file, made with the Windows encoder.
            auto m4a = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-test-song.m4a");
            const auto written = MediaFoundationAudio::writeAacFile (m4a, makeSine (48000.0, 440.0, 3.0)->audio, 48000.0);
            check (written.wasOk(), "test M4A written" + (written.wasOk() ? juce::String() : ": " + written.getErrorMessage()));

            PlayerProcessor m4aPlayer;
            const auto m4aResult = loadFile (m4a, m4aPlayer);
            check (m4aResult.isEmpty(), "M4A file loads without error" + (m4aResult.isEmpty() ? juce::String() : ": " + m4aResult));

            if (auto song = m4aPlayer.getSong(); song != nullptr && m4aResult.isEmpty())
            {
                check (std::abs (song->getLengthSeconds() - 3.0) < 0.1, "M4A length is 3 s: " + juce::String (song->getLengthSeconds(), 3));
                check (song->sampleRate == 48000.0, "M4A sample rate 48 kHz: " + juce::String (song->sampleRate));

                juce::AudioBuffer<float> middle (1, 48000);
                middle.copyFrom (0, 0, song->audio, 0, 48000, 48000);
                const auto f = estimateFrequency (middle, song->sampleRate);
                check (std::abs (f - 440.0) < 5.0, "M4A decodes the tone, not noise: " + juce::String (f, 1) + " Hz (want 440)");
                const auto peak = middle.getMagnitude (0, 0, 48000);
                check (peak > 0.4f && peak < 0.6f, "M4A level: peak " + juce::String (peak, 2) + " (want 0.5)");
                check (! song->peakLevels.empty(), "M4A waveform cache built");
            }

            // Junk with an .m4a name must fail with a message, not "load" as noise.
            auto junk = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-test-junk.m4a");
            juce::MemoryBlock bytes;

            for (int i = 0; i < 200000; ++i)
                bytes.append (i % 4000 == 0 ? "ÿûd" : "abcd", 4);   // sprinkle MP3 frame-sync words

            junk.replaceWithData (bytes.getData(), bytes.getSize());
            PlayerProcessor junkPlayer;
            const auto junkResult = loadFile (junk, junkPlayer);
            check (junkResult.isNotEmpty() && junkResult != "pending", "junk .m4a is refused: " + junkResult);

            m4a.deleteFile();
            junk.deleteFile();
        }
    }

    void testMetronome()
    {
        std::cout << "Metronome" << std::endl;
        constexpr double rate = 48000.0;

        {
            MetronomeProcessor metronome;
            metronome.prepareToPlay (rate, 512);
            metronome.startBpm = 120.0;
            metronome.start();
            const auto clicks = countClicks (render (metronome, rate, 4.0, 4.0), rate);
            check (clicks == 8, "120 bpm for 4 s = 8 clicks: got " + juce::String (clicks));
        }

        for (auto [sub, expected, name] : { std::tuple { MetronomeProcessor::Subdivision::eighths, 16, "8ths" },
                                            std::tuple { MetronomeProcessor::Subdivision::triplets, 24, "triplets" },
                                            std::tuple { MetronomeProcessor::Subdivision::sixteenths, 32, "16ths" } })
        {
            MetronomeProcessor metronome;
            metronome.prepareToPlay (rate, 512);
            metronome.startBpm = 120.0;
            metronome.subdivision = (int) sub;
            metronome.subdivisionLevel = 0.5f;
            metronome.start();
            const auto clicks = countClicks (render (metronome, rate, 4.0, 4.0), rate);
            check (clicks == expected, juce::String ("120 bpm ") + name + " for 4 s = " + juce::String (expected) + " clicks: got " + juce::String (clicks));
        }

        {
            // Downbeat > beat > subdivision
            MetronomeProcessor metronome;
            metronome.prepareToPlay (rate, 512);
            metronome.startBpm = 120.0;
            metronome.subdivision = (int) MetronomeProcessor::Subdivision::eighths;
            metronome.subdivisionLevel = 0.5f;
            metronome.start();
            const auto out = render (metronome, rate, 1.0, 1.0);
            auto peakAround = [&out, rate] (double seconds)
            {
                const auto start = (int) (seconds * rate);
                return out.getMagnitude (0, start, (int) (0.02 * rate));
            };
            const auto downbeat = peakAround (0.0), beat = peakAround (0.5), sub = peakAround (0.25);
            check (downbeat > beat && beat > sub && sub > 0.01f,
                   "levels downbeat > beat > subdivision: " + juce::String (downbeat, 2) + " / " + juce::String (beat, 2) + " / " + juce::String (sub, 2));
        }

        {
            // Swing: the off-beat 8th moves later (0.5 = straight, 0.75 = dotted 8th + 16th)
            auto firstOnsetAfter = [] (const juce::AudioBuffer<float>& out, double fromSeconds, double rate_)
            {
                for (int i = (int) (fromSeconds * rate_); i < out.getNumSamples(); ++i)
                    if (std::abs (out.getSample (0, i)) > 0.02f)
                        return i / rate_;

                return -1.0;
            };

            for (auto [sub, swingAmount, from, expected, name] :
                 { std::tuple { MetronomeProcessor::Subdivision::eighths, 0.75, 0.1, 0.375, "8ths dotted" },
                   std::tuple { MetronomeProcessor::Subdivision::eighths, 2.0 / 3.0, 0.1, 1.0 / 3.0, "8ths shuffle" },
                   std::tuple { MetronomeProcessor::Subdivision::sixteenths, 0.75, 0.05, 0.1875, "16ths dotted" },
                   std::tuple { MetronomeProcessor::Subdivision::triplets, 0.75, 0.1, 1.0 / 6.0, "triplets ignore swing" } })
            {
                MetronomeProcessor metronome;
                metronome.prepareToPlay (rate, 512);
                metronome.startBpm = 120.0;
                metronome.subdivision = (int) sub;
                metronome.subdivisionLevel = 1.0f;
                metronome.swing = swingAmount;
                metronome.start();
                const auto out = render (metronome, rate, 1.0, 1.0);
                const auto onset = firstOnsetAfter (out, from, rate);
                check (std::abs (onset - expected) < 0.001, juce::String ("swing ") + name + ": second tick at "
                         + juce::String (onset, 4) + " s, expected " + juce::String (expected, 4));
            }

            // 3 bars of swung 8ths still land the downbeats on time
            MetronomeProcessor metronome;
            metronome.prepareToPlay (rate, 512);
            metronome.startBpm = 120.0;
            metronome.subdivision = (int) MetronomeProcessor::Subdivision::eighths;
            metronome.swing = 0.75;
            metronome.start();
            const auto out = render (metronome, rate, 6.5, 6.5);
            const auto onset = firstOnsetAfter (out, 5.9, rate);
            check (std::abs (onset - 6.0) < 0.001, "swung 8ths keep the bar lines: downbeat 4 at " + juce::String (onset, 4) + " s");
        }

        for (auto [snd, name] : { std::pair { MetronomeProcessor::Sound::click, "click" }, std::pair { MetronomeProcessor::Sound::drums, "drums" } })
        {
            MetronomeProcessor metronome;
            metronome.prepareToPlay (rate, 512);
            metronome.sound = (int) snd;
            metronome.subdivision = (int) MetronomeProcessor::Subdivision::sixteenths;
            metronome.start();
            const auto out = render (metronome, rate, 2.0, 2.0);
            const auto peak = out.getMagnitude (0, 0, out.getNumSamples());
            bool finite = true;

            for (int i = 0; i < out.getNumSamples(); ++i)
                finite = finite && std::isfinite (out.getSample (0, i));

            check (finite && peak > 0.1f && peak <= 1.0f, juce::String ("sound '") + name + "' renders, peak " + juce::String (peak, 2));
        }

        {
            MetronomeProcessor metronome;
            metronome.prepareToPlay (rate, 512);
            metronome.startBpm = 120.0;
            metronome.beatsPerBar = 4;
            metronome.subdivision = (int) MetronomeProcessor::Subdivision::triplets;   // trainer must still count bars
            metronome.trainerEnabled = true;
            metronome.trainerStepBpm = 10.0;
            metronome.trainerInterval = 1.0;
            metronome.trainerUnit = (int) MetronomeProcessor::IntervalUnit::bars;
            metronome.trainerMaxBpm = 140.0;
            metronome.start();

            render (metronome, rate, 1.9, 0.1);   // inside bar 1
            check (std::abs (metronome.getCurrentBpm() - 120.0) < 0.01, "first bar stays at start tempo: " + juce::String (metronome.getCurrentBpm()));

            render (metronome, rate, 0.3, 0.1);   // just past the second downbeat (2.0 s)
            check (std::abs (metronome.getCurrentBpm() - 130.0) < 0.01, "steps +10 after one bar: " + juce::String (metronome.getCurrentBpm()));

            render (metronome, rate, 10.0, 0.1);
            check (std::abs (metronome.getCurrentBpm() - 140.0) < 0.01, "capped at max 140: " + juce::String (metronome.getCurrentBpm()));
        }
    }

    struct TestChord { int root; bool minor; int bass; };   // bass: pitch class, -1 = root

    /** A synthetic band at 120 bpm: kick on every beat, bass + harmonic-rich triads, one chord per bar. */
    std::vector<float> synthesiseBand (const std::vector<TestChord>& progression, int bars, double sampleRate,
                                       int beatsPerBar = 4, double beatSeconds = 0.5)
    {
        const auto length = (int) (sampleRate * beatSeconds * beatsPerBar * bars);
        std::vector<float> audio ((size_t) length, 0.0f);
        auto midiToHz = [] (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); };
        juce::Random random (42);

        for (int i = 0; i < length; ++i)
        {
            const auto t = i / sampleRate;
            const auto beat = (int) (t / beatSeconds);
            const auto sinceBeat = t - beat * beatSeconds;
            const auto& chord = progression[(size_t) ((beat / beatsPerBar) % (int) progression.size())];
            const auto triadBase = 60 + (chord.root > 4 ? chord.root - 12 : chord.root);
            const int notes[] = { triadBase, triadBase + (chord.minor ? 3 : 4), triadBase + 7 };
            auto sample = 0.0;

            for (auto note : notes)
                for (int h = 1; h <= 5; ++h)
                    sample += 0.08 / h * std::sin (juce::MathConstants<double>::twoPi * midiToHz (note) * h * t);

            const auto bassNote = 36 + (chord.bass >= 0 ? chord.bass : chord.root);
            sample += 0.2 * std::sin (juce::MathConstants<double>::twoPi * midiToHz (bassNote) * t) * std::exp (-sinceBeat * 3.0);
            sample += (beat % beatsPerBar == 0 ? 0.7 : 0.5) * std::exp (-sinceBeat * 40.0) * (std::sin (juce::MathConstants<double>::twoPi * 60.0 * sinceBeat)
                                                             + 0.3 * (random.nextFloat() * 2.0f - 1.0f));
            audio[(size_t) i] = (float) (0.5 * sample);
        }

        return audio;
    }

    void testChordAnalysis()
    {
        std::cout << "Chord analysis" << std::endl;

        constexpr double sampleRate = 44100.0, bpm = 120.0;
        constexpr int bars = 16;
        const auto beatSeconds = 60.0 / bpm;
        const std::vector<TestChord> progression { { 0, false, -1 }, { 9, true, -1 }, { 5, false, -1 }, { 7, false, -1 } };
        const auto audio = synthesiseBand (progression, bars, sampleRate);
        const auto length = (int) audio.size();
        const auto track = analyseChords (audio.data(), length, sampleRate);

        const auto tempoOk = std::abs (track.bpm - bpm) < 3.0 || std::abs (track.bpm - bpm / 2) < 1.5 || std::abs (track.bpm - bpm * 2) < 6.0;
        check (tempoOk, "tempo detected: " + juce::String (track.bpm, 1) + " bpm (want 120, or half/double)");
        check (std::abs (track.tuningCents) < 10.0, "tuning near 0 cents: " + juce::String (track.tuningCents, 1));

        auto correct = 0, total = 0;

        for (int beat = 4; beat < bars * 4 - 1; ++beat)   // skip the first bar while things settle
        {
            const auto t = (beat + 0.5) * beatSeconds;
            const auto& truth = progression[(size_t) ((beat / 4) % 4)];
            const auto index = track.chordIndexAt (t);
            ++total;

            if (index >= 0)
            {
                const auto& c = track.chords[(size_t) index];
                const auto minor = c.quality == ChordTrack::minor || c.quality == ChordTrack::minor7;
                correct += (c.root == truth.root && minor == truth.minor) ? 1 : 0;
            }
        }

        check (correct >= total * 9 / 10, "chords correct on " + juce::String (correct) + " of " + juce::String (total) + " beats");

        auto slashes = 0;

        for (const auto& c : track.chords)
            slashes += c.bass >= 0 ? 1 : 0;

        check (slashes == 0, "no slash chords when the bass plays roots: " + juce::String (slashes));

        juce::StringArray names;

        for (const auto& c : track.chords)
            names.add (track.chordName (c));

        std::cout << "        detected: " << names.joinIntoString (" ").substring (0, 120) << std::endl;

        auto downbeatsOk = 0, downbeatsSeen = 0;

        for (size_t i = 0; i < track.beats.size(); ++i)
        {
            if (track.beats[i] < 2.0 || ! track.isDownbeat ((int) i))
                continue;

            ++downbeatsSeen;
            const auto barPosition = std::fmod (track.beats[i], beatSeconds * 4);
            downbeatsOk += (barPosition < 0.1 || barPosition > beatSeconds * 4 - 0.1) ? 1 : 0;
        }

        check (downbeatsSeen > 0 && downbeatsOk >= downbeatsSeen * 9 / 10,
               "downbeats on bar starts: " + juce::String (downbeatsOk) + " of " + juce::String (downbeatsSeen));

        {
            ChordTrack grid;   // 4/4 at 120 bpm, first downbeat on the second beat (pickup)
            for (int i = 0; i < 17; ++i)
            {
                grid.beats.push_back (0.25 + i * 0.5);
                grid.beatPositions.push_back ((i + 3) % 4);
            }

            using U = ChordTrack::GridUnit;
            check (std::abs (grid.snapToGrid (2.6, U::bar) - 2.75) < 1.0e-9 && std::abs (grid.snapToGrid (1.6, U::bar) - 0.75) < 1.0e-9
                       && std::abs (grid.snapToGrid (1.6, U::beat) - 1.75) < 1.0e-9 && grid.snapToGrid (0.2, U::bar) == 0.0,
                   "snap to bars/beats: 2.6->" + juce::String (grid.snapToGrid (2.6, U::bar)) + " 1.6->" + juce::String (grid.snapToGrid (1.6, U::bar))
                       + " (beat " + juce::String (grid.snapToGrid (1.6, U::beat)) + ")");
            check (std::abs (grid.neighbourGridLine (0.75, U::bar, 1) - 2.75) < 1.0e-9 && std::abs (grid.neighbourGridLine (2.75, U::bar, -1) - 0.75) < 1.0e-9
                       && grid.neighbourGridLine (8.25, U::bar, 1) < 0.0,
                   "neighbouring bar lines");
        }

        ChordTrack::Chord bFlat { 0.0, 1.0, 10, ChordTrack::major, -1, 0, 4 };
        ChordTrack spelling;
        spelling.keyRoot = 5;   // F major: flats
        check (spelling.chordName (bFlat) == "Bb" && spelling.chordName (bFlat, 2) == "C", "names follow key and transpose: "
               + spelling.chordName (bFlat) + ", " + spelling.chordName (bFlat, 2));
        spelling.keyRoot = 4;   // E major: sharps
        check (spelling.chordName (bFlat) == "A#", "sharp spelling in E major: " + spelling.chordName (bFlat));
    }

    /** Descending bass line C | G/B | Am | C/G: inversions must keep their chord and show the bass. */
    void testSlashChords()
    {
        std::cout << "Slash chords" << std::endl;

        constexpr double sampleRate = 44100.0;
        constexpr int bars = 16;
        const std::vector<TestChord> progression { { 0, false, -1 }, { 7, false, 11 }, { 9, true, -1 }, { 0, false, 7 } };
        const auto audio = synthesiseBand (progression, bars, sampleRate);
        const auto track = analyseChords (audio.data(), (int) audio.size(), sampleRate);

        auto correct = 0, total = 0;

        for (int bar = 1; bar < bars - 1; ++bar)
        {
            const auto& truth = progression[(size_t) (bar % 4)];
            const auto index = track.chordIndexAt ((bar * 4 + 2) * 0.5);
            ++total;

            if (index >= 0)
            {
                const auto& c = track.chords[(size_t) index];
                const auto minor = c.quality == ChordTrack::minor || c.quality == ChordTrack::minor7;
                correct += (c.root == truth.root && minor == truth.minor && c.bass == truth.bass) ? 1 : 0;
            }
        }

        juce::StringArray names;

        for (const auto& c : track.chords)
            names.add (track.chordName (c));

        check (correct >= total - 1, "chord and bass correct in " + juce::String (correct) + " of " + juce::String (total) + " bars");
        std::cout << "        detected: " << names.joinIntoString (" ").substring (0, 120) << std::endl;

        ChordTrack::Chord gOverB { 0.0, 1.0, 7, ChordTrack::major, 11, 0, 4 };
        ChordTrack spelling;
        check (spelling.chordName (gOverB) == "G/B" && spelling.chordName (gOverB, 3) == "Bb/D",
               "slash names transpose: " + spelling.chordName (gOverB) + ", " + spelling.chordName (gOverB, 3));
    }

    /** Odd meters: chords change once per bar of 3, 5 or 7 beats. */
    void testMeters()
    {
        std::cout << "Meters" << std::endl;

        const std::vector<TestChord> progression { { 0, false, -1 }, { 9, true, -1 }, { 5, false, -1 }, { 7, false, -1 } };

        struct Case { int beatsPerBar; double beatSeconds; const char* name; };

        for (auto c : { Case { 4, 0.5, "4/4" }, Case { 3, 0.5, "3/4" }, Case { 5, 0.5, "5/4" },
                        Case { 7, 0.3, "7/8" }, Case { 7, 0.25, "7/8" }, Case { 5, 0.25, "5/8" } })
        {
            const auto bars = juce::jmax (12, 96 / c.beatsPerBar);
            const auto audio = synthesiseBand (progression, bars, 44100.0, c.beatsPerBar, c.beatSeconds);
            const auto track = analyseChords (audio.data(), (int) audio.size(), 44100.0);
            check (track.timeSignature() == c.name, juce::String (c.name) + " (pulse " + juce::String (60.0 / c.beatSeconds, 0)
                                                        + " bpm): detected " + track.timeSignature() + " at " + juce::String (track.bpm, 1) + " bpm");
        }

        // Meter change: 16 bars of 7/4, 16 of 4/4, 16 of 7/4 (like "Money").
        {
            auto audio = synthesiseBand (progression, 16, 44100.0, 7, 0.5);
            const auto middle = synthesiseBand ({ { 2, false, -1 }, { 7, false, -1 } }, 16, 44100.0, 4, 0.5);
            audio.insert (audio.end(), middle.begin(), middle.end());
            const auto end = synthesiseBand (progression, 16, 44100.0, 7, 0.5);
            audio.insert (audio.end(), end.begin(), end.end());
            const auto track = analyseChords (audio.data(), (int) audio.size(), 44100.0);

            auto meterAt = [&track] (double seconds)
            {
                const auto& b = track.beats;
                const auto index = (int) std::distance (b.begin(), std::upper_bound (b.begin(), b.end(), seconds)) - 1;
                return track.timeSignatureAt (index);
            };

            const auto first = meterAt (28.0), second = meterAt (56.0 + 16.0), third = meterAt (56.0 + 32.0 + 28.0);
            check (first == "7/4" && second == "4/4" && third == "7/4",
                   "meter change 7/4 -> 4/4 -> 7/4: detected " + first + " -> " + second + " -> " + third);
        }
    }

    //==============================================================================
    /** Drives a metronome + looper the way the app's audio callback does. */
    struct LooperRig
    {
        static constexpr double rate = 48000.0;
        MetronomeProcessor metronome;
        Looper looper { metronome };
        juce::int64 time = 0;
        std::vector<float> out;     // output (left channel) by graph time
        bool beforeFx = false;
        int blockSize = 480;        // deliberately not a divisor of the bar length

        LooperRig()
        {
            metronome.prepareToPlay (rate, 1024);
            looper.prepare (rate, 1024);
            metronome.gain = 0.0f;
            metronome.startBpm = 120.0;     // 4/4: a bar is 2 s = 96000 samples
        }

        template <typename Input>
        void runUntil (double seconds, Input input)
        {
            const auto until = (juce::int64) (seconds * rate);
            juce::AudioBuffer<float> click (2, 1024), audio (2, 1024);
            juce::MidiBuffer midi;

            if ((juce::int64) out.size() < until)
                out.resize ((size_t) until, 0.0f);

            while (time < until)
            {
                const auto n = (int) juce::jmin ((juce::int64) blockSize, until - time);
                metronome.advanceClock (time, n);
                looper.beginBlock (time, n);

                for (int i = 0; i < n; ++i)
                {
                    const auto v = input (time + i);
                    audio.setSample (0, i, v);
                    audio.setSample (1, i, v);
                }

                if (beforeFx)
                    looper.processBeforeFx (audio.getWritePointer (0), n);
                else
                    looper.processAfterFx (audio.getWritePointer (0), audio.getWritePointer (1), n);

                click.setSize (2, n, false, false, true);
                metronome.processBlock (click, midi);

                for (int i = 0; i < n; ++i)
                    out[(size_t) (time + i)] = audio.getSample (0, i);

                time += n;
            }
        }

        /** Largest difference between the output and `expected` over [from, to) seconds. */
        template <typename Expected>
        float maxError (double from, double to, Expected expected) const
        {
            float worst = 0.0f;

            for (auto g = (juce::int64) (from * rate); g < (juce::int64) (to * rate); ++g)
                worst = juce::jmax (worst, std::abs (out[(size_t) g] - expected (g)));

            return worst;
        }
    };

    float noiseAt (juce::int64 g, int seed = 1)
    {
        return (float) (((g * 7919 + seed * 104729) % 10007) / 10007.0 - 0.5);
    }

    void testLooper()
    {
        std::cout << "Looper" << std::endl;
        constexpr double rate = LooperRig::rate;
        constexpr double bar = 2.0 * rate;
        const auto silence = [] (juce::int64) { return 0.0f; };

        // The loop holds what was played from `origin`, and repeats with `period`.
        auto loopOf = [] (double origin, double period, int seed = 1)
        {
            return [origin, period, seed] (juce::int64 g)
            {
                auto offset = std::fmod ((double) g - origin, period);
                offset = offset < 0 ? offset + period : offset;
                return noiseAt ((juce::int64) origin + (juce::int64) offset, seed);
            };
        };

        {
            // Fixed 2 bars, pressed 0.1 s late: starts retroactively on the first bar line.
            LooperRig rig;
            rig.looper.fixedBars = 2;
            rig.metronome.start();
            rig.runUntil (0.1, [] (juce::int64 g) { return noiseAt (g); });
            rig.looper.pressTrack (0);
            rig.runUntil (4.05, [] (juce::int64 g) { return noiseAt (g); });
            check (rig.looper.getState (0) == Looper::State::playing, "fixed 2-bar loop plays after 2 bars");
            check (rig.looper.getBars (0) == 2, "loop is 2 bars: " + juce::String (rig.looper.getBars (0)));
            check (rig.metronome.isTempoLocked(), "tempo locked while loops exist");
            rig.runUntil (12.0, silence);
            const auto error = rig.maxError (4.0, 12.0, loopOf (0.0, 2 * bar));
            check (error < 1.0e-6f, "late press: loop starts on the bar line and repeats exactly (error " + juce::String (error) + ")");
        }

        {
            // Latency compensation: the guitar arrives 25 ms late, the loop must still sit on the grid.
            LooperRig rig;
            const auto latency = 1200;
            rig.looper.fixedBars = 1;
            rig.looper.deviceLatencySamples = latency;
            rig.metronome.start();
            rig.runUntil (1.9, silence);
            rig.looper.pressTrack (0);      // early press: waits for the bar line at 2 s
            auto late = [latency] (juce::int64 g) { return g >= 96000 + latency && g < 2 * 96000 + latency ? noiseAt (g - latency) : 0.0f; };
            rig.runUntil (8.0, late);
            const auto error = rig.maxError (4.0, 8.0, loopOf (bar, bar));
            check (error < 1.0e-6f, "latency-compensated loop sits on the grid (error " + juce::String (error) + ")");
        }

        {
            // Free length: stop pressed 0.1 s after the 2nd bar line -> exactly 2 bars.
            LooperRig rig;
            rig.metronome.start();
            rig.runUntil (0.02, [] (juce::int64 g) { return noiseAt (g); });
            rig.looper.pressTrack (0);
            rig.runUntil (4.1, [] (juce::int64 g) { return noiseAt (g); });
            check (rig.looper.getState (0) == Looper::State::recording, "free recording keeps going");
            rig.looper.pressTrack (0);
            rig.runUntil (12.0, silence);
            check (rig.looper.getBars (0) == 2, "free stop rounds to 2 bars: " + juce::String (rig.looper.getBars (0)));
            const auto error = rig.maxError (4.2, 12.0, loopOf (0.0, 2 * bar));
            check (error < 1.0e-6f, "free loop repeats exactly (error " + juce::String (error) + ")");
        }

        {
            // Count-in: metronome stopped, the press starts it and recording begins one bar later.
            LooperRig rig;
            rig.looper.fixedBars = 1;
            rig.runUntil (0.5, silence);
            rig.looper.pressTrack (0);
            rig.runUntil (2.0, silence);
            check (rig.metronome.isRunning(), "pressing record starts the metronome");
            check (rig.looper.getState (0) == Looper::State::armed, "armed during the count-in bar");
            rig.runUntil (8.0, [] (juce::int64 g) { return noiseAt (g); });
            const auto start = 0.5 * rate + bar;    // first block after the press starts the metronome
            const auto error = rig.maxError (5.0, 8.0, loopOf (start, bar));
            check (error < 1.0e-6f, "recording starts after one bar of count-in (error " + juce::String (error) + ")");
        }

        {
            // Overdub, undo, and the merge of an older overdub.
            LooperRig rig;
            rig.looper.fixedBars = 1;
            rig.metronome.start();
            rig.looper.pressTrack (0);
            rig.runUntil (2.0, [] (juce::int64 g) { return noiseAt (g); });
            rig.runUntil (3.0, silence);

            // Overdub input repeats with the loop, so what lands at each loop position is predictable.
            rig.looper.pressTrack (0);      // overdub for exactly one cycle, 3 s -> 5 s
            rig.runUntil (5.0, [] (juce::int64 g) { return noiseAt (g % 96000, 2); });
            rig.looper.pressTrack (0);
            check (rig.looper.canUndo(), "an overdub can be undone");
            rig.runUntil (8.0, silence);

            auto base = loopOf (0.0, bar);
            auto dub = loopOf (0.0, bar, 2);
            auto both = [&] (juce::int64 g) { return base (g) + dub (g); };
            auto error = rig.maxError (6.0, 8.0, both);
            check (error < 1.0e-5f, "overdub adds to the loop (error " + juce::String (error) + ")");

            rig.looper.undo();
            rig.runUntil (10.0, silence);
            error = rig.maxError (8.1, 10.0, base);
            check (error < 1.0e-6f, "undo removes the overdub (error " + juce::String (error) + ")");

            // Two overdubs: starting the second merges the first; undo then removes only the second.
            rig.looper.pressTrack (0);
            rig.runUntil (12.0, [] (juce::int64 g) { return noiseAt (g % 96000, 2); });
            rig.looper.pressTrack (0);
            rig.looper.pressTrack (0);
            rig.runUntil (14.0, [] (juce::int64 g) { return noiseAt (g % 96000, 3); });
            rig.looper.pressTrack (0);
            rig.runUntil (16.0, silence);
            rig.looper.undo();
            rig.runUntil (20.0, silence);
            error = rig.maxError (16.1, 20.0, both);
            check (error < 1.0e-5f, "first overdub merged, second undone (error " + juce::String (error) + ")");
        }

        {
            // A dated press (footswitch MIDI timestamp): the overdub starts and stops at the stomp,
            // not when the command reaches the audio thread 0.1 s later.
            LooperRig rig;
            rig.looper.fixedBars = 1;
            rig.metronome.start();
            const auto input = [] (juce::int64 g)
            {
                return g < 96000 ? noiseAt (g) : g < 144000 ? 0.0f : noiseAt (g % 96000, 2);
            };
            rig.runUntil (0.1, input);
            rig.looper.pressTrack (0);
            rig.runUntil (3.1, input);

            // The rig's next block is 480 samples (10 ms): a press 0.11 s ago maps to 3.0 s.
            const auto wallNow = [] { return juce::Time::getMillisecondCounterHiRes() * 0.001; };
            rig.looper.pressTrack (0, wallNow() - 0.11);
            rig.runUntil (5.1, input);
            rig.looper.pressTrack (0, wallNow() - 0.11);
            rig.runUntil (8.0, silence);

            float worst = 0.0f;

            for (auto g = (juce::int64) (6.0 * rate); g < (juce::int64) (8.0 * rate); ++g)
            {
                const auto position = g % 96000;

                if (std::abs (position - 48000) < 240)
                    continue;       // the stomp itself: the wall clock is only good to a few ms here

                worst = juce::jmax (worst, std::abs (rig.out[(size_t) g] - (noiseAt (position) + noiseAt (position, 2))));
            }

            check (worst < 1.0e-5f, "dated overdub covers exactly the stomped cycle (error " + juce::String (worst) + ")");
        }

        {
            // Second track: 2 bars over a 1-bar loop, started late in a bar; both stay on the grid.
            LooperRig rig;
            rig.looper.fixedBars = 1;
            rig.metronome.start();
            rig.looper.pressTrack (0);
            rig.runUntil (2.0, [] (juce::int64 g) { return noiseAt (g); });
            rig.looper.fixedBars = 2;
            rig.runUntil (5.0, silence);
            rig.looper.pressTrack (1);      // 1 s into a bar: waits for 6 s
            rig.runUntil (10.0, [] (juce::int64 g) { return g >= 6 * 48000 ? noiseAt (g, 4) : 0.0f; });
            rig.runUntil (16.0, silence);
            auto a = loopOf (0.0, bar);
            auto b = loopOf (6.0 * rate, 2 * bar, 4);
            const auto error = rig.maxError (10.0, 16.0, [&] (juce::int64 g) { return a (g) + b (g); });
            check (error < 1.0e-5f, "two tracks of different lengths stay on the grid (error " + juce::String (error) + ")");

            // Stop and restart the metronome: the clicks come back on the loop's bar lines.
            rig.metronome.stop();
            rig.runUntil (16.73, silence);
            rig.metronome.start();
            rig.runUntil (17.0, silence);
            const auto barStart = rig.metronome.getClockBarStart();
            check (std::abs (std::fmod (barStart, bar)) < 1.0e-6 && std::abs (rig.metronome.getCurrentBpm() - 120.0) < 1.0e-6,
                   "restarted metronome follows the loop grid (bar start " + juce::String (barStart / rate, 4) + " s)");

            rig.looper.clearAll();
            rig.runUntil (17.1, silence);
            check (rig.looper.isEmpty() && ! rig.metronome.isTempoLocked(), "clear all frees the tempo");
        }

        {
            // Before FX: loops are mixed into the dry signal that feeds the plugins.
            LooperRig rig;
            rig.beforeFx = true;
            rig.looper.source = (int) Looper::Source::beforeFx;
            rig.looper.fixedBars = 1;
            rig.metronome.start();
            rig.looper.pressTrack (0);
            rig.runUntil (2.0, [] (juce::int64 g) { return noiseAt (g); });
            rig.runUntil (6.0, [] (juce::int64 g) { return 0.25f * noiseAt (g, 5); });
            auto expected = [loop = loopOf (0.0, bar)] (juce::int64 g) { return loop (g) + 0.25f * noiseAt (g, 5); };
            const auto error = rig.maxError (2.0, 6.0, expected);
            check (rig.looper.getActiveSource() == Looper::Source::beforeFx && error < 1.0e-5f,
                   "before-FX loop plays under the live dry guitar (error " + juce::String (error) + ")");
        }
    }


    /** Metronome following the song's beat grid through the player, at two speeds,
        in both processing orders, and across a loop jump. */
    /** copyLoops() waits for the audio thread: run it on another thread while the rig plays. */
    template <typename Input>
    bool copyWhilePlaying (LooperRig& rig, LoopSet& set, juce::String& error, Input input)
    {
        std::atomic<bool> done { false };
        bool ok = false;
        std::thread copier ([&] { ok = rig.looper.copyLoops (set, error); done = true; });

        while (! done.load())
            rig.runUntil ((double) rig.time / LooperRig::rate + 0.01, input);

        copier.join();
        return ok;
    }

    void testLoopFiles()
    {
        std::cout << "Saving and loading loops" << std::endl;
        constexpr double rate = LooperRig::rate;
        constexpr int bar = 96000;      // 120 bpm 4/4
        const auto input = [] (juce::int64 g) { return g < bar ? noiseAt (g, 1) : noiseAt (g, 2); };

        // Track 1: 1 bar from 0 s. Track 2: 2 bars from the bar line at 2 s.
        LooperRig rig;
        rig.looper.fixedBars = 1;
        rig.metronome.start();
        rig.runUntil (0.05, input);
        rig.looper.pressTrack (0);
        rig.runUntil (2.0, input);
        rig.looper.fixedBars = 2;
        rig.looper.pressTrack (1);

        {
            LoopSet busy;
            juce::String error;
            rig.runUntil (3.0, input);
            const auto copied = copyWhilePlaying (rig, busy, error, input);
            check (! copied && error.isNotEmpty(), "no copy while recording: \"" + error + "\"");
        }

        rig.runUntil (8.0, input);
        rig.looper.setTrackGain (0, 0.5f);

        LoopSet set;
        juce::String error;
        check (copyWhilePlaying (rig, set, error, input), "loops copied" + (error.isEmpty() ? juce::String() : ": " + error));
        check (set.getNumLoops() == 2 && set.loops[0].bars == 1 && set.loops[1].bars == 2 && std::abs (set.getBpm() - 120.0) < 1.0e-6,
               "copy has 2 loops (1 and 2 bars) at 120 bpm");

        // Both start on the shared bar line (grid time 0): track 2 is rotated by one bar.
        auto expected0 = [] (int i) { return noiseAt (i % bar, 1); };
        auto expected1 = [] (int i) { return noiseAt (bar + ((i - bar) % (2 * bar) + 2 * bar) % (2 * bar), 2); };
        float worst = 0.0f;

        for (int i = 0; i < bar; ++i)
            worst = juce::jmax (worst, std::abs (set.loops[0].audio.getSample (0, i) - expected0 (i)));

        for (int i = 0; i < 2 * bar; ++i)
            worst = juce::jmax (worst, std::abs (set.loops[1].audio.getSample (1, i) - expected1 (i)));

        check (set.loops[1].audio.getNumSamples() == 2 * bar && worst < 1.0e-6f,
               "copied loops are aligned to the shared bar line (error " + juce::String (worst) + ")");

        // The mix: 2 bars (least common multiple), track 1 at half volume.
        const auto mix = LoopFiles::renderMix (set);
        worst = 0.0f;

        for (int i = 0; i < mix.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (mix.getSample (0, i) - (0.5f * expected0 (i) + expected1 (i))));

        check (mix.getNumSamples() == 2 * bar && worst < 1.0e-5f, "mix: 2 bars, volumes applied (error " + juce::String (worst) + ")");

        // Save and load again: identical audio and settings.
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-loops-test");
        folder.deleteRecursively();
        {
            MetronomeProcessor m;
            DrumSequencer d (m);
            d.setPattern (DrumPatterns::builtIn()[6]);
            set.drums = std::shared_ptr<juce::XmlElement> (d.createStateXml().release());
        }
        const auto saved = LoopFiles::save (set, folder);
        check (saved.wasOk() && folder.getChildFile ("mix.wav").existsAsFile() && folder.getChildFile ("track 2.wav").existsAsFile(),
               "session saved" + (saved.wasOk() ? juce::String() : ": " + saved.getErrorMessage()));

        LoopSet loaded;
        const auto read = LoopFiles::load (folder, loaded);
        bool same = read.wasOk() && loaded.getNumLoops() == 2 && std::abs (loaded.barSamples - set.barSamples) < 1.0e-6
                    && loaded.beatsPerBar == 4 && std::abs (loaded.loops[0].gain - 0.5f) < 1.0e-6f;

        for (int t = 0; t < 2 && same; ++t)
            for (int c = 0; c < 2 && same; ++c)
                for (int i = 0; i < set.loops[(size_t) t].audio.getNumSamples() && same; ++i)
                    same = loaded.loops[(size_t) t].audio.getSample (c, i) == set.loops[(size_t) t].audio.getSample (c, i);

        check (same, "loaded session is identical" + (read.wasOk() ? juce::String() : ": " + read.getErrorMessage()));
        check (loaded.drums != nullptr && loaded.drums->getChildByName ("DRUM_PATTERN") != nullptr
                   && loaded.drums->getChildByName ("DRUM_PATTERN")->getStringAttribute ("name") == DrumPatterns::builtIn()[6].name,
               "the session keeps its drum pattern");
        folder.deleteRecursively();

        // Load into a fresh looper: tempo set, loops start on the metronome's first downbeat.
        LooperRig fresh;
        fresh.metronome.startBpm = 90.0;
        fresh.metronome.beatsPerBar = 3;
        check (fresh.looper.loadLoops (loaded, error), "loaded into the looper" + (error.isEmpty() ? juce::String() : ": " + error));
        fresh.runUntil (10.0, [] (juce::int64) { return 0.0f; });
        const auto start = fresh.metronome.getClockStartTime();
        check (fresh.looper.getState (0) == Looper::State::playing && fresh.looper.getState (1) == Looper::State::playing
                 && std::abs (fresh.metronome.getCurrentBpm() - 120.0) < 1.0e-6 && fresh.metronome.getClockBeatsPerBar() == 4,
               "both tracks play at the saved tempo and meter");

        worst = 0.0f;

        for (auto g = (juce::int64) start; g < (juce::int64) (10.0 * rate); ++g)
        {
            const auto i = (int) (g - (juce::int64) start);
            worst = juce::jmax (worst, std::abs (fresh.out[(size_t) g] - (0.5f * expected0 (i) + expected1 (i))));
        }

        check (start > 0.0 && worst < 1.0e-5f, "loaded loops play in time from the downbeat (error " + juce::String (worst) + ")");

        LoopSet again;
        check (! fresh.looper.loadLoops (loaded, error), "loading into a looper with loops is refused");
    }

    void testPlayHead()
    {
        std::cout << "Play head (bar clock -> plugins)" << std::endl;

        const double rate = 48000.0;
        const int block = 480;
        MetronomeProcessor metronome;
        metronome.prepareToPlay (rate, block);
        metronome.startBpm = 120.0;
        metronome.beatsPerBar = 4;

        juce::int64 time = 0;
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;

        // Like the app: advance the clock, read the play head, then run the graph.
        auto step = [&]
        {
            metronome.advanceClock (time, block);
            const auto s = BarClockPlayHead::compute (metronome, time, rate);
            metronome.processBlock (buffer, midi);
            time += block;
            return s;
        };

        auto s = step();
        check (! s.playing && std::abs (s.bpm - 120.0) < 1.0e-9 && s.numerator == 4, "stopped: not playing, shows the set tempo and meter");

        metronome.start();
        s = step();
        check (s.playing && std::abs (s.ppq) < 1.0e-9 && s.ppqOfBarStart == 0.0, "starts at ppq 0, bar 1");

        // 2 s at 120 bpm = 4 beats = one bar
        for (int i = 1; i < 200; ++i)
            s = step();

        s = step();
        check (std::abs (s.ppq - 4.0) < 1.0e-6 && std::abs (s.ppqOfBarStart - 4.0) < 1.0e-9 && s.barCount == 1,
               "after 2 s: ppq " + juce::String (s.ppq, 4) + ", bar start " + juce::String (s.ppqOfBarStart) + " (want 4, 4)");

        // Tempo changes (user + trainer steps) keep the position continuous and bars whole.
        metronome.trainerEnabled = true;
        metronome.trainerStepBpm = 7.0;
        metronome.trainerInterval = 1.0;
        metronome.trainerUnit = (int) MetronomeProcessor::IntervalUnit::bars;
        metronome.trainerMaxBpm = 200.0;
        metronome.startBpm = 90.0;
        metronome.tempoChangedByUser();

        bool continuous = true, barsOk = true;
        auto previous = s;

        for (int i = 0; i < 2000; ++i)
        {
            s = step();
            const auto delta = s.ppq - previous.ppq;
            const auto slowest = block / (rate * 60.0 / 90.0), fastest = block / (rate * 60.0 / 200.0);

            if (continuous && ! (delta > slowest * 0.999 && delta < fastest * 1.001))
                std::cout << "    jump at block " << i << ": delta " << delta << " ppq " << s.ppq << " bpm " << s.bpm << " prev bpm " << previous.bpm << std::endl;

            continuous = continuous && delta > slowest * 0.999 && delta < fastest * 1.001;
            barsOk = barsOk && std::fmod (s.ppqOfBarStart, 4.0) < 1.0e-6 && s.ppq - s.ppqOfBarStart >= -1.0e-6
                            && s.ppq - s.ppqOfBarStart < 4.0 + 1.0e-6;
            previous = s;
        }

        check (continuous, "ppq advances smoothly through tempo changes (now " + juce::String (s.bpm, 1) + " bpm)");
        check (barsOk, "bar starts stay on whole 4/4 bars");

        metronome.stop();
        s = step();
        check (! s.playing, "metronome stopped: plugins see the transport stop");
    }

    void testFollowSong()
    {
        std::cout << "Metronome follows song" << std::endl;
        constexpr double rate = 48000.0;
        constexpr int blockSize = 512;

        auto grid = std::make_shared<ChordTrack>();

        for (int i = 0; i < 40; ++i)
        {
            grid->beats.push_back (0.5 * i);
            grid->beatPositions.push_back (i % 4);
            grid->beatMeters.push_back (4);
        }

        struct Case { double speed; bool playerFirst; bool loop; const char* name; };

        for (auto c : { Case { 1.0, true, false, "1x, player first" }, Case { 1.0, false, false, "1x, metronome first" },
                        Case { 0.75, true, false, "0.75x" }, Case { 1.0, true, true, "loop 1-3 s" } })
        {
            PlayerProcessor player;
            MetronomeProcessor metronome;
            player.prepareToPlay (rate, blockSize);
            metronome.prepareToPlay (rate, blockSize);
            player.setSong (makeSine (44100.0, 220.0, 12.0));
            player.setSpeed (c.speed);

            if (c.loop)
            {
                player.setLoopSeconds (1.0, 3.0);
                player.setLoopEnabled (true);
            }

            metronome.setPlayer (&player);
            metronome.setSongGrid (grid);
            metronome.followSong = true;
            metronome.sound = (int) MetronomeProcessor::Sound::beep;
            metronome.start();
            player.play();

            juce::AudioBuffer<float> playerBlock (2, blockSize), clickBlock (2, blockSize);
            juce::MidiBuffer midi;
            std::vector<float> clicks;
            std::vector<std::pair<juce::int64, double>> heard;   // (sample index at block end, heard song time)
            const auto totalBlocks = (int) (7.0 * rate / blockSize);

            for (int b = 0; b < totalBlocks; ++b)
            {
                if (c.playerFirst)
                {
                    player.processBlock (playerBlock, midi);
                    metronome.processBlock (clickBlock, midi);
                }
                else
                {
                    metronome.processBlock (clickBlock, midi);
                    player.processBlock (playerBlock, midi);
                }

                heard.push_back ({ (juce::int64) (b + 1) * blockSize, player.getPositionSeconds() });

                for (int i = 0; i < blockSize; ++i)
                    clicks.push_back (clickBlock.getSample (0, i));
            }

            // Click onsets, and the song time heard at each.
            std::vector<double> songTimes;
            std::vector<float> peaks;
            auto quiet = 1 << 30;

            for (size_t i = 0; i < clicks.size(); ++i)
            {
                if (std::abs (clicks[i]) > 0.05f)
                {
                    if (quiet > (int) (0.05 * rate))
                    {
                        const auto block = i / blockSize;
                        const auto t = heard[block].second - (double) (heard[block].first - (juce::int64) i) * c.speed / rate;
                        songTimes.push_back (t);
                        auto peak = 0.0f;

                        for (size_t j = i; j < juce::jmin (clicks.size(), i + 200); ++j)
                            peak = juce::jmax (peak, std::abs (clicks[j]));

                        peaks.push_back (peak);
                    }

                    quiet = 0;
                }
                else
                {
                    ++quiet;
                }
            }

            // Skip the first second (the stretcher fills up), then every click must sit on a beat.
            auto onBeat = 0, considered = 0, accentsOk = 0, loopStarts = 0;

            for (size_t k = 0; k < songTimes.size(); ++k)
            {
                if (songTimes[k] < 0.9 && ! c.loop)
                    continue;

                ++considered;
                const auto nearest = std::round (songTimes[k] / 0.5) * 0.5;
                onBeat += std::abs (songTimes[k] - nearest) < 0.015 ? 1 : 0;

                const auto isDownbeat = ((int) std::lround (nearest / 0.5)) % 4 == 0;
                accentsOk += (isDownbeat == (peaks[k] > 0.55f)) ? 1 : 0;
                loopStarts += c.loop && std::abs (songTimes[k] - 1.0) < 0.03 ? 1 : 0;
            }

            check (considered > 4 && onBeat == considered && accentsOk == considered,
                   juce::String (c.name) + ": " + juce::String (onBeat) + "/" + juce::String (considered) + " clicks on beats, "
                       + juce::String (accentsOk) + " with the right accent");

            if (c.loop)
                check (loopStarts >= 2, "loop restart clicks on its first beat: " + juce::String (loopStarts) + " times");

            if (c.speed != 1.0)
                check (std::abs (metronome.getCurrentBpm() - 120.0 * c.speed) < 1.0, "shown tempo follows speed: " + juce::String (metronome.getCurrentBpm(), 1));
        }
    }
    void testFootswitch()
    {
        std::cout << "Footswitch" << std::endl;
        using Kind = FootswitchAction::Kind;

        auto cc = [] (int number, int value) { return juce::MidiMessage::controllerEvent (1, number, value); };

        struct Rig
        {
            Footswitch fs;
            std::vector<juce::String> fired;
            Rig() { fs.perform = [this] (FootswitchAction a, double) { fired.push_back (a.toString()); }; }
        };

        // Momentary CC: learned from press + quick release; tap on press, hold after 1 s, once.
        {
            Rig r;
            r.fs.startLearning (0);
            check (r.fs.handleMidi (cc (20, 127), 10.0), "learning takes the first message");
            check (r.fs.getPendingIndex() == 0, "CC switch: waiting to see a release");
            r.fs.handleMidi (cc (20, 0), 10.15);
            check (r.fs.getPendingIndex() < 0 && r.fs.getSwitch (0).momentary, "quick release = momentary");
            check (r.fired.empty(), "learning fires no action");

            r.fs.handleMidi (cc (20, 127), 20.0);
            check (r.fired.size() == 1 && r.fired[0] == "press:0", "tap fires track 1 on the press");
            r.fs.update (20.5);
            check (r.fired.size() == 1, "no hold after 0.5 s");
            r.fs.update (21.05);
            r.fs.update (21.5);
            check (r.fired.size() == 2 && r.fired[1] == "clear:0", "hold clears track 1 once after 1 s");
            r.fs.handleMidi (cc (20, 0), 22.0);
            r.fs.handleMidi (cc (20, 127), 23.0);
            r.fs.handleMidi (cc (20, 0), 23.2);
            r.fs.update (24.5);
            check (r.fired.size() == 3 && r.fired[2] == "press:0", "short tap: no hold");
            check (! r.fs.handleMidi (cc (21, 127), 25.0) && r.fired.size() == 3, "unlearned CC is ignored");
        }

        // Latching CC: no release within the window -> every message (127 or 0) is a tap, no holds.
        {
            Rig r;
            r.fs.startLearning (1);
            r.fs.handleMidi (cc (30, 127), 10.0);
            r.fs.update (11.0);
            check (r.fs.getPendingIndex() == 1, "still checking after 1 s");
            r.fs.update (13.1);
            check (r.fs.getPendingIndex() < 0 && ! r.fs.getSwitch (1).momentary, "no release = latching");
            r.fs.handleMidi (cc (30, 0), 20.0);
            r.fs.handleMidi (cc (30, 127), 21.0);
            r.fs.update (23.0);
            check (r.fired.size() == 2 && r.fired[0] == "press:1" && r.fired[1] == "press:1", "latching: each stomp taps, no hold");
        }

        // A latching switch that was "on" starts with 0: latching at once.
        {
            Rig r;
            r.fs.startLearning (2);
            r.fs.handleMidi (cc (40, 0), 10.0);
            check (r.fs.getPendingIndex() < 0 && ! r.fs.getSwitch (2).momentary, "first value 0 = latching");
        }

        // Notes are momentary; program changes tap. Learning a used message moves it.
        {
            Rig r;
            r.fs.startLearning (0);
            r.fs.handleMidi (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 1.0);
            check (r.fs.getSwitch (0).momentary && r.fs.getSwitch (0).trigger.type == MidiTrigger::Type::note, "note switch is momentary");
            r.fs.startLearning (3);
            r.fs.handleMidi (juce::MidiMessage::programChange (1, 5), 2.0);
            r.fs.setTapAction (3, { Kind::undo, 0 });
            r.fs.handleMidi (juce::MidiMessage::programChange (1, 5), 3.0);
            check (r.fired.size() == 1 && r.fired[0] == "undo", "program change taps the chosen action");

            r.fs.startLearning (1);
            r.fs.handleMidi (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 4.0);
            check (! r.fs.getSwitch (0).trigger.isValid() && r.fs.getSwitch (1).trigger.type == MidiTrigger::Type::note,
                   "re-learning a message moves it to the new switch");
        }

        // Settings round trip
        {
            Footswitch a;
            a.startLearning (2);
            a.handleMidi (cc (64, 0), 1.0);
            a.setHoldAction (2, { Kind::metronome, 0 });
            a.setTapAction (1, { Kind::trackMute, 3 });

            Footswitch b;
            b.fromXml (*a.toXml());
            check (b.getSwitch (2).trigger == a.getSwitch (2).trigger && ! b.getSwitch (2).momentary
                       && b.getSwitch (2).hold.kind == Kind::metronome && b.getSwitch (1).tap.toString() == "mute:3"
                       && b.getSwitch (0).tap.toString() == "press:0" && b.getSwitch (0).hold.toString() == "clear:0",
                   "settings survive a save / load");
        }
    }

    // Runs the metronome's clock and the drums together, as the app does (clock first, then the graph).
    juce::AudioBuffer<float> renderDrums (MetronomeProcessor& metronome, DrumSequencer& drums, double rate, double seconds,
                                          std::function<void (juce::int64)> beforeBlock = {})
    {
        constexpr int block = 512;
        const auto total = (int) (seconds * rate);
        juce::AudioBuffer<float> out (2, total), scratch (2, block);
        juce::MidiBuffer midi;
        out.clear();

        for (int pos = 0; pos < total; pos += block)
        {
            const auto n = juce::jmin (block, total - pos);
            scratch.setSize (2, n, false, false, true);

            metronome.advanceClock (pos, n);

            if (beforeBlock)
                beforeBlock (pos);      // like the looper, after the clock

            metronome.processBlock (scratch, midi);
            drums.processBlock (scratch, midi);

            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, scratch, ch, 0, n);
        }

        return out;
    }

    // Sample positions where a hit starts: loud after at least `quiet` seconds of near silence.
    std::vector<int> findOnsets (const juce::AudioBuffer<float>& audio, double rate, double quiet = 0.1)
    {
        std::vector<int> onsets;
        int lastLoud = -1000000;

        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            const auto level = std::abs (audio.getSample (0, i)) + std::abs (audio.getSample (1, i));

            if (level > 0.01f)
            {
                if (i - lastLoud > (int) (quiet * rate))
                    onsets.push_back (i);

                lastLoud = i;
            }
        }

        return onsets;
    }

    void testDrums()
    {
        std::cout << "Drums" << std::endl;
        constexpr double rate = 48000.0;

        // Patterns
        {
            const auto& builtIn = DrumPatterns::builtIn();
            check (builtIn.size() >= 10 && builtIn.size() <= 16, "a dozen or so starter patterns: " + juce::String ((int) builtIn.size()));
            juce::StringArray names;
            bool allPlay = true, allRoundTrip = true;

            for (const auto& p : builtIn)
            {
                names.addIfNotAlreadyThere (p.name);
                allPlay = allPlay && ! p.isEmpty();
                allRoundTrip = allRoundTrip && DrumPattern::fromXml (*p.toXml()).sameNotes (p);
            }

            check (names.size() == (int) builtIn.size(), "starter pattern names are unique");
            check (allPlay, "every starter pattern has notes");
            check (allRoundTrip, "every starter pattern survives saving and loading");

            const auto& rockFill = builtIn[3];
            check (rockFill.bars == 2 && rockFill.get (1, 12, Drum::tomHigh) == DrumPattern::accent
                       && rockFill.get (0, 0, Drum::crash) == DrumPattern::accent,
                   "text form: bars, steps and levels land where written");

            DrumPattern p;
            p.beatsPerBar = 4; p.ticks = 4; p.bars = 1;
            p.rowFromText (Drum::closedHat, "xxxx xxxx xxxx xxxx");
            p.rowFromText (Drum::kick, "X... .... g... ....");
            p.reshape (4, 2, 2);
            check (p.rowToText (Drum::closedHat) == "xx xx xx xx|xx xx xx xx", "16ths -> 8ths keeps the 8ths, new bar repeats: " + p.rowToText (Drum::closedHat));
            check (p.rowToText (Drum::kick) == "X. .. g. ..|X. .. g. ..", "levels kept when reshaping: " + p.rowToText (Drum::kick));
            p.reshape (3, 3, 1);
            check (p.rowToText (Drum::kick) == "X.. ... g..", "8ths -> triplets keeps the downbeats, 4 -> 3 beats drops the 4th: " + p.rowToText (Drum::kick));
            p.set (0, 4, Drum::snare, DrumPattern::normal);
            p.reshape (3, 3, 2);
            p.set (1, 0, Drum::kick, DrumPattern::off);
            p.copyBarToAll (0);
            check (p.get (1, 0, Drum::kick) == DrumPattern::accent && p.get (1, 4, Drum::snare) == DrumPattern::normal, "copy bar to all");
        }

        // Kits
        {
            const auto synth = synthesiseDrumKit (rate);
            const auto sampled = loadSampledDrumKit (rate);
            check (! synth.isEmpty() && ! sampled.isEmpty(), "both kits have every drum");
            check (sampled.hits[(size_t) Drum::kick].size() == 4 && sampled.hits[(size_t) Drum::crash].size() == 3
                       && sampled.hits[(size_t) Drum::kick][0].size() == 2,
                   "sampled kit: 4 kick layers with 2 takes, 3 crash layers");

            if (! sampled.isEmpty())
            {
                // The sampled kit is stored at 44.1 kHz: resampled, a 0.9 s kick stays 0.9 s long.
                const auto kickSeconds = sampled.hits[(size_t) Drum::kick].back()[0].getNumSamples() / rate;
                check (std::abs (kickSeconds - 0.9) < 0.02, "sampled kick resampled to 48 kHz: " + juce::String (kickSeconds, 3) + " s");
            }

            for (auto [kit, name] : { std::pair { &synth, "synthesised" }, std::pair { &sampled, "sampled" } })
            {
                if (kit->isEmpty())
                    continue;

                const auto& snare = kit->hits[(size_t) Drum::snare];
                const auto softPeak = snare.front()[0].getMagnitude (0, snare.front()[0].getNumSamples());
                const auto loudPeak = snare.back()[0].getMagnitude (0, snare.back()[0].getNumSamples());
                check (softPeak < loudPeak * 0.8f && loudPeak > 0.5f && loudPeak <= 0.9f,
                       juce::String (name) + " snare: soft layer quieter than loud (" + juce::String (softPeak, 2) + " / " + juce::String (loudPeak, 2) + ")");
            }
        }

        auto setUp = [&] (MetronomeProcessor& metronome, DrumSequencer& drums, const juce::String& hats, int ticks, double swing = 0.5)
        {
            metronome.prepareToPlay (rate, 512);
            metronome.startBpm = 120.0;
            metronome.beatsPerBar = 4;
            metronome.clicksMuted = true;
            drums.setRateAndBufferSizeDetails (rate, 512);
            drums.prepareToPlay (rate, 512);
            drums.loadKitNow (DrumKitType::electronic, rate);
            DrumPattern p;
            p.ticks = ticks;
            p.swing = swing;
            p.bars = juce::StringArray::fromTokens (hats, "|", {}).size();
            p.rowFromText (Drum::closedHat, hats);
            drums.setPattern (p);
            drums.enabled = true;
            metronome.start();
        };

        auto describe = [rate] (const std::vector<int>& onsets)
        {
            juce::StringArray s;
            for (auto o : onsets)
                s.add (juce::String (o / rate, 3));
            return s.joinIntoString (" ");
        };

        {
            // Quarter notes at 120 bpm: a hit every 0.5 s, on the clock's sample.
            MetronomeProcessor metronome;
            DrumSequencer drums (metronome);
            setUp (metronome, drums, "x. x. x. x.", 2);
            const auto onsets = findOnsets (renderDrums (metronome, drums, rate, 2.2), rate);
            bool onTime = onsets.size() == 5;

            for (size_t i = 0; i < onsets.size() && onTime; ++i)
                onTime = std::abs (onsets[i] - (int) i * 24000) <= 48;

            check (onTime, "hits on every beat at 120 bpm, within 1 ms: " + describe (onsets));
        }

        {
            // A two-bar pattern alternates bars: a hit on beat 1 of bar 1 only -> every 4 s.
            MetronomeProcessor metronome;
            DrumSequencer drums (metronome);
            setUp (metronome, drums, "x. .. .. ..|.. .. .. ..", 2);
            const auto onsets = findOnsets (renderDrums (metronome, drums, rate, 8.5), rate);
            check (onsets.size() == 3 && std::abs (onsets[1] - 4 * 48000) <= 48 && std::abs (onsets[2] - 8 * 48000) <= 48,
                   "two-bar pattern: bar 1 at 0, 4 and 8 s: " + describe (onsets));
        }

        {
            // Swing moves the off-beat 8th: 0.75 -> 3/4 of the beat.
            MetronomeProcessor metronome;
            DrumSequencer drums (metronome);
            setUp (metronome, drums, ".x .. .. ..", 2, 0.75);
            const auto onsets = findOnsets (renderDrums (metronome, drums, rate, 1.0), rate);
            check (onsets.size() == 1 && std::abs (onsets[0] - (int) (0.375 * rate)) <= 48, "swung 8th at 0.375 s: " + describe (onsets));
        }

        {
            // Muted row, switched off, metronome stopped: silence.
            MetronomeProcessor metronome;
            DrumSequencer drums (metronome);
            setUp (metronome, drums, "x. x. x. x.", 2);
            drums.drumMuted[(size_t) Drum::closedHat] = true;
            check (renderDrums (metronome, drums, rate, 1.0).getMagnitude (0, (int) rate) == 0.0f, "muted row is silent");
            drums.drumMuted[(size_t) Drum::closedHat] = false;
            drums.enabled = false;
            check (renderDrums (metronome, drums, rate, 1.0).getMagnitude (0, (int) rate) == 0.0f, "switched off: silent");
            drums.enabled = true;
            metronome.stop();
            renderDrums (metronome, drums, rate, 0.5);
            check (renderDrums (metronome, drums, rate, 1.0).getMagnitude (0, (int) rate) == 0.0f, "metronome stopped: silent");
            check (drums.getPlayingStep() == -1, "no step shown while stopped");
        }

        {
            // The first loop restarts the pattern: bar counting starts again at the loop's first bar.
            MetronomeProcessor metronome;
            DrumSequencer drums (metronome);
            setUp (metronome, drums, "x. .. .. ..|.. .. .. ..", 2);
            const double barSamples = 2.0 * rate;
            // Bars 0, 1, 2 play; the loop starts at bar 3 (6 s), where the 2-bar pattern would be in its 2nd bar.
            const auto out = renderDrums (metronome, drums, rate, 8.5, [&] (juce::int64 pos)
            {
                if (pos + 512 > (juce::int64) (3 * barSamples))     // the looper locks in the block holding the bar line
                    metronome.setLockedGrid (true, 3 * barSamples, barSamples, 4);
            });
            const auto onsets = findOnsets (out, rate);
            check (onsets.size() == 3 && std::abs (onsets[1] - 4 * 48000) <= 48 && std::abs (onsets[2] - 6 * 48000) <= 48,
                   "pattern restarts on the first loop's bar (6 s): " + describe (onsets));
        }

        {
            // State (kit, mix, pattern) round trip.
            MetronomeProcessor metronome;
            DrumSequencer a (metronome), b (metronome);
            a.setKitType (DrumKitType::electronic);
            a.gain = 0.6f;
            a.drumGains[(size_t) Drum::snare] = 0.3f;
            a.drumMuted[(size_t) Drum::ride] = true;
            a.setPattern (DrumPatterns::builtIn()[5]);
            b.applyStateXml (*a.createStateXml());
            check (b.getKitType() == DrumKitType::electronic && std::abs (b.gain.load() - 0.6f) < 1.0e-6f
                       && std::abs (b.drumGains[(size_t) Drum::snare].load() - 0.3f) < 1.0e-6f && b.drumMuted[(size_t) Drum::ride].load()
                       && b.getPattern().sameNotes (DrumPatterns::builtIn()[5]) && b.getPattern().name == DrumPatterns::builtIn()[5].name,
                   "kit, volumes, mutes and pattern saved and restored");
        }
    }

    void testMetronomeSettings()
    {
        std::cout << "Metronome settings" << std::endl;
        MetronomeProcessor a;
        a.startBpm = 87.0;
        a.beatsPerBar = 7;
        a.gain = 0.35f;
        a.subdivision = (int) MetronomeProcessor::Subdivision::sixteenths;
        a.sound = (int) MetronomeProcessor::Sound::drums;
        a.subdivisionLevel = 0.2f;
        a.swing = 0.67;
        a.followSong = true;
        a.trainerEnabled = true;
        a.trainerStepBpm = 3.0;
        a.trainerInterval = 8.0;
        a.trainerUnit = (int) MetronomeProcessor::IntervalUnit::bars;
        a.trainerMaxBpm = 140.0;
        a.clicksMuted = true;

        MetronomeProcessor b;
        b.applySettingsXml (*juce::parseXML (a.createSettingsXml()->toString()));
        check (b.startBpm.load() == 87.0 && b.beatsPerBar.load() == 7 && std::abs (b.gain.load() - 0.35f) < 1.0e-6f
                   && b.subdivision.load() == a.subdivision.load() && b.sound.load() == a.sound.load()
                   && std::abs (b.subdivisionLevel.load() - 0.2f) < 1.0e-6f && b.swing.load() == 0.67
                   && b.followSong.load() && b.trainerEnabled.load() && b.trainerStepBpm.load() == 3.0
                   && b.trainerInterval.load() == 8.0 && b.trainerUnit.load() == a.trainerUnit.load()
                   && b.trainerMaxBpm.load() == 140.0,
               "every setting survives a save / load");
        check (! b.clicksMuted.load(), "Mute clicks is not part of it (looper.xml has it)");

        MetronomeProcessor c;
        juce::XmlElement bad ("PRACTICE_VIBES_METRONOME");
        bad.setAttribute ("bpm", 5000.0);
        bad.setAttribute ("beatsPerBar", 0);
        bad.setAttribute ("swing", 3.0);
        c.applySettingsXml (bad);
        check (c.startBpm.load() == 300.0 && c.beatsPerBar.load() == 1 && c.swing.load() == 0.75, "out-of-range values are clamped");

        MetronomeProcessor d;
        d.applySettingsXml (juce::XmlElement ("PRACTICE_VIBES_METRONOME"));
        check (d.startBpm.load() == 100.0 && d.sound.load() == (int) MetronomeProcessor::Sound::beep, "missing values keep the defaults");
    }

    void testLatencyCalibration()
    {
        std::cout << "Latency calibration" << std::endl;
        constexpr double rate = 48000.0;
        const double beat = rate * 60.0 / 100.0;     // 100 bpm

        // Muted strums (short noise bursts) on every beat, `lagMs` late with some jitter, over hum and noise.
        auto run = [&] (double lagMs, double jitterMs, int beats, double barStart, std::vector<int> skippedBeats = {})
        {
            juce::Random random (42);
            const auto length = (int) ((beats + 1) * beat);
            std::vector<float> input ((size_t) length);

            for (int i = 0; i < length; ++i)
                input[(size_t) i] = 0.003f * (random.nextFloat() * 2.0f - 1.0f)
                                    + 0.004f * (float) std::sin (juce::MathConstants<double>::twoPi * 50.0 * i / rate);

            for (int b = 1; b <= beats; ++b)
            {
                if (std::find (skippedBeats.begin(), skippedBeats.end(), b) != skippedBeats.end())
                    continue;

                const auto onset = (int) (barStart + b * beat + (lagMs + jitterMs * (random.nextFloat() * 2.0f - 1.0f)) * rate / 1000.0);

                for (int i = 0; i < (int) (0.08 * rate) && onset + i < length; ++i)
                    input[(size_t) (onset + i)] += 0.3f * std::exp (-i / (0.015f * (float) rate)) * (random.nextFloat() * 2.0f - 1.0f);
            }

            LatencyCalibrator calibrator;
            calibrator.prepare (rate);
            calibrator.start();

            for (int done = 0; done < length; done += 256)
                calibrator.process (input.data() + done, juce::jmin (256, length - done), done, barStart, beat);

            std::vector<double> lags;
            calibrator.readLags (lags);
            return lags;
        };

        {
            const auto lags = run (37.0, 2.0, 14, 1000.0);
            const auto result = LatencyCalibrator::analyse (lags);
            check (lags.size() == 14, "one hit per strum: " + juce::String ((int) lags.size()) + " of 14");
            check (result.ok && std::abs (result.lagMs - 37.0) < 1.5,
                   "37 ms late strums measure " + juce::String (result.lagMs, 1) + " ms (spread " + juce::String (result.spreadMs, 1) + ")");
        }

        {
            const auto result = LatencyCalibrator::analyse (run (-12.0, 2.0, 14, 0.0));
            check (result.ok && std::abs (result.lagMs + 12.0) < 1.5, "early strums give a negative lag: " + juce::String (result.lagMs, 1) + " ms");
        }

        {
            // A skipped beat and two stray values: the median ignores them.
            auto lags = run (60.0, 1.0, 12, 0.0, { 5 });
            lags.push_back (250.0);
            lags.push_back (-200.0);
            const auto result = LatencyCalibrator::analyse (lags);
            check (result.ok && std::abs (result.lagMs - 60.0) < 1.5 && result.used == 11,
                   "outliers ignored: " + juce::String (result.lagMs, 1) + " ms from " + juce::String (result.used) + " of " + juce::String (result.total));
        }

        {
            std::vector<double> uneven { 10, 80, -40, 150, 30, -90, 200, 5, 60, -150 };
            check (! LatencyCalibrator::analyse (uneven).ok, "random hits are rejected");
            check (! LatencyCalibrator::analyse ({ 30.0, 31.0, 29.0 }).ok, "too few hits are rejected");
        }
    }

    std::vector<float> makeTone (double sampleRate, double frequency, int length, std::initializer_list<float> harmonics,
                                 float noise = 0.0f, double decaySeconds = 0.0)
    {
        std::vector<float> out ((size_t) length);
        juce::Random random (42);

        for (int i = 0; i < length; ++i)
        {
            const auto t = i / sampleRate;
            double v = 0.0;
            int k = 1;

            for (auto a : harmonics)
            {
                v += a * std::sin (juce::MathConstants<double>::twoPi * frequency * k * t + 0.3 * k);
                ++k;
            }

            if (decaySeconds > 0.0)
                v *= std::exp (-t / decaySeconds);

            out[(size_t) i] = (float) (0.25 * v) + noise * (random.nextFloat() * 2.0f - 1.0f);
        }

        return out;
    }

    float centsBetween (float a, float b) { return 1200.0f * std::log2 (a / b); }

    void testTuner()
    {
        std::cout << "Tuner" << std::endl;
        PitchDetector detector;

        for (auto rate : { 44100.0, 48000.0, 96000.0 })
        {
            float worst = 0.0f;
            bool allValid = true;

            // Low B (7-string), drop D, standard strings, A4, a high fret.
            for (auto f : { 61.74f, 73.42f, 82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f, 440.0f, 987.77f })
            {
                const auto tone = makeTone (rate, f, PitchDetector::samplesNeeded (rate), { 1.0f });
                const auto r = detector.analyse (tone.data(), (int) tone.size(), rate);
                allValid = allValid && r.isValid();

                if (r.isValid())
                    worst = juce::jmax (worst, std::abs (centsBetween (r.frequency, f)));
            }

            check (allValid && worst < 1.0f, "sines at " + juce::String (rate, 0) + " Hz are within 1 cent (worst "
                                             + juce::String (worst, 2) + ")");
        }

        {
            // Plucked-string-like: fundamental weaker than the 2nd and 3rd harmonics, decaying, a bit of noise.
            float worst = 0.0f;
            bool allValid = true;

            for (auto f : { 82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f })
            {
                const auto tone = makeTone (48000.0, f, 48000, { 0.3f, 1.0f, 0.8f, 0.5f, 0.4f, 0.3f, 0.2f }, 0.01f, 1.5);
                const auto r = detector.analyse (tone.data(), (int) tone.size(), 48000.0);
                allValid = allValid && r.isValid();

                if (r.isValid())
                    worst = juce::jmax (worst, std::abs (centsBetween (r.frequency, f)));
            }

            check (allValid && worst < 3.0f, "guitar-like tones: fundamental found, no octave errors (worst "
                                             + juce::String (worst, 2) + " cents)");
        }

        {
            std::vector<float> silence (4096, 0.0f), noise (4096);
            juce::Random random (7);

            for (auto& n : noise)
                n = random.nextFloat() * 0.6f - 0.3f;

            check (! detector.analyse (silence.data(), 4096, 48000.0).isValid(), "silence gives no pitch");
            check (! detector.analyse (noise.data(), 4096, 48000.0).isValid(), "white noise gives no pitch");
            check (! detector.analyse (silence.data(), 1000, 48000.0).isValid(), "too few samples give no pitch");
        }

        {
            const auto a = NoteInfo::fromFrequency (440.0f);
            const auto e = NoteInfo::fromFrequency (82.41f);
            const auto sharp = NoteInfo::fromFrequency (445.0f);
            const auto ref432 = NoteInfo::fromFrequency (432.0f, 432.0f);
            check (a.name == "A" && a.octave == 4 && std::abs (a.cents) < 0.01f, "440 Hz = A4, 0 cents");
            check (e.name == "E" && e.octave == 2 && e.midiNote == 40 && std::abs (e.cents) < 0.5f, "82.41 Hz = E2");
            check (std::abs (sharp.cents - 19.56f) < 0.05f, "445 Hz = A4 +19.6 cents");
            check (ref432.name == "A" && std::abs (ref432.cents) < 0.01f, "A4 = 432 reference");
            check (NoteInfo::fromFrequency (277.18f).name == "C#", "sharps are named with #");
        }

        {
            PitchSmoother smoother;
            const auto r = [] (float f) { PitchReading p; p.frequency = f; p.clarity = 1.0f; return p; };
            smoother.add (r (110.0f), 0.0);
            smoother.add (r (110.2f), 0.03);
            smoother.add (r (125.0f), 0.06);        // > 1 semitone away: starts over
            check (std::abs (smoother.add (r (125.1f), 0.09) - 125.05f) < 0.01f, "smoother restarts on a new note");
            smoother.reset();
            smoother.add (r (110.0f), 0.0);
            smoother.add (r (110.1f), 0.03);
            smoother.add (r (110.4f), 0.06);        // outlier within the note
            smoother.add (r (110.1f), 0.09);
            check (std::abs (smoother.add (r (110.0f), 0.12) - 110.1f) < 0.001f, "smoother shows the median");
            check (smoother.add ({}, 0.3) > 0.0f, "smoother holds over a short dropout");
            check (smoother.add ({}, 0.6) == 0.0f, "smoother clears after a long dropout");
        }

        {
            // Through the input stage tap, in audio blocks.
            Tuner tuner;
            GainMeterProcessor stage ("Input stage", 1);
            stage.tap = &tuner;
            stage.prepareToPlay (48000.0, 512);
            const auto tone = makeTone (48000.0, 110.0, 48000, { 0.5f, 1.0f, 0.6f });
            juce::AudioBuffer<float> block (1, 512);
            juce::MidiBuffer midi;

            int fed = 0;
            auto feed = [&] (int blocks)
            {
                for (int b = 0; b < blocks; ++b)
                {
                    block.copyFrom (0, 0, tone.data() + (fed++ % 90) * 512, 512);
                    stage.processBlock (block, midi);
                }
            };

            feed (20);
            check (! tuner.read().isValid(), "inactive tuner ignores the input");

            tuner.active = true;
            feed (4);
            check (! tuner.read().isValid(), "tuner waits for a full window");
            feed (6);
            const auto reading = tuner.read();
            check (reading.isValid() && std::abs (centsBetween (reading.frequency, 110.0f)) < 2.0f, "tuner reads A2 from the input stage");

            tuner.clear();
            check (! tuner.read().isValid(), "cleared tuner forgets old audio");
            feed (10);
            check (tuner.read().isValid(), "tuner reads again after clear");

            stage.silenced = true;
            stage.prepareToPlay (48000.0, 512);
            block.copyFrom (0, 0, tone.data(), 512);
            stage.processBlock (block, midi);
            check (block.getMagnitude (0, 512) == 0.0f, "silenced stage outputs nothing");
            check (tuner.read().isValid(), "tuner still listens while the guitar is silenced");

            tuner.referenceA4 = 436.0f;
            tuner.muteWhileTuning = false;
            Tuner copy;
            copy.fromXml (*tuner.toXml());
            check (copy.referenceA4 == 436.0f && ! copy.muteWhileTuning, "tuner settings round trip");
            juce::XmlElement bad ("TUNER");
            bad.setAttribute ("referenceA4", 1000.0);
            copy.fromXml (bad);
            check (copy.referenceA4 == Tuner::maxReference && copy.muteWhileTuning, "tuner settings are clamped / defaulted");
        }

        check (FootswitchAction::fromString (FootswitchAction { FootswitchAction::Kind::tuner, 0 }.toString()).kind
                   == FootswitchAction::Kind::tuner, "footswitch tuner action round trip");
        check (FootswitchAction::fromString (FootswitchAction { FootswitchAction::Kind::stem, Stems::bass }.toString())
                   == FootswitchAction { FootswitchAction::Kind::stem, Stems::bass }, "footswitch stem action round trip (bass)");
        check (FootswitchAction::fromString ("guitarStem") == FootswitchAction { FootswitchAction::Kind::stem, Stems::guitar },
               "old guitar-stem footswitch setting reads as the guitar stem");
        check (! (FootswitchAction { FootswitchAction::Kind::stem, Stems::bass } == FootswitchAction { FootswitchAction::Kind::stem, Stems::piano }),
               "stem actions for different stems differ");
    }

    /** Plucked strings, string i starting 15 ms after string i-1, after `silence` seconds. */
    std::vector<float> makeStrum (double rate, const std::vector<float>& frequencies, const std::vector<float>& gains,
                                  double silence, double seconds)
    {
        std::vector<float> out ((size_t) (rate * (silence + seconds)), 0.0f);
        const float harmonics[] = { 1.0f, 0.6f, 0.4f, 0.25f, 0.15f, 0.1f };
        juce::Random random (3);

        for (size_t s = 0; s < frequencies.size(); ++s)
        {
            const auto start = (size_t) ((silence + 0.015 * (double) s) * rate);

            for (size_t i = start; i < out.size(); ++i)
            {
                const auto t = (double) (i - start) / rate;
                double v = 0.0;

                for (int h = 0; h < 6; ++h)
                    v += harmonics[h] * std::sin (juce::MathConstants<double>::twoPi * frequencies[s] * (h + 1) * t + h);

                out[i] += (float) (0.06 * gains[s] * v * std::exp (-t / 2.0));
            }
        }

        for (auto& v : out)
            v += 0.0005f * (random.nextFloat() * 2.0f - 1.0f);

        return out;
    }

    void testTunings()
    {
        std::cout << "Tunings and strum check" << std::endl;

        {
            // Diagnostic: juce::dsp::FFT (vDSP on Apple, JUCE's own engine elsewhere) against a direct DFT in double.
            constexpr int order = 17, size = 1 << order, n = 24000;
            juce::dsp::FFT fft (order);
            std::vector<float> data ((size_t) size * 2, 0.0f);
            std::vector<double> x ((size_t) n);

            for (int i = 0; i < n; ++i)
            {
                const auto hann = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (n - 1));
                x[(size_t) i] = hann * (std::sin (0.0171 * i) + 0.3 * std::sin (0.0517 * i + 1.0));
                data[(size_t) i] = (float) x[(size_t) i];
            }

            fft.performRealOnlyForwardTransform (data.data());
            double worst = 0.0, peak = 0.0;

            for (int k : { 0, 1, 7, 356, 357, 358, 1076, 1077, 1078, 5000, size / 2 - 1 })
            {
                std::complex<double> direct;

                for (int i = 0; i < n; ++i)
                    direct += x[(size_t) i] * std::polar (1.0, -juce::MathConstants<double>::twoPi * (double) k * i / size);

                peak = juce::jmax (peak, std::abs (direct));
                worst = juce::jmax (worst, std::abs (direct - std::complex<double> (data[(size_t) (2 * k)], data[(size_t) (2 * k + 1)])));
            }

            check (worst < 1.0e-3 * peak, "juce::dsp::FFT matches a direct DFT: error " + juce::String (worst / peak, 7)
                                          + " of the peak (" + juce::String (peak, 1) + ")");
        }

        {
            juce::StringArray names;

            for (auto& t : tuningPresets())
                names.addIfNotAlreadyThere (t.name);

            check (names.size() == (int) tuningPresets().size() && tuningName (tuningPresets().front().strings) == "Standard",
                   "tuning presets have unique names, standard first");
            check (tuningName ({ 38, 45, 50, 55, 59, 64 }) == "Drop D" && tuningName ({ 40, 45, 50, 55, 59, 65 }) == "Custom",
                   "tuning names are found, others are custom");

            juce::StringArray bassNames;

            for (auto& t : tuningPresets (TunerInstrument::bass))
            {
                bassNames.addIfNotAlreadyThere (t.name);
                bool inRange = t.strings.size() >= 4 && t.strings.size() <= 6;

                for (auto n : t.strings)
                    inRange = inRange && n >= Tuner::minStringNote (TunerInstrument::bass) && n <= Tuner::maxStringNote (TunerInstrument::bass);

                check (inRange, juce::String ("bass preset ") + t.name + " has 4-6 strings in range");
            }

            check (bassNames.size() == (int) tuningPresets (TunerInstrument::bass).size()
                     && tuningName ({ 28, 33, 38, 43 }, TunerInstrument::bass) == "Standard (4 strings)"
                     && tuningName ({ 23, 28, 33, 38, 43 }, TunerInstrument::bass) == "Standard (5 strings)",
                   "bass presets: unique names, 4- and 5-string standard found");
            check (tuningPresets (TunerInstrument::other).empty(), "other instrument: no tunings (chromatic only)");

            float lowB = 0.0f;
            check (nearestString ({ 23, 28, 33, 38, 43 }, 30.87f, 440.0f, lowB) == 0 && std::abs (lowB) < 1.0f,
                   "low B (31 Hz) on a 5-string bass: lowest string");

            float away = 0.0f;
            const StringNotes dropD { 38, 45, 50, 55, 59, 64 };
            const auto s = nearestString (dropD, 82.41f, 440.0f, away);
            check (s == 0 && std::abs (away - 200.0f) < 1.0f, "E2 in drop D: low string, 2 semitones sharp");
            check (nearestString (dropD, 147.5f, 440.0f, away) == 2 && std::abs (away - 8.0f) < 1.0f, "D3 +8 cents: 4th string");
            check (nearestString (dropD, 1000.0f, 440.0f, away) == -1, "far above every string: none");
        }

        {
            Tuner tuner;
            tuner.guitarStrings = { 36, 43, 48, 55, 60, 64 };
            tuner.bassStrings = { 23, 28, 33, 38, 43 };
            tuner.instrument = TunerInstrument::bass;
            tuner.strobe = true;
            tuner.strumMode = true;
            Tuner copy;
            copy.fromXml (*tuner.toXml());
            check (copy.guitarStrings == tuner.guitarStrings && copy.bassStrings == tuner.bassStrings
                     && copy.instrument == TunerInstrument::bass && copy.strings() == tuner.bassStrings
                     && copy.strobe && copy.strumMode, "instrument, tunings and view settings round trip");

            juce::XmlElement old ("TUNER");                 // saved before the instrument setting existed
            old.setAttribute ("strings", "38 45 50 55 59 64");
            copy.fromXml (old);
            check (copy.instrument == TunerInstrument::guitar && copy.strings() == StringNotes { 38, 45, 50, 55, 59, 64 }
                     && copy.bassStrings == tuningPresets (TunerInstrument::bass).front().strings,
                   "old tuner settings: guitar with its tuning, standard bass");

            Tuner other;
            other.instrument = TunerInstrument::other;
            other.strumMode = true;
            check (other.strings().empty() && ! other.isStrumMode(), "other instrument: no strings, no strum mode");

            juce::XmlElement bad ("TUNER");
            bad.setAttribute ("strings", "40 45 50 55 59");
            bad.setAttribute ("bassStrings", "28 33 38");
            copy.fromXml (bad);
            check (copy.strings() == tuningPresets().front().strings && ! copy.strobe
                     && copy.bassStrings == tuningPresets (TunerInstrument::bass).front().strings,
                   "bad string lists fall back to standard");
            bad.setAttribute ("strings", "40 45 50 55 59 120");
            bad.setAttribute ("bassStrings", "20 28 33 38");
            copy.fromXml (bad);
            check (copy.strings() == tuningPresets().front().strings
                     && copy.bassStrings == tuningPresets (TunerInstrument::bass).front().strings,
                   "out-of-range string note falls back to standard");
        }

        const std::vector<float> detune { -12.0f, 6.0f, 0.0f, -4.0f, 9.0f, -7.0f };

        struct StrumCase { double rate; TunerInstrument instrument; int tuningIndex; };

        for (auto [rate, instrument, tuningIndex] : { StrumCase { 48000.0, TunerInstrument::guitar, 0 },
                                                      StrumCase { 44100.0, TunerInstrument::guitar, 3 },
                                                      StrumCase { 96000.0, TunerInstrument::guitar, 6 },
                                                      StrumCase { 48000.0, TunerInstrument::bass, 0 },
                                                      StrumCase { 44100.0, TunerInstrument::bass, 4 } })
        {
            const auto& tuning = tuningPresets (instrument)[(size_t) tuningIndex];
            const auto count = tuning.strings.size();
            std::vector<float> freqs (count);

            for (size_t i = 0; i < count; ++i)
                freqs[i] = noteFrequency (tuning.strings[i], 440.0f) * std::pow (2.0f, detune[i] / 1200.0f);

            const auto strum = makeStrum (rate, freqs, std::vector<float> (count, 1.0f), 0.4, 1.5);
            Tuner tuner;
            tuner.instrument = instrument;
            tuner.strings() = tuning.strings;
            tuner.active = true;

            std::optional<StrumResult> result;
            int calls = 0, resultAt = -1;
            const int block = 512;

            for (size_t pos = 0; pos + block <= strum.size(); pos += block)
            {
                tuner.push (strum.data() + pos, block, rate);

                if (++calls % 3 == 0)       // the UI polls every few blocks
                    if (auto r = tuner.readStrum(); r && ! result)
                    {
                        result = r;
                        resultAt = (int) pos;
                    }
            }

            float worst = 0.0f;
            bool allHeard = result.has_value() && result->size() == count;

            if (allHeard)
                for (size_t i = 0; i < count; ++i)
                {
                    allHeard = allHeard && (*result)[i].heard;
                    worst = juce::jmax (worst, std::abs ((*result)[i].cents - detune[i]));
                }

            const auto delay = resultAt / rate - 0.4;
            juce::String perString;

            if (result && result->size() == count && ! (allHeard && worst < 1.5f))
                for (size_t i = 0; i < count; ++i)
                    perString << (i == 0 ? " [" : ", ") << ((*result)[i].heard ? juce::String ((*result)[i].cents - detune[i], 2) : "-")
                              << (i == count - 1 ? "]" : "");

            check (allHeard && worst < 1.5f, juce::String (instrument == TunerInstrument::bass ? "Bass " : "") + tuning.name
                                             + " strum at " + juce::String (rate, 0) + " Hz: " + juce::String ((int) count)
                                             + " strings within 1.5 cents (worst " + juce::String (worst, 2) + ")" + perString);
            check (result && delay > 0.5 && delay < 0.8, "strum result arrives "
                                                          + juce::String (delay, 2) + " s after the strum");
        }

        {
            // One string muted, one detuned by more than the search range, then a second strum.
            const auto& standard = tuningPresets().front().strings;
            std::vector<float> freqs (6);

            for (size_t i = 0; i < 6; ++i)
                freqs[i] = noteFrequency (standard[i], 440.0f);

            freqs[4] *= std::pow (2.0f, 80.0f / 1200.0f);       // B string far sharp, near E2's 3rd harmonic
            auto strum = makeStrum (48000.0, freqs, { 1, 1, 0, 1, 1, 1 }, 0.3, 1.2);
            freqs[4] = noteFrequency (standard[4], 440.0f);
            const auto second = makeStrum (48000.0, freqs, { 1, 1, 1, 1, 1, 1 }, 0.0, 1.2);
            strum.insert (strum.end(), second.begin(), second.end());

            Tuner tuner;
            tuner.active = true;
            std::vector<StrumResult> results;

            for (size_t pos = 0; pos + 512 <= strum.size(); pos += 512)
            {
                tuner.push (strum.data() + pos, 512, 48000.0);

                if (auto r = tuner.readStrum())
                    results.push_back (*r);
            }

            check (results.size() == 2, "two strums give two results (" + juce::String ((int) results.size()) + ")");

            if (results.size() == 2)
            {
                check (! results[0][2].heard && results[0][0].heard && results[0][5].heard, "muted string is not heard");
                check (results[0][4].heard && std::abs (results[0][4].cents - 80.0f) < 3.0f,
                       "B string 80 cents sharp reads " + juce::String (results[0][4].cents, 1) + ", not E2's harmonic");
                bool all = true;

                for (auto& r : results[1])
                    all = all && r.heard && std::abs (r.cents) < 1.5f;

                check (all, "second strum: all six in tune");
            }

            std::vector<float> quiet (48000 * 2);
            juce::Random random (5);

            for (auto& v : quiet)
                v = 0.002f * (random.nextFloat() * 2.0f - 1.0f);

            Tuner idle;
            idle.active = true;
            bool any = false;

            for (size_t pos = 0; pos + 512 <= quiet.size(); pos += 512)
            {
                idle.push (quiet.data() + pos, 512, 48000.0);
                any = any || idle.readStrum().has_value();
            }

            check (! any, "hiss alone never counts as a strum");

            // A loud noise burst (a knock, a cable being plugged in) is not a strum either.
            std::vector<float> burst (48000 * 2, 0.0f);

            for (size_t i = 14400; i < burst.size(); ++i)
                burst[i] = 0.2f * (random.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) (i - 14400) / 48000.0f);

            Tuner knocked;
            knocked.active = true;
            any = false;

            for (size_t pos = 0; pos + 512 <= burst.size(); pos += 512)
            {
                knocked.push (burst.data() + pos, 512, 48000.0);
                any = any || knocked.readStrum().has_value();
            }

            check (! any, "a noise burst gives no strum result");
        }
    }
}

//==============================================================================
static void testLibrary()
{
    std::cout << "Library (topics / resources)" << std::endl;

    juce::TemporaryFile tempRoot;
    const auto root = tempRoot.getFile();
    root.createDirectory();

    // Classification and badges
    check (classifyFileName ("Lesson 3.PDF") == ResourceKind::pdf, "PDF recognised (upper-case extension)");
    check (classifyFileName ("riff.mp3") == ResourceKind::audio && classifyFileName ("scan.heic") == ResourceKind::image
               && classifyFileName ("notes.docx") == ResourceKind::document && classifyFileName ("README") == ResourceKind::other,
           "audio / image / Word / no extension");
    check (classifyUrl ("https://www.youtube.com/watch?v=abc") == ResourceKind::youtube
               && classifyUrl ("https://youtu.be/abc") == ResourceKind::youtube
               && classifyUrl ("https://docs.google.com/document/d/x") == ResourceKind::googleDoc
               && classifyUrl ("https://open.spotify.com/track/x") == ResourceKind::web,
           "YouTube / Google Docs / other links");
    {
        Resource pdf;  pdf.fileName = "a.pdf";
        Resource jpeg; jpeg.fileName = "b.jpeg";
        Resource yt;   yt.url = "https://youtu.be/x";
        check (badgeText (pdf) == "PDF" && badgeText (jpeg) == "JPG" && badgeText (yt) == "YT", "badge texts");
    }

    // Tags
    check (parseTags ("jazz, #Technique ,, Jazz;  ear training ") == juce::StringArray ({ "jazz", "Technique", "ear training" }),
           "tags: trimmed, '#' dropped, duplicates ignoring case removed");

    {
        Library lib (root);
        check (lib.getTopics().empty() && lib.getInbox().isEmpty(), "empty library");

        auto* t = lib.createTopic ("Autumn Leaves: solo / chords?");
        check (t != nullptr && t->folder.isDirectory() && t->folder.getChildFile ("topic.xml").existsAsFile(),
               "topic created with its folder and topic.xml");
        check (t != nullptr && ! t->folder.getFileName().containsAnyOf (":/?"), "folder name is a legal file name");

        auto* t2 = lib.createTopic ("Autumn Leaves: solo / chords?");
        check (t2 != nullptr && t2->folder != lib.findTopic (lib.getTopics().back().id)->folder, "same title -> separate folder");

        t = lib.findTopic (lib.getTopics().back().id);   // oldest = first one created
        t->description = "Line 1\nLine 2 <with> & \"quotes\"";
        t->tags = parseTags ("jazz, standards");
        check (lib.saveTopic (*t), "save topic");

        // Files and links
        auto source = root.getSiblingFile ("pv-test-source.pdf");
        source.replaceWithText ("%PDF-1.4 test");
        auto* r = lib.addFile (*t, juce::URL (source));
        check (r != nullptr && r->id == "pv-test-source.pdf" && t->getFile (*r).existsAsFile() && source.existsAsFile(),
               "addFile copies into the topic folder (source kept)");
        auto* r2 = lib.addFile (*t, juce::URL (source));
        check (r2 != nullptr && r2->fileName == "pv-test-source (2).pdf", "same file name twice -> numbered copy");
        source.deleteFile();

        auto* link = lib.addLink (*t, "", "www.youtube.com/watch?v=abc");
        check (link != nullptr && link->url == "https://www.youtube.com/watch?v=abc" && link->title == "YouTube video",
               "addLink adds https:// and a default title");
        link->tags = parseTags ("backing track");
        link->description = "Play along 10x";
        lib.saveTopic (*t);

        check (lib.moveResource (*t, link->id, -5) && t->resources.front().isLink(), "move resource to the top");
    }

    {
        Library lib (root);
        check (lib.getTopics().size() == 2, "rescan finds both topics");
        auto& topics = lib.getTopics();
        const Topic* t = nullptr;

        for (auto& topic : topics)
            if (topic.resources.size() == 3)
                t = &topic;

        check (t != nullptr, "topic with 3 resources reloaded");

        if (t != nullptr)
        {
            check (t->title == "Autumn Leaves: solo / chords?" && t->description == "Line 1\nLine 2 <with> & \"quotes\"",
                   "title and multi-line description survive save/load");
            check (t->tags == juce::StringArray ({ "jazz", "standards" }), "topic tags survive save/load");
            check (t->resources[0].isLink() && t->resources[0].description == "Play along 10x"
                       && t->resources[0].tags == juce::StringArray ({ "backing track" }),
                   "resource order, description and tags survive save/load");
            check (t->matches ("STANDARDS") && t->matches ("play along") && ! t->matches ("bebop"), "search: tags + resource text");
            check (t->matchesTag ("Backing Track") && t->matchesTag ("jazz") && ! t->matchesTag ("rock"),
                   "tag filter: topic or resource tags, ignoring case");
            check (t->matches ("autumn sol") && t->matches ("  JAZ ") && ! t->matches ("tumn") && ! t->matches ("autumn bebop"),
                   "search: every word must start a word");
            check (t->describeMatch ("autumn", {}).isEmpty() && t->describeMatch ("standards", "jazz").isEmpty(),
                   "no match note when the title / topic tags explain it");
            check (t->describeMatch ("backing", {}).startsWith ("tag on") && t->describeMatch ("line", {}) == "in notes",
                   "match note for a resource tag / the notes: " + t->describeMatch ("backing", {}) + " | " + t->describeMatch ("line", {}));
            check (t->describeMatch ({}, "backing track").startsWith ("#backing track on"),
                   "tag filter note for a resource tag: " + t->describeMatch ({}, "backing track"));
        }

        check (lib.getAllTags() == juce::StringArray ({ "backing track", "jazz", "standards" }), "all tags, sorted");

        // Another device drops a file into the topic folder and one into the library root,
        // and a cloud sync conflict leaves a second topic.xml.
        if (t != nullptr)
        {
            const auto folder = t->folder;
            const auto id = t->id;
            folder.getChildFile ("tab.gp5").replaceWithText ("x");
            folder.getChildFile ("topic (1).xml").replaceWithText ("<TOPIC/>");
            folder.getChildFile ("desktop.ini").replaceWithText ("x");
            root.getChildFile ("Scales.pdf").replaceWithText ("x");
            root.getChildFile ("_reserved").createDirectory();

            const auto before = lib.computeSignature();
            lib.rescan();
            check (before != juce::String() && lib.computeSignature() == before, "signature stable without changes");

            auto* reloaded = lib.findTopic (id);
            check (reloaded != nullptr && reloaded->findResource ("tab.gp5") != nullptr
                       && reloaded->findResource ("tab.gp5")->title == "tab",
                   "unlisted file in a topic folder shows up as a resource");
            check (reloaded != nullptr && reloaded->conflictFiles == juce::StringArray ({ "topic (1).xml" })
                       && reloaded->findResource ("topic (1).xml") == nullptr && reloaded->findResource ("desktop.ini") == nullptr,
                   "conflict copy flagged, system files ignored");
            check (lib.getInbox().size() == 1 && lib.getInbox()[0].getFileName() == "Scales.pdf", "loose file lands in the Inbox");
            check (lib.getTopics().size() == 2, "'_' folders are not topics");

            if (reloaded != nullptr)
            {
                auto* moved = lib.moveInboxFile (*reloaded, root.getChildFile ("Scales.pdf"));
                check (moved != nullptr && lib.getInbox().isEmpty() && reloaded->getFile (*moved).existsAsFile()
                           && ! root.getChildFile ("Scales.pdf").exists(),
                       "Inbox file moved into a topic");

                // Referenced file missing (e.g. still syncing): kept, flagged.
                reloaded->getFile (*reloaded->findResource ("tab.gp5")).deleteFile();
                lib.saveTopic (*reloaded);
            }

            const auto changed = lib.computeSignature();
            check (changed != before, "signature changes when files change");
        }

        // A folder made by hand (no topic.xml) is a topic named after the folder.
        root.getChildFile ("Sweep picking").createDirectory();
        lib.rescan();
        auto* manual = lib.findTopic ("dir:Sweep picking");
        check (manual != nullptr && manual->title == "Sweep picking", "hand-made folder becomes a topic");
    }

    {
        Library lib (root);
        Topic* t = nullptr;

        for (auto& topic : lib.getTopics())
            if (topic.resources.size() >= 4)
                t = lib.findTopic (topic.id);

        check (t != nullptr, "topic reloaded after Inbox move");

        if (t != nullptr)
        {
            auto* missing = t->findResource ("tab.gp5");
            check (missing != nullptr && t->isMissing (*missing), "listed but missing file is kept and flagged");

            auto* pdf = t->findResource ("pv-test-source.pdf");
            const auto pdfFile = pdf != nullptr ? t->getFile (*pdf) : juce::File();
            check (pdf != nullptr && lib.removeResource (*t, "pv-test-source.pdf") && ! pdfFile.existsAsFile()
                       && t->findResource ("pv-test-source.pdf") == nullptr,
                   "remove resource deletes its file");

            const auto folder = t->folder;
            check (lib.deleteTopic (t->id) && ! folder.exists() && lib.getTopics().size() == 2, "delete topic removes its folder");
        }
    }

    // A topic.xml must not reach outside its folder.
    {
        auto folder = root.getChildFile ("Evil");
        folder.createDirectory();
        folder.getChildFile ("topic.xml").replaceWithText (
            "<TOPIC id=\"x\" title=\"Evil\"><RESOURCE file=\"../secret.txt\" title=\"s\"/><RESOURCE file=\"ok.txt\" title=\"ok\"/></TOPIC>");
        Library lib (root);
        auto* t = lib.findTopic ("x");
        check (t != nullptr && t->findResource ("../secret.txt") == nullptr && t->findResource ("ok.txt") != nullptr,
               "file paths with folders in topic.xml are rejected");
    }

    root.deleteRecursively();

    // YouTube links
    auto yt = [] (const char* url, const char* id, double start)
    {
        const auto link = parseYouTubeUrl (url);
        return link.has_value() && link->videoId == id && std::abs (link->startSeconds - start) < 0.001;
    };
    check (yt ("https://www.youtube.com/watch?v=dQw4w9WgXcQ", "dQw4w9WgXcQ", 0.0)
               && yt ("youtube.com/watch?feature=share&v=dQw4w9WgXcQ&t=90", "dQw4w9WgXcQ", 90.0)
               && yt ("https://youtu.be/dQw4w9WgXcQ?t=1m30s", "dQw4w9WgXcQ", 90.0)
               && yt ("https://m.youtube.com/watch?v=dQw4w9WgXcQ#t=1h2m3s", "dQw4w9WgXcQ", 3723.0)
               && yt ("https://www.youtube.com/shorts/abcDEF12345", "abcDEF12345", 0.0)
               && yt ("https://www.youtube-nocookie.com/embed/abcDEF12345?start=42", "abcDEF12345", 42.0)
               && yt ("https://music.youtube.com/watch?v=abc_DEF-123&list=RD", "abc_DEF-123", 0.0)
               && yt ("https://www.youtube.com/live/abcDEF12345", "abcDEF12345", 0.0),
           "YouTube links: watch / youtu.be / shorts / embed / live / music, start times");
    check (! parseYouTubeUrl ("https://www.youtube.com/playlist?list=PL123").has_value()
               && ! parseYouTubeUrl ("https://www.youtube.com/@somechannel").has_value()
               && ! parseYouTubeUrl ("https://vimeo.com/12345").has_value()
               && ! parseYouTubeUrl ("https://www.youtube.com/watch?v=bad'id\"x").has_value(),
           "no video id (playlist, channel, other site, bad characters) -> not a YouTube video");
}

static void testLinks()
{
    std::cout << "\n[Links tab]\n";

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-links-test");
    root.deleteRecursively();
    root.createDirectory();

    {
        LinkList links (root);
        check (links.getLinks().empty(), "a new library has no links");
        check (links.add ("", "  ", {}, {}) == nullptr, "an empty URL is refused");

        auto* a = links.add ("", "www.circleoffifths.example/learn", { "theory" }, "Keys and their signatures");
        check (a != nullptr && a->url == "https://www.circleoffifths.example/learn" && a->title == "circleoffifths.example",
               "scheme added, title from the domain: " + (a != nullptr ? a->title : juce::String()));
        links.add ("Scale finder", "https://scales.example", { "Theory", "scales" }, {});
        links.add ("Lesson video", "https://youtu.be/abcDEF12345", {}, {});

        check (links.move (links.getLinks()[2].id, -1) && links.getLinks()[1].title == "Lesson video", "move up");
        check (! links.move (links.getLinks()[0].id, -1), "the first link can't move up");
        check (links.getAllTags() == juce::StringArray ({ "scales", "theory" }), "tags merged ignoring case: "
               + links.getAllTags().joinIntoString (","));
    }

    // Another LinkList (another device) reads what was saved; edits made there are picked up here.
    LinkList here (root);
    check (here.getLinks().size() == 3 && here.getLinks()[2].title == "Scale finder"
               && here.getLinks()[0].description == "Keys and their signatures",
           "links, order and notes come back from links.xml");
    check (root.getChildFile (".links").getChildFile ("links.xml").existsAsFile() && Library (root).getInbox().isEmpty(),
           "kept in .links (not shown in the Topics Inbox)");

    {
        LinkList there (root);
        there.remove (there.getLinks()[1].id);
        auto* r = there.find (there.getLinks()[0].id);
        r->title = "Circle of fifths";
        there.save();
    }

    check (here.reloadIfChanged() && here.getLinks().size() == 2 && here.getLinks()[0].title == "Circle of fifths",
           "changes from another device are picked up");
    check (! here.reloadIfChanged(), "no change -> no reload");

    check (resourceMatches (here.getLinks()[0], "circ keys") && resourceMatches (here.getLinks()[0], "theory")
               && ! resourceMatches (here.getLinks()[0], "ircle") && resourceMatches (here.getLinks()[1], "scales.example"),
           "search: word starts in title, notes, tags, URL");

    root.deleteRecursively();
}

static void testLogCorrections()
{
    std::cout << "\n[Practice log corrections]\n";

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-log-edits-test");
    folder.deleteRecursively();

    const juce::Time day (2026, 10, 15, 18, 0, 0, 0, true);

    auto makeEntry = [&] (const juce::String& id, int hourOffset, double played)
    {
        PracticeEntry e;
        e.id = id;
        e.topicId = "t1";
        e.topicTitle = "Scales";
        e.start = day + juce::RelativeTime::hours (hourOffset);
        e.end = e.start + juce::RelativeTime::minutes (30);
        e.playedSeconds = played;
        return e;
    };

    {
        PracticeLog ipad (folder, "ipad");
        ipad.save (makeEntry ("a", 0, 600.0));
        ipad.save (makeEntry ("b", 1, 900.0));
        ipad.save (makeEntry ("c", 2, 300.0));
    }

    PracticeLog pc (folder, "pc");
    check (pc.getEntries().size() == 3, "the PC reads the iPad's entries");

    // The PC corrects an iPad entry and deletes another: written only to the PC's own edits file.
    const auto newStart = day + juce::RelativeTime::hours (-2);
    check (pc.editEntry ("b", newStart, 25.0 * 60.0, "Fixed: was counted twice"), "edit an entry from another device");
    check (pc.deleteEntry ("c"), "delete an entry from another device");
    check (! pc.deleteEntry ("nope"), "unknown entry -> false");

    check (pc.getEntries().size() == 2 && pc.getEntries().front().id == "b"
               && pc.find ("b")->end == newStart + juce::RelativeTime::minutes (30)
               && pc.find ("b")->playedSeconds == 1500.0 && pc.find ("c") == nullptr,
           "applied at once: re-sorted by the new start, end moved with it, played time, deleted entry gone");
    check (folder.getChildFile ("edits-pc.xml").existsAsFile()
               && ! folder.getChildFile ("log-pc-2026.xml").existsAsFile(),
           "the iPad's log file is untouched; the correction sits in edits-pc.xml");
    check (pc.summarise ("t1").sessions == 2 && pc.summarise ("t1").totalSeconds == 2100.0, "totals follow the corrections");

    // The iPad sees the corrections; its tracker re-saving entry b (e.g. a late note) doesn't undo them.
    PracticeLog ipad (folder, "ipad");
    check (ipad.getEntries().size() == 2 && ipad.find ("b")->note == "Fixed: was counted twice", "other devices apply the corrections");
    ipad.save (makeEntry ("b", 1, 900.0));
    ipad.save (makeEntry ("c", 2, 300.0));
    check (ipad.find ("b")->playedSeconds == 1500.0 && ipad.find ("c") == nullptr, "re-saving keeps corrections and deletions");

    // A newer correction from the iPad wins over the PC's.
    juce::Thread::sleep (5);
    ipad.editEntry ("b", newStart, 20.0 * 60.0, {});
    pc.reload();
    check (pc.find ("b")->playedSeconds == 1200.0 && pc.find ("b")->note.isEmpty(), "the newest correction wins on every device");

    // Free practice assigned to a topic afterwards (and back); a later edit without a topic keeps the move.
    {
        PracticeEntry freeEntry = makeEntry ("f", 4, 600.0);
        freeEntry.topicId = {};
        freeEntry.topicTitle = {};
        ipad.save (freeEntry);
        pc.reload();
        check (pc.find ("f") != nullptr && pc.find ("f")->isFree(), "free practice entry logged");

        const auto fStart = pc.find ("f")->start;
        check (pc.editEntry ("f", fStart, 600.0, {}, std::make_pair (juce::String ("t2"), juce::String ("Chords"))),
               "assign a topic to free practice");
        check (pc.find ("f")->topicId == "t2" && pc.find ("f")->topicTitle == "Chords"
                   && pc.summarise ("t2").sessions == 1 && pc.summarise ("t2").totalSeconds == 600.0,
               "the entry now counts for the topic");

        juce::Thread::sleep (5);
        pc.editEntry ("f", fStart, 700.0, "longer");
        check (pc.find ("f")->topicId == "t2" && pc.find ("f")->playedSeconds == 700.0, "a later edit keeps the new topic");

        PracticeLog ipad2 (folder, "ipad");
        check (ipad2.find ("f")->topicId == "t2" && ipad2.find ("f")->topicTitle == "Chords", "other devices see the move");
        ipad2.save (freeEntry);
        check (ipad2.find ("f")->topicId == "t2", "re-saving the original entry keeps the move");

        juce::Thread::sleep (5);
        ipad2.editEntry ("f", fStart, 700.0, {}, std::make_pair (juce::String(), juce::String ("ignored")));
        pc.reload();
        check (pc.find ("f")->isFree() && pc.find ("f")->topicTitle.isEmpty() && pc.summarise ("t2").sessions == 0,
               "moved back to free practice");
    }

    check (parseEntryTime ("15/11/2026 14:30") == juce::Time (2026, 10, 15, 14, 30, 0, 0, true)
               && parseEntryTime (formatEntryTime (day)) == day && parseEntryTime ("5.3.26 9:05").has_value()
               && ! parseEntryTime ("31/02/2026 10:00").has_value() && ! parseEntryTime ("15/11/2026").has_value()
               && ! parseEntryTime ("15/13/2026 10:00").has_value(),
           "entry times: typed forms, bad dates refused");
    check (parseMinutes ("25") == 25.0 && parseMinutes ("12,5") == 12.5 && parseMinutes ("1:10") == 70.0
               && ! parseMinutes ("").has_value() && ! parseMinutes ("abc").has_value() && ! parseMinutes ("1.2.3").has_value()
               && ! parseMinutes ("1:75").has_value(),
           "minutes: 25, 12,5, 1:10; junk refused");

    folder.deleteRecursively();
}

static void testStartButtonMode()
{
    std::cout << "\n[Microphone input: practice timed with Start / Pause]\n";

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-startbutton-test");
    folder.deleteRecursively();

    PracticeLog log (folder, "pc");
    PracticeTracker tracker;
    tracker.setLog (&log);

    PracticeTracker::Activity a;
    auto clock = juce::Time (2026, 10, 17, 18, 0, 0, 0, true);

    auto run = [&] (double seconds, float peak)
    {
        for (double t = 0.0; t < seconds - 1.0e-9; t += 0.1)
        {
            clock = clock + juce::RelativeTime::milliseconds (100);
            // Room noise: loud bursts now and then over a quiet floor.
            const auto noisy = peak > 0.0f && std::fmod (t, 7.0) < 1.0;
            tracker.tick (0.1, noisy ? peak : 0.0005f, a, clock);
        }
    };

    auto played = [&]
    {
        double total = 0.0;
        for (auto& e : log.getEntries())
            total += e.playedSeconds;
        if (auto* c = tracker.getCurrent(); c != nullptr && log.find (c->id) == nullptr)
            total += c->playedSeconds;
        else if (c != nullptr)
            total += c->playedSeconds - log.find (c->id)->playedSeconds;
        return total;
    };

    run (120.0, 0.1f);
    check (played() > 60.0, "automatic: room noise counts as playing (what the Start button is for)");

    tracker.finish();
    const auto before = played();

    tracker.setStartButtonMode (true);
    check (tracker.isPaused() && tracker.isTimedByHand() && ! tracker.isPlaying(), "Start-button mode starts paused");

    run (120.0, 0.1f);
    check (std::abs (played() - before) < 0.01, "paused: room noise logs nothing");

    tracker.setPaused (false);   // Start
    run (90.0, 0.0f);            // silent stretches count too (reading the next exercise)
    check (std::abs (played() - before - 90.0) < 0.5, "after Start the clock counts, sound or not (90 s)");

    tracker.setActiveTopic ("t1", "Scales");
    check (! tracker.isPaused(), "a topic change keeps the clock running");
    tracker.setPaused (true);
    tracker.setActiveTopic ({}, {});
    check (tracker.isPaused(), "... and keeps it paused");

    tracker.setPaused (false);
    run (PracticeTracker::autoPauseSeconds + 5.0, 0.0f);
    check (tracker.isPaused(), "ten silent minutes: the clock pauses itself");

    bool anyNoGuitar = false;
    for (auto& e : log.getEntries())
        anyNoGuitar = anyNoGuitar || e.noGuitar;
    check (! anyNoGuitar, "entries timed with Start aren't marked \"no guitar\"");

    tracker.setStartButtonMode (false);
    check (! tracker.isPaused() && ! tracker.isTimedByHand(), "back to an interface: the input decides again");

    tracker.setLog (nullptr);
    folder.deleteRecursively();
}

namespace
{
    /** Stand-ins for real plugins: an instrument that outputs `level` (DC) while a note is held,
        and an effect that scales its input. */
    class FakePlugin : public juce::AudioPluginInstance
    {
    public:
        FakePlugin (bool isSynth, float value, int latency = 0)
            : juce::AudioPluginInstance (isSynth ? BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())
                                                 : BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                                                    .withOutput ("Out", juce::AudioChannelSet::stereo())),
              synth (isSynth), amount (value)
        {
            setLatencySamples (latency);
        }

        void fillInPluginDescription (juce::PluginDescription& d) const override
        {
            d.name = getName();
            d.pluginFormatName = "Fake";
            d.isInstrument = synth;
            d.numInputChannels = synth ? 0 : 2;
            d.numOutputChannels = 2;
        }

        const juce::String getName() const override             { return synth ? "Fake synth" : "Fake gain"; }
        void prepareToPlay (double, int) override               {}
        void releaseResources() override                        {}

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
        {
            if (! synth)
            {
                buffer.applyGain (amount);
                return;
            }

            for (const auto m : midi)
            {
                const auto msg = m.getMessage();

                if (msg.isNoteOn())
                    ++held;
                else if (msg.isNoteOff())
                    held = juce::jmax (0, held - 1);
            }

            for (int c = 0; c < buffer.getNumChannels(); ++c)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (c), held > 0 ? amount : 0.0f, buffer.getNumSamples());
        }

        double getTailLengthSeconds() const override            { return 0.0; }
        bool acceptsMidi() const override                       { return synth; }
        bool producesMidi() const override                      { return false; }
        juce::AudioProcessorEditor* createEditor() override     { return nullptr; }
        bool hasEditor() const override                         { return false; }
        int getNumPrograms() override                           { return 1; }
        int getCurrentProgram() override                        { return 0; }
        void setCurrentProgram (int) override                   {}
        const juce::String getProgramName (int) override        { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override  {}
        void setStateInformation (const void*, int) override    {}

        const bool synth;
        const float amount;
        int held = 0;
    };
}

static void testInstrumentSlot()
{
    std::cout << "\n[Rig: instrument plugin played from MIDI]\n";

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formats;
    PluginChain chain (deviceManager, formats);
    constexpr int block = 256;
    chain.prepareForTests (1, 2, 48000.0, block);
    chain.getOutputStage().muted = false;

    // Runs a few blocks (gains are smoothed) with `input` on the device input; returns the last output sample.
    auto run = [&] (float input, juce::MidiBuffer midi = {})
    {
        juce::AudioBuffer<float> buffer (2, block);
        float out = 0.0f;

        for (int i = 0; i < 20; ++i)
        {
            buffer.clear();
            juce::FloatVectorOperations::fill (buffer.getWritePointer (0), input, block);
            auto thisBlock = i == 0 ? midi : juce::MidiBuffer();
            chain.processForTests (buffer, thisBlock);
            out = buffer.getSample (1, block - 1);
        }

        return out;
    };

    auto note = [] (bool on)
    {
        juce::MidiBuffer m;
        m.addEvent (on ? juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, 60), 0);
        return m;
    };

    chain.addPluginInstance (std::make_unique<FakePlugin> (false, 0.5f));       // effect first
    chain.addPluginInstance (std::make_unique<FakePlugin> (true, 0.5f, 100));   // then an instrument

    check (chain.getNumPlugins() == 2 && chain.hasInstrument() && chain.isInstrument (0) && ! chain.isInstrument (1)
             && chain.getLastAddedIndex() == 0, "an instrument goes on top of the chain, above the effects");
    check (chain.getPluginLatencySamples() == 0, "the instrument's latency isn't counted as chain latency");

    check (std::abs (run (0.0f)) < 1.0e-4f, "no note, no input: silence");
    check (std::abs (run (0.0f, note (true)) - 0.25f) < 0.01f, "note on: the instrument plays through the effect (0.5 x 0.5)");
    check (std::abs (run (0.2f) - 0.35f) < 0.01f, "the audio input mixes with the instrument before the effects");
    check (std::abs (run (0.2f, note (false)) - 0.1f) < 0.01f, "note off: only the audio input is left");

    chain.movePlugin (1, -1);
    check (chain.isInstrument (0) && chain.getPluginName (1) == "Fake gain", "effects can't move above the instrument");

    chain.addPluginInstance (std::make_unique<FakePlugin> (true, 0.8f));
    check (chain.getNumPlugins() == 2 && chain.isInstrument (0), "a second instrument replaces the first");
    check (std::abs (run (0.0f, note (true)) - 0.4f) < 0.01f, "the new instrument plays (0.8 x 0.5)");

    chain.getOutputStage().muted = true;
    check (std::abs (run (0.0f)) < 1.0e-4f, "monitoring off silences the instrument too");
    chain.getOutputStage().muted = false;

    chain.removePlugin (0);
    check (! chain.hasInstrument() && chain.getNumPlugins() == 1, "removing the instrument leaves the effects");
    check (std::abs (run (0.2f, note (true)) - 0.1f) < 0.01f, "without an instrument MIDI makes no sound");

    // The pedal board's messages never reach the instrument.
    Footswitch fs;
    fs.startLearning (0);
    check (fs.isFootswitchMessage (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), "while learning, every message is kept back");
    fs.handleMidi (juce::MidiMessage::controllerEvent (1, 80, 127), 1.0);
    fs.handleMidi (juce::MidiMessage::controllerEvent (1, 80, 0), 1.1);
    check (fs.isFootswitchMessage (juce::MidiMessage::controllerEvent (1, 80, 127))
             && ! fs.isFootswitchMessage (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100))
             && ! fs.isFootswitchMessage (juce::MidiMessage::controllerEvent (1, 64, 127)),
           "a learned switch is kept back; keyboard notes and the sustain pedal pass");

    // Optional, local only: a real instrument plugin (PV_REAL_INSTRUMENT=<path to a .vst3>).
    const auto realPath = juce::SystemStats::getEnvironmentVariable ("PV_REAL_INSTRUMENT", {});

    if (realPath.isNotEmpty())
    {
        juce::addDefaultFormatsToManager (formats);
        juce::OwnedArray<juce::PluginDescription> found;

        for (auto* format : formats.getFormats())
            if (format->fileMightContainThisPluginType (realPath))
                format->findAllTypesForFile (found, realPath);

        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::String error;

        if (! found.isEmpty())
            instance = formats.createPluginInstance (*found[0], 48000.0, block, error);

        check (instance != nullptr, "real instrument loads: " + realPath + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));

        if (instance != nullptr)
        {
            const auto name = instance->getName();
            check (PluginChain::isInstrumentPlugin (*instance), name + " is recognised as an instrument");
            chain.addPluginInstance (std::move (instance));
            chain.prepareForTests (1, 2, 48000.0, block);

            float peak = 0.0f;
            juce::AudioBuffer<float> buffer (2, block);

            for (int i = 0; i < 200; ++i)       // ~1 s: some plugins load their samples in the background
            {
                buffer.clear();
                juce::MidiBuffer midi;

                if (i % 40 == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, i % 80 == 0 ? 60 : 36, (juce::uint8) 110), 0);   // C4, or C2 (a drum pad)

                chain.processForTests (buffer, midi);

                if (i > 0)
                    peak = juce::jmax (peak, buffer.getMagnitude (0, block));

                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);   // plugins load samples via messages
            }

            check (peak > 0.001f, name + " sounds when a note is played (peak " + juce::String (peak, 4) + ")");
        }
    }
}

static void testMidiPracticeTiming()
{
    std::cout << "\n[MIDI keyboard: practice timed from note-ons]\n";

    PlayDetector detector;
    double played = 0.0;
    played += detector.process (0.0f, 0.1, true);
    check (played > 0.09 && detector.isPlaying(), "a MIDI note counts as playing with a silent input");

    for (int i = 0; i < 45; ++i)
        played += detector.process (0.0f, 0.1);

    check (detector.isPlaying() && played > 4.5, "short pause after a MIDI note still counts (grace)");

    for (int i = 0; i < 20; ++i)
        detector.process (0.0f, 0.1);

    check (! detector.isPlaying(), "more than 5 s after the last note: not playing");

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-midi-timing-test");
    folder.deleteRecursively();

    {
        PracticeLog log (folder, "pc");
        PracticeTracker tracker;
        tracker.setLog (&log);
        PracticeTracker::Activity a;
        auto clock = juce::Time (2026, 10, 17, 18, 0, 0, 0, true);

        // A note every 2 s for 60 s, nothing on the audio input.
        auto run = [&] (double seconds, float peak, bool notes)
        {
            double total = 0.0;

            for (int i = 0; i < juce::roundToInt (seconds * 10.0); ++i)
            {
                clock = clock + juce::RelativeTime::milliseconds (100);

                if (notes && i % 20 == 0)
                    tracker.midiNotePlayed();

                tracker.tick (0.1, peak, a, clock);
                total += tracker.getLastTickPlayedSeconds();
            }

            return total;
        };

        check (std::abs (run (60.0, 0.0f, true) - 60.0) < 0.5, "notes every 2 s for a minute: 60 s played");
        check (run (30.0, 0.0f, false) < 5.5, "no notes, silent input: only the grace counts");

        tracker.setAudioLevelCounts (false);
        check (run (30.0, 0.2f, false) < 0.01, "MIDI notes only: a loud audio input counts nothing");
        check (std::abs (run (20.0, 0.2f, true) - 20.0) < 0.5, "MIDI notes only: notes still count");

        tracker.setAudioLevelCounts (true);
        run (10.0, 0.0f, false);
        check (run (10.0, 0.2f, false) > 9.5, "back to automatic: the audio level counts again");
    }

    {
        // Start-button mode: notes don't start the clock, but keep it from pausing itself.
        PracticeLog log (folder, "pc2");
        PracticeTracker tracker;
        tracker.setLog (&log);
        tracker.setStartButtonMode (true);
        tracker.setPaused (false);
        PracticeTracker::Activity a;
        auto clock = juce::Time (2026, 10, 17, 19, 0, 0, 0, true);

        for (int i = 0; i < (int) (PracticeTracker::autoPauseSeconds + 60.0); ++i)
        {
            clock = clock + juce::RelativeTime::seconds (1.0);

            if (i % 4 == 0)
                tracker.midiNotePlayed();

            tracker.tick (1.0, 0.0f, a, clock);
        }

        check (! tracker.isPaused(), "Start-button mode: MIDI notes keep the clock from pausing itself");
    }

    folder.deleteRecursively();
}

static void testNoGuitarTopics()
{
    std::cout << "\n[Topics timed without the guitar, time added by hand]\n";

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-noguitar-test");
    root.deleteRecursively();
    root.createDirectory();

    juce::String earId;

    {
        Library lib (root);
        auto* t = lib.createTopic ("Ear training");
        t->noGuitar = true;
        lib.saveTopic (*t);
        earId = t->id;
    }

    Library lib (root);
    check (lib.findTopic (earId) != nullptr && lib.findTopic (earId)->noGuitar, "\"timed without the guitar\" is saved in topic.xml");

    PracticeLog log (root.getChildFile (".practice"), "pc");
    PracticeTracker tracker;
    tracker.setLog (&log);
    tracker.isNoGuitarTopic = [&] (const juce::String& id) { auto* t = lib.findTopic (id); return t != nullptr && t->noGuitar; };

    PracticeSession session;
    session.start ({ { earId, "Ear training", 5 } });
    tracker.setActiveTopic (earId, "Ear training");
    check (tracker.clockMode(), "an ear-training topic runs on the clock");

    PracticeTracker::Activity a;
    auto clock = juce::Time (2026, 10, 15, 18, 0, 0, 0, true);

    auto run = [&] (double seconds)
    {
        for (double t = 0.0; t < seconds - 1.0e-9; t += 0.1)
        {
            clock = clock + juce::RelativeTime::milliseconds (100);
            tracker.tick (0.1, 0.0f, a, clock);    // silent input: the guitar isn't touched
            session.tick (tracker.getLastTickPlayedSeconds());
        }
    };

    run (120.0);
    check (std::abs (session.getRemainingSeconds() - 180.0) < 0.5, "silent input, but the session clock runs: 3:00 left");

    tracker.setPaused (true);
    run (240.0);
    check (std::abs (session.getRemainingSeconds() - 180.0) < 0.5 && session.isPaused() && ! tracker.isPlaying(),
           "Pause stops the clock (a 4-minute call)");

    tracker.setPaused (false);
    run (60.0);
    tracker.setActiveTopic ({}, {});   // Stop
    check (! tracker.isPaused(), "changing topic clears Pause");

    double total = 0.0;
    bool marked = true;

    for (auto& e : log.getEntries())
    {
        total += e.playedSeconds;
        marked = marked && e.noGuitar && e.topicId == earId;
    }

    check (std::abs (total - 180.0) < 1.0 && marked, "the log gets 3 minutes, marked \"no guitar\" ("
                                                      + juce::String (total, 1) + " s in " + juce::String ((int) log.getEntries().size()) + " entries)");

    // Free practice with silent input still logs nothing.
    run (60.0);
    check (std::abs (session.getRemainingSeconds() - 120.0) < 0.5, "back on free practice: silence doesn't count");

    // Added by hand: a manual entry is kept like any other and counts in the totals.
    PracticeEntry manual;
    manual.id = juce::Uuid().toDashedString();
    manual.topicId = earId;
    manual.topicTitle = "Ear training";
    manual.start = juce::Time (2026, 10, 14, 8, 0, 0, 0, true);
    manual.end = manual.start + juce::RelativeTime::minutes (20);
    manual.playedSeconds = 1200.0;
    manual.manual = true;
    log.save (manual);

    PracticeLog other (root.getChildFile (".practice"), "ipad");
    check (other.find (manual.id) != nullptr && other.find (manual.id)->manual && other.summarise (earId).totalSeconds > 1370.0,
           "time added by hand: saved, marked, in the totals");

    root.deleteRecursively();
}

static void testOutsideSongs()
{
    std::cout << "\n[Songs from outside the library -> Inbox, stems follow]\n";

    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto root = temp.getChildFile ("pv-inbox-test-library");
    const auto outside = temp.getChildFile ("pv-inbox-test-outside");
    root.deleteRecursively();
    outside.deleteRecursively();
    root.createDirectory();
    outside.createDirectory();

    auto makeSong = [&] (const juce::String& name)
    {
        auto f = outside.getChildFile (name);
        f.replaceWithText ("not really audio");
        return f;
    };

    auto makeStems = [&] (const juce::File& folder, bool complete)
    {
        folder.createDirectory();

        for (int i = 0; i < Stems::count; ++i)
            Stems::stemFile (folder, i).replaceWithText ("stem");

        if (complete)
            Stems::infoFile (folder).replaceWithText ("<STEMS/>");
    };

    Library lib (root);

    // A song with finished stems in app data goes to the Inbox, and its stems to the library's .stems.
    const auto song = makeSong ("pv test song 7f3a.mp3");
    const auto oldStems = Stems::folderFor (song, root);
    makeStems (oldStems, true);

    const auto copy = lib.addToInbox (juce::URL (song));
    check (copy == root.getChildFile (song.getFileName()) && copy.existsAsFile() && song.existsAsFile()
               && lib.getInbox().contains (copy),
           "copied into the Inbox (the original stays where it was)");
    check (Stems::isComplete (Stems::folderFor (copy, root)) && ! oldStems.exists()
               && Stems::folderFor (copy, root) == root.getChildFile (".stems").getChildFile (song.getFileName()),
           "finished stems moved from app data to <library>/.stems/<song>");

    // Same file again: a second copy with a new name; nothing left to move.
    const auto copy2 = lib.addToInbox (juce::URL (song));
    check (copy2.getFileName() == "pv test song 7f3a (2).mp3" && ! Stems::folderFor (copy2, root).exists(),
           "adding it again makes \"(2)\", no stems to move");

    // Stems still being made (no stems.xml) stay where they are.
    const auto busy = makeSong ("pv test busy 7f3a.wav");
    const auto busyStems = Stems::folderFor (busy, root);
    makeStems (busyStems, false);
    const auto busyCopy = lib.addToInbox (juce::URL (busy));
    check (busyStems.isDirectory() && ! Stems::folderFor (busyCopy, root).exists(), "unfinished stems are not moved");
    busyStems.deleteRecursively();

    // A topic's "Add files..." brings the stems along too.
    auto* topic = lib.createTopic ("Songs");
    const auto other = makeSong ("pv test other 7f3a.flac");
    makeStems (Stems::folderFor (other, root), true);
    auto* r = lib.addFile (*topic, juce::URL (other));
    check (r != nullptr && Stems::isComplete (Stems::folderFor (topic->getFile (*r), root))
               && ! Stems::folderFor (other, root).exists(),
           "Add files... to a topic: stems move next to the copy");

    root.deleteRecursively();
    outside.deleteRecursively();
}

static void testPracticeLog()
{
    std::cout << "Practice log (played time, entries, setups)" << std::endl;

    // --- PlayDetector: 10 ticks per second like the app's timer.
    {
        constexpr double dt = 0.1;
        juce::Random random (42);

        auto run = [&] (PlayDetector& d, double seconds, auto&& level)
        {
            double played = 0.0;

            for (double t = 0.0; t < seconds - 1.0e-9; t += dt)
                played += d.process (level (t), dt);

            return played;
        };

        PlayDetector d;
        check (run (d, 60.0, [] (double) { return 0.0f; }) == 0.0, "silence: nothing played");

        d.reset();
        const auto hum = run (d, 60.0, [&] (double) { return 0.01f * (0.9f + 0.2f * random.nextFloat()); });
        check (hum < 1.0, "steady -40 dB hum is not playing: " + juce::String (hum, 1) + " s");

        d.reset();
        const auto hiss = run (d, 30.0, [&] (double) { return 0.003f * random.nextFloat(); });
        check (hiss == 0.0, "-50 dB hiss is not playing");

        // A note every 2 s on top of the hum, decaying from -10 dB: 60 s of playing.
        d.reset();
        run (d, 20.0, [&] (double) { return 0.01f; });
        const auto notes = run (d, 60.0, [&] (double t) { return juce::jmax (0.01f, 0.3f * (float) std::exp (-std::fmod (t, 2.0) * 2.0)); });
        check (notes > 58.0 && notes <= 60.0 + 1.0e-6, "notes over hum: 60 s played, counted " + juce::String (notes, 1) + " s");

        // Stopping: counted for the grace period only.
        const auto after = run (d, 30.0, [&] (double) { return 0.01f; });
        check (after > 3.5 && after < 6.5, "after the last note only the grace period counts: " + juce::String (after, 1) + " s");

        // A pause of 4 s between phrases still counts as playing.
        d.reset();
        const auto phrases = run (d, 40.0, [&] (double t) { return std::fmod (t, 10.0) < 6.0 ? 0.2f : 0.002f; });
        check (phrases > 39.0, "short pauses between phrases count: " + juce::String (phrases, 1) + " of 40 s");
    }

    juce::TemporaryFile tempRoot;
    const auto root = tempRoot.getFile();
    root.createDirectory();

    // --- Setup: XML round trip and portable paths.
    {
        PracticeSetup s;
        s.songPath = "Autumn Leaves/backing.mp3";
        s.speed = 0.85; s.semitones = -2.0; s.loopStart = 12.5; s.loopEnd = 20.0; s.loopOn = true;
        s.metronome = true; s.bpm = 112.0; s.beatsPerBar = 3; s.subdivision = 2;
        juce::XmlElement e ("SETUP");
        s.toXml (e);
        const auto r = PracticeSetup::fromXml (e);
        check (r.songPath == s.songPath && std::abs (r.speed - 0.85) < 1.0e-9 && r.semitones == -2.0 && r.loopStart == 12.5
                   && r.loopEnd == 20.0 && r.loopOn && r.metronome && r.bpm == 112.0 && r.beatsPerBar == 3 && r.subdivision == 2,
               "setup survives XML");
        check (r.stems.isEmpty() && ! e.hasAttribute ("stems"), "a setup without a stem mix writes none");

        StemControls mix;
        mix.mute[(size_t) Stems::guitar] = true;
        mix.volume[(size_t) Stems::bass] = 0.5f;
        s.stems = mix.toString();
        juce::XmlElement e2 ("SETUP");
        s.toXml (e2);
        const auto back = StemControls::fromString (PracticeSetup::fromXml (e2).stems);
        check (back.mute[(size_t) Stems::guitar] && ! back.solo[(size_t) Stems::guitar] && std::abs (back.volume[(size_t) Stems::bass] - 0.5f) < 1.0e-3f
                   && back.gainFor (Stems::guitar) == 0.0f && back.gainFor (Stems::vocals) == 1.0f && ! back.isNeutral(),
               "stem mix survives the practice setup (" + s.stems + ")");
        check (StemControls().toString().isEmpty() && StemControls::fromString ({}).isNeutral()
                   && StemControls::fromString ("nonsense guitar:x:m").gainFor (Stems::guitar) == 0.0f,
               "neutral stem mix = empty string; junk is ignored");

        const auto inside = root.getChildFile ("Topic A").getChildFile ("song.mp3");
        const auto portable = PracticeSetup::portablePath (inside, root);
        check (portable == "Topic A/song.mp3" && PracticeSetup::resolvePath (portable, root) == inside,
               "song inside the library is stored relative to it: " + portable);
        const auto outside = root.getParentDirectory().getChildFile ("elsewhere.mp3");
        check (PracticeSetup::portablePath (outside, root) == outside.getFullPathName(), "song outside the library keeps its full path");
        check (PracticeSetup::resolvePath ("../secret.mp3", root) == juce::File(), "relative paths can't leave the library");
    }

    // --- Log: two devices share a folder, each writes its own file.
    const auto logFolder = root.getChildFile (".practice");
    const auto day = juce::Time (2026, 10, 10, 18, 0, 0, 0, true);   // 10 Nov 2026 (month is 0-based)
    {
        PracticeLog pc (logFolder, "aaaaaaaa");
        PracticeLog ipad (logFolder, "bbbbbbbb");

        PracticeEntry e;
        e.id = "e1"; e.topicId = "T1"; e.topicTitle = "Sweep picking";
        e.start = day; e.end = day + juce::RelativeTime::minutes (15);
        e.playedSeconds = 600.0; e.maxBpm = 100.0;
        e.setup.metronome = true; e.setup.bpm = 90.0;
        check (pc.save (e), "entry written");

        PracticeEntry f = e;
        f.id = "e2"; f.start = day + juce::RelativeTime::days (1); f.end = f.start + juce::RelativeTime::minutes (10);
        f.playedSeconds = 300.0; f.maxBpm = 0.0; f.maxSpeed = 0.75; f.setup = {}; f.note = "bar 3 still sloppy";
        check (ipad.save (f), "second device writes too");

        check (logFolder.findChildFiles (juce::File::findFiles, false, "log-*.xml").size() == 2, "one file per device");

        PracticeLog reader (logFolder, "cccccccc");
        check (reader.getEntries().size() == 2 && reader.getEntries()[0].id == "e1" && reader.getEntries()[1].note == "bar 3 still sloppy",
               "a third device reads both, oldest first, notes kept");

        const auto sum = reader.summarise ("T1");
        check (sum.sessions == 2 && sum.totalSeconds == 900.0 && sum.last == f.end && sum.lastBpm == 100.0 && std::abs (sum.lastSpeed - 0.75) < 1.0e-9,
               "summary: sessions, total, last practised, last tempo and speed");
        check (reader.summarise ("nope").last == juce::Time(), "unknown topic: never practised");

        const auto* withSetup = reader.latestSetupFor ("T1");
        check (withSetup != nullptr && withSetup->id == "e1", "latest restorable setup skips entries without one");

        check (reader.playedBetween (day - juce::RelativeTime::hours (1), day + juce::RelativeTime::hours (23)) == 600.0,
               "played time of one day");

        // Updating an entry replaces it.
        e.playedSeconds = 700.0;
        pc.save (e);
        reader.reload();
        check (reader.getEntries().size() == 2 && reader.summarise ("T1").totalSeconds == 1000.0, "saving the same entry again replaces it");
        check (reader.computeSignature() == PracticeLog (logFolder, "dddddddd").computeSignature(), "signature is stable");
    }

    // --- Tracker
    {
        PracticeLog log (root.getChildFile (".tracker"), "aaaaaaaa");
        PracticeTracker tracker;
        tracker.setLog (&log);
        tracker.captureSetup = [] { PracticeSetup s; s.metronome = true; s.bpm = 80.0; return s; };
        int saves = 0;
        tracker.onSaved = [&] { ++saves; };

        auto now = day;
        auto play = [&] (double seconds, float level, const PracticeTracker::Activity& a)
        {
            for (double t = 0.0; t < seconds - 1.0e-9; t += 0.1)
            {
                now = now + juce::RelativeTime::milliseconds (100);
                tracker.tick (0.1, level, a, now);
            }
        };

        PracticeTracker::Activity idle, metronome;
        metronome.metronomeRunning = true;
        metronome.bpm = 120.0;

        tracker.setActiveTopic ("T1", "Sweep picking");
        play (10.0, 0.002f, idle);
        check (tracker.getCurrent() == nullptr, "no entry until you play");

        tracker.setNote ("warm up first");
        play (10.0, 0.3f, metronome);
        check (tracker.getCurrent() != nullptr && tracker.getCurrent()->note == "warm up first", "playing opens an entry with the note typed before");
        tracker.finish();
        check (log.getEntries().empty(), "entries shorter than 20 s are dropped");

        play (90.0, 0.3f, metronome);
        check (saves == 1 && log.getEntries().size() == 1, "saved after a minute of playing, while still open");
        check (std::abs (tracker.getActiveSeconds() - 100.0) < 0.5, "active time counts every entry of the topic: " + juce::String (tracker.getActiveSeconds(), 1));

        PracticeTracker::Activity faster = metronome;
        faster.bpm = 126.0;
        play (10.0, 0.002f, metronome);
        play (5.0, 0.002f, faster);    // tempo raised after you stopped: doesn't count
        play (5.0, 0.3f, metronome);

        tracker.setActiveTopic ("T2", "Autumn Leaves");   // closes the T1 entry
        check (log.getEntries().size() == 1 && log.getEntries()[0].maxBpm == 120.0 && log.getEntries()[0].topicId == "T1"
                   && log.getEntries()[0].setup.bpm == 80.0,
               "switching topic closes the entry: max tempo only while playing, setup captured");
        const auto t1Played = log.getEntries()[0].playedSeconds;
        check (t1Played > 99.0 && t1Played < 106.0, "T1 played time includes the grace after stopping: " + juce::String (t1Played, 1) + " s");

        PracticeTracker::Activity song;
        song.songPlaying = true;
        song.speed = 0.8;
        song.songName = "backing.mp3";
        play (30.0, 0.3f, song);
        play (PracticeTracker::idleGapSeconds + 10.0, 0.002f, song);
        check (tracker.getCurrent() == nullptr && log.getEntries().size() == 2 && std::abs (log.getEntries()[1].maxSpeed - 0.8) < 1.0e-9
                   && log.getEntries()[1].songName == "backing.mp3",
               "five minutes of silence closes the entry");

        play (25.0, 0.3f, song);
        play (6.0, 0.002f, song);   // stopped playing (past the grace period)
        tracker.finish();
        tracker.setNote ("felt good at 80 %");
        check (tracker.getNote() == "felt good at 80 %", "a note typed right after stopping belongs to the entry that ended");
        play (1.0, 0.002f, song);
        check (log.getEntries().size() == 3 && log.getEntries()[2].note.isEmpty(), "the note isn't written on every keystroke");
        play (2.0, 0.002f, song);
        check (log.getEntries()[2].note == "felt good at 80 %", "...but a moment after the last one");
        play (25.0, 0.3f, song);
        tracker.setActiveTopic ({}, {});
        play (25.0, 0.3f, idle);
        tracker.finish();
        check (log.getEntries().size() == 5 && log.getEntries()[4].isFree() && log.getEntries()[3].note.isEmpty(),
               "stopping the topic: playing is logged as free practice");

        // Suspended app: a long gap between ticks isn't counted as playing.
        tracker.setActiveTopic ("T3", "Scales");
        tracker.tick (3600.0, 0.3f, idle, now);
        check (tracker.getCurrent() != nullptr && tracker.getCurrent()->playedSeconds <= 5.0, "a long gap between ticks counts at most 5 s");
        tracker.setLog (nullptr);
    }

    // --- Library: rotation flag, song lookup, app data ignored by the change check.
    {
        Library lib (root);
        auto* t = lib.createTopic ("Alternate picking");
        t->inRotation = true;
        lib.saveTopic (*t);
        const auto folder = t->folder;
        const auto id = t->id;

        Library again (root);
        check (again.findTopic (id) != nullptr && again.findTopic (id)->inRotation, "rotation flag saved in topic.xml");
        check (again.findTopicForFile (folder.getChildFile ("track.mp3")) == again.findTopic (id)
                   && again.findTopicForFile (root.getChildFile ("loose.mp3")) == nullptr,
               "a song in a topic's folder belongs to that topic");

        const auto sig = again.computeSignature();
        root.getChildFile (".practice").getChildFile ("log-x-2026.xml").replaceWithText ("<PRACTICELOG/>");
        check (again.computeSignature() == sig, "writing the practice log doesn't look like a topic change");
        check (std::none_of (again.getTopics().begin(), again.getTopics().end(), [] (const Topic& x) { return x.title.startsWith ("."); }),
               "the .practice folder is not a topic");
    }

    // --- Library: saving leaves no temp files, conflict copies are recognised precisely, "New topic" folders get renamed.
    {
        check (Library::isConflictCopy ("topic (1).xml") && Library::isConflictCopy ("topic 2.xml") && Library::isConflictCopy ("topic2.xml")
                   && Library::isConflictCopy ("topic-DESKTOP-PC.xml") && Library::isConflictCopy ("topic (Joe's conflicted copy 2026-11-16).xml"),
               "cloud conflict copies (Google Drive, iCloud, OneDrive, Dropbox) recognised");
        check (! Library::isConflictCopy ("topic.xml") && ! Library::isConflictCopy ("topic_temp1a2b3c.xml")
                   && ! Library::isConflictCopy ("topics of the week.xml") && ! Library::isConflictCopy (".topic.xml.tmp"),
               "temp files and other names are not conflict copies");

        Library lib (root);
        auto* t = lib.createTopic ("New topic");
        const auto id = t->id;
        auto folder = t->folder;
        check (Library::isDefaultFolderName (folder.getFileName()), "new topic folder: " + folder.getFileName());

        for (int i = 0; i < 5; ++i)
        {
            t->title = "Sweep picking " + juce::String (i);
            lib.saveTopic (*t);
        }

        folder.getChildFile ("topic_temp5e1f.xml").replaceWithText ("<TOPIC/>");   // left behind by an older build
        auto files = folder.findChildFiles (juce::File::findFiles, false);
        check (files.size() == 2, "five saves leave only topic.xml (+ the old leftover): " + juce::String (files.size()) + " file(s)");

        Library again (root);
        auto* reloaded = again.findTopic (id);
        check (reloaded != nullptr && reloaded->conflictFiles.isEmpty() && reloaded->resources.empty() && reloaded->title == "Sweep picking 4",
               "a leftover topic_temp file is neither a conflict nor a resource");

        if (reloaded != nullptr)
        {
            check (again.renameFolderAfterTitle (*reloaded) && reloaded->folder.getFileName() == "Sweep picking 4"
                       && reloaded->folder.getChildFile ("topic.xml").existsAsFile() && ! folder.exists(),
                   "\"New topic\" folder renamed after the title: " + reloaded->folder.getFileName());
            check (! again.renameFolderAfterTitle (*reloaded), "a folder with a real name isn't renamed again");

            // Conflict copy from iCloud: keep it, the current one goes to the bin.
            auto copy = reloaded->folder.getChildFile ("topic 2.xml");
            copy.replaceWithText ("<TOPIC id=\"" + id + "\" title=\"From the iPad\"/>");
            again.rescan();
            reloaded = again.findTopic (id);
            check (reloaded != nullptr && reloaded->conflictFiles == juce::StringArray ({ "topic 2.xml" }), "iCloud copy flagged");

            if (reloaded != nullptr)
            {
                const auto versions = Library::getTopicFileVersions (*reloaded);
                check (versions.size() == 2 && versions[1].title == "From the iPad", "both versions listed with their titles");
                check (Library::keepTopicFileVersion (*reloaded, "topic 2.xml"), "keep the copy");
                again.rescan();
                reloaded = again.findTopic (id);
                check (reloaded != nullptr && reloaded->title == "From the iPad" && reloaded->conflictFiles.isEmpty()
                           && ! copy.exists(), "kept copy became topic.xml, no conflict left");
            }
        }

        auto* withSong = again.createTopic ("New topic");
        withSong->folder.getChildFile ("song.mp3").replaceWithText ("x");
        withSong->title = "Blues in A";
        check (! again.renameFolderAfterTitle (*withSong), "a folder holding audio keeps its name (songs are remembered by path)");
    }

    // Formatting
    check (formatPracticeDuration (10.0) == "< 1 min" && formatPracticeDuration (12 * 60.0) == "12 min"
               && formatPracticeDuration (65 * 60.0) == "1 h 05 min",
           "durations: " + formatPracticeDuration (65 * 60.0));
    check (formatPracticeAgo (juce::Time(), day) == "never" && formatPracticeAgo (day, day) == "today"
               && formatPracticeAgo (day - juce::RelativeTime::days (1), day) == "yesterday"
               && formatPracticeAgo (day - juce::RelativeTime::days (5), day) == "5 days ago"
               && formatPracticeAgo (day - juce::RelativeTime::days (21), day) == "3 weeks ago",
           "'ago' texts");

    root.deleteRecursively();
}

static void testPracticePlanner()
{
    std::cout << "Practice planner (dice, sessions, weekly history, chime)" << std::endl;

    const auto now = juce::Time (2026, 10, 14, 17, 0, 0, 0, true);   // Sat 14 Nov 2026

    check (neglectWeight (juce::Time(), now) == 30.0 && neglectWeight (now - juce::RelativeTime::days (9), now) > 8.9
               && neglectWeight (now - juce::RelativeTime::hours (1), now) == 0.5 && neglectWeight (now - juce::RelativeTime::days (90), now) == 30.0,
           "neglect weight: never = 30, days since, at least 0.5, at most 30");

    std::vector<PlanCandidate> topics { { "fresh", "Practised today", 0.5 }, { "old", "Two weeks ago", 14.0 },
                                        { "never", "Never", 30.0 }, { "mid", "Five days ago", 5.0 } };
    juce::Random random (7);

    // The dice: neglected topics come up more often, but everything can come up.
    {
        std::map<juce::String, int> counts;

        for (int i = 0; i < 10000; ++i)
            ++counts[topics[(size_t) pickWeighted (topics, random)].topicId];

        check (counts["never"] > counts["old"] && counts["old"] > counts["mid"] && counts["mid"] > counts["fresh"] && counts["fresh"] > 0,
               "dice favours neglected topics: never " + juce::String (counts["never"]) + ", 2 weeks " + juce::String (counts["old"])
                   + ", 5 days " + juce::String (counts["mid"]) + ", today " + juce::String (counts["fresh"]));
        check (std::abs (counts["never"] / 10000.0 - 30.0 / 49.5) < 0.03, "pick frequency follows the weights");

        bool neverExcluded = true;

        for (int i = 0; i < 500; ++i)
            neverExcluded = neverExcluded && topics[(size_t) pickWeighted (topics, random, { "never", "old" })].topicId != "never";

        check (neverExcluded, "excluded topics are never picked");
        check (pickWeighted (topics, random, { "fresh", "old", "never", "mid" }) == -1 && pickWeighted ({}, random) == -1,
               "nothing left: no pick");
    }

    // Session plans
    {
        auto plan = planSession (topics, 45, random);
        juce::StringArray ids;

        for (auto& s : plan)
            ids.addIfNotAlreadyThere (s.topicId);

        check (plan.size() == 4 && ids.size() == 4, "45 min -> 4 different topics: " + juce::String ((int) plan.size()));
        check (totalMinutes (plan) == 45 && plan[1].minutes == 10 && plan[0].minutes == 15, "45 min split 15 + 10 + 10 + 10");

        auto shortPlan = planSession (topics, 15, random);
        check (shortPlan.size() == 1 && shortPlan[0].minutes == 15, "15 min -> one topic");

        auto longPlan = planSession (topics, 120, random);
        check (longPlan.size() == 4 && totalMinutes (longPlan) == 120, "more time than topics: each topic once, time spread: "
                                                                          + juce::String ((int) longPlan.size()) + " slots");

        std::vector<PlanCandidate> two (topics.begin(), topics.begin() + 2);
        auto plan2 = planSession (two, 45, random);
        check (plan2.size() == 2 && rerollSlot (plan2, 0, two, random) == false, "re-roll with no other topic left: nothing changes");

        const auto before = plan[2].topicId;
        auto withSpare = topics;
        withSpare.push_back ({ "spare", "Spare", 3.0 });
        const auto ok = rerollSlot (plan, 2, withSpare, random);
        check (ok && plan[2].topicId == "spare" && plan[2].topicId != before, "re-roll picks a topic not already in the session");
        check (planSession ({}, 45, random).empty(), "no rotation topics: empty plan");
    }

    // A running session
    {
        PracticeSession s;
        s.start ({ { "a", "A", 10 }, { "b", "B", 5 } });
        check (s.isRunning() && s.getCurrent()->topicId == "a" && s.getNext()->topicId == "b", "session starts on the first slot");

        int announced = 0;

        for (int i = 0; i < 6100; ++i)   // 610 s in 0.1 s ticks
            announced += s.tick (0.1) ? 1 : 0;

        check (announced == 1 && s.isTimeUp() && s.isRunning() && std::abs (s.getRemainingSeconds() + 10.0) < 0.2,
               "time's up announced once; the slot keeps going (10 s over)");

        s.extend (5);
        check (! s.isTimeUp() && std::abs (s.getRemainingSeconds() - 300.0) < 0.2, "+5 min after time's up gives five full minutes");

        for (int i = 0; i < 3010; ++i)
            announced += s.tick (0.1) ? 1 : 0;

        check (announced == 2, "announced again when the extra time runs out");

        check (s.next() && s.getCurrent()->topicId == "b" && s.getNext() == nullptr && ! s.isTimeUp(), "next slot, fresh clock");
        check (! s.next() && ! s.isRunning() && s.isFinished() && s.getTopicsReached() == 2, "next on the last slot ends the session");
        check (std::abs (s.getElapsedSeconds() - 911.0) < 0.5, "total elapsed: " + juce::String (s.getElapsedSeconds(), 1) + " s");
        s.dismiss();
        check (! s.isFinished(), "summary dismissed");

        s.start ({ { "a", "A", 10 }, { "b", "B", 5 }, { "c", "C", 5 } });
        s.stop();
        check (s.isFinished() && s.getTopicsReached() == 1 && s.getCurrent() == nullptr, "ending early: summary of what was reached");
        check (s.tick (1000.0) == false, "a stopped session doesn't tick");
    }

    // The slot's clock runs only while the guitar is played (owner's example: "warm-up 5 min", a phone call 2 min in).
    {
        PracticeSession s;
        PracticeTracker tracker;
        PracticeTracker::Activity a;
        s.start ({ { "w", "Warm-up", 5 } });
        auto clock = juce::Time (2026, 10, 15, 18, 0, 0, 0, true);
        float lastLevel = 0.0f;
        int noteTick = 0;

        auto run = [&] (double seconds, bool playingGuitar)
        {
            for (double t = 0.0; t < seconds - 1.0e-9; t += 0.1)
            {
                // Plucked notes every second over a quiet floor, or just the floor.
                lastLevel = playingGuitar ? juce::jmax (0.002f, 0.3f * (float) std::exp (-0.25 * (noteTick++ % 10))) : 0.002f;
                clock = clock + juce::RelativeTime::milliseconds (100);
                tracker.tick (0.1, lastLevel, a, clock);
                s.tick (tracker.getLastTickPlayedSeconds());
            }
        };

        run (40.0, false);   // pressed Start, tuning up / getting ready: silence
        check (std::abs (s.getRemainingSeconds() - 300.0) < 0.01 && s.isPaused(), "before playing: the clock hasn't started");

        run (120.0, true);   // two minutes of playing
        const auto afterTwo = s.getRemainingSeconds();
        check (std::abs (afterTwo - 180.0) < 2.0 && ! s.isPaused(), "two minutes of playing: 3:00 left (" + formatClock (afterTwo) + ")");

        run (180.0, false);  // the phone rings: only the detector's short grace period still counts
        const auto afterCall = s.getRemainingSeconds();
        check (afterTwo - afterCall <= PlayDetector::graceSeconds + 0.5 && s.isPaused(),
               "a 3-minute phone call pauses the clock (" + formatClock (afterCall) + " left)");

        bool chimed = false;

        for (int i = 0; i < 2000 && ! chimed; ++i)
        {
            lastLevel = juce::jmax (0.002f, 0.3f * (float) std::exp (-0.25 * (noteTick++ % 10)));
            clock = clock + juce::RelativeTime::milliseconds (100);
            tracker.tick (0.1, lastLevel, a, clock);
            chimed = s.tick (tracker.getLastTickPlayedSeconds());
        }

        check (chimed && std::abs (s.getElapsedSeconds() - 300.0) < 0.2, "time's up after five minutes of actual playing");
    }

    check (formatClock (365.0) == "6:05" && formatClock (-90.0) == "-1:30" && formatClock (0.4) == "0:01", "clock texts");

    // Weekly history
    {
        juce::TemporaryFile temp;
        const auto folder = temp.getFile();
        PracticeLog log (folder, "aaaaaaaa");
        int n = 0;

        auto add = [&] (juce::Time start, double minutes, const char* topic)
        {
            PracticeEntry e;
            e.id = "w" + juce::String (++n);
            e.topicId = topic;
            e.start = start;
            e.end = start + juce::RelativeTime::minutes (minutes);
            e.playedSeconds = minutes * 60.0;
            log.save (e);
        };

        const auto monday = PracticeLog::startOfWeek (now);
        check (monday.getDayOfWeek() == 1 && monday.getHours() == 0 && monday.getDayOfMonth() == 9, "week starts Monday 9 Nov 00:00");
        check (PracticeLog::startOfWeek (monday) == monday && PracticeLog::startOfWeek (monday - juce::RelativeTime::seconds (1)).getDayOfMonth() == 2,
               "Monday 00:00 is in its own week; Sunday night in the previous one");

        add (now, 20, "T");                                        // this week
        add (monday + juce::RelativeTime::hours (1), 10, "T");     // this week, Monday
        add (monday - juce::RelativeTime::hours (1), 15, "T");     // last week, Sunday night
        add (now - juce::RelativeTime::days (28), 30, "T");        // 4 weeks ago
        add (now - juce::RelativeTime::days (200), 99, "T");       // too old
        add (now, 50, "Other");                                    // another topic

        const auto weeks = log.weeklySeconds ("T", now, 12);
        check (weeks.size() == 12 && weeks[11] == 30 * 60.0 && weeks[10] == 15 * 60.0 && weeks[7] == 30 * 60.0,
               "minutes per week: this week 30, last week 15, 4 weeks ago 30");
        double total = 0.0;

        for (auto w : weeks)
            total += w;

        check (total == 75 * 60.0, "older entries and other topics aren't counted");

        add (now, 5, "");                                          // free practice, this week
        const auto allWeeks = log.weeklySecondsAll (now, 12);
        check (allWeeks.size() == 12 && allWeeks[11] == 85 * 60.0 && allWeeks[10] == 15 * 60.0 && allWeeks[7] == 30 * 60.0,
               "all topics: this week 30 + 50 (other topic) + 5 (free practice), older weeks as before");
        check (log.historyFor ("T").size() == 5 && log.historyFor ("T").front()->playedSeconds == 99 * 60.0, "a topic's history, oldest first");
        folder.deleteRecursively();
    }

    // The chime plays even while the metronome is stopped and muted, then stops.
    {
        MetronomeProcessor metronome;
        metronome.prepareToPlay (48000.0, 512);
        metronome.clicksMuted = true;
        metronome.requestChime();
        const auto first = render (metronome, 48000.0, 0.5, 0.5);
        const auto later = render (metronome, 48000.0, 3.0, 0.5);
        const auto peak = first.getMagnitude (0, 0, first.getNumSamples());
        check (peak > 0.05f && peak < 0.5f, "chime is audible but soft: peak " + juce::String (peak, 3));
        check (later.getMagnitude (0, 0, later.getNumSamples()) == 0.0f, "chime has ended after 3 s");
    }
}

//==============================================================================
namespace
{
    /** A song of short 1 kHz bursts at irregular times, so any timing error shows up. */
    std::shared_ptr<LoadedSong> makeClickSong (double sampleRate, double seconds)
    {
        auto song = std::make_shared<LoadedSong>();
        song->sampleRate = sampleRate;
        song->name = "clicks.wav";
        const auto length = (int) (sampleRate * seconds);
        song->audio.setSize (2, length);
        song->audio.clear();
        juce::Random random (42);
        const auto burst = (int) (0.02 * sampleRate);

        for (double t = 0.2; t < seconds - 0.1; t += 0.17 + 0.2 * random.nextDouble())
        {
            const auto start = (int) (t * sampleRate);
            const auto amplitude = 0.3f + 0.6f * random.nextFloat();

            for (int i = 0; i < burst && start + i < length; ++i)
            {
                const auto v = amplitude * (float) (std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / sampleRate)
                                                    * std::exp (-i / (0.004 * sampleRate)));
                song->audio.setSample (0, start + i, v);
                song->audio.setSample (1, start + i, v);
            }
        }

        return song;
    }

    /** Lag (in samples, positive = b later than a) that best lines up the envelopes of a and b in [from, to). */
    int envelopeLag (const std::vector<float>& a, const std::vector<float>& b, int from, int to, int maxLag)
    {
        constexpr int hop = 16;
        auto envelope = [&] (const std::vector<float>& x, int start, int count)
        {
            std::vector<float> e ((size_t) count, 0.0f);

            for (int i = 0; i < count; ++i)
                for (int j = 0; j < hop; ++j)
                    if (const auto k = start + i * hop + j; k >= 0 && k < (int) x.size())
                        e[(size_t) i] += std::abs (x[(size_t) k]);

            return e;
        };

        const auto count = (to - from) / hop;
        const auto lags = maxLag / hop;
        const auto ea = envelope (a, from, count);
        const auto eb = envelope (b, from - lags * hop, count + 2 * lags);
        auto best = 0.0f;
        auto bestLag = 0;

        for (int lag = -lags; lag <= lags; ++lag)
        {
            auto sum = 0.0f;

            for (int i = 0; i < count; ++i)
                sum += ea[(size_t) i] * eb[(size_t) (i + lag + lags)];

            if (sum > best)
            {
                best = sum;
                bestLag = lag;
            }
        }

        return bestLag * hop;
    }

    float rms (const std::vector<float>& x, int from, int to)
    {
        double sum = 0.0;

        for (int i = from; i < to && i < (int) x.size(); ++i)
            sum += (double) x[(size_t) i] * x[(size_t) i];

        return (float) std::sqrt (sum / juce::jmax (1, to - from));
    }
}

static void testSessionPlans()
{
    std::cout << "\n[Saved session plans]\n";

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-plans-test");
    root.deleteRecursively();
    root.createDirectory();

    std::map<juce::String, juce::String> titles { { "a", "Alternate picking" }, { "b", "Barre chords" }, { "c", "Chord melody" } };
    auto titleOf = [&titles] (const juce::String& id) { auto it = titles.find (id); return it != titles.end() ? it->second : juce::String(); };
    auto candidatesFor = [&titles] (const juce::String& tag)
    {
        std::vector<PlanCandidate> result;

        for (auto& [id, title] : titles)
            if (tag.isEmpty() || id != "a")   // only b and c carry the tag
                result.push_back ({ id, title, 1.0 });

        return result;
    };

    // What the planner had on screen: a fixed topic, a random slot, another fixed topic.
    std::vector<SessionSlot> onScreen { { "a", "Alternate picking", 15 }, { "c", "Chord melody", 10 }, { "b", "Barre chords", 20 } };
    onScreen[1].random = true;
    onScreen[1].tag = "chords";

    juce::String id;
    {
        PlanList plans (root);
        check (plans.getPlans().empty(), "a new library has no plans");
        check (plans.add ("  ", {}).isEmpty(), "an empty name is refused");

        id = plans.add ("Morning", planSlotsFrom (onScreen));
        const auto* p = plans.find (id);
        check (p != nullptr && p->slots.size() == 3 && p->slots[1].random && p->slots[1].topicId.isEmpty()
                   && p->slots[1].tag == "chords" && p->slots[2].topicId == "b" && p->slots[2].minutes == 20,
               "saved: fixed topics by id, the random slot keeps only its tag");
        check (plans.uniqueName ("morning") == "morning 2" && plans.uniqueName ("Evening") == "Evening", "unique names ignore case");
    }

    // Another device reads it; edits there are picked up here.
    PlanList here (root);
    {
        check (here.getPlans().size() == 1 && here.getPlans()[0].name == "Morning", "another PlanList reads the saved plan");

        PlanList there (root);
        const auto copy = there.duplicate (id);
        check (there.find (copy) != nullptr && there.find (copy)->name == "Morning (copy)" && there.find (copy)->slots.size() == 3,
               "duplicate: same slots, \"(copy)\" name");
        check (there.rename (copy, "Evening") && ! there.rename (copy, "  "), "rename (not to an empty name)");
        juce::Thread::sleep (20);
        there.setSlots (id, { { "a", 30 } });
    }
    check (here.reloadIfChanged() && here.getPlans().size() == 2 && here.findByName ("evening") != nullptr
               && here.find (id)->slots.size() == 1 && here.find (id)->slots[0].minutes == 30,
           "changes from another device are reloaded");
    check (! here.reloadIfChanged(), "no change: nothing re-read");

    // Using a plan: random slots avoid the plan's fixed topics, deleted topics show as missing.
    {
        juce::Random random (3);
        SessionPlan plan { "x", "Test", { { "a", 15 }, { {}, 10, true, "chords" }, { "b", 20 } } };
        bool alwaysC = true;

        for (int i = 0; i < 50; ++i)
        {
            const auto slots = slotsFromPlan (plan, titleOf, candidatesFor, random);
            alwaysC = alwaysC && slots.size() == 3 && slots[1].topicId == "c" && slots[1].random && ! slots[1].missing;
        }

        check (alwaysC, "random slot picks a tagged topic not already in the plan");

        titles.erase ("b");
        auto slots = slotsFromPlan (plan, titleOf, candidatesFor, random);
        check (slots[2].missing && slots[2].topicId == "b" && slots[2].title.contains ("Missing"), "deleted topic -> missing slot");
        check (playableSlots (slots).size() == 2 && totalMinutes (playableSlots (slots)) == 25, "missing slots are skipped when starting");
        check (planSlotsFrom (slots)[2].topicId == "b", "re-saving keeps the missing topic's id");

        std::vector<PlanCandidate> pool { { "c", "Chord melody", 1.0 }, { "d", "Dorian", 1.0 } };
        check (rerollSlot (slots, 2, pool, random) && ! slots[2].missing && slots[2].topicId == "d", "re-roll fixes a missing slot");

        titles.erase ("c");
        slots = slotsFromPlan (plan, titleOf, candidatesFor, random);
        check (slots[1].missing && slots[1].title.contains ("#chords"), "no topic left for a random slot -> missing");
    }

    check (here.remove (id) && here.find (id) == nullptr && here.getPlans().size() == 1, "delete a plan");
    root.deleteRecursively();
}

static void testTakes()
{
    std::cout << "Takes (recording + replay over the song)" << std::endl;

    // Timeline maths -----------------------------------------------------------
    {
        TakeTimeline tl;
        TakeSegment a;  a.takeSeconds = 0.0; a.songSeconds = 10.0; a.playing = true; a.speed = 0.5;
        TakeSegment b;  b.takeSeconds = 4.0; b.songSeconds = 30.0; b.playing = true; b.speed = 1.0;
                        b.loopOn = true; b.loopStart = 30.0; b.loopEnd = 32.0;
        TakeSegment c;  c.takeSeconds = 9.0; c.songSeconds = 31.0; c.playing = false;
        tl.segments = { a, b, c };

        check (tl.segmentAt (-1.0) == 0 && tl.segmentAt (3.9) == 0 && tl.segmentAt (4.0) == 1 && tl.segmentAt (20.0) == 2,
               "segmentAt picks the segment in force");
        check (std::abs (tl.songSecondsAt (2.0) - 11.0) < 1e-9, "half speed: 2 s of take = 1 s of song");
        check (std::abs (tl.songSecondsAt (-1.0) - 9.5) < 1e-9, "before the first segment: extrapolated back");
        check (std::abs (tl.songSecondsAt (5.5) - 31.5) < 1e-9, "inside the loop before it wraps");
        check (std::abs (tl.songSecondsAt (6.5) - 30.5) < 1e-9, "loop wraps like the player (32 -> 30)");
        check (std::abs (tl.songSecondsAt (12.0) - 31.0) < 1e-9, "paused: the song stays put");
        check (tl.usesSong(), "a timeline with playing segments uses the song");

        TakeTimeline silent;
        silent.segments = { c };
        check (! silent.usesSong() && ! TakeTimeline().usesSong(), "no playing segment: the take stands alone");

        // Round trip through the info file.
        TakeInfo info;
        info.recorded = juce::Time (2026, 10, 15, 14, 32, 5, 0, true);
        info.lengthSeconds = 151.25;
        info.topicId = "topic-1";
        info.songPath = "Jazz/Autumn Leaves.mp3";
        info.songName = "Autumn Leaves.mp3";
        info.timeline = tl;

        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-test-takes");
        folder.deleteRecursively();
        folder.createDirectory();
        const auto audioFile = folder.getChildFile ("Take 1.flac");
        audioFile.replaceWithText ("x");
        check (info.save (audioFile) && TakeInfo::infoFileFor (audioFile) == folder.getChildFile (".takes/Take 1.flac.xml"),
               "info saved in the folder's hidden .takes folder");

        const auto loaded = TakeInfo::load (audioFile);
        auto same = loaded.has_value() && loaded->songPath == info.songPath && loaded->topicId == "topic-1"
                 && std::abs (loaded->lengthSeconds - 151.25) < 1e-9 && loaded->recorded == info.recorded
                 && loaded->timeline.segments.size() == 3;

        if (same)
            for (size_t i = 0; i < 3; ++i)
            {
                const auto& x = loaded->timeline.segments[i];
                const auto& y = tl.segments[i];
                same = same && x.takeSeconds == y.takeSeconds && x.songSeconds == y.songSeconds && x.playing == y.playing
                         && x.speed == y.speed && x.loopOn == y.loopOn && x.loopStart == y.loopStart && x.loopEnd == y.loopEnd;
            }

        check (same, "take info survives a save / load round trip");
        check (! TakeInfo::load (folder.getChildFile ("nothing.flac")).has_value(), "no info file: not a take");

        // The library marks it and shows a TAKE badge; removing it removes its info too.
        {
            auto topicFolder = folder.getChildFile ("Lesson");
            topicFolder.createDirectory();
            const auto takeFile = topicFolder.getChildFile ("Take 2.flac");
            takeFile.replaceWithText ("x");
            info.save (takeFile);
            topicFolder.getChildFile ("song.mp3").replaceWithText ("x");

            Library lib (folder);
            lib.rescan();
            auto* topic = lib.getTopics().empty() ? nullptr : lib.findTopic (lib.getTopics().front().id);
            auto* take = topic != nullptr ? topic->findResource ("Take 2.flac") : nullptr;
            auto* song = topic != nullptr ? topic->findResource ("song.mp3") : nullptr;
            check (take != nullptr && take->isTake && badgeText (*take) == "TAKE" && song != nullptr && ! song->isTake,
                   "library: takes are marked and badged TAKE, other audio isn't");
            check (lib.getInbox().contains (audioFile) && lib.getInbox().size() == 1, "the .takes folder itself is not listed");

            if (topic != nullptr)
            {
                lib.removeResource (*topic, "Take 2.flac");
                check (! TakeInfo::infoFileFor (takeFile).existsAsFile(), "removing a take removes its info file");

                auto* moved = lib.moveInboxFile (*topic, audioFile);
                check (moved != nullptr && moved->isTake && TakeInfo::infoFileFor (topic->getFile (*moved)).existsAsFile()
                           && ! TakeInfo::infoFileFor (audioFile).existsAsFile(),
                       "moving a take from the Inbox into a topic moves its info along");
            }
        }

        check (TakeInfo::makeFileName (info.recorded, "Autumn Leaves.mp3") == "Take 2026-11-15 14.32 - Autumn Leaves"
                   && TakeInfo::makeFileName (info.recorded, {}) == "Take 2026-11-15 14.32"
                   && TakeInfo::makeFileName (info.recorded, "AC/DC: T.N.T.") == "Take 2026-11-15 14.32 - ACDC T.N.T",
               "take file names: date + song, no extension, no illegal characters");
        folder.deleteRecursively();
    }

    // Record along to the song, then replay the take over the song ------------------------------------------
    for (const auto songRate : { 48000.0, 44100.0 })
    {
        constexpr double rate = 48000.0;
        constexpr int block = 512;
        constexpr int comp = 777;           // round-trip latency: the "guitar" arrives this late
        const auto total = (int) (12.0 * rate);
        auto song = makeClickSong (songRate, 20.0);
        const auto label = juce::String (" (song at ") + juce::String (songRate / 1000.0, 1) + " kHz)";

        // 1. Record: the "guitar" plays exactly what it hears, comp samples late.
        PlayerProcessor player;
        player.prepareToPlay (rate, block);
        player.setSong (song);
        player.setSpeed (0.8);      // starts paused at 0:00; Play is pressed 0.3 s into the take (like in real use)

        Recorder recorder (30.0);
        player.setSongBlockListener (&recorder);
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-test-take.flac");
        juce::String error;
        check (recorder.start (file, rate, comp, error), "recorder starts" + label);

        std::vector<float> heard ((size_t) total, 0.0f);
        juce::AudioBuffer<float> buffer (2, block), guitar (2, block);
        juce::MidiBuffer midi;

        for (int g = 0; g < total; g += block)
        {
            auto at = [&] (double seconds) { const auto s = (int) (seconds * rate); return g <= s && s < g + block; };

            if (at (0.3))  player.play();
            if (at (4.0))  player.seekToSeconds (8.0);
            if (at (6.0))  { player.setLoopSeconds (9.0, 10.5); player.setLoopEnabled (true); }
            if (at (9.0))  player.pause();
            if (at (9.5))  player.play();

            recorder.beginBlock (g, block);
            buffer.clear();
            player.processBlock (buffer, midi);

            for (int i = 0; i < block && g + i < total; ++i)
                heard[(size_t) (g + i)] = buffer.getSample (0, i);

            for (int i = 0; i < block; ++i)
            {
                const auto k = g + i - comp;
                const auto v = k >= 0 && k < total ? heard[(size_t) k] : 0.0f;
                guitar.setSample (0, i, v);
                guitar.setSample (1, i, v);
            }

            recorder.captureGuitar (guitar.getReadPointer (0), guitar.getReadPointer (1), block);
        }

        const auto result = recorder.stop();
        player.setSongBlockListener (nullptr);
        const auto expectedSeconds = (total - comp) / rate;
        check (! recorder.isRecording() && std::abs (result.seconds - expectedSeconds) < 0.001 && ! recorder.hasDroppedAudio(),
               "take length = recording time minus the latency" + label);
        check (result.timeline.usesSong() && result.timeline.segments.size() == 6,
               "timeline: paused, play, seek, loop, pause, play (" + juce::String ((int) result.timeline.segments.size()) + " segments)" + label);

        // The file holds the guitar shifted back by the latency: take sample k = what was heard at k.
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        auto take = std::make_shared<PlayerProcessor::Take>();
        take->timeline = result.timeline;

        if (reader != nullptr)
        {
            take->audio.sampleRate = reader->sampleRate;
            take->audio.audio.setSize (2, (int) reader->lengthInSamples);
            reader->read (&take->audio.audio, 0, (int) reader->lengthInSamples, 0, true, true);
        }

        reader.reset();
        auto maxError = take->audio.audio.getNumSamples() > 0 ? 0.0f : 1.0f;

        for (int k = 0; k < take->audio.audio.getNumSamples() && k < total; ++k)
            maxError = juce::jmax (maxError, std::abs (take->audio.audio.getSample (0, k) - heard[(size_t) k]));

        check (take->audio.audio.getNumSamples() == total - comp && maxError < 1.0e-4f,
               "FLAC take lines up with what was heard, latency removed (max error " + juce::String (maxError, 6) + ")" + label);

        // 2. Replay: song alone (take muted), then take alone (song muted).
        auto replay = [&] (float songGain, float takeGain, double startAt)
        {
            PlayerProcessor p;
            p.prepareToPlay (rate, block);
            p.setSong (song);
            p.setTake (take);
            p.gain = songGain;
            p.takeGain = takeGain;
            p.seekTake (startAt);
            p.playTake();

            std::vector<float> out ((size_t) total, 0.0f);
            const auto start = (int) (startAt * rate);

            for (int g = start; g < total; g += block)
            {
                buffer.clear();
                p.processBlock (buffer, midi);

                for (int i = 0; i < block && g + i < total; ++i)
                    out[(size_t) (g + i)] = buffer.getSample (0, i);
            }

            return std::make_pair (out, p.isTakePlaying());
        };

        const auto [songOut, stillPlaying] = replay (1.0f, 0.0f, 0.0);
        const auto takeOut = replay (0.0f, 1.0f, 0.0).first;

        struct Window { double from, to; const char* what; };

        for (auto w : { Window { 0.5, 3.5, "start" }, Window { 4.3, 5.8, "after a seek" }, Window { 6.5, 8.8, "in the A-B loop" },
                        Window { 9.7, 11.5, "after a pause" } })
        {
            const auto lag = envelopeLag (songOut, takeOut, (int) (w.from * rate), (int) (w.to * rate), 2400);
            check (std::abs (lag) <= 48, juce::String ("replay: take and song in step ") + w.what + " (lag "
                                             + juce::String (lag) + " samples)" + label);
        }

        check (rms (songOut, (int) (9.1 * rate), (int) (9.45 * rate)) < 1.0e-4f && rms (songOut, (int) (8.0 * rate), (int) (8.8 * rate)) > 1.0e-3f,
               "replay: the song pauses where it paused while recording" + label);
        check (! stillPlaying || take->getLengthSeconds() > 11.9, "replay stops at the end of the take" + label);

        // Starting in the middle (seekTake) lines up too.
        const auto fromMiddle = replay (1.0f, 0.0f, 7.0).first;
        const auto lag = envelopeLag (fromMiddle, takeOut, (int) (7.3 * rate), (int) (8.8 * rate), 2400);
        check (std::abs (lag) <= 48, "replay from the middle of the take is in step (lag " + juce::String (lag) + ")" + label);

        // 3. Export mix: the offline render matches the live replay (song + take), scaled down only if it would clip.
        {
            juce::AudioBuffer<float> mix;
            double mixRate = 0.0;
            const auto rendered = PlayerProcessor::renderTakeMix (song, take, 1.0f, 1.0f, mix, mixRate);
            const auto both = replay (1.0f, 1.0f, 0.0).first;
            double dot = 0.0, energy = 0.0, residual = 0.0;
            auto peak = 0.0f;

            for (int k = 0; k < mix.getNumSamples() && k < total; ++k)
            {
                dot += (double) mix.getSample (0, k) * both[(size_t) k];
                energy += (double) both[(size_t) k] * both[(size_t) k];
                peak = juce::jmax (peak, std::abs (mix.getSample (0, k)), std::abs (mix.getSample (1, k)));
            }

            const auto scale = energy > 0.0 ? dot / energy : 0.0;

            for (int k = 0; k < mix.getNumSamples() && k < total; ++k)
                residual += juce::square ((double) mix.getSample (0, k) - scale * both[(size_t) k]);

            const auto relError = energy > 0.0 && scale > 0.0 ? std::sqrt (residual / (scale * scale * energy)) : 1.0;
            check (rendered.wasOk() && mixRate == take->audio.sampleRate && mix.getNumSamples() == take->audio.audio.getNumSamples(),
                   "export mix renders the whole take at its rate" + label + (rendered.wasOk() ? juce::String() : ": " + rendered.getErrorMessage()));
            check (relError < 0.01 && scale > 0.2 && scale < 1.001, "export mix = song + take as replayed (error " + juce::String (relError, 5)
                                                                     + ", scale " + juce::String (scale, 3) + ")" + label);
            check (peak <= 0.9801f, "export mix doesn't clip: peak " + juce::String (peak, 3) + label);
        }

       #if JUCE_WINDOWS
        // 4. The take is kept as AAC (.m4a): small, and still in step with the song.
        if (MediaFoundationAudio::isAvailable())
        {
            const auto m4a = file.withFileExtension ("m4a");
            const auto converted = Recorder::convertToAac (file, m4a);
            check (converted.wasOk(), "take converted to m4a" + label + (converted.wasOk() ? juce::String() : ": " + converted.getErrorMessage()));
            juce::AudioBuffer<float> decoded;
            double decodedRate = 0.0;
            const auto read = MediaFoundationAudio::decodeFile (m4a, decoded, decodedRate);
            const auto seconds = decodedRate > 0.0 ? decoded.getNumSamples() / decodedRate : 0.0;
            check (read.wasOk() && decodedRate == rate && std::abs (seconds - take->getLengthSeconds()) < 0.05,
                   "m4a take decodes: " + juce::String (seconds, 3) + " s (want " + juce::String (take->getLengthSeconds(), 3) + ")" + label);
            const auto kbps = (double) m4a.getSize() * 8.0 / 1000.0 / juce::jmax (0.1, take->getLengthSeconds());
            check (kbps > 64.0 && kbps < 200.0, "m4a take is small: " + juce::String (kbps, 0) + " kbit/s" + label);

            std::vector<float> original ((size_t) total, 0.0f), aac ((size_t) total, 0.0f);

            for (int k = 0; k < total; ++k)
            {
                if (k < take->audio.audio.getNumSamples()) original[(size_t) k] = take->audio.audio.getSample (0, k);
                if (k < decoded.getNumSamples())           aac[(size_t) k] = decoded.getSample (0, k);
            }

            const auto aacLag = envelopeLag (original, aac, (int) (0.5 * rate), (int) (3.5 * rate), 4800);
            check (std::abs (aacLag) <= 48, "m4a take keeps its timing (lag " + juce::String (aacLag) + " samples)" + label);
            m4a.deleteFile();
        }
       #endif

        file.deleteFile();
    }
}

static void testStorage()
{
    std::cout << "Storage and clean-up" << std::endl;

    const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-test-storage");
    base.deleteRecursively();
    const auto root = base.getChildFile ("Library"), loops = base.getChildFile ("Loops"), appData = base.getChildFile ("AppData");
    auto writeBytes = [] (const juce::File& f, int n)
    {
        f.getParentDirectory().createDirectory();
        juce::MemoryBlock block ((size_t) n, true);
        f.replaceWithData (block.getData(), block.getSize());
    };

    root.createDirectory();

    {
        Library lib (root);
        auto* topic = lib.createTopic ("Storage test");
        check (topic != nullptr, "storage: test topic created");

        if (topic == nullptr)
            return;

        // A topic with a song, a 40-day-old take and a 2-day-old take; the practice log; a take in the Inbox.
        writeBytes (topic->folder.getChildFile ("song.mp3"), 50000);
        const auto oldTake = topic->folder.getChildFile ("Take old.m4a"), newTake = topic->folder.getChildFile ("Take new.m4a");
        writeBytes (oldTake, 3000);
        writeBytes (newTake, 2000);
        const auto now = juce::Time::getCurrentTime();
        TakeInfo info;
        info.recorded = now - juce::RelativeTime::days (40);
        info.lengthSeconds = 75.0;
        info.save (oldTake);
        info.recorded = now - juce::RelativeTime::days (2);
        info.save (newTake);
        writeBytes (root.getChildFile (".practice/log.xml"), 700);
        const auto inboxTake = root.getChildFile ("Take inbox.m4a");
        writeBytes (inboxTake, 1000);
        info.recorded = now - juce::RelativeTime::days (10);
        info.save (inboxTake);

        writeBytes (loops.getChildFile ("Session A/loop1.wav"), 4000);
        writeBytes (loops.getChildFile ("Session B/loop1.wav"), 6000);
        writeBytes (appData.getChildFile ("chain.xml"), 100);
        writeBytes (appData.getChildFile ("WebView2/cache.bin"), 9000);

        lib.rescan();
        const auto takes = StorageScan::listTakes (lib);
        check (takes.size() == 3, "storage: 3 takes found (2 in the topic, 1 in the Inbox): " + juce::String ((int) takes.size()));

        const auto report = StorageScan::run (root, takes, loops, appData);
        using K = StorageItem::Kind;
        auto count = [&] (K k) { return (int) std::count_if (report.items.begin(), report.items.end(), [k] (const StorageItem& i) { return i.kind == k; }); };
        check (count (K::take) == 3 && count (K::loops) == 2 && count (K::webCache) == 1,
               "storage: items = 3 takes, 2 loop sessions, the web cache");
        check (report.takeBytes == 6000 && report.loopBytes == 10000 && report.webCacheBytes == 9000 && report.practiceLogBytes == 700,
               "storage: sizes added up (takes " + juce::String (report.takeBytes) + ", loops " + juce::String (report.loopBytes)
                   + ", cache " + juce::String (report.webCacheBytes) + ", log " + juce::String (report.practiceLogBytes) + ")");
        check (report.libraryBytes >= 50000 + 6000 + 700, "storage: library size includes everything: " + juce::String (report.libraryBytes));
        check (report.appDataBytes >= 9100 && report.appDataBytes < 20000, "storage: app data size: " + juce::String (report.appDataBytes));
        check (! report.items.empty() && report.items[0].file == newTake, "storage: newest take listed first");

        const auto month = report.olderThan (K::take, juce::RelativeTime::days (30), now);
        const auto week = report.olderThan (K::take, juce::RelativeTime::weeks (1), now);
        check (month.size() == 1 && report.items[(size_t) month[0]].file == oldTake, "storage: 'older than a month' picks the 40-day-old take");
        check (week.size() == 2, "storage: 'older than a week' picks 2 takes");
        check (report.olderThan (K::loops, {}, now).size() == 2, "storage: 'all saved loops'");

        check (StorageScan::formatBytes (1536) == "1 KB" && StorageScan::formatBytes (5 * 1024 * 1024) == "5.0 MB"
                   && StorageScan::formatBytes ((juce::int64) 3 * 1024 * 1024 * 1024) == "3.00 GB",
               "storage: sizes formatted (" + StorageScan::formatBytes (5 * 1024 * 1024) + ")");

        // Stems of the song: listed, and removed with the song.
        const auto stemFolder = Stems::folderFor (topic->folder.getChildFile ("song.mp3"), root);
        writeBytes (Stems::stemFile (stemFolder, 0), 5000);
        Stems::infoFile (stemFolder).replaceWithText ("<STEMS song=\"song\"/>");
        const auto withStems = StorageScan::run (root, takes, loops, appData);
        const auto stemItems = withStems.olderThan (K::stems, {}, now);
        check (stemItems.size() == 1 && withStems.items[(size_t) stemItems[0]].title == "song" && withStems.stemBytes > 5000,
               "storage: a song's stems are listed (" + juce::String ((int) stemItems.size()) + ")");

        if (auto* songTopic = lib.findTopic (topic->id))
        {
            juce::String songId;

            for (auto& r : songTopic->resources)
                if (r.fileName == "song.mp3")
                    songId = r.id;

            check (songId.isNotEmpty() && lib.removeResource (*songTopic, songId) && ! stemFolder.exists(), "storage: removing a song removes its stems");
        }

        // Removing a take from the topic takes its details file with it.
        auto* t = lib.findTopic (report.items[0].topicId);
        check (t != nullptr && lib.removeResource (*t, report.items[0].resourceId) && ! newTake.exists()
                   && ! TakeInfo::infoFileFor (newTake).exists(), "storage: removing a take removes its details too");

        // The web cache is cleared at the next start, and only when asked.
        StorageScan::clearWebCacheIfAsked (appData);
        check (appData.getChildFile ("WebView2").isDirectory(), "storage: web cache kept unless asked");
        StorageScan::clearWebCacheMarker (appData).replaceWithText ("x");
        StorageScan::clearWebCacheIfAsked (appData);
        check (! appData.getChildFile ("WebView2").exists() && ! StorageScan::clearWebCacheMarker (appData).exists(),
               "storage: web cache cleared at start when asked");
    }

    base.deleteRecursively();
}

/** A stand-in network: source 0 gets the input spectrogram back (and nothing from the time branch), the rest
    silence, so the whole separation pipeline must give the input back as stem 0. */
struct PassThroughModel final : DemucsModel
{
    juce::Result run (const float*, const float* spec, float* freqOut, float* timeOut) override
    {
        std::fill (freqOut, freqOut + (size_t) Stems::count * specSize, 0.0f);
        std::fill (timeOut, timeOut + (size_t) Stems::count * 2 * segment, 0.0f);
        std::copy_n (spec, specSize, freqOut);
        ++calls;
        return juce::Result::ok();
    }

    int calls = 0;
};

static void testStems()
{
    std::cout << "Stem separation" << std::endl;

    // JUCE's real FFT: forward is e^-i (as numpy / torch), the inverse is scaled by 1/N.
    {
        juce::dsp::FFT fft (12);
        std::vector<float> d (8192, 0.0f);

        for (int n = 0; n < 4096; ++n)
            d[(size_t) n] = std::sin (juce::MathConstants<float>::twoPi * 5.0f * (float) n / 4096.0f);

        fft.performRealOnlyForwardTransform (d.data(), true);
        check (std::abs (d[11] + 2048.0f) < 0.5f && std::abs (d[10]) < 0.5f, "FFT sign: sin at bin 5 = -i N/2 (" + juce::String (d[11], 1) + ")");
        fft.performRealOnlyInverseTransform (d.data());
        check (std::abs (d[1024] - std::sin (juce::MathConstants<float>::twoPi * 5.0f * 1024.0f / 4096.0f)) < 1.0e-4f, "inverse FFT scaled by 1/N");
    }

    // Spectrogram there and back = the segment (away from its edges, where demucs drops frames).
    {
        constexpr int S = DemucsModel::segment;
        std::vector<float> l ((size_t) S), r ((size_t) S), spec ((size_t) DemucsModel::specSize), outL ((size_t) S, 0.0f), outR ((size_t) S, 0.0f);
        juce::Random random (7);

        for (int i = 0; i < S; ++i)
        {
            l[(size_t) i] = 0.5f * std::sin ((float) i * 0.031f) + 0.1f * (random.nextFloat() - 0.5f);
            r[(size_t) i] = 0.3f * std::sin ((float) i * 0.007f) + 0.1f * (random.nextFloat() - 0.5f);
        }

        DemucsDsp dsp;
        dsp.spectrogram (l.data(), r.data(), spec.data());
        dsp.addInverse (spec.data(), outL.data(), outR.data());
        auto maxError = 0.0f;

        for (int i = 3000; i < S - 3000; ++i)
            maxError = juce::jmax (maxError, std::abs (outL[(size_t) i] - l[(size_t) i]), std::abs (outR[(size_t) i] - r[(size_t) i]));

        check (maxError < 3.0e-3f, "Demucs spectrogram -> inverse gives the segment back (minus the Nyquist bin demucs drops) (max error " + juce::String (maxError, 7) + ")");
    }

    // Whole pipeline (segments, overlap-add, normalisation) with the pass-through network.
    {
        const int length = (int) (20.0 * DemucsModel::sampleRate);
        juce::AudioBuffer<float> mix (2, length);
        juce::Random random (3);

        for (int i = 0; i < length; ++i)
        {
            mix.setSample (0, i, 0.4f * std::sin ((float) i * 0.02f) + 0.05f * (random.nextFloat() - 0.5f) + 0.01f);
            mix.setSample (1, i, 0.2f * std::sin ((float) i * 0.05f) + 0.05f * (random.nextFloat() - 0.5f));
        }

        PassThroughModel model;
        DemucsDsp dsp;
        std::vector<std::vector<float>> out (12, std::vector<float> ((size_t) length, 0.0f));
        std::vector<int> nextStart (6, 0);
        auto inOrder = true;
        float lastProgress = -1.0f;

        const auto result = dsp.separate (mix, model, [&] (int s, const float* l, const float* r, int start, int count)
        {
            inOrder = inOrder && start == nextStart[(size_t) s];
            nextStart[(size_t) s] = start + count;
            std::copy_n (l, count, out[(size_t) s * 2].data() + start);
            std::copy_n (r, count, out[(size_t) s * 2 + 1].data() + start);
            return true;
        }, [&] (float p) { lastProgress = p; return true; });

        double err = 0.0, ref = 0.0, other = 0.0;
        const auto edge = (int) (0.2 * DemucsModel::sampleRate);

        for (int c = 0; c < 2; ++c)
            for (int i = edge; i < length - edge; ++i)
            {
                err += juce::square ((double) out[(size_t) c][(size_t) i] - mix.getSample (c, i));
                ref += juce::square ((double) mix.getSample (c, i));
                other += juce::square ((double) out[2 + (size_t) c][(size_t) i]);
            }

        const auto relError = std::sqrt (err / ref);
        check (result.wasOk() && model.calls == 4 && inOrder && nextStart[0] == length && lastProgress == 1.0f,
               "separation runs 4 overlapping segments for 20 s, stems delivered in order (" + juce::String (model.calls) + " segments)");
        check (relError < 0.01, "pass-through network: stem 0 = the mix (error " + juce::String (relError * 100.0, 3) + " %)");
        check (other < 1.0e-3 * ref, "pass-through network: the other stems are silent (except the mean)");

        // Cancel from the progress callback.
        PassThroughModel model2;
        const auto cancelled = dsp.separate (mix, model2, [] (int, const float*, const float*, int, int) { return true; },
                                             [] (float p) { return p < 0.4f; });
        check (cancelled.failed() && model2.calls < 4, "separation can be cancelled");
    }

    // The player plays the stems with their gains (before speed / pitch), the song itself when all are at 1.
    {
        constexpr double rate = 48000.0;
        auto song = std::make_shared<LoadedSong>();
        song->sampleRate = rate;
        song->audio.setSize (2, (int) (6 * rate));

        for (int i = 0; i < song->audio.getNumSamples(); ++i)
        {
            const auto v = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / (float) rate);
            song->audio.setSample (0, i, v);
            song->audio.setSample (1, i, v);
        }

        // Stem "drums" = the song, "bass" = the song too, the rest silent.
        auto stems = std::make_shared<PlayerProcessor::StemSet>();
        stems->numSamples = song->audio.getNumSamples();

        for (int st = 0; st < Stems::count; ++st)
        {
            stems->scale[(size_t) st] = 0.5f / 32767.0f;

            for (int c = 0; c < 2; ++c)
            {
                auto& d = stems->samples[(size_t) (st * 2 + c)];
                d.assign ((size_t) stems->numSamples, 0);

                if (st == Stems::drums || st == Stems::bass)
                    for (int i = 0; i < stems->numSamples; ++i)
                        d[(size_t) i] = (juce::int16) juce::roundToInt (song->audio.getSample (c, i) / 0.5f * 32767.0f);
            }
        }

        PlayerProcessor player;
        player.prepareToPlay (rate, 512);
        player.setSong (song);
        player.setStems (stems, song.get());
        player.play();
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        auto rmsOver = [&] (double seconds)
        {
            double sum = 0.0;
            int n = 0;

            for (int b = 0; b < (int) (seconds * rate / 512); ++b)
            {
                player.processBlock (buffer, midi);

                if (b >= 45)    // after the gains have glided and the stretcher caught up
                    for (int i = 0; i < 512; ++i, ++n)
                        sum += juce::square ((double) buffer.getSample (0, i));
            }

            return std::sqrt (sum / juce::jmax (1, n));
        };

        const auto original = rmsOver (1.5);
        player.stemGains[Stems::bass] = 0.0f;           // drums alone = the song
        const auto oneStem = rmsOver (1.5);
        player.stemGains[Stems::drums] = 0.5f;          // half
        const auto half = rmsOver (1.5);
        player.stemGains[Stems::drums] = 0.0f;          // everything muted
        const auto muted = rmsOver (1.0);

        check (original > 0.3 && std::abs (oneStem / original - 1.0) < 0.05 && std::abs (half / original - 0.5) < 0.05 && muted < 0.01,
               "player mixes stems: as is " + juce::String (original, 3) + ", one stem " + juce::String (oneStem, 3) + ", half "
                   + juce::String (half, 3) + ", all muted " + juce::String (muted, 4));

        player.setSong (song);
        check (! player.hasStems(), "loading a song drops the previous song's stems");
        player.setStems (stems, nullptr);
        check (! player.hasStems(), "stems for another song are ignored");
    }

    // Where stems are kept.
    {
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pv-stem-lib");
        const auto song = root.getChildFile ("Topic/Song A.mp3");
        check (Stems::folderFor (song, root) == root.getChildFile ("Topic/.stems/Song A.mp3"), "stems of a library song sit next to it in .stems");
        const auto outside = Stems::folderFor (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("x/Song A.mp3"), root);
        check (outside.getParentDirectory().getFileName() == "Stems" && outside.getFileName().startsWith ("Song A "),
               "stems of other songs go to the app's Stems folder: " + outside.getFileName());
    }

    // The real network, when this machine has it (developer machines; CI has no model).
    if (StemSeparator::isSupported() && StemSeparator::hasModel())
    {
        juce::String error;
        auto model = StemSeparator::openModel (error);
        check (model != nullptr, "ONNX model opens" + (error.isEmpty() ? juce::String() : ": " + error));

        if (model != nullptr)
        {
            const int length = (int) (8.0 * DemucsModel::sampleRate);
            juce::AudioBuffer<float> mix (2, length);
            juce::Random random (5);

            for (int i = 0; i < length; ++i)        // a bass note + a hi-hat-like noise burst every 0.25 s
            {
                const auto t = (float) i / DemucsModel::sampleRate;
                const auto hat = (i % 11025) < 1500 ? (random.nextFloat() - 0.5f) * 0.4f * (1.0f - (float) (i % 11025) / 1500.0f) : 0.0f;
                const auto v = 0.4f * std::sin (juce::MathConstants<float>::twoPi * 55.0f * t) + hat;
                mix.setSample (0, i, v);
                mix.setSample (1, i, v);
            }

            DemucsDsp dsp;
            std::vector<float> sum ((size_t) length, 0.0f);
            const auto started = juce::Time::getMillisecondCounterHiRes();
            const auto result = dsp.separate (mix, *model, [&] (int, const float* l, const float*, int start, int count)
            {
                for (int k = 0; k < count; ++k)
                    sum[(size_t) (start + k)] += l[k];

                return true;
            }, {});
            const auto seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;

            double err = 0.0, ref = 0.0;

            for (int i = 0; i < length; ++i)
            {
                err += juce::square ((double) sum[(size_t) i] - mix.getSample (0, i));
                ref += juce::square ((double) mix.getSample (0, i));
            }

            const auto snr = 10.0 * std::log10 (ref / juce::jmax (1.0e-12, err));
            check (result.wasOk() && snr > 10.0, "real network: the stems add up to the mix (" + juce::String (snr, 1) + " dB), "
                                                     + juce::String (seconds, 1) + " s for 8 s of audio" + (result.wasOk() ? juce::String() : ": " + result.getErrorMessage()));
        }
    }
    else
    {
        std::cout << "  (real separation network not installed here: skipped)" << std::endl;
    }
}

/** Developer check against the reference Python Demucs: PV_STEMS_DEV="<audio file>|<output folder>" writes
    clip.raw (the 44.1 kHz stereo clip, planar float32) and cpp.raw (6 x 2 x samples) there. */
static void stemsDeveloperRun()
{
    const auto arg = juce::SystemStats::getEnvironmentVariable ("PV_STEMS_DEV", {});

    if (arg.isEmpty())
        return;

    const juce::File input (arg.upToFirstOccurrenceOf ("|", false, false)), outDir (arg.fromFirstOccurrenceOf ("|", false, false));
    outDir.createDirectory();
    juce::AudioBuffer<float> audio;
    double rate = 0.0;

    if (input.hasFileExtension ("m4a") || input.hasFileExtension ("mp3"))
    {
        const auto r = MediaFoundationAudio::decodeFile (input, audio, rate);
        std::cout << "decode: " << (r.wasOk() ? "ok" : r.getErrorMessage()) << std::endl;
    }

    if (audio.getNumSamples() == 0)
        return;

    const auto from = (int) (30.0 * rate), count = juce::jmin ((int) (20.0 * rate), audio.getNumSamples() - from);
    juce::AudioBuffer<float> clip (2, count);

    for (int c = 0; c < 2; ++c)
        clip.copyFrom (c, 0, audio, juce::jmin (c, audio.getNumChannels() - 1), from, count);

    if (rate != 44100.0)
        clip = AacFile::resample (clip, rate, 44100.0);

    const auto n = clip.getNumSamples();
    {
        juce::FileOutputStream out (outDir.getChildFile ("clip.raw"));
        out.setPosition (0);
        out.truncate();

        for (int c = 0; c < 2; ++c)
            out.write (clip.getReadPointer (c), (size_t) n * sizeof (float));
    }

    juce::String error;
    auto model = StemSeparator::openModel (error);

    if (model == nullptr)
    {
        std::cout << "model: " << error << std::endl;
        return;
    }

    std::vector<float> stems ((size_t) 12 * n, 0.0f);
    DemucsDsp dsp;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto result = dsp.separate (clip, *model, [&] (int s, const float* l, const float* r, int start, int cnt)
    {
        std::copy_n (l, cnt, stems.data() + ((size_t) s * 2) * n + start);
        std::copy_n (r, cnt, stems.data() + ((size_t) s * 2 + 1) * n + start);
        return true;
    }, {});
    std::cout << "separate: " << (result.wasOk() ? "ok" : result.getErrorMessage()) << " in "
              << (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0 << " s" << std::endl;

    juce::FileOutputStream out (outDir.getChildFile ("cpp.raw"));
    out.setPosition (0);
    out.truncate();
    out.write (stems.data(), stems.size() * sizeof (float));
}

static void testStudentGroups()
{
    std::cout << "Student groups (one topic to an ensemble)" << std::endl;

    juce::TemporaryFile tempT, tempA, tempM, tempOut;
    const auto rootT = tempT.getFile(), rootA = tempA.getFile(), rootM = tempM.getFile(), out = tempOut.getFile();
    for (auto& f : { rootT, rootA, rootM, out })
        f.createDirectory();

    Library teacher (rootT), anna (rootA), marco (rootM);
    auto* master = teacher.createTopic ("Quartet in G");
    const auto masterId = master->id;
    master->description = "Parts for Tuesday";
    master->folder.getChildFile ("Score.pdf").replaceWithText ("score");
    teacher.saveTopic (*master);
    teacher.rescan();

    // Groups: saved in the library, members matched loosely.
    StudentGroups groups;
    auto& g = groups.add ("Quartetto Tuesday");
    g.members = { "Anna", "Marco" };
    const auto groupId = g.id;
    check (groups.save (rootT), "groups saved");
    groups = StudentGroups::load (rootT);
    check (groups.groups.size() == 1 && groups.groups[0].members.size() == 2 && groups.groups[0].name == "Quartetto Tuesday",
           "groups load back");
    check (groups.groups[0].findMember ("anna rossi") == "Anna" && groups.groups[0].findMember ("Marc").isEmpty(),
           "member from the sender's name");

    // One file for the whole group, with its own id.
    const auto groupFile = out.getChildFile ("group.pvtopic");
    TopicShare::ExportOptions options;
    options.includedFiles = { "Score.pdf" };
    options.sendAsId = juce::Uuid().toDashedString();
    check (TopicShare::write (*teacher.findTopic (masterId), options, groupFile).wasOk(), "group file written");
    groups.sends.push_back ({ options.sendAsId, groupId, masterId, {}, juce::Time::getCurrentTime() });
    groups.save (rootT);
    groups = StudentGroups::load (rootT);
    check (groups.findSend (options.sendAsId) != nullptr && groups.lastSend (masterId, groupId) != nullptr
               && groups.lastSend (masterId, groupId, "Anna") == nullptr, "send remembered");

    // Both members import it and send it back with something of their own.
    auto memberReturn = [&] (Library& lib, const juce::String& name, const juce::File& back)
    {
        juce::String error, id;
        auto package = TopicShare::open (groupFile, error);
        TopicShare::apply (lib, *package, TopicShare::Mode::update, {}, id);
        lib.rescan();
        auto* t = lib.findTopic (id);
        check (t != nullptr && t->id == options.sendAsId, name + ": the group's id");
        t->folder.getChildFile (name + " take.m4a").replaceWithText (name);
        lib.rescan();
        t = lib.findTopic (id);
        TopicShare::ExportOptions o;
        o.senderName = name + " Rossi";
        o.includedFiles = { name + " take.m4a" };
        TopicShare::write (*t, o, back);
    };

    const auto fromAnna = out.getChildFile ("anna.pvtopic"), fromMarco = out.getChildFile ("marco.pvtopic");
    memberReturn (anna, "Anna", fromAnna);
    memberReturn (marco, "Marco", fromMarco);

    // Teacher: each return becomes that member's own copy; the master is left alone.
    auto importReturn = [&] (const juce::File& file, bool expectExisting) -> juce::String
    {
        juce::String error, id;
        auto package = TopicShare::open (file, error);
        const auto* send = groups.findSend (package->topic.id);
        const auto member = send != nullptr ? groups.find (send->groupId)->findMember (package->senderName) : juce::String();
        const TopicShare::ImportAs as { member, groupId };
        const auto p = TopicShare::preview (teacher, *package, TopicShare::Mode::update, {}, as);
        check (send != nullptr && member.isNotEmpty() && p.updatesExisting == expectExisting,
               "return from " + package->senderName + (expectExisting ? ": updates their copy" : ": new copy"));
        TopicShare::apply (teacher, *package, TopicShare::Mode::update, {}, id, as);
        teacher.rescan();
        return id;
    };

    const auto annaCopy = importReturn (fromAnna, false);
    const auto marcoCopy = importReturn (fromMarco, false);
    const auto* a = teacher.findTopic (annaCopy);
    check (a != nullptr && annaCopy != masterId && annaCopy != marcoCopy && a->student == "Anna" && a->group == groupId
               && a->sharedId == options.sendAsId && a->folder.getChildFile ("Anna take.m4a").existsAsFile(),
           "member copy: Student, group, the id the member has");
    check (teacher.findTopic (masterId)->resources.size() == 1, "master untouched by the returns");
    check (importReturn (fromAnna, true) == annaCopy, "second return from Anna updates her copy");
    check (teacher.findTopic (marcoCopy)->matchesTag (Topic::groupFilter (groupId)), "group filter finds member copies");

    const auto status = groups.describeSends (teacher, masterId);
    check (status.contains ("Sent to Quartetto Tuesday") && status.contains ("Back from Anna") && status.contains ("Marco"),
           "group status: " + status);

    // A reply from Anna's copy carries her id; local fields never travel.
    const auto reply = out.getChildFile ("reply.pvtopic");
    TopicShare::write (*teacher.findTopic (annaCopy), {}, reply);
    juce::String error;
    auto package = TopicShare::open (reply, error);
    check (package != nullptr && package->topic.id == options.sendAsId && package->topic.group.isEmpty()
               && package->topic.sharedId.isEmpty(), "reply: Anna's id, no group or sharedId inside");
    juce::ZipFile zip (reply);
    std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*zip.getEntry ("topic.xml")));
    const auto xml = in != nullptr ? in->readEntireStreamAsString() : juce::String();
    check (! xml.contains ("sharedId") && ! xml.contains (groupId) && ! xml.contains ("student="), "reply topic.xml has no local fields");

    juce::String importedId;
    TopicShare::apply (anna, *package, TopicShare::Mode::update, {}, importedId);
    check (importedId == options.sendAsId, "Anna's copy updates on her side");
}

static void testTopicShare()
{
    std::cout << "Topic sharing (.pvtopic)" << std::endl;

    juce::TemporaryFile tempA, tempB, tempOut;
    const auto rootA = tempA.getFile(), rootB = tempB.getFile(), out = tempOut.getFile();
    rootA.createDirectory(); rootB.createDirectory(); out.createDirectory();

    auto writeFile = [] (const juce::File& f, int bytes, int seed)
    {
        juce::MemoryBlock data ((size_t) bytes);
        for (int i = 0; i < bytes; ++i)
            static_cast<char*> (data.getData())[i] = (char) (i * 7 + seed);
        f.replaceWithData (data.getData(), data.getSize());
    };

    juce::String topicId;
    const auto pvtopic = out.getChildFile ("Autumn Leaves.pvtopic");

    // Teacher: a PDF, a song (left out by default), a link, private notes, rotation on.
    Library teacher (rootA);
    {
        auto* t = teacher.createTopic ("Autumn Leaves");
        topicId = t->id;
        writeFile (t->folder.getChildFile ("Lead sheet.pdf"), 3000, 1);
        writeFile (t->folder.getChildFile ("Autumn Leaves.mp3"), 50000, 2);
        teacher.rescan();
        t = teacher.findTopic (topicId);
        t->description = "Bars 1-8, then the solo";
        t->privateNotes = "Student struggles with the bridge";
        t->inRotation = true;
        t->tags = parseTags ("jazz");
        teacher.addLink (*t, "Chet Baker", "https://www.youtube.com/watch?v=abc");
        teacher.saveTopic (*t);

        const auto items = TopicShare::listFiles (*t);
        check (items.size() == 2, "share list: the topic's two files");

        bool pdfIn = false, songOut = false;
        juce::StringArray included;

        for (auto& item : items)
        {
            if (item.fileName == "Lead sheet.pdf") pdfIn = item.include;
            if (item.fileName == "Autumn Leaves.mp3") songOut = ! item.include && item.isSong;
            if (item.include) included.add (item.fileName);
        }

        check (pdfIn && songOut, "default ticks: document in, song out");
        check (TopicShare::sizeHint (12 * 1024 * 1024).contains ("fits in an email")
                   && ! TopicShare::sizeHint (40 * 1024 * 1024).contains ("fits"), "size hint: email limit");

        TopicShare::ExportOptions options;
        options.includedFiles = included;
        check (TopicShare::write (*t, options, pvtopic).wasOk() && pvtopic.existsAsFile(), "write .pvtopic");
    }

    {
        juce::ZipFile zip (pvtopic);
        check (zip.getEntry ("topic.xml") != nullptr && zip.getEntry ("Lead sheet.pdf") != nullptr
                   && zip.getEntry ("Autumn Leaves.mp3") == nullptr, "zip holds topic.xml + chosen files only");

        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*zip.getEntry ("topic.xml")));
        const auto text = in != nullptr ? in->readEntireStreamAsString() : juce::String();
        check (text.contains (topicId) && ! text.contains ("bridge") && ! text.contains ("rotation"),
               "shared topic.xml keeps the id, drops private notes and rotation");
    }

    // Student: has the song already in another topic (same name and size) -> reconnected.
    Library student (rootB);
    {
        auto* other = student.createTopic ("Songs");
        writeFile (other->folder.getChildFile ("Autumn Leaves.mp3"), 50000, 2);
        student.rescan();

        juce::String error;
        auto package = TopicShare::open (pvtopic, error);
        check (package != nullptr && error.isEmpty(), "open .pvtopic " + error);

        if (package == nullptr)
            return;

        check (package->topic.id == topicId && package->topic.privateNotes.isEmpty() && package->isIncluded ("Lead sheet.pdf")
                   && ! package->isIncluded ("Autumn Leaves.mp3"), "package: id, no private notes, which files came");

        const auto p = TopicShare::preview (student, *package, TopicShare::Mode::update, {});
        check (! p.updatesExisting && p.resourceCount == 3 && p.reconnected.contains ("Autumn Leaves.mp3") && p.missing.isEmpty(),
               "preview: new topic, song found in the library by name + size");

        juce::String importedId;
        check (TopicShare::apply (student, *package, TopicShare::Mode::update, {}, importedId).wasOk() && importedId == topicId,
               "import as new topic keeps the id");
        student.rescan();

        auto* t = student.findTopic (topicId);
        check (t != nullptr && t->folder.getChildFile ("Autumn Leaves.mp3").getSize() == 50000
                   && t->folder.getChildFile ("Lead sheet.pdf").getSize() == 3000 && t->description == "Bars 1-8, then the solo"
                   && ! t->inRotation && t->resources.size() == 3, "imported: files, notes, link");

        if (t == nullptr)
            return;

        t->privateNotes = "My own notes";
        t->inRotation = true;
        student.saveTopic (*t);
    }

    // Teacher updates the topic: new PDF, notes changed; re-import updates instead of duplicating.
    {
        auto* t = teacher.findTopic (topicId);
        writeFile (t->folder.getChildFile ("Exercise 2.pdf"), 1200, 3);
        writeFile (t->folder.getChildFile ("Lead sheet.pdf"), 3100, 4);
        teacher.rescan();
        t = teacher.findTopic (topicId);
        t->description = "Now the whole tune";
        teacher.saveTopic (*t);

        TopicShare::ExportOptions options;
        options.includedFiles = { "Lead sheet.pdf", "Exercise 2.pdf" };
        check (TopicShare::write (*t, options, pvtopic).wasOk(), "write the updated topic");

        juce::String error;
        auto package = TopicShare::open (pvtopic, error);

        if (package == nullptr)
            return;

        const auto p = TopicShare::preview (student, *package, TopicShare::Mode::update, {});
        check (p.updatesExisting && p.newResources.size() == 1 && p.updatedFiles.contains ("Lead sheet.pdf") && p.notesChanged,
               "preview of an update: 1 new resource, 1 updated file, notes changed");
        check (p.summary().contains ("Updates your topic"), "update summary text");

        const auto topicsBefore = student.getTopics().size();
        juce::String importedId;
        check (TopicShare::apply (student, *package, TopicShare::Mode::update, {}, importedId).wasOk(), "apply update");
        student.rescan();

        auto* s = student.findTopic (topicId);
        check (student.getTopics().size() == topicsBefore && s != nullptr && s->description == "Now the whole tune"
                   && s->privateNotes == "My own notes" && s->inRotation && s->folder.getChildFile ("Lead sheet.pdf").getSize() == 3100
                   && s->folder.getChildFile ("Exercise 2.pdf").existsAsFile(),
               "update: same topic, receiver's private notes and rotation kept, files replaced/added");

        const auto again = TopicShare::preview (student, *package, TopicShare::Mode::update, {});
        check (! again.changesAnything(), "same file twice: nothing to update");

        check (TopicShare::apply (student, *package, TopicShare::Mode::copy, {}, importedId).wasOk() && importedId != topicId,
               "import as copy: new id");
        student.rescan();
        check (student.getTopics().size() == topicsBefore + 1, "copy is a second topic");
    }

    // A song nobody has: listed as missing; a located file fills it in.
    {
        auto* t = teacher.findTopic (topicId);
        writeFile (t->folder.getChildFile ("Rare song.mp3"), 7000, 5);
        teacher.rescan();
        t = teacher.findTopic (topicId);

        TopicShare::ExportOptions options;
        options.includedFiles = { "Lead sheet.pdf" };
        TopicShare::write (*t, options, pvtopic);

        juce::String error;
        auto package = TopicShare::open (pvtopic, error);

        if (package == nullptr)
            return;

        auto p = TopicShare::preview (student, *package, TopicShare::Mode::update, {});
        check (p.missing.contains ("Rare song.mp3"), "left-out file nobody has: missing");

        const auto located = out.getChildFile ("my copy.mp3");
        writeFile (located, 7000, 5);
        TopicShare::LocatedFiles chosen { { "Rare song.mp3", located } };
        p = TopicShare::preview (student, *package, TopicShare::Mode::update, chosen);
        check (p.missing.isEmpty() && p.reconnected.contains ("Rare song.mp3"), "located file connects it");

        juce::String importedId;
        TopicShare::apply (student, *package, TopicShare::Mode::update, chosen, importedId);
        student.rescan();
        check (student.findTopic (topicId)->folder.getChildFile ("Rare song.mp3").getSize() == 7000, "located file copied in");
    }

    // Marks, sender name, fresh copies, practice summary; the teacher's private fields never travel.
    {
        auto* t = teacher.findTopic (topicId);
        SongMark loop;
        loop.id = "mark-1"; loop.fileName = "Autumn Leaves.mp3"; loop.start = 42.0; loop.end = 58.5; loop.speed = 0.8;
        loop.text = "bar 17: watch the fingering";
        t->setMark (loop);
        SongMark point;
        point.id = "mark-2"; point.fileName = "Autumn Leaves.mp3"; point.start = 90.0; point.text = "solo starts";
        t->setMark (point);
        t->student = "Anna";
        t->lastSent = juce::Time::getCurrentTime();
        teacher.saveTopic (*t);
        teacher.rescan();
        t = teacher.findTopic (topicId);
        check (t->marks.size() == 2 && t->marksFor ("Autumn Leaves.mp3").front().id == "mark-1" && t->student == "Anna",
               "marks and student saved in topic.xml");
        check (loop.describe() == "Loop 0:42-0:59 at 80 %  bar 17: watch the fingering" || loop.describe().startsWith ("Loop 0:42-0:58"),
               "mark description: " + loop.describe());

        TopicShare::ExportOptions options;
        options.includedFiles = { "Lead sheet.pdf" };
        options.senderName = "Mr Rossi";
        check (TopicShare::write (*t, options, pvtopic).wasOk(), "write with marks");

        {
            juce::ZipFile zip (pvtopic);
            std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*zip.getEntry ("topic.xml")));
            const auto text = in->readEntireStreamAsString();
            check (! text.contains ("Anna") && ! text.contains ("lastSent") && text.contains ("Mr Rossi"),
                   "student name and dates never sent; sender name is");
        }

        juce::String error;
        auto package = TopicShare::open (pvtopic, error);

        if (package == nullptr)
            return;

        check (package->senderName == "Mr Rossi" && package->topic.marks.size() == 2, "package: sender and marks");
        const auto p = TopicShare::preview (student, *package, TopicShare::Mode::update, {});
        check (p.newMarks == 2 && p.summary().contains ("2 new markers or loops"), "preview counts new marks");

        juce::String importedId;
        TopicShare::apply (student, *package, TopicShare::Mode::update, {}, importedId);
        student.rescan();
        auto* s = student.findTopic (topicId);
        check (s != nullptr && s->marksFor ("Autumn Leaves.mp3").size() == 2 && s->lastReceived != juce::Time() && s->student.isEmpty(),
               "marks imported, received date set, no student name on the student's side");

        // The student sends back a practice summary.
        PracticeSummary summary;
        summary.made = juce::Time::getCurrentTime();
        summary.totalSeconds = 3 * 3600 + 600;
        summary.sessions = 12;
        summary.weeklySeconds = { 1200, 2400, 3600, 4200 };
        summary.firstBpm = 80; summary.lastBpm = 96; summary.streakDays = 4;
        check (summary.describe().contains ("3 h 10 min in 4 weeks (12 sessions)") && summary.describe().contains ("80 -> 96 bpm"),
               "summary text: " + summary.describe());

        TopicShare::ExportOptions back;
        back.summary = summary;
        back.senderName = "Anna";
        TopicShare::write (*s, back, pvtopic);
        package = TopicShare::open (pvtopic, error);

        if (package == nullptr)
            return;

        check (! package->summary.isEmpty() && package->summary.sessions == 12 && package->summary.weeklySeconds.size() == 4,
               "summary travels to the teacher");
        TopicShare::apply (teacher, *package, TopicShare::Mode::update, {}, importedId, { "Anna B." });
        teacher.rescan();
        t = teacher.findTopic (topicId);
        check (t->studentSummary.sessions == 12 && t->student == "Anna B.", "teacher keeps the summary and the Student box");

        // Re-sharing from the teacher never sends the student's summary on.
        TopicShare::ExportOptions again;
        TopicShare::write (*t, again, pvtopic);
        package = TopicShare::open (pvtopic, error);
        check (package != nullptr && package->summary.isEmpty(), "summary is not passed on");

        // A fresh copy for another student: new id.
        again.asNewTopic = true;
        TopicShare::write (*t, again, pvtopic);
        package = TopicShare::open (pvtopic, error);
        check (package != nullptr && package->topic.id != topicId && package->topic.marks.size() == 2, "fresh copy: new id, marks kept");
    }

    // Assignment: saved, travels, a new imported topic with one goes into the rotation.
    {
        auto* t = teacher.findTopic (topicId);
        t->assignment.goal = "Bars 1-16 clean";
        t->assignment.targetBpm = 96;
        t->assignment.minutesPerDay = 15;
        t->assignment.nextLesson = juce::Time (2026, 10, 20, 12, 0);   // 20 Nov 2026
        teacher.saveTopic (*t);
        teacher.rescan();
        t = teacher.findTopic (topicId);
        check (t->assignment.targetBpm == 96 && t->assignment.minutesPerDay == 15
                   && t->assignment.nextLesson.formatted ("%Y-%m-%d") == "2026-11-20"
                   && t->assignment.describe() == "Bars 1-16 clean" + middleDot() + "target 96 bpm" + middleDot() + "15 min a day" + middleDot() + "next lesson Fri 20 Nov",
               "assignment saved: " + t->assignment.describe());

        TopicShare::ExportOptions options;
        options.asNewTopic = true;
        TopicShare::write (*t, options, pvtopic);
        juce::String error, importedId;
        auto package = TopicShare::open (pvtopic, error);

        if (package == nullptr)
            return;

        check (TopicShare::preview (student, *package, TopicShare::Mode::update, {}).summary().startsWith ("New topic"), "fresh copy imports as new");
        TopicShare::apply (student, *package, TopicShare::Mode::update, {}, importedId);
        student.rescan();
        auto* s = student.findTopic (importedId);
        check (s != nullptr && s->assignment.goal == "Bars 1-16 clean" && s->inRotation, "assignment travels; homework goes into the rotation");
    }

    // Document for people without the app: a web page + the chosen files.
    {
        auto* t = teacher.findTopic (topicId);
        t->description = "Watch bar 17 <slowly> & relax";
        teacher.saveTopic (*t);

        TopicShare::ExportOptions options;
        options.includedFiles = { "Lead sheet.pdf" };
        juce::Array<juce::File> written;
        check (TopicShare::writeDocument (*t, options, out, written).wasOk() && written.size() == 2
                   && written[0].hasFileExtension ("html") && written[1].getFileName() == "Lead sheet.pdf",
               "document: page first, then the chosen file");

        const auto html = written[0].loadFileAsString();
        check (html.contains ("<a href=\"https://www.youtube.com/watch?v=abc\">Chet Baker</a>")
                   && html.contains ("&lt;slowly&gt; &amp; relax") && html.contains ("href=\"Lead%20sheet.pdf\"")
                   && html.contains ("not included") && ! html.contains ("bridge"),
               "document: titled links, escaped notes, attached file linked, left-out files marked, no private notes");
    }

    // Hostile zip: names that climb out of the folder are never extracted.
    {
        const auto evil = out.getChildFile ("evil.pvtopic");
        juce::ZipFile::Builder zip;
        const juce::String xml = "<TOPIC id=\"x\" title=\"Evil\"/>";
        zip.addEntry (std::make_unique<juce::MemoryInputStream> (xml.toRawUTF8(), xml.getNumBytesAsUTF8(), true), 0, "topic.xml", {});
        zip.addEntry (std::make_unique<juce::MemoryInputStream> ("bad", 3, true), 0, "../escaped.txt", {});
        zip.addEntry (std::make_unique<juce::MemoryInputStream> ("bad", 3, true), 0, "sub/inner.txt", {});
        {
            juce::FileOutputStream os (evil);
            zip.writeToStream (os, nullptr);
        }

        juce::String error;
        auto package = TopicShare::open (evil, error);
        check (package != nullptr && ! package->stage.getParentDirectory().getChildFile ("escaped.txt").exists()
                   && ! package->stage.getChildFile ("sub").exists(), "zip-slip names ignored");

        const auto notTopic = out.getChildFile ("plain.pvtopic");
        notTopic.replaceWithText ("hello");
        check (TopicShare::open (notTopic, error) == nullptr && error.isNotEmpty(), "not a topic file: clear error");
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (juce::SystemStats::getEnvironmentVariable ("PV_STEMS_DEV", {}).isNotEmpty())
    {
        stemsDeveloperRun();
        return 0;
    }

    testPlayer();
    testFileLoading();
    testMetronome();
    testChordAnalysis();
    testSlashChords();
    testMeters();
    testFollowSong();
    testLooper();
    testPlayHead();
    testFootswitch();
    testDrums();
    testMetronomeSettings();
    testLatencyCalibration();
    testLoopFiles();
    testTuner();
    testTunings();
    testLibrary();
    testTopicShare();
    testStudentGroups();
    testLinks();
    testPracticeLog();
    testLogCorrections();
    testInstrumentSlot();
    testMidiPracticeTiming();
    testNoGuitarTopics();
    testStartButtonMode();
    testOutsideSongs();
    testPracticePlanner();
    testSessionPlans();
    testTakes();
    testStorage();
    testStems();

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " TEST(S) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
