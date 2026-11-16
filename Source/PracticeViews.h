#pragma once

#include <map>

#include <juce_gui_basics/juce_gui_basics.h>
#include "PracticeLog.h"
#include "PracticePlanner.h"
#include "SessionPlans.h"

namespace pv
{

/** Overlay: "I have 45 minutes" (+ an optional focus tag) -> a few rotation topics with time slots, or a saved plan.
    Any slot can be re-rolled, retimed, made random or removed before starting. Loading a saved plan and tweaking it
    only changes today's session; the saved plan changes only when it is saved again.
*/
class SessionPlanner final : public juce::Component
{
public:
    using CandidatesFn = std::function<std::vector<PlanCandidate> (const juce::String& tag)>;

    /** `plans` may be null (no library: nothing can be saved). `planId`: open with that saved plan loaded. */
    SessionPlanner (CandidatesFn, TopicTitleFn, PlanList* plans, const juce::StringArray& tags, int minutes,
                    const juce::String& tag, const juce::String& planId = {});
    ~SessionPlanner() override;

    /** Only the slots that can be practised (missing topics left out). */
    std::function<void (std::vector<SessionSlot>, int minutes, const juce::String& tag)> onStart;
    std::function<void()> onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SlotRow;

    void replan();
    void loadPlan (const juce::String& planId);
    void rebuildRows();
    void slotsEdited();
    void updateTotal();
    void refreshPlanBox();
    void showSaveMenu();
    void showPlanMenu();
    void askName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done);
    void saveAsNew (const juce::String& name);
    void showAddMenu();
    juce::String currentTag() const;
    const SessionPlan* loadedPlan() const;

    CandidatesFn candidatesFor;
    TopicTitleFn titleOf;
    PlanList* plans = nullptr;
    std::vector<PlanCandidate> candidates;
    std::vector<SessionSlot> slots;
    juce::Random random;
    juce::String loadedPlanId;
    bool edited = false;

    juce::Label planLabel, timeLabel, focusLabel, totalLabel, statusLabel, emptyLabel;
    juce::ComboBox planBox, timeBox, focusBox;
    juce::TextButton saveButton { "Save as plan..." }, planMenuButton { "Plan..." },
                     shuffleButton { "Shuffle" }, addButton { "+ Add a topic" },
                     startButton { "Start session" }, cancelButton { "Cancel" };
    std::vector<std::unique_ptr<SlotRow>> rows;
    juce::Viewport rowViewport;
    juce::Component rowHolder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionPlanner)
};

/** Overlay form: correct or delete one practice log entry (start time, played minutes, note). */
class EntryEditor final : public juce::Component
{
public:
    /** `stillRunning`: the entry is the one being recorded right now (it can't be changed until it ends). */
    EntryEditor (const PracticeEntry&, bool stillRunning);

    /** "Add time": a new entry. `topics` (id, title) offers a topic choice ("" = free practice); empty = keep the
        entry's own topic. */
    struct NewEntry {};
    EntryEditor (NewEntry, const PracticeEntry& defaults, std::vector<std::pair<juce::String, juce::String>> topics);

    std::function<void (juce::Time start, double playedSeconds, const juce::String& note)> onSave;
    std::function<void()> onDelete, onCancel;

    /** The topic picked in "Add time" mode (the entry's own topic otherwise). */
    juce::String getTopicId() const;
    juce::String getTopicTitle() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void setUpFields (const PracticeEntry&);
    void save();

    std::vector<std::pair<juce::String, juce::String>> topicChoices;
    juce::String ownTopicId, ownTopicTitle;
    juce::ComboBox topicBox;
    juce::Label summaryLabel, topicLabel, startLabel, minutesLabel, noteLabel, errorLabel;
    juce::TextEditor startEditor, minutesEditor, noteEditor;
    juce::TextButton saveButton { "Save" }, cancelButton { "Cancel" }, deleteButton { "Delete entry" };
};

/** A recorded take listed in a topic's history. */
struct TakeRow
{
    juce::Time when;
    juce::String title;         // what it was played along to ("guitar only" if nothing)
    double seconds = 0.0;
    juce::File file;
};

/** Overlay: one topic's practice history - tempo and song speed over time, minutes per week, every session
    and every recorded take (tap a take to replay it).

    With topicId == allTopicsId it shows everything instead: minutes per week over all topics, time per topic
    (tap a bar for that topic's history) and every session labelled with its topic. `topicTitles` (id -> current
    title) names the topics there; entries of topics not in it keep the title they were logged with. */
class TopicHistory final : public juce::Component
{
public:
    static constexpr const char* allTopicsId = "*all-topics*";

    TopicHistory (const PracticeLog&, const juce::String& topicId, std::vector<TakeRow> takes = {},
                  const std::map<juce::String, juce::String>& topicTitles = {});

    std::function<void (const juce::String& topicId)> onShowTopic;    // all-topics view: a topic's bar was tapped
    std::function<void (const juce::File&)> onOpenTake;
    std::function<void (const juce::String& entryId)> onEditEntry;     // a session row was tapped
    std::function<void()> onAddTime;                                   // "+ Add time..."
    ~TopicHistory() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class LineChart;
    class WeekBars;
    class TopicBars;
    class SessionList;

    juce::Label statsLabel, emptyLabel, sessionsHeader;
    std::unique_ptr<TopicBars> topicBars;
    juce::TextButton addTimeButton { "+ Add time..." };
    std::unique_ptr<LineChart> tempoChart, speedChart;
    std::unique_ptr<WeekBars> weekBars;
    std::unique_ptr<SessionList> sessionList;
    juce::Viewport sessionViewport;
};

} // namespace pv
