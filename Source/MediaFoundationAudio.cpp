#include "MediaFoundationAudio.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #include <mferror.h>
 #include <wrl/client.h>

 #pragma comment (lib, "mfplat.lib")
 #pragma comment (lib, "mfreadwrite.lib")
 #pragma comment (lib, "mfuuid.lib")
 #pragma comment (lib, "ole32.lib")
#endif

namespace pv::MediaFoundationAudio
{

bool handlesExtension (const juce::String& extension)
{
    return isAvailable()
        && juce::StringArray { ".m4a", ".aac", ".mp4", ".m4b", ".3gp" }.contains (extension.toLowerCase());
}

#if JUCE_WINDOWS

namespace
{
    using Microsoft::WRL::ComPtr;

    /** COM + Media Foundation for the calling thread (the song loader runs on a pool thread). */
    struct Session
    {
        Session()
        {
            const auto hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
            ownsCom = SUCCEEDED (hr);   // RPC_E_CHANGED_MODE: COM already set up on this thread, fine
            started = SUCCEEDED (MFStartup (MF_VERSION, MFSTARTUP_LITE));
        }

        ~Session()
        {
            if (started)
                MFShutdown();

            if (ownsCom)
                CoUninitialize();
        }

        bool ownsCom = false, started = false;
    };

    juce::String hrText (HRESULT hr)
    {
        return "error 0x" + juce::String::toHexString ((juce::uint32) hr);
    }
}

bool isAvailable()     { return true; }

juce::Result decodeFile (const juce::File& file, juce::AudioBuffer<float>& stereo, double& sampleRate)
{
    Session session;

    if (! session.started)
        return juce::Result::fail ("Media Foundation is not available on this PC");

    ComPtr<IMFSourceReader> reader;
    auto hr = MFCreateSourceReaderFromURL (file.getFullPathName().toWideCharPointer(), nullptr, &reader);

    if (hr == MF_E_UNSUPPORTED_BYTESTREAM_TYPE || hr == MF_E_INVALID_FILE_FORMAT || hr == MF_E_UNSUPPORTED_FORMAT)
        return juce::Result::fail ("the file is damaged or not really in this format");

    if (FAILED (hr))
        return juce::Result::fail ("can't open the file (" + hrText (hr) + ")");

    reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
    hr = reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    if (FAILED (hr))
        return juce::Result::fail ("the file has no audio");

    ComPtr<IMFMediaType> wanted;
    MFCreateMediaType (&wanted);
    wanted->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    wanted->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_Float);
    hr = reader->SetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, wanted.Get());

    if (FAILED (hr))
        return juce::Result::fail ("no decoder for this audio (" + hrText (hr) + ")");

    UINT32 channels = 0, rate = 0;

    auto readFormat = [&]
    {
        ComPtr<IMFMediaType> actual;

        if (FAILED (reader->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual)))
            return false;

        GUID subtype {};
        actual->GetGUID (MF_MT_SUBTYPE, &subtype);
        channels = MFGetAttributeUINT32 (actual.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
        rate = MFGetAttributeUINT32 (actual.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
        return subtype == MFAudioFormat_Float && channels > 0 && rate > 0;
    };

    if (! readFormat())
        return juce::Result::fail ("unexpected decoder output format");

    // Pre-size from the duration (100 ns units) to avoid growing a long song many times.
    juce::int64 expected = 0;
    PROPVARIANT duration;
    PropVariantInit (&duration);

    if (SUCCEEDED (reader->GetPresentationAttribute ((DWORD) MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration)))
        expected = (juce::int64) ((double) duration.uhVal.QuadPart * rate / 1.0e7);

    PropVariantClear (&duration);

    constexpr juce::int64 maxFrames = std::numeric_limits<int>::max();
    stereo.setSize (2, (int) juce::jlimit<juce::int64> ((juce::int64) rate, maxFrames, expected + rate), false, true, false);
    juce::int64 written = 0;

    for (;;)
    {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);

        if (FAILED (hr))
            return juce::Result::fail ("decoding failed (" + hrText (hr) + ")");

        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0 && ! readFormat())
            return juce::Result::fail ("unexpected decoder output format");

        if (sample != nullptr)
        {
            ComPtr<IMFMediaBuffer> buffer;

            if (FAILED (sample->ConvertToContiguousBuffer (&buffer)))
                return juce::Result::fail ("decoding failed");

            BYTE* bytes = nullptr;
            DWORD length = 0;

            if (SUCCEEDED (buffer->Lock (&bytes, nullptr, &length)))
            {
                const auto* data = reinterpret_cast<const float*> (bytes);
                const auto frames = juce::jmin<juce::int64> ((juce::int64) (length / (sizeof (float) * channels)),
                                                             maxFrames - written);

                if (written + frames > stereo.getNumSamples())
                {
                    const auto grown = juce::jmin (maxFrames, juce::jmax (written + frames, (juce::int64) stereo.getNumSamples() * 3 / 2));
                    stereo.setSize (2, (int) grown, true, true, false);
                }

                auto* left = stereo.getWritePointer (0, (int) written);
                auto* right = stereo.getWritePointer (1, (int) written);

                for (juce::int64 i = 0; i < frames; ++i)
                {
                    left[i] = data[i * channels];
                    right[i] = channels > 1 ? data[i * channels + 1] : left[i];
                }

                written += frames;
                buffer->Unlock();
            }
        }

        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0 || written >= maxFrames)
            break;
    }

    if (written <= 0)
        return juce::Result::fail ("the file contains no audio");

    stereo.setSize (2, (int) written, true, false, true);
    sampleRate = (double) rate;
    return juce::Result::ok();
}

juce::Result writeAacFile (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate)
{
    Session session;

    if (! session.started)
        return juce::Result::fail ("Media Foundation is not available");

    const auto channels = (UINT32) juce::jlimit (1, 2, audio.getNumChannels());
    const auto rate = (UINT32) sampleRate;

    file.deleteFile();
    ComPtr<IMFSinkWriter> writer;
    auto hr = MFCreateSinkWriterFromURL (file.getFullPathName().toWideCharPointer(), nullptr, nullptr, &writer);

    if (FAILED (hr))
        return juce::Result::fail ("can't create the file (" + hrText (hr) + ")");

    ComPtr<IMFMediaType> outType, inType;
    MFCreateMediaType (&outType);
    outType->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    outType->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_AAC);
    outType->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    outType->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    outType->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, channels);
    outType->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);   // 128 kbit/s

    DWORD stream = 0;
    hr = writer->AddStream (outType.Get(), &stream);

    if (FAILED (hr))
        return juce::Result::fail ("no AAC encoder (" + hrText (hr) + ")");

    MFCreateMediaType (&inType);
    inType->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    inType->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
    inType->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    inType->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    inType->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, channels);
    inType->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, 2 * channels);
    inType->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 2 * channels * rate);

    if (FAILED (hr = writer->SetInputMediaType (stream, inType.Get(), nullptr)) || FAILED (hr = writer->BeginWriting()))
        return juce::Result::fail ("can't start the AAC encoder (" + hrText (hr) + ")");

    constexpr int chunk = 1024;

    for (int start = 0; start < audio.getNumSamples(); start += chunk)
    {
        const auto frames = juce::jmin (chunk, audio.getNumSamples() - start);
        const auto bytes = (DWORD) (frames * 2 * (int) channels);

        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFSample> sample;
        MFCreateMemoryBuffer (bytes, &buffer);
        MFCreateSample (&sample);

        BYTE* data = nullptr;
        buffer->Lock (&data, nullptr, nullptr);
        auto* pcm = reinterpret_cast<int16_t*> (data);

        for (int i = 0; i < frames; ++i)
            for (UINT32 c = 0; c < channels; ++c)
                pcm[i * (int) channels + (int) c] = (int16_t) juce::roundToInt (32767.0f * juce::jlimit (-1.0f, 1.0f, audio.getSample ((int) c, start + i)));

        buffer->Unlock();
        buffer->SetCurrentLength (bytes);
        sample->AddBuffer (buffer.Get());
        sample->SetSampleTime ((LONGLONG) (start * 1.0e7 / rate));
        sample->SetSampleDuration ((LONGLONG) (frames * 1.0e7 / rate));

        if (FAILED (hr = writer->WriteSample (stream, sample.Get())))
            return juce::Result::fail ("encoding failed (" + hrText (hr) + ")");
    }

    if (FAILED (hr = writer->Finalize()))
        return juce::Result::fail ("can't finish the file (" + hrText (hr) + ")");

    return juce::Result::ok();
}

#else

bool isAvailable()     { return false; }

juce::Result decodeFile (const juce::File&, juce::AudioBuffer<float>&, double&)
{
    return juce::Result::fail ("not supported on this platform");
}

juce::Result writeAacFile (const juce::File&, const juce::AudioBuffer<float>&, double)
{
    return juce::Result::fail ("not supported on this platform");
}

#endif

} // namespace pv::MediaFoundationAudio
