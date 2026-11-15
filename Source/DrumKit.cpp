#include "DrumKit.h"
#include "DrumKitData.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace pv
{

namespace
{
    int drumForFileStem (const juce::String& stem)
    {
        static const std::pair<const char*, Drum> names[] {
            { "kick", Drum::kick }, { "snare", Drum::snare }, { "hihat", Drum::closedHat }, { "openhat", Drum::openHat },
            { "ride", Drum::ride }, { "crash", Drum::crash }, { "tomhigh", Drum::tomHigh }, { "tomlow", Drum::tomLow } };

        for (const auto& [name, drum] : names)
            if (stem == name)
                return (int) drum;

        return -1;
    }

    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double fromRate, double toRate)
    {
        if (std::abs (fromRate - toRate) < 0.5)
            return in;

        const auto ratio = fromRate / toRate;
        const auto length = (int) std::ceil (in.getNumSamples() / ratio);
        juce::AudioBuffer<float> out (in.getNumChannels(), length);

        for (int ch = 0; ch < in.getNumChannels(); ++ch)
        {
            juce::LagrangeInterpolator interpolator;
            const auto available = in.getNumSamples();
            const auto used = interpolator.process (ratio, in.getReadPointer (ch), out.getWritePointer (ch), length, available, 0);
            juce::ignoreUnused (used);
        }

        return out;
    }
}

DrumSamples loadSampledDrumKit (double sampleRate)
{
    DrumSamples kit;
    kit.sampleRate = sampleRate;
    juce::FlacAudioFormat flac;

    // Files are named <drum>_<layer>_<round robin>.flac (see Tools/DrumKitTool.cpp).
    for (int i = 0; i < DrumKitData::namedResourceListSize; ++i)
    {
        const juce::String fileName (DrumKitData::originalFilenames[i]);
        const auto parts = juce::StringArray::fromTokens (fileName.upToLastOccurrenceOf (".", false, false), "_", {});

        if (parts.size() != 3)
            continue;

        const auto drum = drumForFileStem (parts[0]);
        const auto layer = parts[1].getIntValue() - 1;
        const auto roundRobin = parts[2].getIntValue() - 1;

        if (drum < 0 || layer < 0 || roundRobin < 0)
            continue;

        int size = 0;
        const auto* data = DrumKitData::getNamedResource (DrumKitData::namedResourceList[i], size);

        if (data == nullptr)
            continue;

        std::unique_ptr<juce::AudioFormatReader> reader (
            flac.createReaderFor (new juce::MemoryInputStream (data, (size_t) size, false), true));

        if (reader == nullptr)
            continue;

        juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
        reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);

        auto& layers = kit.hits[(size_t) drum];

        if ((int) layers.size() <= layer)
            layers.resize ((size_t) layer + 1);

        auto& roundRobins = layers[(size_t) layer];

        if ((int) roundRobins.size() <= roundRobin)
            roundRobins.resize ((size_t) roundRobin + 1);

        roundRobins[(size_t) roundRobin] = resample (audio, reader->sampleRate, sampleRate);
    }

    // Drop gaps (a missing file), so every layer has at least one playable hit.
    for (auto& layers : kit.hits)
    {
        for (auto& roundRobins : layers)
            roundRobins.erase (std::remove_if (roundRobins.begin(), roundRobins.end(),
                                               [] (const juce::AudioBuffer<float>& b) { return b.getNumSamples() == 0; }),
                               roundRobins.end());

        layers.erase (std::remove_if (layers.begin(), layers.end(),
                                      [] (const std::vector<juce::AudioBuffer<float>>& r) { return r.empty(); }),
                      layers.end());
    }

    return kit;
}

} // namespace pv
