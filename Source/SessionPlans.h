#pragma once

#include "PracticePlanner.h"

namespace pv
{

/** One slot of a saved plan: a fixed topic (by its stable UUID), or "random" (a rotation topic, optionally with a
    tag, picked again every time the plan is used). */
struct PlanSlot
{
    juce::String topicId;       // fixed slots
    int minutes = 10;
    bool random = false;
    juce::String tag;           // random slots: only topics with this tag ("" = any rotation topic)
};

/** A named routine saved from the session planner. */
struct SessionPlan
{
    juce::String id, name;
    std::vector<PlanSlot> slots;
};

/** Saved session plans, kept in <library>/.plans/plans.xml so every device using the same library folder shares them.
    Changes made on another device are picked up with reloadIfChanged(); call it before editing so a stale list
    doesn't overwrite them. */
class PlanList
{
public:
    explicit PlanList (const juce::File& libraryRoot);

    const juce::File& getFile() const noexcept                  { return file; }
    const std::vector<SessionPlan>& getPlans() const noexcept   { return plans; }

    /** Re-reads the file if it changed on disk since the last read or write; true if it did. */
    bool reloadIfChanged();

    const SessionPlan* find (const juce::String& id) const;
    const SessionPlan* findByName (const juce::String& name) const;   // ignoring case

    /** Adds a plan and saves; returns its id (empty for an empty name). */
    juce::String add (const juce::String& name, std::vector<PlanSlot>);

    bool setSlots (const juce::String& id, std::vector<PlanSlot>);
    bool rename (const juce::String& id, const juce::String& name);
    juce::String duplicate (const juce::String& id);                  // "Name (copy)"; returns the new id
    bool remove (const juce::String& id);

    /** `base`, or "base 2", "base 3"... so no two plans share a name. */
    juce::String uniqueName (const juce::String& base) const;

    static constexpr const char* folderName = ".plans";
    static constexpr const char* fileName = "plans.xml";

private:
    void read();
    bool save();
    void rememberFileState();
    SessionPlan* findMutable (const juce::String& id);

    juce::File file;
    std::vector<SessionPlan> plans;
    juce::Time lastModified;
    juce::int64 lastSize = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlanList)
};

/** A topic's title, or empty if there's no such topic (deleted). */
using TopicTitleFn = std::function<juce::String (const juce::String& topicId)>;
using CandidatesForTagFn = std::function<std::vector<PlanCandidate> (const juce::String& tag)>;

/** Today's slots from a saved plan: fixed slots keep their topic (marked missing if it was deleted), random slots
    pick a rotation topic not already in the session (missing if none is left). */
std::vector<SessionSlot> slotsFromPlan (const SessionPlan&, const TopicTitleFn&, const CandidatesForTagFn&, juce::Random&);

/** What "Save as plan" stores for the planner's current slots. */
std::vector<PlanSlot> planSlotsFrom (const std::vector<SessionSlot>&);

/** The slots that can actually be practised (missing ones left out). */
std::vector<SessionSlot> playableSlots (const std::vector<SessionSlot>&);

} // namespace pv
