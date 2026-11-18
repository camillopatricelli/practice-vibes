#include "SessionPlans.h"
#include "Localisation.h"

namespace pv
{

PlanList::PlanList (const juce::File& libraryRoot)
    : file (libraryRoot.getChildFile (folderName).getChildFile (fileName))
{
    read();
}

void PlanList::rememberFileState()
{
    lastModified = file.getLastModificationTime();
    lastSize = file.existsAsFile() ? file.getSize() : -1;
}

void PlanList::read()
{
    plans.clear();
    rememberFileState();

    auto xml = juce::parseXML (file);

    if (xml == nullptr || ! xml->hasTagName ("PLANS"))
        return;

    for (auto* e : xml->getChildWithTagNameIterator ("PLAN"))
    {
        SessionPlan p;
        p.id = e->getStringAttribute ("id");
        p.name = e->getStringAttribute ("name").trim();

        if (p.id.isEmpty())
            p.id = juce::Uuid().toDashedString();

        if (p.name.isEmpty())
            p.name = tr ("Plan");

        for (auto* s : e->getChildWithTagNameIterator ("SLOT"))
        {
            PlanSlot slot;
            slot.random = s->getBoolAttribute ("random");
            slot.topicId = s->getStringAttribute ("topic");
            slot.tag = s->getStringAttribute ("tag");
            slot.minutes = juce::jlimit (1, 240, s->getIntAttribute ("minutes", 10));

            if (slot.random || slot.topicId.isNotEmpty())
                p.slots.push_back (slot);
        }

        plans.push_back (std::move (p));
    }
}

bool PlanList::save()
{
    juce::XmlElement xml ("PLANS");

    for (auto& p : plans)
    {
        auto* e = xml.createNewChildElement ("PLAN");
        e->setAttribute ("id", p.id);
        e->setAttribute ("name", p.name);

        for (auto& slot : p.slots)
        {
            auto* s = e->createNewChildElement ("SLOT");
            s->setAttribute ("minutes", slot.minutes);

            if (slot.random)
            {
                s->setAttribute ("random", true);

                if (slot.tag.isNotEmpty())
                    s->setAttribute ("tag", slot.tag);
            }
            else
            {
                s->setAttribute ("topic", slot.topicId);
            }
        }
    }

    file.getParentDirectory().createDirectory();
    const auto ok = xml.writeTo (file);
    rememberFileState();
    return ok;
}

bool PlanList::reloadIfChanged()
{
    const auto size = file.existsAsFile() ? file.getSize() : -1;

    if (size == lastSize && file.getLastModificationTime() == lastModified)
        return false;

    read();
    return true;
}

const SessionPlan* PlanList::find (const juce::String& id) const
{
    for (auto& p : plans)
        if (p.id == id)
            return &p;

    return nullptr;
}

SessionPlan* PlanList::findMutable (const juce::String& id)
{
    return const_cast<SessionPlan*> (find (id));
}

const SessionPlan* PlanList::findByName (const juce::String& name) const
{
    for (auto& p : plans)
        if (p.name.equalsIgnoreCase (name.trim()))
            return &p;

    return nullptr;
}

juce::String PlanList::uniqueName (const juce::String& base) const
{
    const auto clean = base.trim().isNotEmpty() ? base.trim() : tr ("Plan");

    if (findByName (clean) == nullptr)
        return clean;

    for (int i = 2;; ++i)
        if (findByName (clean + " " + juce::String (i)) == nullptr)
            return clean + " " + juce::String (i);
}

juce::String PlanList::add (const juce::String& name, std::vector<PlanSlot> slots)
{
    if (name.trim().isEmpty())
        return {};

    SessionPlan p;
    p.id = juce::Uuid().toDashedString();
    p.name = name.trim();
    p.slots = std::move (slots);
    plans.push_back (p);
    save();
    return p.id;
}

bool PlanList::setSlots (const juce::String& id, std::vector<PlanSlot> slots)
{
    auto* p = findMutable (id);

    if (p == nullptr)
        return false;

    p->slots = std::move (slots);
    return save();
}

bool PlanList::rename (const juce::String& id, const juce::String& name)
{
    auto* p = findMutable (id);

    if (p == nullptr || name.trim().isEmpty())
        return false;

    p->name = name.trim();
    return save();
}

juce::String PlanList::duplicate (const juce::String& id)
{
    const auto* p = find (id);

    if (p == nullptr)
        return {};

    const auto slots = p->slots;   // add() may reallocate `plans`
    return add (uniqueName (p->name + " (copy)"), slots);
}

bool PlanList::remove (const juce::String& id)
{
    const auto before = plans.size();
    plans.erase (std::remove_if (plans.begin(), plans.end(), [&] (const SessionPlan& p) { return p.id == id; }), plans.end());
    return plans.size() != before && save();
}

//==============================================================================
std::vector<SessionSlot> slotsFromPlan (const SessionPlan& plan, const TopicTitleFn& titleOf,
                                        const CandidatesForTagFn& candidatesFor, juce::Random& random)
{
    std::vector<SessionSlot> slots;
    juce::StringArray used;

    // Fixed topics first, so a random slot never picks one the plan already has.
    for (auto& p : plan.slots)
        if (! p.random)
            used.add (p.topicId);

    for (auto& p : plan.slots)
    {
        SessionSlot s;
        s.minutes = p.minutes;
        s.random = p.random;
        s.tag = p.tag;

        if (p.random)
        {
            const auto candidates = candidatesFor ? candidatesFor (p.tag) : std::vector<PlanCandidate> {};
            const auto pick = pickWeighted (candidates, random, used);

            if (pick >= 0)
            {
                s.topicId = candidates[(size_t) pick].topicId;
                s.title = candidates[(size_t) pick].title;
                used.add (s.topicId);
            }
            else
            {
                s.missing = true;
                s.title = p.tag.isNotEmpty() ? tr ("No #{tag} rotation topic left", p.tag) : tr ("No rotation topic left");
            }
        }
        else
        {
            s.topicId = p.topicId;
            s.title = titleOf ? titleOf (p.topicId) : juce::String();
            s.missing = s.title.isEmpty();

            if (s.missing)
                s.title = tr ("Missing topic (deleted)");
        }

        slots.push_back (s);
    }

    return slots;
}

std::vector<PlanSlot> planSlotsFrom (const std::vector<SessionSlot>& slots)
{
    std::vector<PlanSlot> result;

    for (auto& s : slots)
    {
        PlanSlot p;
        p.minutes = s.minutes;
        p.random = s.random;

        if (s.random)
            p.tag = s.tag;
        else
            p.topicId = s.topicId;

        if (p.random || p.topicId.isNotEmpty())
            result.push_back (p);
    }

    return result;
}

std::vector<SessionSlot> playableSlots (const std::vector<SessionSlot>& slots)
{
    std::vector<SessionSlot> result;

    for (auto& s : slots)
        if (! s.missing && s.topicId.isNotEmpty())
            result.push_back (s);

    return result;
}

} // namespace pv
