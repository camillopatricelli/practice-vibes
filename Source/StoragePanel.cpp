#include "StoragePanel.h"
#include "Stems.h"
#include "Take.h"
#include "AppPaths.h"
#include "Theme.h"
#include "Localisation.h"

namespace pv
{

namespace
{
    /** Total size of the files in a folder (recursively), leaving out the `excluded` folders. */
    juce::int64 folderBytes (const juce::File& folder, const juce::Array<juce::File>& excluded = {})
    {
        juce::int64 total = 0;

        if (! folder.isDirectory())
            return 0;

        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories))
        {
            const auto& f = entry.getFile();

            if (entry.isDirectory())
            {
                if (! excluded.contains (f))
                    total += folderBytes (f, excluded);
            }
            else
            {
                total += entry.getFileSize();
            }
        }

        return total;
    }

    juce::String formatLength (double seconds)
    {
        const auto s = juce::roundToInt (seconds);
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}

//==============================================================================
std::vector<int> StorageReport::olderThan (StorageItem::Kind kind, juce::RelativeTime age, juce::Time now) const
{
    std::vector<int> result;

    for (int i = 0; i < (int) items.size(); ++i)
        if (items[(size_t) i].kind == kind && (age.inSeconds() <= 0.0 || items[(size_t) i].when < now - age))
            result.push_back (i);

    return result;
}

std::vector<StorageItem> StorageScan::listTakes (const Library& library)
{
    std::vector<StorageItem> takes;

    for (auto& topic : library.getTopics())
        for (auto& r : topic.resources)
            if (r.isTake)
            {
                StorageItem item;
                item.file = topic.getFile (r);
                item.title = r.title;
                item.detail = topic.title;
                item.topicId = topic.id;
                item.resourceId = r.id;
                takes.push_back (item);
            }

    for (auto& f : library.getInbox())
        if (TakeInfo::infoFileFor (f).existsAsFile())
        {
            StorageItem item;
            item.file = f;
            item.title = f.getFileNameWithoutExtension();
            item.detail = tr ("Inbox");
            takes.push_back (item);
        }

    return takes;
}

StorageReport StorageScan::run (const juce::File& libraryRoot, std::vector<StorageItem> takes,
                                const juce::File& loopsFolder, const juce::File& appDataFolder)
{
    StorageReport report;
    report.libraryBytes = folderBytes (libraryRoot);
    report.practiceLogBytes = folderBytes (libraryRoot.getChildFile (".practice"));

    for (auto& take : takes)
    {
        take.kind = StorageItem::Kind::take;
        take.bytes = take.file.getSize();
        take.when = take.file.getCreationTime();
        juce::StringArray detail { take.detail };

        if (const auto info = TakeInfo::load (take.file))
        {
            if (info->recorded != juce::Time())
                take.when = info->recorded;

            if (info->lengthSeconds > 0.0)
                detail.add (formatLength (info->lengthSeconds));
        }

        detail.add (take.when.formatted ("%d %b %Y, %H:%M"));
        take.detail = detail.joinIntoString ("  -  ");
        report.takeBytes += take.bytes;

        if (take.file.existsAsFile())
            report.items.push_back (take);
    }

    std::stable_sort (report.items.begin(), report.items.end(), [] (const StorageItem& a, const StorageItem& b) { return a.when > b.when; });

    // Saved loop sessions, newest first.
    std::vector<StorageItem> loops;

    if (loopsFolder.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (loopsFolder, false, "*", juce::File::findDirectories))
        {
            StorageItem item;
            item.kind = StorageItem::Kind::loops;
            item.file = entry.getFile();
            item.title = item.file.getFileName();
            item.bytes = folderBytes (item.file);
            item.when = item.file.getLastModificationTime();
            item.detail = tr ("Last changed {date}", i18n::date (item.when, "%d %b %Y, %H:%M"));
            report.loopBytes += item.bytes;
            loops.push_back (item);
        }

    std::stable_sort (loops.begin(), loops.end(), [] (const StorageItem& a, const StorageItem& b) { return a.when > b.when; });
    report.items.insert (report.items.end(), loops.begin(), loops.end());

    // Stems: "<folder>/.stems/<song file>" in the library (top level = Inbox, and each topic), and the app's Stems folder.
    {
        juce::Array<juce::File> stemParents;

        if (libraryRoot.isDirectory())
        {
            stemParents.add (libraryRoot.getChildFile (Stems::folderName));

            for (const auto& entry : juce::RangedDirectoryIterator (libraryRoot, false, "*", juce::File::findDirectories))
                stemParents.add (entry.getFile().getChildFile (Stems::folderName));
        }

        stemParents.add (appDataFolder.getChildFile ("Stems"));
        std::vector<StorageItem> stems;

        for (const auto& parent : stemParents)
        {
            if (! parent.isDirectory())
                continue;

            for (const auto& entry : juce::RangedDirectoryIterator (parent, false, "*", juce::File::findDirectories))
            {
                StorageItem item;
                item.kind = StorageItem::Kind::stems;
                item.file = entry.getFile();
                item.bytes = folderBytes (item.file);
                item.when = Stems::infoFile (item.file).getLastModificationTime();
                auto title = item.file.getFileName();

                if (auto xml = juce::XmlDocument::parse (Stems::infoFile (item.file)))
                    title = xml->getStringAttribute ("song", title);

                item.title = title;
                const auto where = parent.getParentDirectory();
                item.detail = (where == libraryRoot ? tr ("Inbox") : where == appDataFolder ? tr ("not in the library") : where.getFileName())
                              + "  -  " + (Stems::isComplete (item.file) ? tr ("separated {date}", i18n::date (item.when, "%d %b %Y")) : tr ("incomplete"));
                report.stemBytes += item.bytes;
                stems.push_back (item);
            }
        }

        std::stable_sort (stems.begin(), stems.end(), [] (const StorageItem& a, const StorageItem& b) { return a.when > b.when; });
        report.items.insert (report.items.end(), stems.begin(), stems.end());
    }

    // The app's own folder (on the iPad it also holds the library and the loops: those are counted above).
    report.appDataBytes = folderBytes (appDataFolder, { libraryRoot, loopsFolder });
    const auto webCache = appDataFolder.getChildFile ("WebView2");
    report.webCacheBytes = folderBytes (webCache);

    if (report.webCacheBytes > 0)
    {
        StorageItem item;
        item.kind = StorageItem::Kind::webCache;
        item.file = webCache;
        item.title = tr ("Web viewer cache");
        item.detail = clearWebCacheMarker (appDataFolder).existsAsFile()
                          ? tr ("will be cleared the next time the app starts")
                          : tr ("YouTube and documents viewer; cleared the next time the app starts");
        item.bytes = report.webCacheBytes;
        item.when = juce::Time::getCurrentTime();
        report.items.push_back (item);
    }

    return report;
}

juce::String StorageScan::formatBytes (juce::int64 bytes)
{
    if (bytes < 1024 * 1024)
        return juce::String (juce::jmax<juce::int64> (bytes > 0 ? 1 : 0, bytes / 1024)) + " KB";

    if (bytes < (juce::int64) 1024 * 1024 * 1024)
    {
        const auto mb = (double) bytes / (1024.0 * 1024.0);
        return (mb < 10.0 ? i18n::decimal (mb, 1) : juce::String (juce::roundToInt (mb))) + " MB";
    }

    return i18n::decimal ((double) bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
}

juce::File StorageScan::clearWebCacheMarker (const juce::File& appDataFolder)
{
    return appDataFolder.getChildFile ("clear-web-cache.txt");
}

void StorageScan::clearWebCacheIfAsked (const juce::File& appDataFolder)
{
    const auto marker = clearWebCacheMarker (appDataFolder);

    if (! marker.existsAsFile())
        return;

    const auto ok = appDataFolder.getChildFile ("WebView2").deleteRecursively();
    marker.deleteFile();
    juce::Logger::writeToLog (ok ? "Cleared the web viewer cache" : "Could not clear all of the web viewer cache");
}

//==============================================================================
class StoragePanel::List final : public juce::ListBox,
                                 private juce::ListBoxModel
{
public:
    explicit List (StoragePanel& o) : owner (o)
    {
        setModel (this);
        setRowHeight (48);
        setColour (juce::ListBox::backgroundColourId, theme::panel());
    }

    void rebuild()
    {
        rows.clear();
        auto lastKind = -1;

        for (int i = 0; i < (int) owner.report.items.size(); ++i)
        {
            const auto kind = (int) owner.report.items[(size_t) i].kind;

            if (kind != lastKind)
            {
                rows.push_back (-1 - kind);     // a group header
                lastKind = kind;
            }

            rows.push_back (i);
        }

        updateContent();
        repaint();
    }

private:
    int getNumRows() override   { return (int) rows.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()))
            return;

        const auto index = rows[(size_t) row];
        auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);

        if (index < 0)
        {
            const auto kind = (StorageItem::Kind) (-1 - index);
            const auto text = kind == StorageItem::Kind::take ? tr ("Takes")
                            : kind == StorageItem::Kind::loops ? tr ("Saved loops")
                            : kind == StorageItem::Kind::stems ? tr ("Stems (they can be made again)") : tr ("Caches");
            g.setColour (theme::text().withAlpha (0.9f));
            g.setFont (theme::headerFont());
            g.drawText (text, r.withTrimmedTop (14), juce::Justification::centredLeft, true);
            return;
        }

        const auto& item = owner.report.items[(size_t) index];
        const auto on = owner.selected[(size_t) index];

        auto box = r.removeFromLeft (26).withSizeKeepingCentre (20, 20).toFloat();
        g.setColour (on ? theme::accent() : theme::text().withAlpha (0.35f));

        if (on)
            g.fillRoundedRectangle (box, 4.0f);
        else
            g.drawRoundedRectangle (box, 4.0f, 1.5f);

        if (on)
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 4.5f, box.getCentreY());
            tick.lineTo (box.getX() + 8.5f, box.getBottom() - 5.0f);
            tick.lineTo (box.getRight() - 4.0f, box.getY() + 5.0f);
            g.setColour (theme::onAccent());
            g.strokePath (tick, juce::PathStrokeType (2.2f));
        }

        r.removeFromLeft (8);
        g.setFont (juce::FontOptions (15.0f));
        g.setColour (theme::text().withAlpha (0.85f));
        g.drawText (StorageScan::formatBytes (item.bytes), r.removeFromRight (90), juce::Justification::centredRight, true);
        g.setColour (theme::text().withAlpha (0.92f));
        g.drawText (item.title, r.withTrimmedBottom (height / 2 - 2), juce::Justification::bottomLeft, true);
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (theme::text().withAlpha (0.55f));
        g.drawText (item.detail, r.withTrimmedTop (height / 2 + 2), juce::Justification::topLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row] < 0)
            return;

        const auto index = (size_t) rows[(size_t) row];
        owner.selected[index] = ! owner.selected[index];
        repaintRow (row);
        owner.updateSummary();
    }

    StoragePanel& owner;
    std::vector<int> rows;      // item index, or -1 - kind for a group header
};

//==============================================================================
StoragePanel::StoragePanel (Library* lib, juce::File loops, juce::File appData)
    : library (lib), loopsFolder (std::move (loops)), appDataFolder (std::move (appData))
{
    getProperties().set ("fillOverlay", true);

    totalsLabel.setFont (juce::FontOptions (16.0f));
    totalsLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.92f));
    totalsLabel.setJustificationType (juce::Justification::topLeft);
    totalsLabel.setText (tr ("Adding up the space used..."), juce::dontSendNotification);
    addAndMakeVisible (totalsLabel);

    selectBox.setTextWhenNothingSelected (tr ("Quick select..."));
    selectBox.addItem (tr ("Takes older than a week"), 1);
    selectBox.addItem (tr ("Takes older than a month"), 2);
    selectBox.addItem (tr ("Takes older than 3 months"), 3);
    selectBox.addItem (tr ("All takes"), 4);
    selectBox.addSeparator();
    selectBox.addItem (tr ("Saved loops older than a month"), 5);
    selectBox.addItem (tr ("All saved loops"), 6);
    selectBox.addSeparator();
    selectBox.addItem (tr ("All stems"), 8);
    selectBox.addSeparator();
    selectBox.addItem (tr ("Nothing"), 7);
    selectBox.onChange = [this]
    {
        const auto id = selectBox.getSelectedId();
        const auto now = juce::Time::getCurrentTime();
        using K = StorageItem::Kind;
        std::vector<int> pick;

        switch (id)
        {
            case 1: pick = report.olderThan (K::take, juce::RelativeTime::weeks (1), now); break;
            case 2: pick = report.olderThan (K::take, juce::RelativeTime::days (30), now); break;
            case 3: pick = report.olderThan (K::take, juce::RelativeTime::days (91), now); break;
            case 4: pick = report.olderThan (K::take, {}, now); break;
            case 5: pick = report.olderThan (K::loops, juce::RelativeTime::days (30), now); break;
            case 6: pick = report.olderThan (K::loops, {}, now); break;
            case 7: std::fill (selected.begin(), selected.end(), false); break;
            case 8: pick = report.olderThan (K::stems, {}, now); break;
            default: break;
        }

        for (auto i : pick)
            selected[(size_t) i] = true;

        selectBox.setSelectedId (0, juce::dontSendNotification);
        list->repaint();
        updateSummary();
    };
    addAndMakeVisible (selectBox);

    selectionLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.75f));
    selectionLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (selectionLabel);

    removeButton.onClick = [this] { removeSelected(); };
    addAndMakeVisible (removeButton);

    emptyLabel.setText (tr ("Nothing to clean up: no takes, saved loops, stems or caches."), juce::dontSendNotification);
    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.55f));
    addChildComponent (emptyLabel);

    list = std::make_unique<List> (*this);
    addAndMakeVisible (*list);

    updateSummary();
    rescan();
}

StoragePanel::~StoragePanel()
{
    alive.reset();
}

void StoragePanel::rescan()
{
    auto takes = library != nullptr ? StorageScan::listTakes (*library) : std::vector<StorageItem>();
    const auto root = library != nullptr ? library->getRoot() : juce::File();
    std::weak_ptr<bool> weakAlive = alive;

    juce::Thread::launch ([this, weakAlive, takes, root, loops = loopsFolder, appData = appDataFolder]
    {
        auto result = StorageScan::run (root, takes, loops, appData);

        juce::MessageManager::callAsync ([this, weakAlive, result]
        {
            if (! weakAlive.expired())
                scanned (result);
        });
    });
}

void StoragePanel::scanned (StorageReport newReport)
{
    report = std::move (newReport);
    selected.assign (report.items.size(), false);

    juce::StringArray lines;

    if (library != nullptr)
        lines.add (tr ("Library: {total}   (takes {takes}, stems {stems}, practice log {log}, the rest are your songs and documents)",
                       StorageScan::formatBytes (report.libraryBytes), StorageScan::formatBytes (report.takeBytes),
                       StorageScan::formatBytes (report.stemBytes), StorageScan::formatBytes (report.practiceLogBytes)));

    lines.add (tr ("Saved loops: {loops}      App data: {appData}", StorageScan::formatBytes (report.loopBytes), StorageScan::formatBytes (report.appDataBytes))
               + (report.webCacheBytes > 0 ? "   " + tr ("(web viewer cache {size})", StorageScan::formatBytes (report.webCacheBytes)) : juce::String()));
    lines.add (tr ("Your songs and documents are never offered for removal. Tap items below to select them."));

    totalsLabel.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
    emptyLabel.setVisible (report.items.empty());
    list->rebuild();
    updateSummary();
}

void StoragePanel::updateSummary()
{
    int count = 0;
    juce::int64 bytes = 0;

    for (size_t i = 0; i < selected.size() && i < report.items.size(); ++i)
        if (selected[i])
        {
            ++count;
            bytes += report.items[i].bytes;
        }

    selectionLabel.setText (count == 0 ? tr ("Nothing selected")
                                       : tr ("{count} selected, {size}", count, StorageScan::formatBytes (bytes)),
                            juce::dontSendNotification);
    removeButton.setEnabled (count > 0);
}

void StoragePanel::removeSelected()
{
    int count = 0;

    for (auto s : selected)
        count += s ? 1 : 0;

    if (count == 0)
        return;

   #if JUCE_IOS
    const juce::String where = tr ("They are deleted for good (there's no bin on the iPad).");
   #else
    const juce::String where = tr ("They go to the Recycle Bin.");
   #endif

    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::QuestionIcon)
                             .withTitle (trn ("Remove {n} item?", "Remove {n} items?", count))
                             .withMessage (where)
                             .withButton (tr ("Remove"), 1)
                             .withButton (tr ("Cancel"), 0)
                             .withAssociatedComponent (this);
    std::weak_ptr<bool> weakAlive = alive;

    juce::AlertWindow::showAsync (options, [this, weakAlive] (int result)
    {
        if (weakAlive.expired() || result != 1)
            return;

        int failed = 0;

        for (size_t i = 0; i < report.items.size(); ++i)
        {
            if (! selected[i])
                continue;

            const auto& item = report.items[i];
            auto ok = true;

            switch (item.kind)
            {
                case StorageItem::Kind::take:
                {
                    auto* topic = library != nullptr && item.topicId.isNotEmpty() ? library->findTopic (item.topicId) : nullptr;

                    if (topic != nullptr && topic->findResource (item.resourceId) != nullptr)
                    {
                        ok = library->removeResource (*topic, item.resourceId);
                    }
                    else
                    {
                        ok = Library::moveToBin (item.file);
                        TakeInfo::infoFileFor (item.file).deleteFile();
                    }
                    break;
                }

                case StorageItem::Kind::loops:
                case StorageItem::Kind::stems:
                    ok = Library::moveToBin (item.file);
                    break;

                case StorageItem::Kind::webCache:
                    ok = StorageScan::clearWebCacheMarker (appDataFolder).replaceWithText ("The web viewer cache is deleted at the next start.\n");
                    break;
            }

            failed += ok ? 0 : 1;
        }

        juce::Logger::writeToLog ("Storage clean-up: removed " + juce::String (juce::jmax (0, (int) std::count (selected.begin(), selected.end(), true) - failed))
                                  + " item(s)" + (failed > 0 ? ", " + juce::String (failed) + " could not be removed" : juce::String()));

        if (onRemoved)
            onRemoved();

        rescan();
    });
}

//==============================================================================
void StoragePanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
}

void StoragePanel::resized()
{
    auto area = getLocalBounds().reduced (8);
    totalsLabel.setBounds (area.removeFromTop (80));
    area.removeFromTop (6);

    auto bottom = area.removeFromBottom (48).reduced (0, 4);
    removeButton.setBounds (bottom.removeFromRight (180).reduced (2, 0));
    selectBox.setBounds (bottom.removeFromLeft (juce::jmin (300, bottom.getWidth() / 2)).reduced (2, 2));
    selectionLabel.setBounds (bottom.reduced (12, 0));

    area.removeFromBottom (6);
    list->setBounds (area);
    emptyLabel.setBounds (area);
}

} // namespace pv
