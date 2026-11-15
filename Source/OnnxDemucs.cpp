// The Demucs network run by ONNX Runtime (Windows). onnxruntime.dll sits next to the app and is loaded on first use,
// so the app starts (and everything else works) even without it.

#include "Stems.h"

#if JUCE_WINDOWS && PV_HAS_ONNX
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #define ORT_API_MANUAL_INIT
 #include <onnxruntime_cxx_api.h>
#endif

namespace pv::StemSeparator
{

#if JUCE_WINDOWS && PV_HAS_ONNX

static juce::File runtimeFile()
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("onnxruntime.dll");
}

bool isSupported()      { return runtimeFile().existsAsFile(); }

/** Loads onnxruntime.dll from the app's folder (not the older copy Windows keeps in System32). */
static bool initRuntime (juce::String& error)
{
    static juce::CriticalSection lock;
    static bool ready = false;
    const juce::ScopedLock sl (lock);

    if (ready)
        return true;

    const auto dll = runtimeFile();
    auto* module = LoadLibraryExW (dll.getFullPathName().toWideCharPointer(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

    if (module == nullptr)
    {
        error = "can't load " + dll.getFileName();
        return false;
    }

    using GetApiBase = const OrtApiBase* (ORT_API_CALL*) ();
    auto getApiBase = reinterpret_cast<GetApiBase> (reinterpret_cast<void*> (GetProcAddress (module, "OrtGetApiBase")));
    const auto* api = getApiBase != nullptr ? getApiBase()->GetApi (ORT_API_VERSION) : nullptr;

    if (api == nullptr)
    {
        error = "this onnxruntime.dll is too old";
        return false;
    }

    Ort::InitApi (api);
    ready = true;
    return true;
}

class OnnxModel final : public DemucsModel
{
public:
    OnnxModel (const juce::File& file, int threads)
    {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads (threads);
        options.SetInterOpNumThreads (1);
        options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        session = std::make_unique<Ort::Session> (env, file.getFullPathName().toWideCharPointer(), options);

        Ort::AllocatorWithDefaultOptions allocator;

        for (size_t i = 0; i < session->GetInputCount(); ++i)
            inputNames.push_back (session->GetInputNameAllocated (i, allocator).get());

        for (size_t i = 0; i < session->GetOutputCount(); ++i)
        {
            outputNames.push_back (session->GetOutputNameAllocated (i, allocator).get());
            outputRanks.push_back (session->GetOutputTypeInfo (i).GetTensorTypeAndShapeInfo().GetShape().size());
        }

        if (inputNames.size() != 2 || outputNames.size() != 2)
            throw std::runtime_error ("unexpected model inputs / outputs");
    }

    juce::Result run (const float* mix, const float* spec, float* freqOut, float* timeOut) override
    {
        try
        {
            const auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
            const std::array<int64_t, 3> mixShape { 1, 2, segment };
            const std::array<int64_t, 4> specShape { 1, 4, bins, frames };
            const std::array<int64_t, 5> freqShape { 1, Stems::count, 4, bins, frames };
            const std::array<int64_t, 4> timeShape { 1, Stems::count, 2, segment };

            std::array<Ort::Value, 2> inputs {
                Ort::Value::CreateTensor<float> (memory, const_cast<float*> (mix), (size_t) (2 * segment), mixShape.data(), mixShape.size()),
                Ort::Value::CreateTensor<float> (memory, const_cast<float*> (spec), (size_t) specSize, specShape.data(), specShape.size())
            };

            // Outputs written straight into the caller's buffers; the 5-D one is the spectrogram.
            auto freq = Ort::Value::CreateTensor<float> (memory, freqOut, (size_t) Stems::count * specSize, freqShape.data(), freqShape.size());
            auto time = Ort::Value::CreateTensor<float> (memory, timeOut, (size_t) Stems::count * 2 * segment, timeShape.data(), timeShape.size());
            std::array<Ort::Value, 2> outputs { std::move (freq), std::move (time) };

            if (outputRanks[0] != 5)
                std::swap (outputs[0], outputs[1]);

            const char* in[] { inputNames[0].c_str(), inputNames[1].c_str() };
            const char* out[] { outputNames[0].c_str(), outputNames[1].c_str() };
            session->Run (Ort::RunOptions { nullptr }, in, inputs.data(), 2, out, outputs.data(), 2);
            return juce::Result::ok();
        }
        catch (const std::exception& e)
        {
            return juce::Result::fail (e.what());
        }
    }

private:
    Ort::Env env { ORT_LOGGING_LEVEL_ERROR, "PracticeVibes" };
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inputNames, outputNames;
    std::vector<size_t> outputRanks;
};

std::unique_ptr<DemucsModel> openModel (juce::String& error)
{
    if (! initRuntime (error))
        return {};

    if (! hasModel())
    {
        error = "the separation model isn't downloaded";
        return {};
    }

    try
    {
        // Leave a core for the audio and the UI.
        const auto threads = juce::jmax (1, juce::SystemStats::getNumPhysicalCpus() - 1);
        return std::make_unique<OnnxModel> (getModelFile(), threads);
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return {};
    }
}

#else

bool isSupported()      { return false; }

std::unique_ptr<DemucsModel> openModel (juce::String& error)
{
    error = "stem separation isn't available on this device";
    return {};
}

#endif

} // namespace pv::StemSeparator
