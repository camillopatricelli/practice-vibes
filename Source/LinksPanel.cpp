#include "LinksPanel.h"
#include "ResourceWidgets.h"
#include "WebView.h"
#include "Localisation.h"

namespace pv
{

LinksPanel::LinksPanel()
{
    searchBox.setTextToShowWhenEmpty (tr ("Search links"), theme::text().withAlpha (0.4f));
    searchBox.setFont (juce::FontOptions (15.0f));
    searchBox.setJustification (juce::Justification::centredLeft);
    styleEditor (searchBox);
    searchBox.onTextChange = [this] { rebuild(); };
    searchBox.onEscapeKey = [this] { searchBox.clear(); rebuild(); };
    addAndMakeVisible (searchBox);

    newButton.onClick = [this] { edit ({}); };
    addAndMakeVisible (newButton);

    tagsButton.onClick = [this] { showTagMenu(); };
    addAndMakeVisible (tagsButton);

    setupButton.onClick = [this] { if (onChooseLibrary) onChooseLibrary(); };
    addChildComponent (setupButton);

    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setFont (juce::FontOptions (16.0f));
    emptyLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.6f));
    addChildComponent (emptyLabel);

    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    rebuild();
}

LinksPanel::~LinksPanel()
{
    rows.clear();
}

void LinksPanel::setLibraryRoot (const juce::File& root)
{
    if (root == juce::File())
        links.reset();
    else if (links == nullptr || links->getFile() != root.getChildFile (LinkList::folderName).getChildFile (LinkList::fileName))
        links = std::make_unique<LinkList> (root);

    tagFilter = {};
    updateTagButton();
    rebuild();
}

bool LinksPanel::isFiltering() const
{
    return searchBox.getText().trim().isNotEmpty() || tagFilter.isNotEmpty();
}

void LinksPanel::rebuild()
{
    rows.clear();

    const auto hasLibrary = links != nullptr;
    searchBox.setVisible (hasLibrary);
    newButton.setVisible (hasLibrary);
    tagsButton.setVisible (hasLibrary);
    setupButton.setVisible (! hasLibrary);

    if (! hasLibrary)
    {
        emptyLabel.setText (tr ("Links are kept in your library folder, so all your devices share them.\n"
                            "Choose the library folder first."), juce::dontSendNotification);
        emptyLabel.setVisible (true);
        resized();
        return;
    }

    const auto search = searchBox.getText().trim();

    for (auto& link : links->getLinks())
    {
        if (! resourceMatches (link, search) || (tagFilter.isNotEmpty() && ! hasTag (link.tags, tagFilter)))
            continue;

        auto row = std::make_unique<ResourceRow> (link, false, "...", tagFilter);
        row->onOpen = [this, link] { open (link); };
        row->onMenu = [this, id = link.id] (juce::Component& anchor) { showMenu (id, anchor); };
        rowHolder.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    if (links->getLinks().empty())
        emptyLabel.setText (tr ("No links yet.\nAdd the websites you keep coming back to: a page that explains the circle "
                            "of fifths, a scale finder, a chord dictionary..."), juce::dontSendNotification);
    else if (rows.empty())
        emptyLabel.setText (tr ("No links match."), juce::dontSendNotification);

    emptyLabel.setVisible (rows.empty());
    resized();
}

void LinksPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
}

void LinksPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);

    if (links == nullptr)
    {
        auto box = area.withSizeKeepingCentre (juce::jmin (640, area.getWidth()), 160);
        emptyLabel.setBounds (box.removeFromTop (100));
        setupButton.setBounds (box.removeFromTop (44).reduced (80, 2));
        viewport.setBounds ({});
        return;
    }

    auto top = area.removeFromTop (40);
    newButton.setBounds (top.removeFromRight (130).reduced (3));
    tagsButton.setBounds (top.removeFromRight (juce::jlimit (90, 260, top.getWidth() / 3)).reduced (3));
    searchBox.setBounds (top.reduced (3));
    area.removeFromTop (8);

    viewport.setBounds (area);
    emptyLabel.setBounds (area.withSizeKeepingCentre (juce::jmin (600, area.getWidth()), 120));
    layoutRows();
}

void LinksPanel::layoutRows()
{
    constexpr int rowHeight = 64, gap = 6;
    const auto width = viewport.getMaximumVisibleWidth();
    rowHolder.setSize (juce::jmax (1, width), juce::jmax (1, (int) rows.size() * (rowHeight + gap)));

    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, (int) i * (rowHeight + gap), rowHolder.getWidth(), rowHeight);
}

void LinksPanel::visibilityChanged()
{
    if (isVisible())
    {
        if (links != nullptr && links->reloadIfChanged())
            rebuild();

        startTimer (3000);
    }
    else
    {
        stopTimer();
    }
}

void LinksPanel::timerCallback()
{
    // Picks up changes made on another device (the file syncs through the cloud drive).
    if (links != nullptr && links->reloadIfChanged())
        rebuild();
}

void LinksPanel::open (const Resource& link)
{
    if (kindOf (link) == ResourceKind::youtube && webViewAvailable() && onOpenYouTube != nullptr)
    {
        if (const auto yt = parseYouTubeUrl (link.url))
        {
            onOpenYouTube (*yt, link.title);
            return;
        }
    }

    juce::URL (link.url).launchInDefaultBrowser();
}

void LinksPanel::edit (const juce::String& id)
{
    if (links == nullptr || showOverlay == nullptr)
        return;

    links->reloadIfChanged();
    Resource current;

    if (id.isNotEmpty())
    {
        auto* r = links->find (id);

        if (r == nullptr)
            return;

        current = *r;
    }

    auto editor = std::make_unique<ResourceEditor> (current, true);
    editor->onCancel = [this] { if (hideOverlay) hideOverlay(); };
    editor->onSave = [this, id] (const juce::String& title, const juce::String& url,
                                 const juce::StringArray& tags, const juce::String& description)
    {
        if (links != nullptr)
        {
            links->reloadIfChanged();

            if (id.isEmpty())
            {
                links->add (title, url, tags, description);
            }
            else if (auto* r = links->find (id))
            {
                r->url = normaliseUrl (url);
                r->title = title.isNotEmpty() ? title : Library::titleFromUrl (r->url);
                r->tags = tags;
                r->description = description;
                links->save();
            }
        }

        // Closing the overlay deletes the editor, and this lambda with it: read `this` before that.
        auto* self = this;

        if (hideOverlay)
            hideOverlay();

        self->rebuild();
    };

    showOverlay (id.isEmpty() ? tr ("New link") : tr ("Edit link"), std::move (editor));
}

void LinksPanel::showMenu (const juce::String& id, juce::Component& anchor)
{
    if (links == nullptr)
        return;

    links->reloadIfChanged();
    const auto& all = links->getLinks();
    const auto it = std::find_if (all.begin(), all.end(), [&] (const Resource& r) { return r.id == id; });

    if (it == all.end())
    {
        rebuild();
        return;
    }

    const auto index = (int) std::distance (all.begin(), it);
    const auto count = (int) all.size();
    const auto canMove = ! isFiltering();   // moving inside a filtered list would jump over hidden links
    const auto url = it->url;
    const auto title = it->title;

    auto move = [this, id] (int delta)
    {
        if (links != nullptr && links->move (id, delta))
            rebuild();
    };

    juce::PopupMenu menu;
    menu.addItem (tr ("Edit..."), [this, id] { edit (id); });
    menu.addItem (canMove ? tr ("Move up") : tr ("Move up (clear the search / tag first)"), canMove && index > 0, false, [move] { move (-1); });
    menu.addItem (canMove ? tr ("Move down") : tr ("Move down (clear the search / tag first)"), canMove && index < count - 1, false, [move] { move (1); });
    menu.addItem (tr ("Copy the address"), [url] { juce::SystemClipboard::copyTextToClipboard (url); });

    if (kindOf (*it) == ResourceKind::youtube)
        menu.addItem (tr ("Open in the browser"), [url] { juce::URL (url).launchInDefaultBrowser(); });

    menu.addSeparator();
    menu.addItem (tr ("Remove link"), [this, id, title]
    {
        auto options = confirmBox (juce::MessageBoxIconType::QuestionIcon, tr ("Remove link"),
                                   tr ("Remove \"{title}\" from your links?", title), tr ("Remove"), this);
        juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<LinksPanel> (this), id] (int result)
        {
            if (result == 1 && safe != nullptr && safe->links != nullptr)
            {
                safe->links->reloadIfChanged();
                safe->links->remove (id);
                safe->rebuild();
            }
        });
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor));
}

void LinksPanel::showTagMenu()
{
    if (links == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem (tr ("All links"), true, tagFilter.isEmpty(), [this] { tagFilter = {}; updateTagButton(); rebuild(); });
    menu.addSeparator();

    const auto tags = links->getAllTags();

    if (tags.isEmpty())
        menu.addItem (tr ("No tags yet: add them when you edit a link"), false, false, [] {});

    for (auto& tag : tags)
    {
        int count = 0;

        for (auto& link : links->getLinks())
            if (hasTag (link.tags, tag))
                ++count;

        menu.addItem (tag + "  (" + juce::String (count) + ")", true, tag.equalsIgnoreCase (tagFilter), [this, tag]
        {
            tagFilter = tag;
            updateTagButton();
            rebuild();
        });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tagsButton));
}

void LinksPanel::updateTagButton()
{
    tagsButton.setButtonText (tagFilter.isEmpty() ? tr ("Tags") : tr ("Tag: {tag}   (change / clear)", tagFilter));
    tagsButton.setColour (juce::TextButton::buttonColourId,
                          tagFilter.isEmpty() ? getLookAndFeel().findColour (juce::TextButton::buttonColourId) : theme::accent());
}

} // namespace pv
