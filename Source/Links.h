#pragma once

#include "Library.h"

namespace pv
{

/** The Links tab's bookmarks to outside references (a site about the circle of fifths, a scale finder...).

    Kept in <library>/.links/links.xml, so every device using the same library folder shares them. Each link is a
    Resource with a URL (title, description, tags), in the user's own order. Changes made on another device are
    picked up with reloadIfChanged(); call it before editing so a stale list doesn't overwrite them.
*/
class LinkList
{
public:
    explicit LinkList (const juce::File& libraryRoot);

    const juce::File& getFile() const noexcept                 { return file; }
    const std::vector<Resource>& getLinks() const noexcept     { return links; }

    /** Re-reads the file if it changed on disk since the last read or write; true if it did. */
    bool reloadIfChanged();

    Resource* find (const juce::String& id);

    /** Appends a link (URL normalised, title from the URL when empty) and saves; nullptr for an empty URL. */
    Resource* add (const juce::String& title, const juce::String& url, const juce::StringArray& tags,
                   const juce::String& description);

    bool remove (const juce::String& id);
    bool move (const juce::String& id, int delta);

    /** Writes the list (after changing a link found with find()). */
    bool save();

    /** Every tag used by a link, sorted, first spelling kept. */
    juce::StringArray getAllTags() const;

    static constexpr const char* folderName = ".links";
    static constexpr const char* fileName = "links.xml";

private:
    void read();
    void rememberFileState();

    juce::File file;
    std::vector<Resource> links;
    juce::Time lastModified;
    juce::int64 lastSize = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkList)
};

} // namespace pv
