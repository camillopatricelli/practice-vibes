#pragma once

namespace pv
{

/** Receives a copy of a mono signal on the audio thread (see GainMeterProcessor::tap). */
struct AudioTap
{
    virtual ~AudioTap() = default;

    /** Audio thread: must be lock-free and not allocate. */
    virtual void push (const float* samples, int numSamples, double sampleRate) noexcept = 0;
};

/** Receives a copy of the processed (stereo) guitar on the audio thread (see Looper::afterFxTap). */
struct StereoTap
{
    virtual ~StereoTap() = default;

    /** Audio thread: must be lock-free and not allocate. */
    virtual void captureGuitar (const float* left, const float* right, int numSamples) noexcept = 0;
};

} // namespace pv
