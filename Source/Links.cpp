#include "Links.h"

namespace pv
{

LinkList::LinkList (const juce::File& libraryRoot)
    : file (libraryRoot.getChildFile (folderName).getChildFile (fileName))
{
    read();
}

void LinkList::rememberFileState()
{
    lastModified = file.getLastModificationTime();
    lastSize = file.existsAsFile() ? file.getSize() : -1;
}

void LinkList::read()
{
    links.clear();
    rememberFileState();

    auto xml = juce::parseXML (file);

    if (xml == nullptr || ! xml->hasTagName ("LINKS"))
        return;

    for (auto* e : xml->getChildWithTagNameIterator ("LINK"))
    {
        Resource r;
        r.id = e->getStringAttribute ("id");
        r.url = e->getStringAttribute ("url");
        r.title = e->getStringAttribute ("title");
        r.tags = parseTags (e->getStringAttribute ("tags"));
        r.added = juce::Time::fromISO8601 (e->getStringAttribute ("added"));

        if (auto* d = e->getChildByName ("DESCRIPTION"))
            r.description = d->getAllSubText();

        if (r.url.isEmpty())
            continue;

        if (r.id.isEmpty())
            r.id = juce::Uuid().toDashedString();

        if (r.title.isEmpty())
            r.title = Library::titleFromUrl (r.url);

        links.push_back (std::move (r));
    }
}

bool LinkList::reloadIfChanged()
{
    const auto size = file.existsAsFile() ? file.getSize() : -1;

    if (size == lastSize && file.getLastModificationTime() == lastModified)
        return false;

    read();
    return true;
}

Resource* LinkList::find (const juce::String& id)
{
    for (auto& r : links)
        if (r.id == id)
            return &r;

    return nullptr;
}

Resource* LinkList::add (const juce::String& title, const juce::String& url, const juce::StringArray& tags,
                         const juce::String& description)
{
    const auto clean = normaliseUrl (url);

    if (clean.isEmpty())
        return nullptr;

    Resource r;
    r.id = juce::Uuid().toDashedString();
    r.url = clean;
    r.title = title.trim().isNotEmpty() ? title.trim() : Library::titleFromUrl (clean);
    r.tags = tags;
    r.description = description;
    r.added = juce::Time::getCurrentTime();
    links.push_back (r);
    save();
    return find (r.id);
}

bool LinkList::remove (const juce::String& id)
{
    const auto before = links.size();
    links.erase (std::remove_if (links.begin(), links.end(), [&] (const Resource& r) { return r.id == id; }), links.end());
    return links.size() != before && save();
}

bool LinkList::move (const juce::String& id, int delta)
{
    for (size_t i = 0; i < links.size(); ++i)
    {
        if (links[i].id != id)
            continue;

        const auto target = (int) i + delta;

        if (! juce::isPositiveAndBelow (target, (int) links.size()))
            return false;

        std::swap (links[i], links[(size_t) target]);
        return save();
    }

    return false;
}

bool LinkList::save()
{
    juce::XmlElement xml ("LINKS");

    for (auto& r : links)
    {
        auto* e = xml.createNewChildElement ("LINK");
        e->setAttribute ("id", r.id);
        e->setAttribute ("url", r.url);
        e->setAttribute ("title", r.title);
        e->setAttribute ("added", r.added.toISO8601 (true));

        if (! r.tags.isEmpty())
            e->setAttribute ("tags", joinTags (r.tags));

        if (r.description.isNotEmpty())
            e->createNewChildElement ("DESCRIPTION")->addTextElement (r.description);
    }

    file.getParentDirectory().createDirectory();
    const auto ok = xml.writeTo (file);
    rememberFileState();
    return ok;
}

juce::StringArray LinkList::getAllTags() const
{
    juce::StringArray tags;

    for (auto& r : links)
        for (auto& tag : r.tags)
            if (! hasTag (tags, tag))
                tags.add (tag);

    tags.sortNatural();
    return tags;
}

} // namespace pv
