#include "LibraryPanel.h"
#include "ResourceWidgets.h"
#include "AppPaths.h"
#include "FolderAccess.h"
#include "LoopFiles.h"
#include "StoragePanel.h"
#include "Theme.h"
#include "WebView.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace pv
{

namespace
{
    const juce::String inboxId = "inbox";

    juce::String formatDate (juce::Time t)
    {
        return t == juce::Time() ? juce::String() : t.formatted ("%d %b %Y");
    }

    /** Files the in-app viewer can show on this platform (others open in their own app on desktop). */
    bool viewerCanShow (const juce::File& f)
    {
        if (! webViewAvailable())
            return false;

        const auto ext = f.getFileExtension().toLowerCase();

       #if JUCE_IOS
        // WKWebView shows PDFs, images, plain text, Office/iWork documents and video.
        return classifyFileName (f.getFileName()) != ResourceKind::audio && ext != ".gdoc" && ext != ".gsheet" && ext != ".gslides";
       #else
        return juce::StringArray { ".pdf", ".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".svg",
                                   ".txt", ".htm", ".html", ".mp4", ".webm", ".m4v" }.contains (ext);
       #endif
    }

}

//==============================================================================
/** Title, created date, notes and the resource list of one topic. Files can be dropped onto it (desktop). */
class LibraryPanel::TopicView final : public juce::Component,
                                      public juce::FileDragAndDropTarget
{
public:
    explicit TopicView (LibraryPanel& p) : owner (p)
    {
        titleEditor.setFont (juce::FontOptions (22.0f).withStyle ("Bold"));
        titleEditor.setTextToShowWhenEmpty ("Topic title", theme::text().withAlpha (0.35f));
        titleEditor.setJustification (juce::Justification::centredLeft);
        styleEditor (titleEditor);
        titleEditor.onTextChange = [this] { edited(); };
        titleEditor.onReturnKey = [this] { descriptionEditor.grabKeyboardFocus(); };
        titleEditor.onFocusLost = [this] { owner.titleSettled (topicId); };
        addAndMakeVisible (titleEditor);

        tagsEditor.setTextToShowWhenEmpty ("Tags, separated by commas (e.g. technique, jazz, lesson)", theme::text().withAlpha (0.35f));
        tagsEditor.setFont (juce::FontOptions (14.0f));
        styleEditor (tagsEditor);
        tagsEditor.onTextChange = [this] { edited(); };
        tagsEditor.onReturnKey = [this] { descriptionEditor.grabKeyboardFocus(); };
        tagsEditor.onFocusLost = [this]
        {
            owner.flush();


            // Show the cleaned-up list (duplicates and stray commas removed).
            if (auto* t = owner.library->findTopic (topicId))
                tagsEditor.setText (joinTags (t->tags), false);
        };
        addAndMakeVisible (tagsEditor);

        createdLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.55f));
        addAndMakeVisible (createdLabel);

        deleteButton.onClick = [this] { owner.deleteTopic (topicId); };
        addAndMakeVisible (deleteButton);

        practiseButton.onClick = [this] { if (owner.onPractise) owner.onPractise (topicId); };
        practiseButton.setTooltip ("Make this the topic you're practising, and bring back the song, loop, speed and tempo you used last time");
        practiseButton.setColour (juce::TextButton::buttonColourId, theme::accent());
        addAndMakeVisible (practiseButton);

        historyButton.onClick = [this] { if (owner.onShowHistory) owner.onShowHistory (topicId); };
        historyButton.setTooltip ("Tempo, song speed and minutes over the weeks, and every session");
        addAndMakeVisible (historyButton);

        rotationToggle.setTooltip ("Topics in the rotation are listed on the Today tab, most neglected first");
        rotationToggle.onClick = [this] { owner.setRotation (topicId, rotationToggle.getToggleState()); };
        addAndMakeVisible (rotationToggle);

        noGuitarToggle.setTooltip ("For ear training, theory...: while you practise this topic the time is counted on the clock "
                                   "(with Pause on the Today tab) instead of from the guitar input");
        noGuitarToggle.onClick = [this] { owner.setNoGuitar (topicId, noGuitarToggle.getToggleState()); };
        addAndMakeVisible (noGuitarToggle);

        addAndMakeVisible (styleCaption (notesLabel, "Notes"));
        descriptionEditor.setMultiLine (true, true);
        descriptionEditor.setReturnKeyStartsNewLine (true);
        descriptionEditor.setScrollbarsShown (true);
        descriptionEditor.setFont (juce::FontOptions (15.0f));
        descriptionEditor.setTextToShowWhenEmpty ("What to practise, how, lesson notes...", theme::text().withAlpha (0.35f));
        styleEditor (descriptionEditor);
        descriptionEditor.onTextChange = [this] { edited(); };
        descriptionEditor.onFocusLost = [this] { owner.flush(); };
        addAndMakeVisible (descriptionEditor);

        conflictLabel.setColour (juce::Label::textColourId, theme::highlight());
        conflictLabel.setMinimumHorizontalScale (0.6f);
        addChildComponent (conflictLabel);
        conflictButton.setTooltip ("Shows each copy's title and when it was saved; the ones you don't keep go to the bin");
        conflictButton.onClick = [this] { owner.chooseTopicVersion (topicId, conflictButton); };
        addChildComponent (conflictButton);

        addAndMakeVisible (styleCaption (resourcesLabel, "Resources"));
        addFilesButton.onClick = [this] { owner.addFiles (topicId); };
        addLinkButton.onClick = [this] { owner.editResource (topicId, {}, true); };
        addFilesButton.setTooltip ("Copies documents, images or audio files into this topic's folder"
                                  #if ! JUCE_IOS
                                   " (you can also drop files here)"
                                  #endif
                                   );
        addLinkButton.setTooltip ("YouTube video, Google document or any web page");
        addAndMakeVisible (addFilesButton);
        addAndMakeVisible (addLinkButton);

        noResourcesLabel.setText ("No resources yet: add documents, audio files or links.", juce::dontSendNotification);
        noResourcesLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.45f));
        addChildComponent (noResourcesLabel);

        viewport.setViewedComponent (&rowHolder, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
    }

    juce::String topicId;

    void load (const Topic& topic, bool keepFocusedEditors)
    {
        const auto sameTopic = topic.id == topicId;
        topicId = topic.id;

        if (! (sameTopic && keepFocusedEditors && titleEditor.hasKeyboardFocus (true)) && titleEditor.getText() != topic.title)
            titleEditor.setText (topic.title, false);

        if (! (sameTopic && keepFocusedEditors && descriptionEditor.hasKeyboardFocus (true)) && descriptionEditor.getText() != topic.description)
            descriptionEditor.setText (topic.description, false);

        if (! (sameTopic && keepFocusedEditors && tagsEditor.hasKeyboardFocus (true)) && parseTags (tagsEditor.getText()) != topic.tags)
            tagsEditor.setText (joinTags (topic.tags), false);

        createdLabel.setText ("Created " + formatDate (topic.created), juce::dontSendNotification);
        rotationToggle.setToggleState (topic.inRotation, juce::dontSendNotification);
        noGuitarToggle.setToggleState (topic.noGuitar, juce::dontSendNotification);

        conflictLabel.setVisible (! topic.conflictFiles.isEmpty());
        conflictButton.setVisible (! topic.conflictFiles.isEmpty());
        conflictLabel.setText ("The cloud sync made " + juce::String (topic.conflictFiles.size() == 1 ? "an extra copy" : "extra copies")
                                   + " of this topic's file (" + topic.conflictFiles.joinIntoString (", ")
                                   + "). Choose which version to keep.",
                               juce::dontSendNotification);

        rows.clear();

        for (auto& r : topic.resources)
        {
            auto row = std::make_unique<ResourceRow> (r, topic.isMissing (r), "...", owner.tagFilter);
            const auto resourceId = r.id;
            row->onOpen = [this, resourceId]
            {
                if (auto* t = owner.library->findTopic (topicId))
                    if (auto* res = t->findResource (resourceId))
                        owner.openResource (*t, *res);
            };
            row->onMenu = [this, resourceId] (juce::Component& anchor) { owner.showResourceMenu (topicId, resourceId, anchor); };
            rowHolder.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }

        noResourcesLabel.setVisible (rows.empty());
        resized();
    }

    void focusTitle()
    {
        titleEditor.grabKeyboardFocus();
        titleEditor.selectAll();
    }

    void paint (juce::Graphics& g) override
    {
        if (dragOver)
        {
            g.setColour (theme::accent().withAlpha (0.25f));
            g.fillRoundedRectangle (viewport.getBounds().toFloat().expanded (4.0f), 8.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();

        auto top = area.removeFromTop (44);
        deleteButton.setBounds (top.removeFromRight (120).reduced (2, 4));
        practiseButton.setBounds (top.removeFromRight (100).reduced (2, 4));
        top.removeFromRight (8);
        createdLabel.setBounds (top.removeFromRight (150));
        titleEditor.setBounds (top.reduced (0, 2));

        area.removeFromTop (6);
        auto tagsRow = area.removeFromTop (30);
        historyButton.setBounds (tagsRow.removeFromRight (90).reduced (0, 1));
        rotationToggle.setBounds (tagsRow.removeFromRight (190).withTrimmedLeft (12));
        noGuitarToggle.setBounds (tagsRow.removeFromRight (220).withTrimmedLeft (12));
        tagsEditor.setBounds (tagsRow);
        area.removeFromTop (4);
        notesLabel.setBounds (area.removeFromTop (26));
        const auto notesHeight = juce::jlimit (110, 360, (int) (area.getHeight() * 0.38f));
        descriptionEditor.setBounds (area.removeFromTop (notesHeight));

        area.removeFromTop (6);

        if (conflictLabel.isVisible())
        {
            auto row = area.removeFromTop (28);
            conflictButton.setBounds (row.removeFromRight (150).reduced (2, 2));
            conflictLabel.setBounds (row);
        }

        auto header = area.removeFromTop (40);
        addLinkButton.setBounds (header.removeFromRight (120).reduced (2, 4));
        addFilesButton.setBounds (header.removeFromRight (120).reduced (2, 4));
        resourcesLabel.setBounds (header);

        area.removeFromTop (4);
        viewport.setBounds (area);
        noResourcesLabel.setBounds (area.removeFromTop (40));

        constexpr int rowHeight = 64, gap = 6;
        const auto width = viewport.getMaximumVisibleWidth() - (viewport.canScrollVertically() ? 4 : 0);
        rowHolder.setSize (juce::jmax (1, width), juce::jmax (1, (int) rows.size() * (rowHeight + gap)));

        for (size_t i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, (int) i * (rowHeight + gap), rowHolder.getWidth(), rowHeight);
    }

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray&) override    { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override  { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override             { dragOver = false; repaint(); }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver = false;
        repaint();

        juce::Array<juce::URL> urls;

        for (auto& f : files)
            if (juce::File (f).existsAsFile())
                urls.add (juce::URL (juce::File (f)));

        owner.addUrls (topicId, urls);
    }

private:
    void edited()
    {
        if (auto* t = owner.library->findTopic (topicId))
        {
            t->title = titleEditor.getText().trim();
            t->description = descriptionEditor.getText();
            t->tags = parseTags (tagsEditor.getText());
            owner.editedTopic();
        }
    }

    LibraryPanel& owner;
    juce::TextEditor titleEditor, tagsEditor, descriptionEditor;
    juce::Label createdLabel, notesLabel, resourcesLabel, conflictLabel, noResourcesLabel;
    juce::TextButton deleteButton { "Delete topic" }, addFilesButton { "Add files..." }, addLinkButton { "Add link..." },
                     practiseButton { "Practise" }, historyButton { "History" }, conflictButton { "Choose version..." };
    juce::ToggleButton rotationToggle { "In practice rotation" }, noGuitarToggle { "Timed without the guitar" };
    juce::Viewport viewport;
    juce::Component rowHolder;
    std::vector<std::unique_ptr<ResourceRow>> rows;
    bool dragOver = false;
};

//==============================================================================
/** Loose files at the top of the library folder, waiting to be moved into a topic. */
class LibraryPanel::InboxView final : public juce::Component
{
public:
    explicit InboxView (LibraryPanel& p) : owner (p)
    {
        addAndMakeVisible (styleCaption (header, "Inbox"));
        info.setText ("Files placed directly in the library folder (for example from another device). "
                      "Move each one into a topic.", juce::dontSendNotification);
        info.setColour (juce::Label::textColourId, theme::text().withAlpha (0.6f));
        info.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (info);
        viewport.setViewedComponent (&rowHolder, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
    }

    void load (const juce::Array<juce::File>& files)
    {
        rows.clear();

        for (auto& f : files)
        {
            Resource r;
            r.id = r.fileName = f.getFileName();
            r.title = f.getFileName();
            auto row = std::make_unique<ResourceRow> (r, false, "Move to topic...");
            row->onOpen = [this, f] { owner.openFile (f, f.getFileName()); };
            row->onMenu = [this, f] (juce::Component& anchor) { owner.showInboxMenu (f, anchor); };
            rowHolder.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }

        resized();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        header.setBounds (area.removeFromTop (30));
        info.setBounds (area.removeFromTop (44));
        viewport.setBounds (area);

        constexpr int rowHeight = 64, gap = 6;
        const auto width = viewport.getMaximumVisibleWidth();
        rowHolder.setSize (juce::jmax (1, width), juce::jmax (1, (int) rows.size() * (rowHeight + gap)));

        for (size_t i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, (int) i * (rowHeight + gap), rowHolder.getWidth(), rowHeight);
    }

private:
    LibraryPanel& owner;
    juce::Label header, info;
    juce::Viewport viewport;
    juce::Component rowHolder;
    std::vector<std::unique_ptr<ResourceRow>> rows;
};

//==============================================================================
/** In-app viewer for documents and web pages (WebView2 on Windows, WKWebView on iOS). */
class LibraryPanel::Viewer final : public juce::Component
{
public:
    Viewer (const juce::String& urlToShow, std::function<void()> openOutsideFn)
        : browser (webViewOptions()), openOutside (std::move (openOutsideFn))
    {
        getProperties().set ("fillOverlay", true);
        addAndMakeVisible (browser);

        if (openOutside != nullptr)
        {
            outsideButton.onClick = [this] { openOutside(); };
            addAndMakeVisible (outsideButton);
        }

        browser.goToURL (urlToShow);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        if (outsideButton.isVisible())
            outsideButton.setBounds (area.removeFromBottom (40).removeFromRight (220).reduced (2, 4));

        browser.setBounds (area);
    }

private:
    /** Links that want a new window open in the same view. */
    struct Browser final : public juce::WebBrowserComponent
    {
        using juce::WebBrowserComponent::WebBrowserComponent;
        void newWindowAttemptingToLoad (const juce::String& url) override    { goToURL (url); }
    };

    Browser browser;
    std::function<void()> openOutside;
    juce::TextButton outsideButton {
       #if JUCE_IOS
        "Open in Safari / app"
       #else
        "Open outside the app"
       #endif
    };
};

//==============================================================================
LibraryPanel::LibraryPanel()
{
    setupLabel.setJustificationType (juce::Justification::centred);
    setupLabel.setFont (juce::FontOptions (16.0f));
    // iPadOS only lets apps use whole folders from iCloud Drive (Google Drive and others are greyed out in the
    // picker), so iCloud Drive is the one way to share a library between an iPad/iPhone and another device.
    setupLabel.setText ("Where should Practice Vibes keep your topics?\n\n"
                       #if JUCE_IOS
                        "Keep them on this iPad, or pick a folder in iCloud Drive to share the same topics with "
                        "your other devices (on a Windows PC: install iCloud for Windows).\n"
                        "iCloud Drive is the only cloud drive that can keep topics in sync with an iPad or iPhone.",
                       #else
                        "Pick a folder on this computer, or one inside a cloud drive (OneDrive, Google Drive, "
                        "Dropbox, iCloud Drive) to share the same topics with your other computers.\n"
                        "To share them with an iPad or iPhone too, use iCloud Drive (install iCloud for Windows): "
                        "it is the only cloud drive iPadOS lets apps use for a whole folder.",
                       #endif
                        juce::dontSendNotification);
    addChildComponent (setupLabel);

    useDefaultButton.setButtonText ("Use " + LibraryLocation::getDefaultFolder().getFullPathName());
   #if JUCE_IOS
    useDefaultButton.setButtonText ("Keep them on this iPad");
   #endif
    useDefaultButton.onClick = [this]
    {
        LibraryLocation::save (LibraryLocation::getDefaultFolder());
        openLibrary (LibraryLocation::getDefaultFolder());
    };
    addChildComponent (useDefaultButton);

    chooseFolderButton.onClick = [this] { chooseFolder(); };
   #if JUCE_IOS
    chooseFolderButton.setButtonText ("Choose a folder in iCloud Drive");
   #endif
    addChildComponent (chooseFolderButton);

    searchBox.setTextToShowWhenEmpty ("Search topics", theme::text().withAlpha (0.4f));
    searchBox.setFont (juce::FontOptions (15.0f));
    searchBox.setJustification (juce::Justification::centredLeft);
    styleEditor (searchBox);
    searchBox.onTextChange = [this] { rebuildList (selectedId); };
    searchBox.onEscapeKey = [this] { searchBox.clear(); rebuildList (selectedId); };

    newTopicButton.onClick = [this] { newTopic(); };
    folderButton.onClick = [this] { showFolderMenu(); };
    tagsButton.onClick = [this] { showTagMenu(); };
    tagsButton.setTooltip ("Show only topics with a tag (on the topic or one of its resources)");

    topicList.setRowHeight (54);
    topicList.setColour (juce::ListBox::backgroundColourId, theme::panel());
    topicList.setOutlineThickness (0);

    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.5f));

    topicView = std::make_unique<TopicView> (*this);
    inboxView = std::make_unique<InboxView> (*this);

    for (juce::Component* c : { (juce::Component*) &searchBox, (juce::Component*) &newTopicButton, (juce::Component*) &folderButton,
                                (juce::Component*) &tagsButton, (juce::Component*) &topicList, (juce::Component*) &emptyLabel,
                                (juce::Component*) topicView.get(), (juce::Component*) inboxView.get() })
        addChildComponent (c);

    const auto location = LibraryLocation::load();

    if (location != juce::File())
        openLibrary (location);
    else
        resized();

    startTimerHz (4);
}

LibraryPanel::~LibraryPanel()
{
    flush();
}

//==============================================================================
void LibraryPanel::openLibrary (const juce::File& folder)
{
    flush();
    library = std::make_unique<Library> (folder);
    signature = library->computeSignature();
    folderButton.setTooltip ("Library folder: " + folder.getFullPathName());
    juce::Logger::writeToLog ("Library: " + folder.getFullPathName() + " (" + juce::String ((int) library->getTopics().size())
                              + " topics, " + juce::String (library->getInbox().size()) + " in the Inbox)");
    selectedId = {};
    rebuildList (library->getTopics().empty() ? juce::String() : library->getTopics().front().id);
    resized();

    if (onLibraryOpened)
        onLibraryOpened();

    requestDownloads();
}

void LibraryPanel::requestDownloads()
{
   #if JUCE_IOS
    static std::atomic<bool> busy { false };

    if (library == nullptr || busy.exchange (true))
        return;

    lastDownloadCheck = juce::Time::getMillisecondCounter();

    juce::Thread::launch ([root = library->getRoot()]
    {
        if (const auto n = FolderAccess::requestDownloads (root); n > 0)
            juce::Logger::writeToLog ("Library: asked the cloud drive to download " + juce::String (n) + " file(s)");

        busy = false;
    });
   #endif
}

void LibraryPanel::chooseFolder()
{
    const auto start = library != nullptr ? library->getRoot() : LibraryLocation::getDefaultFolder().getParentDirectory();
    fileChooser = std::make_unique<juce::FileChooser> ("Choose the library folder", start);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto url = chooser.getURLResult();

        if (url.isEmpty())
            return;

        const auto folder = LibraryLocation::saveChosen (url);

        if (folder == juce::File())
        {
            showError ("Practice Vibes can't use that folder. Try another one, or keep the topics on this device.");
            return;
        }

        openLibrary (folder);
    });
}

void LibraryPanel::showFolderMenu()
{
    if (library == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader (library->getRoot().getFullPathName());
    const auto isDefault = library->getRoot() == LibraryLocation::getDefaultFolder();
   #if ! JUCE_IOS
    menu.addItem ("Show the folder", [this] { library->getRoot().revealToUser(); });
    menu.addItem ("To share topics with an iPad or iPhone, use a folder in iCloud Drive", false, false, [] {});
   #else
    if (isDefault)
        menu.addItem ("Files app: On My iPad > Practice Vibes > Library", false, false, [] {});

    menu.addItem ("Only iCloud Drive can keep topics in sync with your other devices", false, false, [] {});
   #endif
    menu.addItem ("Use a different folder...", [this] { chooseFolder(); });

    if (! isDefault)
        menu.addItem (
                     #if JUCE_IOS
                      "Use the folder on this iPad",
                     #else
                      "Use the default folder",
                     #endif
                      [this]
        {
            LibraryLocation::save (LibraryLocation::getDefaultFolder());
            openLibrary (LibraryLocation::getDefaultFolder());
        });
    menu.addItem ("Check for changes now", [this]
    {
        flush();
        library->rescan();
        signature = library->computeSignature();
        refreshAfterRescan();
    });
    menu.addSeparator();
    menu.addItem ("Storage and clean-up...", [this] { showStorage(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&folderButton));
}

void LibraryPanel::showStorage()
{
    if (library == nullptr || ! showOverlay)
        return;

    flush();
    auto panel = std::make_unique<StoragePanel> (library.get(), LoopFiles::getLoopsFolder(), getAppDataFolder());
    panel->onRemoved = [this] { checkForChanges(); };
    showOverlay ("Storage and clean-up", std::move (panel));
}

//==============================================================================
void LibraryPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
}

void LibraryPanel::resized()
{
    const auto hasLibrary = library != nullptr;

    setupLabel.setVisible (! hasLibrary);
    useDefaultButton.setVisible (! hasLibrary);
    chooseFolderButton.setVisible (! hasLibrary && chooseFolderButton.getParentComponent() == this);

    for (juce::Component* c : { (juce::Component*) &searchBox, (juce::Component*) &newTopicButton,
                                (juce::Component*) &folderButton, (juce::Component*) &tagsButton, (juce::Component*) &topicList })
        c->setVisible (hasLibrary);

    auto area = getLocalBounds().reduced (12, 8);

    if (! hasLibrary)
    {
        auto box = area.withSizeKeepingCentre (juce::jmin (640, area.getWidth()), 300);
        setupLabel.setBounds (box.removeFromTop (170));
        box.removeFromTop (10);
        useDefaultButton.setBounds (box.removeFromTop (44).reduced (40, 2));
        box.removeFromTop (8);
        chooseFolderButton.setBounds (box.removeFromTop (44).reduced (40, 2));
        topicView->setVisible (false);
        inboxView->setVisible (false);
        emptyLabel.setVisible (false);
        return;
    }

    auto left = area.removeFromLeft (juce::jlimit (220, 340, area.getWidth() / 3));
    area.removeFromLeft (12);

    auto leftTop = left.removeFromTop (40);
    newTopicButton.setBounds (leftTop.removeFromRight (120).reduced (2, 3));
    searchBox.setBounds (leftTop.reduced (0, 4).withTrimmedRight (6));
    left.removeFromTop (4);
    tagsButton.setBounds (left.removeFromTop (34).reduced (0, 2));
    left.removeFromTop (4);
    folderButton.setBounds (left.removeFromBottom (40).reduced (0, 4));
    left.removeFromBottom (4);
    topicList.setBounds (left);

    topicView->setBounds (area);
    inboxView->setBounds (area);
    emptyLabel.setBounds (area);
}

void LibraryPanel::visibilityChanged()
{
    if (! isVisible())
        flush();
    else
        lastPoll = 0;   // check for changes from other devices right away
}

//==============================================================================
int LibraryPanel::getNumRows()
{
    return (int) rowIds.size();
}

void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (library == nullptr || ! juce::isPositiveAndBelow (row, (int) rowIds.size()))
        return;

    if (selected)
    {
        g.setColour (theme::selection());
        g.fillRect (0, 0, width, height);
    }

    g.setColour (theme::line());
    g.fillRect (0, height - 1, width, 1);

    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (12, 6);
    const auto& id = rowIds[(size_t) row];

    if (id == inboxId)
    {
        g.setColour (theme::highlight());
        g.setFont (juce::FontOptions (15.0f).withStyle ("Bold"));
        g.drawText ("Inbox", r.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (theme::text().withAlpha (0.65f));
        g.setFont (juce::FontOptions (12.5f));
        const auto n = library->getInbox().size();
        g.drawText (juce::String (n) + (n == 1 ? " file" : " files") + " to sort into topics", r, juce::Justification::centredLeft);
        return;
    }

    if (auto* t = library->findTopic (id))
    {
        g.setColour (theme::text());
        g.setFont (juce::FontOptions (15.0f).withStyle ("Bold"));
        g.drawText (t->title.isNotEmpty() ? t->title : juce::String ("(untitled)"), r.removeFromTop (22), juce::Justification::centredLeft, true);
        g.setColour (theme::text().withAlpha (0.6f));
        g.setFont (juce::FontOptions (12.5f));
        const auto n = (int) t->resources.size();
        auto info = formatDate (t->created) + (n > 0 ? "   " + juce::String (n) + (n == 1 ? " resource" : " resources") : juce::String());

        // When the search or tag filter matched something not shown in this row, say where instead.
        if (const auto why = t->describeMatch (searchBox.getText(), tagFilter); why.isNotEmpty())
        {
            g.setColour (selected ? theme::text().withAlpha (0.85f) : theme::highlight().withAlpha (0.9f));
            info = why;
        }

        const auto infoWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (12.5f)), info)) + 10;
        g.drawText (info, r.removeFromLeft (juce::jmin (infoWidth, r.getWidth())), juce::Justification::centredLeft, true);
        drawTagChips (g, r.reduced (0, 3), t->tags, tagFilter);
    }
}

void LibraryPanel::selectedRowsChanged (int lastRowSelected)
{
    const auto id = juce::isPositiveAndBelow (lastRowSelected, (int) rowIds.size()) ? rowIds[(size_t) lastRowSelected] : juce::String();

    if (rebuilding || id == selectedId)
        return;

    flush();
    selectedId = id;
    showSelection();
}

void LibraryPanel::showTagMenu()
{
    if (library == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem ("All topics", true, tagFilter.isEmpty(), [this] { tagFilter = {}; updateTagButton(); rebuildList (selectedId); });
    menu.addSeparator();

    const auto tags = library->getAllTags();

    if (tags.isEmpty())
        menu.addItem ("No tags yet: type them under a topic's title or in a resource's details", false, false, [] {});

    for (auto& tag : tags)
    {
        int count = 0;

        for (auto& t : library->getTopics())
            if (t.matchesTag (tag))
                ++count;

        menu.addItem (tag + "  (" + juce::String (count) + ")", true, tag.equalsIgnoreCase (tagFilter), [this, tag]
        {
            tagFilter = tag;
            updateTagButton();
            rebuildList (selectedId);
        });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tagsButton));
}

void LibraryPanel::updateTagButton()
{
    tagsButton.setButtonText (tagFilter.isEmpty() ? "Tags" : "Tag: " + tagFilter + "   (change / clear)");
    tagsButton.setColour (juce::TextButton::buttonColourId,
                          tagFilter.isEmpty() ? getLookAndFeel().findColour (juce::TextButton::buttonColourId) : theme::accent());
}

void LibraryPanel::rebuildList (const juce::String& idToSelect)
{
    const auto selectId = idToSelect;   // copy: callers often pass selectedId itself, which changes below
    const juce::ScopedValueSetter<bool> svs (rebuilding, true);
    rowIds.clear();

    if (library != nullptr)
    {
        const auto filtering = searchBox.getText().trim().isNotEmpty() || tagFilter.isNotEmpty();

        if (! library->getInbox().isEmpty() && ! filtering)
            rowIds.push_back (inboxId);

        for (auto& t : library->getTopics())
            if (t.matches (searchBox.getText()) && t.matchesTag (tagFilter))
                rowIds.push_back (t.id);
    }

    topicList.updateContent();

    const auto it = std::find (rowIds.begin(), rowIds.end(), selectId);
    selectedId = it != rowIds.end() ? selectId : juce::String();

    if (it != rowIds.end())
        topicList.selectRow ((int) std::distance (rowIds.begin(), it), true, true);
    else
        topicList.deselectAllRows();

    topicList.repaint();
    showSelection();
}

void LibraryPanel::showSelection()
{
    if (library == nullptr)
        return;

    const auto* topic = library->findTopic (selectedId);
    const auto inbox = selectedId == inboxId;

    topicView->setVisible (topic != nullptr);
    inboxView->setVisible (inbox);
    emptyLabel.setVisible (topic == nullptr && ! inbox);

    if (topic != nullptr)
        topicView->load (*topic, true);
    else if (inbox)
        inboxView->load (library->getInbox());
    else
        emptyLabel.setText (library->getTopics().empty()
                                ? "No topics yet.\n\nA topic is anything you practise: a technique, a song, a lesson.\n"
                                  "Press \"+ New topic\" to create one, then add notes, documents, audio files and links."
                                : "Select a topic on the left.",
                            juce::dontSendNotification);
}

void LibraryPanel::refreshAfterRescan()
{
    rebuildList (selectedId);

    if (onTopicsChanged)
        onTopicsChanged();
}

void LibraryPanel::checkForChanges()
{
    if (library == nullptr)
        return;

    flush();
    const auto newSignature = library->computeSignature();

    if (newSignature == signature)
        return;

    library->rescan();
    signature = library->computeSignature();
    refreshAfterRescan();
}

void LibraryPanel::selectTopic (const juce::String& topicId)
{
    if (library == nullptr || library->findTopic (topicId) == nullptr)
        return;

    flush();

    if (auto* t = library->findTopic (topicId); ! t->matches (searchBox.getText()) || ! t->matchesTag (tagFilter))
    {
        searchBox.clear();
        tagFilter = {};
        updateTagButton();
    }

    rebuildList (topicId);
}

void LibraryPanel::setRotation (const juce::String& topicId, bool inRotation)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr || t->inRotation == inRotation)
        return;

    flush();
    t->inRotation = inRotation;

    if (! library->saveTopic (*t))
        showError ("Could not save " + t->folder.getChildFile (Library::topicFileName).getFullPathName());

    signature = library->computeSignature();

    if (onTopicsChanged)
        onTopicsChanged();
}

void LibraryPanel::setNoGuitar (const juce::String& topicId, bool noGuitar)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr || t->noGuitar == noGuitar)
        return;

    flush();
    t->noGuitar = noGuitar;

    if (! library->saveTopic (*t))
        showError ("Could not save " + t->folder.getChildFile (Library::topicFileName).getFullPathName());

    signature = library->computeSignature();

    if (onTopicsChanged)
        onTopicsChanged();
}

bool LibraryPanel::openFirstMedia (const juce::String& topicId)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr)
        return false;

    for (auto& r : t->resources)
    {
        const auto kind = kindOf (r);
        const auto playable = (kind == ResourceKind::audio && ! t->isMissing (r))
                           || (kind == ResourceKind::youtube && webViewAvailable() && parseYouTubeUrl (r.url).has_value());

        if (playable)
        {
            openResource (*t, r);
            return true;
        }
    }

    return false;
}

//==============================================================================
void LibraryPanel::editedTopic()
{
    dirty = true;
    lastEdit = juce::Time::getMillisecondCounter();
    topicList.repaint();
}

void LibraryPanel::flush()
{
    if (! dirty || library == nullptr)
        return;

    dirty = false;

    if (auto* t = library->findTopic (topicView->topicId))
    {
        if (! library->saveTopic (*t))
            juce::Logger::writeToLog ("Library: could not save " + t->folder.getChildFile (Library::topicFileName).getFullPathName());

        signature = library->computeSignature();

        if (onTopicsChanged)
            onTopicsChanged();
    }
}

void LibraryPanel::titleSettled (const juce::String& topicId)
{
    if (library == nullptr)
        return;

    flush();

    if (auto* t = library->findTopic (topicId); t != nullptr && library->renameFolderAfterTitle (*t))
    {
        signature = library->computeSignature();

        if (onTopicsChanged)
            onTopicsChanged();
    }
}

void LibraryPanel::chooseTopicVersion (const juce::String& topicId, juce::Component& anchor)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr)
        return;

    flush();
    const auto versions = Library::getTopicFileVersions (*t);
    juce::PopupMenu menu;
    menu.addSectionHeader ("Keep which version? The others go to the bin.");

    for (size_t i = 0; i < versions.size(); ++i)
    {
        const auto& v = versions[i];
        const auto isCurrent = v.fileName.equalsIgnoreCase (Library::topicFileName);
        menu.addItem ((int) i + 1, "\"" + (v.title.isNotEmpty() ? v.title : juce::String ("(no title)")) + "\", saved "
                                       + v.saved.formatted ("%d %b %H:%M:%S") + ", "
                                       + juce::String (v.resources) + (v.resources == 1 ? " resource" : " resources")
                                       + (isCurrent ? " (the one shown)" : " (" + v.fileName + ")"));
    }

    juce::Component::SafePointer<LibraryPanel> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                        [safeThis, topicId, versions] (int result)
    {
        if (safeThis == nullptr || safeThis->library == nullptr || result <= 0 || result > (int) versions.size())
            return;

        auto* topic = safeThis->library->findTopic (topicId);

        if (topic == nullptr)
            return;

        if (! Library::keepTopicFileVersion (*topic, versions[(size_t) result - 1].fileName))
            safeThis->showError ("Could not tidy up the copies in " + topic->folder.getFullPathName());

        safeThis->library->rescan();
        safeThis->signature = safeThis->library->computeSignature();
        safeThis->refreshAfterRescan();
    });
}

void LibraryPanel::timerCallback()
{
    if (library == nullptr)
        return;

    const auto now = juce::Time::getMillisecondCounter();

    // Not after every short pause: each save is an upload for a cloud drive, and quick rewrites while the
    // previous upload is still running is what makes cloud clients create duplicate copies.
    // Leaving the field (or the topic) saves at once.
    if (dirty && now - lastEdit > 2500)
        flush();

    if (! isShowing() || (lastPoll != 0 && now - lastPoll < 3000))
        return;

    if (now - lastDownloadCheck > 30000)
        requestDownloads();

    lastPoll = now;
    const auto newSignature = library->computeSignature();

    if (newSignature == signature)
        return;

    flush();
    library->rescan();
    signature = library->computeSignature();
    refreshAfterRescan();
}

//==============================================================================
void LibraryPanel::newTopic()
{
    if (library == nullptr)
        return;

    flush();

    if (auto* t = library->createTopic ("New topic"))
    {
        const auto id = t->id;
        signature = library->computeSignature();
        searchBox.clear();
        tagFilter = {};
        updateTagButton();
        rebuildList (id);
        topicView->focusTitle();
    }
    else
    {
        showError ("Could not create a topic folder in " + library->getRoot().getFullPathName());
    }
}

void LibraryPanel::deleteTopic (const juce::String& topicId)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr)
        return;

    flush();
    const auto files = (int) std::count_if (t->resources.begin(), t->resources.end(), [] (const Resource& r) { return ! r.isLink(); });

   #if JUCE_IOS
    const juce::String where = "This can't be undone.";
   #else
    const juce::String where = "The folder goes to the Recycle Bin.";
   #endif

    auto options = confirmBox (juce::MessageBoxIconType::WarningIcon, "Delete topic",
                                                          "Delete \"" + t->title + "\" with its notes"
                                                              + (files > 0 ? " and " + juce::String (files) + (files == 1 ? " file" : " files") : juce::String())
                                                              + "?\n" + where,
                                                          "Delete", this);

    juce::AlertWindow::showAsync (options, [safeThis = SafePointer<LibraryPanel> (this), topicId] (int result)
    {
        if (safeThis == nullptr || result != 1 || safeThis->library == nullptr)
            return;

        if (! safeThis->library->deleteTopic (topicId))
            safeThis->showError ("Could not delete the topic folder (is a file in it open in another app?).");

        safeThis->signature = safeThis->library->computeSignature();
        safeThis->rebuildList (safeThis->library->getTopics().empty() ? juce::String() : safeThis->library->getTopics().front().id);
    });
}

void LibraryPanel::addFiles (const juce::String& topicId)
{
    fileChooser = std::make_unique<juce::FileChooser> ("Add files to the topic",
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this, topicId] (const juce::FileChooser& chooser)
    {
        addUrls (topicId, chooser.getURLResults());
    });
}

void LibraryPanel::addUrls (const juce::String& topicId, const juce::Array<juce::URL>& urls)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr || urls.isEmpty())
        return;

    flush();
    juce::StringArray failed;

    for (auto& url : urls)
        if (library->addFile (*t, url) == nullptr)
            failed.add (juce::URL::removeEscapeChars (url.getFileName()));

    signature = library->computeSignature();
    showSelection();
    topicList.repaint();

    if (! failed.isEmpty())
        showError ("Could not copy: " + failed.joinIntoString (", "));
}

void LibraryPanel::editResource (const juce::String& topicId, const juce::String& resourceId, bool isNewLink)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr || showOverlay == nullptr)
        return;

    Resource current;

    if (! isNewLink)
    {
        auto* r = t->findResource (resourceId);

        if (r == nullptr)
            return;

        current = *r;
    }

    auto editor = std::make_unique<ResourceEditor> (current, isNewLink || current.isLink());

    editor->onCancel = [this] { if (hideOverlay) hideOverlay(); };
    editor->onSave = [this, topicId, resourceId, isNewLink] (const juce::String& title, const juce::String& url,
                                                             const juce::StringArray& tags, const juce::String& description)
    {
        if (auto* topic = library->findTopic (topicId))
        {
            if (isNewLink)
            {
                if (auto* r = library->addLink (*topic, title, url))
                {
                    r->description = description;
                    r->tags = tags;
                    library->saveTopic (*topic);
                }
            }
            else if (auto* r = topic->findResource (resourceId))
            {
                r->title = title.isNotEmpty() ? title : (r->isLink() ? Library::titleFromUrl (url) : r->fileName);
                r->description = description;
                r->tags = tags;

                if (r->isLink() && url.isNotEmpty())
                    r->url = url.containsChar (':') ? url : "https://" + url;

                library->saveTopic (*topic);
            }

            signature = library->computeSignature();
            showSelection();
            topicList.repaint();
        }

        if (hideOverlay)
            hideOverlay();
    };

    showOverlay (isNewLink ? "Add link" : "Edit resource", std::move (editor));
}

void LibraryPanel::showResourceMenu (const juce::String& topicId, const juce::String& resourceId, juce::Component& anchor)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;
    auto* r = t != nullptr ? t->findResource (resourceId) : nullptr;

    if (r == nullptr)
        return;

    const auto index = (int) (r - t->resources.data());
    const auto count = (int) t->resources.size();
    const auto isLink = r->isLink();
    const auto file = t->getFile (*r);

    auto move = [this, topicId, resourceId] (int delta)
    {
        if (auto* topic = library->findTopic (topicId))
        {
            library->moveResource (*topic, resourceId, delta);
            signature = library->computeSignature();
            showSelection();
        }
    };

    juce::PopupMenu menu;
    menu.addItem ("Edit title and description...", [this, topicId, resourceId] { editResource (topicId, resourceId, false); });
    menu.addItem ("Move up", index > 0, false, [move] { move (-1); });
    menu.addItem ("Move down", index < count - 1, false, [move] { move (1); });

   #if ! JUCE_IOS
    if (! isLink)
        menu.addItem ("Show in folder", file.existsAsFile(), false, [file] { file.revealToUser(); });
   #endif

    if (isLink && kindOf (*r) == ResourceKind::youtube)
        menu.addItem ("Open in the browser", [url = r->url] { juce::URL (url).launchInDefaultBrowser(); });

    menu.addSeparator();
    menu.addItem (isLink ? "Remove link" : "Remove (deletes the file)", [this, topicId, resourceId, isLink, title = r->title]
    {
        auto doRemove = [safeThis = SafePointer<LibraryPanel> (this), topicId, resourceId]
        {
            if (safeThis == nullptr || safeThis->library == nullptr)
                return;

            if (auto* topic = safeThis->library->findTopic (topicId))
                if (! safeThis->library->removeResource (*topic, resourceId))
                    safeThis->showError ("Could not remove the file (is it open in another app?).");

            safeThis->signature = safeThis->library->computeSignature();
            safeThis->showSelection();
            safeThis->topicList.repaint();
        };

        if (isLink)
        {
            doRemove();
            return;
        }

        auto options = confirmBox (juce::MessageBoxIconType::QuestionIcon, "Remove resource",
                                                              "Remove \"" + title + "\" and delete its file from the topic folder?",
                                                              "Remove", this);
        juce::AlertWindow::showAsync (options, [doRemove] (int result) { if (result == 1) doRemove(); });
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor));
}

void LibraryPanel::showInboxMenu (const juce::File& file, juce::Component& anchor)
{
    if (library == nullptr)
        return;

    auto moveTo = [safeThis = SafePointer<LibraryPanel> (this), file] (const juce::String& topicId)
    {
        if (safeThis == nullptr || safeThis->library == nullptr)
            return;

        if (auto* t = safeThis->library->findTopic (topicId))
            if (safeThis->library->moveInboxFile (*t, file) == nullptr)
                safeThis->showError ("Could not move " + file.getFileName());

        safeThis->signature = safeThis->library->computeSignature();
        safeThis->rebuildList (safeThis->library->getInbox().isEmpty() ? topicId : inboxId);
    };

    juce::PopupMenu menu;
    menu.addItem ("New topic named \"" + file.getFileNameWithoutExtension() + "\"", [this, file, moveTo]
    {
        if (auto* t = library->createTopic (file.getFileNameWithoutExtension()))
            moveTo (t->id);
    });
    menu.addSeparator();

    for (auto& t : library->getTopics())
        menu.addItem (t.title.isNotEmpty() ? t.title : juce::String ("(untitled)"), [moveTo, id = t.id] { moveTo (id); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor));
}

//==============================================================================
void LibraryPanel::openResource (const Topic& topic, const Resource& r)
{
    if (onTopicOpened)
        onTopicOpened (topic.id);

    if (! r.isLink())
    {
        openFile (topic.getFile (r), r.title);
        return;
    }

    const auto url = r.url;

    if (kindOf (r) == ResourceKind::youtube && webViewAvailable() && onOpenYouTube != nullptr)
    {
        if (const auto link = parseYouTubeUrl (url))
        {
            onOpenYouTube (*link, r.title);
            return;
        }
    }

    // Websites open in the system browser (owner's choice); only YouTube stays in the app.
    juce::URL (url).launchInDefaultBrowser();
}

void LibraryPanel::openFile (const juce::File& file, const juce::String& title)
{
    if (! file.existsAsFile())
        return;

    if (classifyFileName (file.getFileName()) == ResourceKind::audio)
    {
        if (onOpenAudio)
            onOpenAudio (file);

        return;
    }

    if (viewerCanShow (file) && showOverlay != nullptr)
    {
       #if JUCE_IOS
        std::function<void()> outside;   // files have no "outside" on iOS without a share sheet
       #else
        std::function<void()> outside = [file] { file.startAsProcess(); };
       #endif
        showOverlay (title, std::make_unique<Viewer> (juce::URL (file).toString (false), outside));
        return;
    }

   #if JUCE_IOS
    showError ("This kind of file can't be shown on the iPad.");
   #else
    if (! file.startAsProcess())
        showError ("No app on this computer opens " + file.getFileName());
   #endif
}

void LibraryPanel::showError (const juce::String& message)
{
    juce::Logger::writeToLog ("Library: " + message);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon, "Topics", message, "OK", this),
                                  nullptr);
}

} // namespace pv
