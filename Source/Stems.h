#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace pv
{

/** Stem separation (htdemucs_6s, "Hybrid Transformer Demucs", MIT licence): a song split into six stems.

    Stems are kept as .m4a files in "<song folder>/.stems/<song file name>/" when the song is in the library (they
    travel with it, e.g. to the iPad through the cloud folder), otherwise under the app's data folder.
    The folder is complete when its stems.xml exists (written last). */
namespace Stems
{
    constexpr int count = 6;

    /** The model's output order. */
    enum Index { drums, bass, other, vocals, guitar, piano };

    juce::String fileStem (int index);          // "drums" ...
    juce::String displayName (int index);       // "Drums" ...

    /** The order the mixer shows them in. */
    constexpr std::array<int, count> displayOrder { vocals, guitar, bass, drums, piano, other };

    constexpr const char* folderName = ".stems";

    /** Where the stems of this song live (libraryRoot: the library's folder, may be empty). */
    juce::File folderFor (const juce::File& song, const juce::File& libraryRoot);

    juce::File stemFile (const juce::File& folder, int index);
    juce::File infoFile (const juce::File& folder);
    bool isComplete (const juce::File& folder);
}

//==============================================================================
/** The arithmetic around the network: Demucs' spectrogram in and out, and the overlapping 7.8 s segments
    (the same as demucs' apply_model with shifts = 0 and overlap = 0.25). The network itself is a DemucsModel. */
class DemucsModel
{
public:
    virtual ~DemucsModel() = default;

    static constexpr int sampleRate = 44100;
    static constexpr int segment = 343980;              // 7.8 s
    static constexpr int fftSize = 4096, hop = 1024;
    static constexpr int bins = 2048, frames = 336;     // spectrogram given to / returned by the network
    static constexpr int specSize = 4 * bins * frames;  // (left re, left im, right re, right im) x bins x frames

    /** mix: 2 x segment (planar), spec: specSize. Writes Stems::count x specSize to freqOut (masked spectrograms)
        and Stems::count x 2 x segment to timeOut (the time branch). */
    virtual juce::Result run (const float* mix, const float* spec, float* freqOut, float* timeOut) = 0;
};

class DemucsDsp
{
public:
    DemucsDsp();
    ~DemucsDsp();

    /** Spectrogram of one segment (2 channels x DemucsModel::segment), as the network wants it. */
    void spectrogram (const float* left, const float* right, float* spec);

    /** One source's masked spectrogram (specSize) back to audio (adds into left/right, segment samples each). */
    void addInverse (const float* spec, float* left, float* right);

    /** Final samples of stem `stem`, in order: [start, start + count). Return false to stop. */
    using Sink = std::function<bool (int stem, const float* left, const float* right, int start, int count)>;
    using Progress = std::function<bool (float)>;      // 0..1; return false to cancel

    /** Separates a stereo song at 44.1 kHz. */
    juce::Result separate (const juce::AudioBuffer<float>& mix, DemucsModel&, const Sink&, const Progress&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

//==============================================================================
/** The ONNX Runtime network (Windows: onnxruntime.dll next to the app). */
namespace StemSeparator
{
    bool isSupported();                              // this platform can separate (Windows)
    juce::File getModelFile();                       // in the app's data folder, downloaded on first use
    juce::URL getModelUrl();
    constexpr juce::int64 modelBytes = 114559139;
    bool hasModel();

    /** Loads the runtime and the model (slow: a few seconds). */
    std::unique_ptr<DemucsModel> openModel (juce::String& error);

    /** Whole job (any thread): resample to 44.1 kHz, separate, write the six .m4a files and stems.xml into `folder`.
        progress: 0..1, returns false to cancel. */
    juce::Result separateToFolder (const juce::AudioBuffer<float>& song, double sampleRate, const juce::File& folder,
                                   const juce::String& songName, DemucsModel&, const DemucsDsp::Progress&);
}

} // namespace pv
