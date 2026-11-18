#include "StudentGroups.h"
#include "Localisation.h"

namespace pv
{

juce::String StudentGroup::findMember (const juce::String& typed) const
{
    const auto wanted = typed.trim();

    if (wanted.isEmpty())
        return {};

    for (auto& m : members)
        if (m.equalsIgnoreCase (wanted))
            return m;

    // "Anna Bianchi" typed as "Your name" by the student, "Anna" in the group (or the other way round).
    for (auto& m : members)
        if (wanted.startsWithIgnoreCase (m + " ") || m.startsWithIgnoreCase (wanted + " "))
            return m;

    return {};
}

juce::File StudentGroups::fileFor (const juce::File& libraryRoot)
{
    return libraryRoot.getChildFile (folderName).getChildFile (fileName);
}

StudentGroups StudentGroups::load (const juce::File& libraryRoot)
{
    StudentGroups result;
    auto xml = juce::parseXMLIfTagMatches (fileFor (libraryRoot), "STUDENT_GROUPS");

    if (xml == nullptr)
        return result;

    for (auto* g : xml->getChildWithTagNameIterator ("GROUP"))
    {
        StudentGroup group;
        group.id = g->getStringAttribute ("id");
        group.name = g->getStringAttribute ("name");

        for (auto* m : g->getChildWithTagNameIterator ("MEMBER"))
            if (const auto name = m->getStringAttribute ("name").trim(); name.isNotEmpty())
                group.members.addIfNotAlreadyThere (name, true);

        if (group.id.isNotEmpty())
            result.groups.push_back (std::move (group));
    }

    for (auto* s : xml->getChildWithTagNameIterator ("SENT"))
    {
        GroupSend send;
        send.sharedId = s->getStringAttribute ("id");
        send.groupId = s->getStringAttribute ("group");
        send.topicId = s->getStringAttribute ("topic");
        send.member = s->getStringAttribute ("member");
        send.sent = juce::Time::fromISO8601 (s->getStringAttribute ("at"));

        if (send.sharedId.isNotEmpty())
            result.sends.push_back (std::move (send));
    }

    return result;
}

bool StudentGroups::save (const juce::File& libraryRoot) const
{
    juce::XmlElement xml ("STUDENT_GROUPS");

    for (auto& g : groups)
    {
        auto* e = xml.createNewChildElement ("GROUP");
        e->setAttribute ("id", g.id);
        e->setAttribute ("name", g.name);

        for (auto& m : g.members)
            e->createNewChildElement ("MEMBER")->setAttribute ("name", m);
    }

    for (auto& s : sends)
    {
        auto* e = xml.createNewChildElement ("SENT");
        e->setAttribute ("id", s.sharedId);
        e->setAttribute ("group", s.groupId);
        e->setAttribute ("topic", s.topicId);

        if (s.member.isNotEmpty())
            e->setAttribute ("member", s.member);

        e->setAttribute ("at", s.sent.toISO8601 (true));
    }

    const auto file = fileFor (libraryRoot);
    file.getParentDirectory().createDirectory();
    return xml.writeTo (file);
}

const StudentGroup* StudentGroups::find (const juce::String& groupId) const
{
    for (auto& g : groups)
        if (g.id == groupId)
            return &g;

    return nullptr;
}

StudentGroup& StudentGroups::add (const juce::String& name)
{
    StudentGroup g;
    g.id = juce::Uuid().toDashedString();
    g.name = name.trim();
    groups.push_back (std::move (g));
    return groups.back();
}

const GroupSend* StudentGroups::findSend (const juce::String& sharedId) const
{
    if (sharedId.isEmpty())
        return nullptr;

    for (auto& s : sends)
        if (s.sharedId == sharedId)
            return &s;

    return nullptr;
}

const GroupSend* StudentGroups::lastSend (const juce::String& topicId, const juce::String& groupId, const juce::String& member) const
{
    const GroupSend* latest = nullptr;

    for (auto& s : sends)
        if (s.topicId == topicId && s.groupId == groupId && s.member.equalsIgnoreCase (member))
            if (latest == nullptr || s.sent > latest->sent)
                latest = &s;

    return latest;
}

const Topic* StudentGroups::findMemberCopy (const Library& library, const juce::String& sharedId, const juce::String& groupId,
                                            const juce::String& member)
{
    for (auto& t : library.getTopics())
        if ((t.sharedId == sharedId || t.id == sharedId) && t.group == groupId && t.student.equalsIgnoreCase (member))
            return &t;

    return nullptr;
}

juce::String StudentGroups::describeSends (const Library& library, const juce::String& topicId) const
{
    juce::StringArray lines;

    for (auto& g : groups)
    {
        juce::Time lastSent;
        juce::StringArray back, waiting;

        for (auto& member : g.members)
        {
            // The member's file: their own send, else the group's latest one-file send.
            const GroupSend* send = lastSend (topicId, g.id, member);

            if (const auto* groupSend = lastSend (topicId, g.id); groupSend != nullptr && (send == nullptr || groupSend->sent > send->sent))
                send = groupSend;

            if (send == nullptr)
                continue;

            lastSent = juce::jmax (lastSent, send->sent);
            const auto* copy = findMemberCopy (library, send->sharedId, g.id, member);

            if (copy != nullptr && copy->lastReceived != juce::Time())
                back.add (member + " (" + i18n::date (copy->lastReceived, "%d %b") + ")");
            else
                waiting.add (member);
        }

        if (lastSent == juce::Time())
            continue;

        juce::String line = tr ("Sent to {group} on {date}.", g.name, i18n::date (lastSent, "%d %b"));

        if (! back.isEmpty())
            line << " " << tr ("Back from {names}.", back.joinIntoString (", "));

        if (! waiting.isEmpty())
            line << " " << (back.isEmpty() ? tr ("Nothing back yet from {names}.", waiting.joinIntoString (", "))
                                           : tr ("Not yet: {names}.", waiting.joinIntoString (", ")));

        lines.add (line);
    }

    return lines.joinIntoString ("\n");
}

} // namespace pv
