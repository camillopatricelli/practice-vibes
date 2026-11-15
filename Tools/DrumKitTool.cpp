// Developer tool: builds the app's small sampled drum kit from the Big Rusty Drums multisample library
// (Karoryfer Samples, CC0 1.0: https://github.com/sfzinstruments/karoryfer.big-rusty-drums).
// For each drum it mixes the close and overhead microphones, trims the start, shortens the tail with a fade,
// normalises the drum and writes 16-bit stereo FLAC files named <drum>_<layer>_<roundrobin>.flac.
// It also renders demo.wav, a short groove, so the kit can be judged by ear.
//
// Usage: DrumKitTool <Big Rusty Drums folder (the one that holds Samples/)> <output folder>

#include "../Source/DrumKit.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <iostream>

namespace
{
    struct Mic
    {
        const char* folder;
        float gain;
        float pan;   // for mono microphones: -1 left .. +1 right
    };

    struct Drum
    {
        pv::Drum id;
        const char* name;
        const char* source;   // folder under Samples/
        const char* prefix;   // file name up to the velocity layer number
        std::vector<Mic> mics;
        std::vector<int> layers;   // velocity layers of the source, soft to loud
        int roundRobins;
        double lengthSeconds;
        double fadeSeconds;
    };

    const std::vector<Drum> drums {
        { pv::Drum::kick, "kick", "kick_24/kick",     "k_vl",         { { "kick", 1.0f, 0.0f }, { "oh", 0.5f, 0.0f } },
          { 4, 8, 11, 14 }, 2, 0.9, 0.3 },
        { pv::Drum::snare, "snare", "snare_14/center",  "sn_center_vl", { { "top", 1.0f, 0.0f }, { "btm", 0.35f, 0.0f }, { "oh", 0.6f, 0.0f } },
          { 3, 6, 8, 10 }, 2, 1.0, 0.4 },
        { pv::Drum::closedHat, "hihat", "hihat_14/tc",      "ht_tc_vl",     { { "cl", 0.8f, 0.3f }, { "oh", 0.8f, 0.0f } },
          { 2, 4, 6, 8 }, 2, 0.6, 0.3 },
        { pv::Drum::openHat, "openhat", "hihat_14/open",    "ht_open_vl",   { { "cl", 0.8f, 0.3f }, { "oh", 0.8f, 0.0f } },
          { 2, 4, 6 }, 1, 1.8, 0.8 },
        { pv::Drum::ride, "ride", "ride_22/rd",       "rd_vl",        { { "cl", 0.7f, -0.35f }, { "oh", 0.9f, 0.0f } },
          { 3, 6, 8, 10 }, 1, 2.6, 1.6 },
        { pv::Drum::crash, "crash", "crash_17/cr",      "cr_vl",        { { "cl", 0.7f, 0.4f }, { "oh", 0.9f, 0.0f } },
          { 2, 4, 5 }, 1, 3.2, 2.0 },
        { pv::Drum::tomHigh, "tomhigh", "tom_14/center",    "t14_vl",       { { "cl", 1.0f, 0.2f }, { "oh", 0.6f, 0.0f } },
          { 2, 4, 6 }, 2, 1.4, 0.6 },
        { pv::Drum::tomLow, "tomlow", "tom_18/center",    "t18_vl",       { { "cl", 1.0f, -0.25f }, { "oh", 0.6f, 0.0f } },
          { 3, 6, 8 }, 2, 1.8, 0.8 },
    };

    constexpr double kitRate = 44100.0;

    juce::AudioBuffer<float> readFile (juce::AudioFormatManager& formats, const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr)
        {
            std::cout << "Cannot read " << file.getFullPathName() << std::endl;
            return {};
        }

        if (reader->sampleRate != kitRate)
            std::cout << "Unexpected sample rate in " << file.getFileName() << std::endl;

        juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
        return audio;
    }

    // Mixes the microphones of one hit into a stereo buffer.
    juce::AudioBuffer<float> mixHit (juce::AudioFormatManager& formats, const juce::File& samples,
                                     const Drum& drum, int layer, int roundRobin)
    {
        juce::AudioBuffer<float> mix (2, 0);
        const auto fileName = juce::String (drum.prefix) + juce::String (layer) + "_rr" + juce::String (roundRobin) + ".flac";

        for (const auto& mic : drum.mics)
        {
            const auto audio = readFile (formats, samples.getChildFile (drum.source).getChildFile (mic.folder).getChildFile (fileName));

            if (audio.getNumSamples() == 0)
                continue;

            if (audio.getNumSamples() > mix.getNumSamples())
                mix.setSize (2, audio.getNumSamples(), true, true);

            if (audio.getNumChannels() >= 2)
            {
                mix.addFrom (0, 0, audio, 0, 0, audio.getNumSamples(), mic.gain);
                mix.addFrom (1, 0, audio, 1, 0, audio.getNumSamples(), mic.gain);
            }
            else
            {
                const auto angle = (mic.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                mix.addFrom (0, 0, audio, 0, 0, audio.getNumSamples(), mic.gain * std::cos (angle) * juce::MathConstants<float>::sqrt2);
                mix.addFrom (1, 0, audio, 0, 0, audio.getNumSamples(), mic.gain * std::sin (angle) * juce::MathConstants<float>::sqrt2);
            }
        }

        return mix;
    }

    // Starts the hit a millisecond before it gets audible and shortens the tail with a cosine fade.
    juce::AudioBuffer<float> trimHit (const juce::AudioBuffer<float>& hit, const Drum& drum)
    {
        const auto peak = hit.getMagnitude (0, hit.getNumSamples());
        int start = 0;

        while (start < hit.getNumSamples()
               && std::abs (hit.getSample (0, start)) < peak * 0.02f
               && std::abs (hit.getSample (1, start)) < peak * 0.02f)
            ++start;

        start = juce::jmax (0, start - (int) (0.001 * kitRate));

        const auto length = juce::jmin (hit.getNumSamples() - start, (int) (drum.lengthSeconds * kitRate));
        const auto fadeLength = juce::jmin (length, (int) (drum.fadeSeconds * kitRate));

        juce::AudioBuffer<float> out (2, length);

        for (int ch = 0; ch < 2; ++ch)
        {
            out.copyFrom (ch, 0, hit, ch, start, length);

            for (int i = 0; i < fadeLength; ++i)
            {
                const auto t = (float) i / (float) fadeLength;
                out.setSample (ch, length - fadeLength + i,
                               out.getSample (ch, length - fadeLength + i) * 0.5f * (1.0f + std::cos (t * juce::MathConstants<float>::pi)));
            }
        }

        return out;
    }

    bool writeFlac (const juce::File& file, const juce::AudioBuffer<float>& audio)
    {
        file.deleteFile();
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());

        if (stream == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer (
            flac.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                              .withSampleRate (kitRate)
                                              .withNumChannels (2)
                                              .withBitsPerSample (16)));

        return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    // A two-bar groove plus a fill, at 96 BPM in 16th notes: the quickest way to judge the kit.
    void renderDemo (const pv::DrumSamples& kit, const juce::File& file)
    {
        const double bpm = 96.0;
        const auto step = (int) (kitRate * 60.0 / bpm / 4.0);
        const int steps = 16 * 4;

        juce::AudioBuffer<float> out (2, step * steps + (int) (3.5 * kitRate));
        out.clear();
        int roundRobin = 0;

        auto hit = [&] (pv::Drum drum, int stepIndex, int layer)
        {
            const auto& layers = kit.hits[(size_t) drum];
            const auto& rrs = layers[(size_t) juce::jlimit (0, (int) layers.size() - 1, layer)];
            const auto& sample = rrs[(size_t) (roundRobin++ % (int) rrs.size())];
            const auto offset = stepIndex * step;
            const auto length = juce::jmin (sample.getNumSamples(), out.getNumSamples() - offset);

            for (int ch = 0; ch < 2; ++ch)
                out.addFrom (ch, offset, sample, ch, 0, length, 0.5f);
        };

        for (int bar = 0; bar < 4; ++bar)
        {
            const auto b = bar * 16;
            const bool ride = bar == 2;

            if (bar < 3)
            {
                for (int s = 0; s < 16; s += 2)
                    hit (ride ? pv::Drum::ride : (s == 14 && bar == 1 ? pv::Drum::openHat : pv::Drum::closedHat), b + s, s % 4 == 0 ? 2 : 1);

                hit (pv::Drum::kick, b, 3);
                hit (pv::Drum::kick, b + 7, 1);
                hit (pv::Drum::kick, b + 8, 2);
                hit (pv::Drum::snare, b + 4, 2);
                hit (pv::Drum::snare, b + 12, 3);
                hit (pv::Drum::snare, b + 15, 0);
            }
            else
            {
                hit (pv::Drum::kick, b, 3);
                hit (pv::Drum::closedHat, b, 2);
                hit (pv::Drum::snare, b + 4, 3);
                hit (pv::Drum::snare, b + 6, 1);
                hit (pv::Drum::snare, b + 7, 1);
                for (int s = 8; s < 12; ++s)
                    hit (pv::Drum::tomHigh, b + s, s == 8 ? 2 : 1);
                for (int s = 12; s < 16; ++s)
                    hit (pv::Drum::tomLow, b + s, s == 12 ? 2 : 1);
            }

            if (bar == 0)
                hit (pv::Drum::crash, b, 2);
        }

        hit (pv::Drum::crash, steps, 2);
        hit (pv::Drum::kick, steps, 3);

        out.applyGain (0.89f / juce::jmax (0.01f, out.getMagnitude (0, out.getNumSamples())));

        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                             .withSampleRate (kitRate)
                                             .withNumChannels (2)
                                             .withBitsPerSample (16)));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    }
}

int main (int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout << "Usage: DrumKitTool <Big Rusty Drums folder> <output folder>" << std::endl;
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI juce;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const juce::File samples = juce::File (juce::String (argv[1])).getChildFile ("Samples");
    const juce::File output { juce::String (argv[2]) };
    output.createDirectory();

    pv::DrumSamples kit;
    int64_t totalBytes = 0;

    for (const auto& drum : drums)
    {
        auto& layers = kit.hits[(size_t) drum.id];

        for (auto layer : drum.layers)
        {
            layers.emplace_back();

            for (int rr = 1; rr <= drum.roundRobins; ++rr)
                layers.back().push_back (trimHit (mixHit (formats, samples, drum, layer, rr), drum));
        }

        // Normalise the whole drum (not each layer) so the loudest hit peaks at -1 dB and soft layers stay soft.
        float peak = 0.0f;
        for (auto& rrs : layers)
            for (auto& hit : rrs)
                peak = juce::jmax (peak, hit.getMagnitude (0, hit.getNumSamples()));

        for (size_t l = 0; l < layers.size(); ++l)
        {
            for (size_t r = 0; r < layers[l].size(); ++r)
            {
                auto& hit = layers[l][r];
                hit.applyGain (0.89f / juce::jmax (0.0001f, peak));

                const auto file = output.getChildFile (juce::String (drum.name) + "_" + juce::String ((int) l + 1)
                                                       + "_" + juce::String ((int) r + 1) + ".flac");
                if (! writeFlac (file, hit))
                    std::cout << "Cannot write " << file.getFullPathName() << std::endl;

                totalBytes += file.getSize();
            }
        }

        std::cout << drum.name << ": " << layers.size() << " layers x " << drum.roundRobins << std::endl;
    }

    renderDemo (kit, output.getChildFile ("demo-acoustic.wav"));
    renderDemo (pv::synthesiseDrumKit (kitRate), output.getChildFile ("demo-electronic.wav"));
    std::cout << "Kit size " << juce::File::descriptionOfSizeInBytes (totalBytes) << std::endl;
    return 0;
}
