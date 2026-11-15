#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppEngine.h"
#include "DrumEditor.h"

namespace pv
{

/** The "Looper" tab: four pedal-style tracks locked to the metronome.
    Only the UI lives here: loops keep playing while other tabs are shown.
*/
class LooperPanel final : public juce::Component,
                          private juce::Timer
{
public:
    explicit LooperPanel (AppEngine&);
    ~LooperPanel() override;

    /** Supplied by the owner: shows a component full-window (the footswitch settings). */
    std::function<void (const juce::String& title, std::unique_ptr<juce::Component>)> showOverlay;
    std::function<void()> hideOverlay;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class TrackRow;
    class BeatDots;

    void timerCallback() override;
    void updatePedalBadges();
    void loadSettings();
    void saveSettings() const;

    AppEngine& engine;
    Looper& looper;
    MetronomeProcessor& metronome;

    juce::Label header, hintLabel, tempoDisplay, latencyLabel;
    juce::TextButton afterFxButton { "After FX" }, beforeFxButton { "Before FX" };
    juce::Label sourceLabel, lengthLabel, tempoLabel, beatsLabel, offsetLabel;
    juce::ComboBox lengthBox, beatsBox;
    juce::Slider tempoSlider, offsetSlider;
    juce::TextButton metronomeButton { "Start metronome" }, undoButton { "Undo overdub" },
                     muteAllButton { "Mute all" }, clearAllButton { "Clear all" },
                     footswitchButton { "Footswitch..." };
    std::unique_ptr<BeatDots> beatDots;

    // Latency calibration: play muted strums on the clicks (or loop the output back to the input).
    juce::TextButton calibrateButton { "Calibrate..." };
    bool calibrating = false, stopMetronomeAfterCalibration = false, followSongBefore = false, clicksMutedBefore = false;
    std::vector<double> calibrationLags;
    double calibrationStartMs = 0.0, calibrationMessageMs = 0.0;
    juce::String calibrationMessage;
    // Saving / loading loops (folders under LoopFiles::getLoopsFolder())
    juce::TextButton saveButton { "Save loops" }, loadButton { "Load loops..." };
    juce::String fileMessage;
    double fileMessageMs = 0.0;
    void saveLoops();
    void showLoadMenu();
    void loadLoops (const juce::File& folder);
    void showFileMessage (const juce::String&);

    void startCalibration();
    void updateCalibration();
    void endCalibration();

    std::unique_ptr<DrumSection> drumSection;     // the drum machine (pattern editor can be hidden)
    std::array<std::unique_ptr<TrackRow>, Looper::numTracks> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPanel)
};

} // namespace pv
