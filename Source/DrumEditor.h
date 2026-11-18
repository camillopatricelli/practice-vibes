#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppEngine.h"
#include "Localisation.h"

namespace pv
{

/** The drum machine on the Looper tab: one row of controls (play, pattern, kit, volume, mute clicks)
    and, when shown, the pattern editor (one bar at a time: a row per drum, a column per step).
    Tap a step: on -> accent -> off. Right-click or long-press: ghost note.
*/
class DrumSection final : public juce::Component,
                          private juce::Timer
{
public:
    explicit DrumSection (AppEngine&);
    ~DrumSection() override;

    /** Height for the current state: the control row alone, or with the editor. */
    int getPreferredHeight() const;
    bool isEditorShown() const noexcept                 { return editorShown; }
    void setEditorShown (bool);

    /** Called when the editor is shown or hidden (the owner lays out again). */
    std::function<void()> onLayoutChange;
    /** Called when "Mute clicks" is toggled (the owner saves it). */
    std::function<void()> onClicksMutedChange;

    /** A new pattern arrived from outside (loaded loop session): refresh everything. */
    void patternChangedElsewhere();

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Grid;

    void timerCallback() override;
    void showPatternMenu();
    void choosePattern (const DrumPattern&);
    void saveAs();
    void deleteCurrent();
    void togglePlay();
    void refreshControls();
    void reshape (int beats, int ticks, int bars);
    void setShownBar (int bar);
    juce::String displayName() const;

    AppEngine& engine;
    DrumSequencer& drums;
    MetronomeProcessor& metronome;

    // Control row
    juce::Label title, status;
    bool showsSaveError = false;     // "Not saved: ..." stays until the drums play
    juce::TextButton playButton { tr ("Play drums") }, patternButton, showButton { tr ("Edit pattern") };
    juce::ComboBox kitBox;
    juce::Slider volume;
    juce::ToggleButton clicksMutedButton { tr ("Mute clicks") };

    // Editor
    juce::Label stepsLabel, swingLabel, barsLabel, hint;
    juce::ComboBox stepsBox, barsBox;
    juce::Slider swingSlider;
    juce::OwnedArray<juce::TextButton> barTabs;
    juce::TextButton copyBarButton { tr ("Copy to all bars") }, clearButton { tr ("Clear") },
                     saveButton { tr ("Save as...") }, deleteButton { tr ("Delete") };
    std::unique_ptr<Grid> grid;

    bool editorShown = false, startedMetronome = false, metronomeWasRunning = false;
    int shownBar = 0, shownPlayingBar = -2;
    DrumPattern savedVersion;           // as chosen or last saved: tells whether it was edited

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumSection)
};

} // namespace pv
