#pragma once

#include "Library.h"

namespace pv
{

/** Teachers: a group of students (an ensemble, "Quartetto Tuesday") to send one topic to. */
struct StudentGroup
{
    juce::String id, name;
    juce::StringArray members;      // first names / nicknames, as in the Student boxes

    juce::String findMember (const juce::String& typed) const;   // the member's spelling, or empty
};

/** A topic sent to a group: one file for everyone (member empty) or one file for one member. The id in the file
    (sharedId) is what comes back, from every member of the group in the first case. */
struct GroupSend
{
    juce::String sharedId, groupId, topicId, member;
    juce::Time sent;
};

/** The teacher's groups and what was sent to them: <library>/.students/groups.xml, so the teacher's devices share
    them. Never sent to anyone (same rule as the Student box). Load, change, save: the file is small. */
struct StudentGroups
{
    std::vector<StudentGroup> groups;
    std::vector<GroupSend> sends;

    static juce::File fileFor (const juce::File& libraryRoot);
    static StudentGroups load (const juce::File& libraryRoot);
    bool save (const juce::File& libraryRoot) const;

    const StudentGroup* find (const juce::String& groupId) const;
    StudentGroup& add (const juce::String& name);     // a new group with a fresh id

    /** The send whose file carried this id, or nullptr. */
    const GroupSend* findSend (const juce::String& sharedId) const;

    /** The latest send of the topic to the group (member empty: the one-file-for-everyone send), or nullptr. */
    const GroupSend* lastSend (const juce::String& topicId, const juce::String& groupId, const juce::String& member = {}) const;

    /** "Sent to Quartetto Tuesday on 17 Nov. Back from Anna (18 Nov); not yet: Marco, Luca." per group the topic
        went to; empty if it never went to a group. */
    juce::String describeSends (const Library&, const juce::String& topicId) const;

    /** A member's copy on the teacher's side: the topic that came back with this id from this member. */
    static const Topic* findMemberCopy (const Library&, const juce::String& sharedId, const juce::String& groupId,
                                        const juce::String& member);

    static constexpr const char* folderName = ".students";
    static constexpr const char* fileName = "groups.xml";
};

} // namespace pv
