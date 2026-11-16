#if defined (__APPLE__)
 #include <AudioToolbox/AudioToolbox.h>     // before JUCE, so Apple's names don't clash with JUCE's
#endif

#include "AacFile.h"
#include "MediaFoundationAudio.h"

namespace pv::AacFile
{

juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double fromRate, double toRate)
{
    const auto ratio = fromRate / toRate;
    const auto outLength = (int) std::floor (in.getNumSamples() / ratio);
    juce::AudioBuffer<float> out (in.getNumChannels(), juce::jmax (0, outLength));

    for (int ch = 0; ch < in.getNumChannels(); ++ch)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio, in.getReadPointer (ch), out.getWritePointer (ch), out.getNumSamples(),
                              in.getNumSamples(), 0);
    }

    return out;
}

#if JUCE_WINDOWS

bool isAvailable()      { return MediaFoundationAudio::isAvailable(); }

static juce::Result writeNative (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate, int kbps)
{
    return MediaFoundationAudio::writeAacFile (file, audio, rate, kbps);
}

#elif JUCE_MAC || JUCE_IOS

bool isAvailable()      { return true; }

static juce::Result writeNative (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate, int kbps)
{
    const auto channels = (UInt32) juce::jlimit (1, 2, audio.getNumChannels());

    AudioStreamBasicDescription encoded {};
    encoded.mSampleRate = rate;
    encoded.mFormatID = kAudioFormatMPEG4AAC;
    encoded.mChannelsPerFrame = channels;

    file.deleteFile();
    const auto path = file.getFullPathName();
    auto url = CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*) path.toRawUTF8(),
                                                        (CFIndex) path.getNumBytesAsUTF8(), false);

    if (url == nullptr)
        return juce::Result::fail ("bad file name");

    ExtAudioFileRef out = nullptr;
    auto status = ExtAudioFileCreateWithURL (url, kAudioFileM4AType, &encoded, nullptr, kAudioFileFlags_EraseFile, &out);
    CFRelease (url);

    if (status != noErr || out == nullptr)
        return juce::Result::fail ("can't create the file (error " + juce::String ((int) status) + ")");

    // The software encoder also works while the app is in the background.
    // = kAppleSoftwareAudioCodecManufacturer ('appl'), spelled out: macOS's AudioToolbox.h doesn't declare it.
    UInt32 codec = ((UInt32) 'a' << 24) | ((UInt32) 'p' << 16) | ((UInt32) 'p' << 8) | (UInt32) 'l';
    ExtAudioFileSetProperty (out, kExtAudioFileProperty_CodecManufacturer, sizeof (codec), &codec);

    AudioStreamBasicDescription client {};
    client.mSampleRate = rate;
    client.mFormatID = kAudioFormatLinearPCM;
    client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
    client.mBitsPerChannel = 32;
    client.mChannelsPerFrame = channels;
    client.mFramesPerPacket = 1;
    client.mBytesPerFrame = 4 * channels;
    client.mBytesPerPacket = 4 * channels;
    status = ExtAudioFileSetProperty (out, kExtAudioFileProperty_ClientDataFormat, sizeof (client), &client);

    if (status != noErr)
    {
        ExtAudioFileDispose (out);
        return juce::Result::fail ("the AAC encoder refused the audio (error " + juce::String ((int) status) + ")");
    }

    AudioConverterRef converter = nullptr;
    UInt32 size = sizeof (converter);

    if (ExtAudioFileGetProperty (out, kExtAudioFileProperty_AudioConverter, &size, &converter) == noErr && converter != nullptr)
    {
        UInt32 bitRate = (UInt32) kbps * 1000;
        AudioConverterSetProperty (converter, kAudioConverterEncodeBitRate, sizeof (bitRate), &bitRate);
        CFArrayRef config = nullptr;
        ExtAudioFileSetProperty (out, kExtAudioFileProperty_ConverterConfig, sizeof (config), &config);
    }

    constexpr int chunk = 4096;
    std::vector<float> interleaved ((size_t) (chunk * (int) channels));

    for (int start = 0; start < audio.getNumSamples() && status == noErr; start += chunk)
    {
        const auto frames = juce::jmin (chunk, audio.getNumSamples() - start);

        for (int i = 0; i < frames; ++i)
            for (UInt32 c = 0; c < channels; ++c)
                interleaved[(size_t) (i * (int) channels + (int) c)] = juce::jlimit (-1.0f, 1.0f, audio.getSample ((int) c, start + i));

        AudioBufferList list;
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = channels;
        list.mBuffers[0].mDataByteSize = (UInt32) (frames * 4 * (int) channels);
        list.mBuffers[0].mData = interleaved.data();
        status = ExtAudioFileWrite (out, (UInt32) frames, &list);
    }

    const auto closed = ExtAudioFileDispose (out);

    if (status != noErr || closed != noErr)
        return juce::Result::fail ("encoding failed (error " + juce::String ((int) (status != noErr ? status : closed)) + ")");

    return juce::Result::ok();
}

#else

bool isAvailable()      { return false; }

static juce::Result writeNative (const juce::File&, const juce::AudioBuffer<float>&, double, int)
{
    return juce::Result::fail ("no AAC encoder on this platform");
}

#endif

juce::Result write (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate, int kbps)
{
    if (audio.getNumSamples() <= 0 || audio.getNumChannels() <= 0)
        return juce::Result::fail ("no audio");

    if (sampleRate == 44100.0 || sampleRate == 48000.0)
        return writeNative (file, audio, sampleRate, kbps);

    return writeNative (file, resample (audio, sampleRate, 48000.0), 48000.0, kbps);
}

} // namespace pv::AacFile
