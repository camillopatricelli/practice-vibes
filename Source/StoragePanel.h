#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Library.h"
#include "Localisation.h"

namespace pv
{

/** What the clean-up overlay can offer for removal. */
struct StorageItem
{
    enum class Kind { take, loops, stems, webCache };

    Kind kind = Kind::take;
    juce::File file;                    // the take's audio file, the loop session's / song's stems folder, the cache folder
    juce::String title, detail;
    juce::Time when;
    juce::int64 bytes = 0;
    juce::String topicId, resourceId;   // takes inside a topic (removed through the library)
};

/** Sizes of everything the app keeps, and the items that can be removed. Built by StorageScan::run. */
struct StorageReport
{
    juce::int64 libraryBytes = 0, takeBytes = 0, practiceLogBytes = 0, loopBytes = 0, stemBytes = 0, appDataBytes = 0, webCacheBytes = 0;
    std::vector<StorageItem> items;     // takes (newest first), loop sessions, stems, then the web cache

    /** Indices of the takes / loop sessions older than `age` (relative to `now`); age 0 = all of them. */
    std::vector<int> olderThan (StorageItem::Kind, juce::RelativeTime age, juce::Time now) const;
};

namespace StorageScan
{
    /** Takes known to the library (message thread: reads the library). Sizes are filled in by run(). */
    std::vector<StorageItem> listTakes (const Library&);

    /** Walks the folders and adds up sizes (any thread: only touches the file system). */
    StorageReport run (const juce::File& libraryRoot, std::vector<StorageItem> takes,
                       const juce::File& loopsFolder, const juce::File& appDataFolder);

    juce::String formatBytes (juce::int64);

    /** The web view's cache can't be deleted while it's in use: it is cleared at the next start instead. */
    juce::File clearWebCacheMarker (const juce::File& appDataFolder);
    void clearWebCacheIfAsked (const juce::File& appDataFolder);    // call at start-up, before any web view exists
}

/** Overlay: how much space the app uses, and a list of takes, saved loops and caches to remove (to the bin). */
class StoragePanel final : public juce::Component
{
public:
    StoragePanel (Library*, juce::File loopsFolder, juce::File appDataFolder);
    ~StoragePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onRemoved;    // something was removed: the library should rescan

private:
    class List;

    void rescan();
    void scanned (StorageReport);
    void updateSummary();
    void removeSelected();

    Library* library;
    juce::File loopsFolder, appDataFolder;
    StorageReport report;
    std::vector<bool> selected;

    juce::Label totalsLabel, selectionLabel, emptyLabel;
    juce::ComboBox selectBox;
    juce::TextButton removeButton { tr ("Remove selected") };
    std::unique_ptr<List> list;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace pv
