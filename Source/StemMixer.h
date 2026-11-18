#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PlayerProcessor.h"
#include "Localisation.h"

namespace pv
{

/** Runs one stem separation at a time in the background: downloads the model the first time, separates, writes the
    stems folder. Message-thread object; the work happens on its own thread. */
class StemService final : private juce::Timer
{
public:
    StemService();
    ~StemService() override;

    /** Starts separating `song` (decoded) into `folder`. False if a job is running already. */
    bool start (std::shared_ptr<const LoadedSong> song, const juce::File& folder);
    void cancel();

    bool isBusy() const noexcept                { return job != nullptr; }
    juce::File getBusyFolder() const;
    juce::String getBusySongName() const;
    double getProgress() const;                 // 0..1
    juce::String getStatus() const;             // "Downloading the separation model... 40 MB of 115 MB"

    /** A job ended (message thread). `folder` is complete if the result is ok. */
    std::function<void (const juce::File& folder, const juce::Result&)> onFinished;
    juce::String getLastError() const           { return lastError; }

private:
    struct Job;
    class Download;

    void timerCallback() override;
    void runSeparation();
    void finish (juce::Result);     // by value: the job holding it is released here

    std::shared_ptr<Job> job;
    std::unique_ptr<Download> download;
    juce::String lastError;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

/** Mute / solo / volume per stem, as set in the mixer. */
struct StemControls
{
    std::array<float, Stems::count> volume { 1, 1, 1, 1, 1, 1 };
    std::array<bool, Stems::count> mute {}, solo {};

    float gainFor (int stem) const;
    bool isNeutral() const;
    juce::String describe() const;              // "guitar off", "vocals solo", "" when neutral
    void reset()                                { *this = {}; }

    /** For the practice setup: "guitar:0:m vocals:0.5" (stems left as they are are not listed; "" = neutral). */
    juce::String toString() const;
    static StemControls fromString (const juce::String&);
};

/** The pop-up behind the Practice tab's "Stems" button. */
class StemMixer final : public juce::Component,
                        private juce::Timer
{
public:
    struct Host
    {
        virtual ~Host() = default;
        virtual PlayerProcessor& getPlayer() = 0;
        virtual StemService& getStemService() = 0;
        virtual StemControls& getStemControls() = 0;
        virtual void stemControlsChanged() = 0;
        virtual juce::File getStemFolder() = 0;             // for the song loaded now (empty: none / not a file)
        virtual bool isLoadingStems() = 0;
        virtual void separateCurrentSong() = 0;
    };

    explicit StemMixer (Host&);
    ~StemMixer() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Mode { none, mixer, busy, loading, offer, unavailable };

    void timerCallback() override;
    void update();
    void syncFromControls();

    Host& host;
    Mode mode = Mode::none;

    juce::Label title, message;
    juce::TextButton actionButton, resetButton { tr ("Reset") };
    double progress = 0.0;
    juce::ProgressBar progressBar { progress };

    struct Row
    {
        int stem = 0;
        juce::Label name;
        juce::TextButton mute { tr ("M") }, solo { tr ("S") };
        juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    };

    std::array<std::unique_ptr<Row>, Stems::count> rows;
};

} // namespace pv
