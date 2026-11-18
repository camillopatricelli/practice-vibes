#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Links.h"
#include "Localisation.h"

namespace pv
{

class ResourceRow;

/** The "Links" tab: bookmarks to outside references, separate from the topics, stored in the library folder.

    Websites open in the system browser; YouTube links open in the app's YouTube player.
*/
class LinksPanel final : public juce::Component,
                         private juce::Timer
{
public:
    LinksPanel();
    ~LinksPanel() override;

    /** The library folder to keep the links in (empty File = none chosen yet). */
    void setLibraryRoot (const juce::File& root);

    std::function<void (const juce::String&, std::unique_ptr<juce::Component>)> showOverlay;
    std::function<void()> hideOverlay;
    std::function<void (const YouTubeLink&, const juce::String& title)> onOpenYouTube;
    std::function<void()> onChooseLibrary;      // "Set up the library" button when no folder is chosen

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void rebuild();
    void layoutRows();
    void open (const Resource&);
    void edit (const juce::String& id);        // empty id = new link
    void showMenu (const juce::String& id, juce::Component& anchor);
    void showTagMenu();
    void updateTagButton();
    bool isFiltering() const;

    std::unique_ptr<LinkList> links;
    juce::String tagFilter;

    juce::TextEditor searchBox;
    juce::TextButton newButton { tr ("+ New link") }, tagsButton { tr ("Tags") }, setupButton { tr ("Set up the library in the Topics tab") };
    juce::Label emptyLabel;
    juce::Viewport viewport;
    juce::Component rowHolder;
    std::vector<std::unique_ptr<ResourceRow>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinksPanel)
};

} // namespace pv
