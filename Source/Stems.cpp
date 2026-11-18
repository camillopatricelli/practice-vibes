#include "Stems.h"
#include "AacFile.h"
#include "AppPaths.h"
#include "Localisation.h"

#include <juce_dsp/juce_dsp.h>

namespace pv
{

//==============================================================================
namespace Stems
{
    juce::String fileStem (int index)
    {
        static const char* const names[] { "drums", "bass", "other", "vocals", "guitar", "piano" };
        return juce::isPositiveAndBelow (index, count) ? names[index] : "";
    }

    juce::String displayName (int index)
    {
        static const char* const names[] { i18n::key ("Drums"), i18n::key ("Bass"), i18n::key ("Other"),
                                           i18n::key ("Vocals"), i18n::key ("Guitar"), i18n::key ("Piano") };
        return juce::isPositiveAndBelow (index, count) ? tr (names[index]) : juce::String();
    }

    juce::File folderFor (const juce::File& song, const juce::File& libraryRoot)
    {
        if (song == juce::File())
            return {};

        if (libraryRoot != juce::File() && song.isAChildOf (libraryRoot))
            return song.getParentDirectory().getChildFile (folderName).getChildFile (song.getFileName());

        // Songs outside the library: the app's own folder, one subfolder per song path.
        const auto key = song.getFileNameWithoutExtension().retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_")
                             .substring (0, 40).trim()
                         + " " + juce::String::toHexString (song.getFullPathName().toLowerCase().hashCode64());
        return getAppDataFolder().getChildFile ("Stems").getChildFile (key);
    }

    juce::File stemFile (const juce::File& folder, int index)   { return folder.getChildFile (fileStem (index) + ".m4a"); }
    juce::File infoFile (const juce::File& folder)              { return folder.getChildFile ("stems.xml"); }

    bool isComplete (const juce::File& folder)
    {
        if (! infoFile (folder).existsAsFile())
            return false;

        for (int i = 0; i < count; ++i)
            if (! stemFile (folder, i).existsAsFile())
                return false;

        return true;
    }
}

//==============================================================================
struct DemucsDsp::Impl
{
    static constexpr int N = DemucsModel::fftSize, H = DemucsModel::hop, S = DemucsModel::segment;
    static constexpr int le = DemucsModel::frames;                  // ceil (S / H)
    static constexpr int pad = H / 2 * 3;                           // demucs' own padding (1536)
    static constexpr int padRight = pad + le * H - S;               // 1620
    static constexpr int padded = S + pad + padRight;               // 347136 = 339 hops
    static constexpr int allFrames = padded / H + 1;                // 340 (torch.stft, center = True)
    static constexpr int centre = N / 2;                            // torch.stft's reflect padding

    juce::dsp::FFT fft { 12 };
    std::vector<float> window = std::vector<float> ((size_t) N);
    std::vector<float> envelope = std::vector<float> ((size_t) ((allFrames - 1) * H + N), 0.0f);   // sum of window^2
    std::vector<float> work = std::vector<float> ((size_t) (2 * N));
    std::vector<float> ola = std::vector<float> ((size_t) ((allFrames - 1) * H + N));

    Impl()
    {
        for (int n = 0; n < N; ++n)     // periodic Hann
            window[(size_t) n] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) N));

        for (int t = 0; t < allFrames; ++t)
            for (int n = 0; n < N; ++n)
                envelope[(size_t) (t * H + n)] += window[(size_t) n] * window[(size_t) n];
    }

    static int reflect (int i, int length) noexcept
    {
        if (length <= 1)
            return 0;

        while (i < 0 || i >= length)
            i = i < 0 ? -i : 2 * (length - 1) - i;

        return i;
    }

    /** Sample k of the doubly padded signal torch.stft frames (demucs' reflect pad, then torch's own). */
    static float sampleAt (const float* x, int k) noexcept
    {
        const auto j = reflect (k - centre, padded);
        return x[reflect (j - pad, S)];
    }

    void spectrogram (const float* const* channels, float* spec)
    {
        const auto norm = 1.0f / std::sqrt ((float) N);

        for (int c = 0; c < 2; ++c)
        {
            const auto* x = channels[c];
            auto* re = spec + (size_t) (2 * c) * DemucsModel::bins * le;
            auto* im = spec + (size_t) (2 * c + 1) * DemucsModel::bins * le;

            for (int f = 0; f < le; ++f)
            {
                const auto start = (f + 2) * H;     // demucs keeps frames 2 .. 2 + le

                for (int n = 0; n < N; ++n)
                    work[(size_t) n] = sampleAt (x, start + n) * window[(size_t) n];

                fft.performRealOnlyForwardTransform (work.data(), true);

                for (int b = 0; b < DemucsModel::bins; ++b)
                {
                    re[(size_t) b * le + (size_t) f] = work[(size_t) (2 * b)] * norm;
                    im[(size_t) b * le + (size_t) f] = work[(size_t) (2 * b + 1)] * norm;
                }
            }
        }
    }

    void addInverse (const float* spec, float* const* out)
    {
        const auto norm = std::sqrt ((float) N);

        for (int c = 0; c < 2; ++c)
        {
            const auto* re = spec + (size_t) (2 * c) * DemucsModel::bins * le;
            const auto* im = spec + (size_t) (2 * c + 1) * DemucsModel::bins * le;
            std::fill (ola.begin(), ola.end(), 0.0f);

            for (int f = 0; f < le; ++f)
            {
                std::fill (work.begin(), work.end(), 0.0f);

                for (int b = 0; b < DemucsModel::bins; ++b)     // the top bin (N / 2) stays 0
                {
                    work[(size_t) (2 * b)] = re[(size_t) b * le + (size_t) f] * norm;
                    work[(size_t) (2 * b + 1)] = im[(size_t) b * le + (size_t) f] * norm;
                }

                work[1] = 0.0f;     // DC is real
                fft.performRealOnlyInverseTransform (work.data());     // scaled by 1 / N

                const auto start = (f + 2) * H;

                for (int n = 0; n < N; ++n)
                    ola[(size_t) (start + n)] += work[(size_t) n] * window[(size_t) n];
            }

            // torch.istft (center = True) then demucs' crop: sample i = frame position i + centre + pad.
            auto* y = out[c];

            for (int i = 0; i < S; ++i)
            {
                const auto k = (size_t) (i + centre + pad);
                const auto e = envelope[k];
                y[i] += e > 1.0e-11f ? ola[k] / e : 0.0f;
            }
        }
    }
};

DemucsDsp::DemucsDsp() : impl (std::make_unique<Impl>()) {}
DemucsDsp::~DemucsDsp() = default;

void DemucsDsp::spectrogram (const float* left, const float* right, float* spec)
{
    const float* channels[] { left, right };
    impl->spectrogram (channels, spec);
}

void DemucsDsp::addInverse (const float* spec, float* left, float* right)
{
    float* channels[] { left, right };
    impl->addInverse (spec, channels);
}

juce::Result DemucsDsp::separate (const juce::AudioBuffer<float>& mix, DemucsModel& model, const Sink& sink, const Progress& progress)
{
    constexpr int S = DemucsModel::segment;
    constexpr int stride = (int) (0.75 * S);    // overlap 0.25, as demucs
    constexpr int numStems = Stems::count;
    const auto length = mix.getNumSamples();

    if (length <= 0 || mix.getNumChannels() < 1)
        return juce::Result::fail ("no audio");

    const auto* inL = mix.getReadPointer (0);
    const auto* inR = mix.getReadPointer (juce::jmin (1, mix.getNumChannels() - 1));

    // Normalise like demucs: by the mean and standard deviation of the mono mix.
    double sum = 0.0;

    for (int i = 0; i < length; ++i)
        sum += 0.5 * ((double) inL[i] + inR[i]);

    const auto mean = sum / length;
    double var = 0.0;

    for (int i = 0; i < length; ++i)
        var += juce::square (0.5 * ((double) inL[i] + inR[i]) - mean);

    const auto stdDev = juce::jmax (1.0e-8, std::sqrt (var / juce::jmax (1, length - 1)));
    const auto scale = (float) (1.0 / stdDev);

    // Triangular weights for overlap-add.
    std::vector<float> weight ((size_t) S);

    for (int k = 0; k < S; ++k)
        weight[(size_t) k] = (float) (k < S / 2 ? k + 1 : S - k) / (float) (S / 2);

    std::vector<float> chunk ((size_t) (2 * S)), spec ((size_t) DemucsModel::specSize);
    std::vector<float> freqOut ((size_t) numStems * DemucsModel::specSize), timeOut ((size_t) numStems * 2 * S);
    std::vector<float> source ((size_t) (2 * S));

    // Accumulators for [base, base + S), shifted by `stride` after each segment.
    std::vector<float> acc ((size_t) numStems * 2 * S, 0.0f), weightSum ((size_t) S, 0.0f);
    std::vector<float> outL, outR;
    const auto numSegments = (length + stride - 1) / stride;
    int segmentIndex = 0;

    for (int offset = 0; offset < length; offset += stride, ++segmentIndex)
    {
        if (progress && ! progress ((float) segmentIndex / (float) numSegments))
            return juce::Result::fail ("cancelled");

        // The segment, centred on the part that's left when it's shorter than S (as demucs' TensorChunk.padded).
        const auto chunkLength = juce::jmin (S, length - offset);
        const auto delta = S - chunkLength;
        const auto start = offset - delta / 2;

        for (int k = 0; k < S; ++k)
        {
            const auto i = start + k;
            const auto inside = i >= 0 && i < length;
            chunk[(size_t) k] = inside ? (float) ((inL[i] - mean) * scale) : 0.0f;
            chunk[(size_t) (S + k)] = inside ? (float) ((inR[i] - mean) * scale) : 0.0f;
        }

        spectrogram (chunk.data(), chunk.data() + S, spec.data());

        if (auto r = model.run (chunk.data(), spec.data(), freqOut.data(), timeOut.data()); r.failed())
            return r;

        for (int s = 0; s < numStems; ++s)
        {
            std::copy_n (timeOut.data() + (size_t) s * 2 * S, 2 * S, source.data());
            addInverse (freqOut.data() + (size_t) s * DemucsModel::specSize, source.data(), source.data() + S);

            for (int c = 0; c < 2; ++c)
            {
                auto* a = acc.data() + ((size_t) s * 2 + (size_t) c) * S;
                const auto* src = source.data() + (size_t) c * S + (size_t) (delta / 2);

                for (int k = 0; k < chunkLength; ++k)
                    a[k] += weight[(size_t) k] * src[k];
            }
        }

        for (int k = 0; k < chunkLength; ++k)
            weightSum[(size_t) k] += weight[(size_t) k];

        // [offset, offset + stride) gets nothing from later segments: it's final.
        const auto last = offset + stride >= length;
        const auto finished = last ? chunkLength : juce::jmin (stride, chunkLength);
        outL.resize ((size_t) finished);
        outR.resize ((size_t) finished);

        for (int s = 0; s < numStems; ++s)
        {
            const auto* aL = acc.data() + (size_t) s * 2 * S;
            const auto* aR = aL + S;

            for (int k = 0; k < finished; ++k)
            {
                const auto w = weightSum[(size_t) k] > 0.0f ? stdDev / weightSum[(size_t) k] : 0.0;
                outL[(size_t) k] = (float) (aL[k] * w + mean);
                outR[(size_t) k] = (float) (aR[k] * w + mean);
            }

            if (! sink (s, outL.data(), outR.data(), offset, finished))
                return juce::Result::fail ("cancelled");
        }

        if (! last)
        {
            for (int s = 0; s < numStems * 2; ++s)
            {
                auto* a = acc.data() + (size_t) s * S;
                std::copy (a + stride, a + S, a);
                std::fill (a + (S - stride), a + S, 0.0f);
            }

            std::copy (weightSum.begin() + stride, weightSum.end(), weightSum.begin());
            std::fill (weightSum.end() - stride, weightSum.end(), 0.0f);
        }
    }

    if (progress)
        progress (1.0f);

    return juce::Result::ok();
}

//==============================================================================
namespace StemSeparator
{
    juce::File getModelFile()   { return getAppDataFolder().getChildFile ("Models").getChildFile ("htdemucs_6s.onnx"); }

    juce::URL getModelUrl()
    {
        return juce::URL ("https://github.com/camillopatricelli/practice-vibes/releases/download/stem-model-v1/htdemucs_6s.onnx");
    }

    bool hasModel()             { return getModelFile().getSize() == modelBytes; }

    juce::Result separateToFolder (const juce::AudioBuffer<float>& song, double sampleRate, const juce::File& folder,
                                   const juce::String& songName, DemucsModel& model, const DemucsDsp::Progress& progress)
    {
        const auto mix = sampleRate == (double) DemucsModel::sampleRate
                             ? song
                             : AacFile::resample (song, sampleRate, (double) DemucsModel::sampleRate);
        const auto length = mix.getNumSamples();

        // The stems are kept as 16-bit in memory until they're encoded (a 4-minute song: about 250 MB).
        constexpr float headroom = 2.0f;
        std::vector<std::vector<juce::int16>> stems ((size_t) Stems::count * 2, std::vector<juce::int16> ((size_t) length));

        auto toInt = [] (float v) { return (juce::int16) juce::jlimit (-32767, 32767, juce::roundToInt (v / headroom * 32767.0f)); };

        DemucsDsp dsp;
        const auto result = dsp.separate (mix, model, [&] (int s, const float* l, const float* r, int start, int count)
        {
            auto& left = stems[(size_t) s * 2];
            auto& right = stems[(size_t) s * 2 + 1];

            for (int k = 0; k < count && start + k < length; ++k)
            {
                left[(size_t) (start + k)] = toInt (l[k]);
                right[(size_t) (start + k)] = toInt (r[k]);
            }

            return true;
        }, [&] (float p) { return ! progress || progress (p * 0.95f); });

        if (result.failed())
            return result;

        if (! folder.createDirectory())
            return juce::Result::fail ("can't create " + folder.getFullPathName());

        Stems::infoFile (folder).deleteFile();      // incomplete until the new one is written
        juce::AudioBuffer<float> stem (2, length);
        std::array<double, Stems::count> gains;
        gains.fill (1.0);

        for (int s = 0; s < Stems::count; ++s)
        {
            if (progress && ! progress (0.95f + 0.05f * (float) s / (float) Stems::count))
                return juce::Result::fail ("cancelled");

            for (int c = 0; c < 2; ++c)
            {
                const auto& src = stems[(size_t) s * 2 + (size_t) c];
                auto* dst = stem.getWritePointer (c);

                for (int k = 0; k < length; ++k)
                    dst[k] = (float) src[(size_t) k] * (headroom / 32767.0f);
            }

            // Encoded at their own level; loud stems (rare) are scaled down to fit, and the factor is kept.
            const auto peak = stem.getMagnitude (0, length);

            if (peak > 0.999f)
            {
                stem.applyGain (0.999f / peak);
                gains[(size_t) s] = peak / 0.999f;
            }

            const auto file = Stems::stemFile (folder, s);

            if (auto r = AacFile::write (file, stem, (double) DemucsModel::sampleRate, 160); r.failed())
                return juce::Result::fail ("can't write " + file.getFileName() + ": " + r.getErrorMessage());
        }

        juce::XmlElement xml ("STEMS");
        xml.setAttribute ("model", "htdemucs_6s");
        xml.setAttribute ("song", songName);
        xml.setAttribute ("created", juce::Time::getCurrentTime().toISO8601 (true));
        xml.setAttribute ("lengthSeconds", (double) length / DemucsModel::sampleRate);

        for (int s = 0; s < Stems::count; ++s)
        {
            auto* e = xml.createNewChildElement ("STEM");
            e->setAttribute ("name", Stems::fileStem (s));
            e->setAttribute ("gain", gains[(size_t) s]);
        }

        if (! xml.writeTo (Stems::infoFile (folder)))
            return juce::Result::fail ("can't write stems.xml");

        if (progress)
            progress (1.0f);

        return juce::Result::ok();
    }
}

} // namespace pv
