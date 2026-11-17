#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "AppEngine.h"
#include "AppLog.h"
#include "Library.h"
#include "PracticeLog.h"
#include "PracticeInput.h"
#include "PracticePlanner.h"
#include "SessionPlans.h"
#include "Take.h"

namespace pv
{

class LibraryPanel;
class LinksPanel;
class LooperPanel;
class PracticePanel;
class RigPanel;
class TodayPanel;
class YouTubePlayer;

/** Top-level UI: title bar, tabs (Today / Practice / Topics / Links / Looper / Rig, plus an optional Log) and a full-window
    overlay used for plugin UIs and the audio settings.
*/
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent (AppEngine&, AppLog&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** A .pvtopic opened from outside (double-click, "Open in", command line): Topics tab + import preview. */
    void openSharedTopic (const juce::File&);

private:
    class LogPanel;
    class Overlay;

    void openAudioSettings();
    bool isLogTabShown() const;
    void setLogTabShown (bool);     // the Log tab is hidden unless switched on in the audio settings
    void toggleTuner();
    void openBluetoothMidi();
    void showOverlay (const juce::String& title, std::unique_ptr<juce::Component>);
    void closeOverlay();

    void timerCallback() override;
    void showTab (juce::Component* content);
    bool isTabShowing (juce::Component* content) const;
    void openYouTube (const YouTubeLink&, const juce::String& title);

    // Practice log
    void libraryOpened();
    void practiseTopic (const juce::String& topicId);
    void tickTracker();
    void updatePractisingButton();
    void showPractisingMenu();
    /** Classifies the input again if it changed (or `force`), and sets the tracker's Start-button mode from it. */
    void updatePracticeInput (bool force = false);

    // Dice, sessions, history
    std::vector<PlanCandidate> rotationCandidates (const juce::String& tag);
    juce::StringArray rotationTags();
    /** `planId`: open with that saved plan loaded (to tweak it before starting). */
    void openSessionPlanner (const juce::String& tag, const juce::String& planId = {});
    /** Starts a saved plan as it is (random slots picked now); with missing topics it opens the planner instead. */
    void startSavedPlan (const juce::String& planId);
    /** The library's saved plans, re-read if another device changed them; nullptr without a library. */
    PlanList* savedPlans();
    juce::String topicTitle (const juce::String& topicId);
    void startSession (std::vector<SessionSlot>);
    void sessionNext();
    void sessionExtend();
    void sessionEnd();
    void showTopicHistory (const juce::String& topicId);
    void showAllHistory();
    /** Correct / delete a log entry; `historyTopicId` = reopen that topic's History afterwards (empty: just close). */
    void editLogEntry (const juce::String& entryId, const juce::String& historyTopicId);
    /** "Add time": a log entry typed by hand. Empty topicId = choose the topic in the form (from the Today tab). */
    void addLogTime (const juce::String& topicId, const juce::String& historyTopicId);
    /** Topic choices for the entry forms: free practice, then the library's topics by title (empty without a library). */
    std::vector<std::pair<juce::String, juce::String>> logTopicChoices() const;
    /** A song picked from outside the library: offers to copy it into the Inbox (asked once per file per run). */
    void offerAddToInbox (const juce::URL& song);
    juce::StringArray inboxDeclined;

    // Takes
    void toggleRecording();
    void startRecording();
    void stopRecording (bool waitForFile = false);
    void updateRecordButton();
    juce::File takeFolder() const;
    void openAudioFile (const juce::File&);     // a take replays over its song; anything else loads as a song

    void layoutYouTube();
    juce::Rectangle<int> defaultCompactBounds() const;

    AppEngine& engine;

    juce::Label titleLabel;
    static constexpr int iconSize = 30;
    juce::Image appIcon;                // top bar, left of the title
    juce::Rectangle<int> iconBounds;
    juce::TextButton recordButton        { "Rec" },
                     tunerButton         { "Tuner" },
                     audioSettingsButton { "Audio settings" },
                     bluetoothButton     { "Bluetooth MIDI" },
                     saveButton          { "Save" },
                     practisingButton;

    // The log outlives the tracker (which writes its last entry when destroyed).
    std::unique_ptr<PracticeLog> practiceLog;
    PracticeTracker tracker;
    double lastTrackerTick = 0.0;
    juce::File lastSongFile;
    int practisingButtonCountdown = 0;

    // The input practice is timed from: a microphone uses the Start button (it hears the room too).
    PracticeInputSettings practiceInputSettings;
    InputInfo practiceInput;
    juce::String practiceInputKey;
    int practiceInputCountdown = 0;

    // The take being recorded: where it goes and what it was played along to.
    juce::File recordingFile, recordingFolder;
    TakeInfo recordingInfo;
    juce::String recordMessage;         // shown on the button for a moment after a take was saved
    int recordMessageCountdown = 0;

    PracticeSession session;
    int planMinutes = 45;
    std::unique_ptr<PlanList> planList;     // <library>/.plans/plans.xml

    std::unique_ptr<TodayPanel> todayPanel;
    std::unique_ptr<PracticePanel> practicePanel;
    std::unique_ptr<LibraryPanel> libraryPanel;
    std::unique_ptr<LinksPanel> linksPanel;
    std::unique_ptr<LooperPanel> looperPanel;
    std::unique_ptr<RigPanel> rigPanel;
    std::unique_ptr<LogPanel> logPanel;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    std::unique_ptr<Overlay> overlay;
    juce::TooltipWindow tooltipWindow { this, 700 };     // the icon-only buttons say what they do on hover

    // Docked over the Practice tab's video area, or floating ("compact"); never reparented (native web view).
    std::unique_ptr<YouTubePlayer> youtube;
    bool youTubeAutoCompact = false;           // compacted because you left the Practice tab while it played
    juce::Rectangle<int> lastCompactBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace pv
