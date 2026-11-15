#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PracticeLog.h"
#include "PracticePlanner.h"

namespace pv
{

/** Overlay: "I have 45 minutes" (+ an optional focus tag) -> a few rotation topics with time slots.
    Any slot can be re-rolled, retimed or removed before starting. Nothing is saved as a routine.
*/
class SessionPlanner final : public juce::Component
{
public:
    using CandidatesFn = std::function<std::vector<PlanCandidate> (const juce::String& tag)>;

    SessionPlanner (CandidatesFn, const juce::StringArray& tags, int minutes, const juce::String& tag);
    ~SessionPlanner() override;

    std::function<void (std::vector<SessionSlot>, int minutes, const juce::String& tag)> onStart;
    std::function<void()> onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SlotRow;

    void replan();
    void rebuildRows();
    void updateTotal();
    juce::String currentTag() const;

    CandidatesFn candidatesFor;
    std::vector<PlanCandidate> candidates;
    std::vector<SessionSlot> slots;
    juce::Random random;

    juce::Label timeLabel, focusLabel, totalLabel, emptyLabel;
    juce::ComboBox timeBox, focusBox;
    juce::TextButton shuffleButton { "Shuffle" }, addButton { "+ Add a topic" },
                     startButton { "Start session" }, cancelButton { "Cancel" };
    std::vector<std::unique_ptr<SlotRow>> rows;
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
    and every recorded take (tap a take to replay it). */
class TopicHistory final : public juce::Component
{
public:
    TopicHistory (const PracticeLog&, const juce::String& topicId, std::vector<TakeRow> takes = {});

    std::function<void (const juce::File&)> onOpenTake;
    std::function<void (const juce::String& entryId)> onEditEntry;     // a session row was tapped
    std::function<void()> onAddTime;                                   // "+ Add time..."
    ~TopicHistory() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class LineChart;
    class WeekBars;
    class SessionList;

    juce::Label statsLabel, emptyLabel, sessionsHeader;
    juce::TextButton addTimeButton { "+ Add time..." };
    std::unique_ptr<LineChart> tempoChart, speedChart;
    std::unique_ptr<WeekBars> weekBars;
    std::unique_ptr<SessionList> sessionList;
    juce::Viewport sessionViewport;
};

} // namespace pv
