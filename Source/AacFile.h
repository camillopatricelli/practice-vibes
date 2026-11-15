#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace pv
{

/** Writes AAC .m4a files with the system's own encoder: Media Foundation on Windows, AudioToolbox on Apple
    platforms. Used for recorded takes (small files) and exported mixes (to share). */
namespace AacFile
{
    bool isAvailable();

    /** Encodes mono or stereo audio. Any sample rate: the encoders take 44.1 / 48 kHz, so others are resampled to
        48 kHz first. kbps: 96, 128, 160 or 192. Samples beyond +-1 are clipped. */
    juce::Result write (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate, int kbps);

    /** Simple (Lagrange) resampling, good enough before encoding at 128-192 kbit/s. */
    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>&, double fromRate, double toRate);
}

} // namespace pv
