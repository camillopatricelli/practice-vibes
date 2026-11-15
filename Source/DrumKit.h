#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

namespace pv
{
    // The eight sounds of the drum sequencer, top to bottom as the pattern editor shows them.
    enum class Drum : int { crash, ride, openHat, closedHat, tomHigh, tomLow, snare, kick };
    constexpr int numDrums = 8;

    const char* drumName (Drum drum);

    // A kit as plain audio: hits[drum][velocity layer, soft to loud][round robin], stereo, at sampleRate.
    // The sequencer plays any kit the same way; the synthesised kit just computes its hits instead of decoding them.
    struct DrumSamples
    {
        double sampleRate = 44100.0;
        std::array<std::vector<std::vector<juce::AudioBuffer<float>>>, numDrums> hits;

        bool isEmpty() const;
    };

    enum class DrumKitType : int { acoustic, electronic };

    // Synthesised kit (DrumSynth.cpp): no data files, computed in a few milliseconds.
    DrumSamples synthesiseDrumKit (double sampleRate);

    // Sampled kit, made by Tools/DrumKitTool from the Big Rusty Drums library (Karoryfer Samples, CC0).
    // Decoded from the app's binary data and resampled to sampleRate.
    DrumSamples loadSampledDrumKit (double sampleRate);

    inline DrumSamples loadDrumKit (DrumKitType type, double sampleRate)
    {
        return type == DrumKitType::acoustic ? loadSampledDrumKit (sampleRate) : synthesiseDrumKit (sampleRate);
    }
}
