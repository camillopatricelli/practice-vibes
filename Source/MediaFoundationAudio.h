#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace pv
{

/** Windows only: decodes AAC / M4A files with Media Foundation (part of Windows 10/11).
    JUCE has no AAC decoder on Windows, and its lenient MP3 decoder turns .m4a files into noise.
    On other platforms isAvailable() is false and the functions fail.
*/
namespace MediaFoundationAudio
{
    bool isAvailable();

    /** True for extensions (".m4a", ".aac", ...) that should go through Media Foundation. */
    bool handlesExtension (const juce::String& extension);

    /** Decodes the whole file to stereo float (mono is copied to both channels, extra channels dropped). */
    juce::Result decodeFile (const juce::File& file, juce::AudioBuffer<float>& stereo, double& sampleRate);

    /** Encodes audio as an AAC .m4a file (used by the tests to make a test file). 44.1 or 48 kHz. */
    juce::Result writeAacFile (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate);
}

} // namespace pv
