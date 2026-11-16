#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Library.h"
#include "Theme.h"

/* Widgets shared by the Topics and Links tabs: type badges, tag chips, resource rows and the resource editor. */

namespace pv
{

inline juce::Colour badgeColour (ResourceKind kind)
{
    switch (kind)
    {
        case ResourceKind::pdf:          return juce::Colour (0xffd64545);
        case ResourceKind::image:        return juce::Colour (0xff2a9d8f);
        case ResourceKind::text:         return juce::Colour (0xff7a7f8a);
        case ResourceKind::document:     return juce::Colour (0xff2f6fd0);
        case ResourceKind::spreadsheet:  return juce::Colour (0xff2e8b57);
        case ResourceKind::presentation: return juce::Colour (0xffe07b39);
        case ResourceKind::audio:        return juce::Colour (0xff8e5bd6);
        case ResourceKind::video:        return juce::Colour (0xffc2448f);
        case ResourceKind::youtube:      return juce::Colour (0xffe62117);
        case ResourceKind::googleDoc:    return juce::Colour (0xff4285f4);
        case ResourceKind::web:          return juce::Colour (0xff4a6fa5);
        case ResourceKind::other:        break;
    }

    return juce::Colour (0xff5a5f6a);
}

/** Generic type badge (no brand logos): coloured tile with a short label, a play triangle for YouTube. */
inline void drawBadge (juce::Graphics& g, juce::Rectangle<float> r, const Resource& resource)
{
    const auto kind = kindOf (resource);
    g.setColour (badgeColour (kind));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colours::white);     // on the badge colour, in both themes

    if (kind == ResourceKind::youtube)
    {
        auto tri = r.withSizeKeepingCentre (r.getWidth() * 0.36f, r.getHeight() * 0.40f).translated (r.getWidth() * 0.03f, 0.0f);
        juce::Path p;
        p.addTriangle (tri.getTopLeft(), tri.getBottomLeft(), { tri.getRight(), tri.getCentreY() });
        g.fillPath (p);
        return;
    }

    if (kind == ResourceKind::audio)
    {
        // Small note symbol above the extension.
        auto top = r.withHeight (r.getHeight() * 0.55f).reduced (r.getWidth() * 0.3f, r.getHeight() * 0.1f);
        g.fillEllipse (top.getX(), top.getBottom() - top.getHeight() * 0.4f, top.getWidth() * 0.55f, top.getHeight() * 0.4f);
        g.fillRect (top.getX() + top.getWidth() * 0.45f, top.getY(), top.getWidth() * 0.1f, top.getHeight() * 0.8f);
        g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        g.drawFittedText (badgeText (resource), r.removeFromBottom (r.getHeight() * 0.42f).toNearestInt(), juce::Justification::centred, 1);
        return;
    }

    const auto text = badgeText (resource);
    g.setFont (juce::FontOptions (text.length() > 3 ? 12.0f : 14.0f).withStyle ("Bold"));
    g.drawFittedText (text, r.toNearestInt().reduced (2), juce::Justification::centred, 1, 0.7f);
}

/** Draws tags as small rounded chips from the left of `area`; stops when out of room. */
inline void drawTagChips (juce::Graphics& g, juce::Rectangle<int> area, const juce::StringArray& tags, const juce::String& highlighted)
{
    const juce::Font font (juce::FontOptions (12.0f));
    g.setFont (font);

    for (auto& tag : tags)
    {
        const auto w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, tag)) + 14;

        if (w > area.getWidth())
            break;

        auto chip = area.removeFromLeft (w).toFloat();
        area.removeFromLeft (5);
        const auto on = highlighted.isNotEmpty() && tag.equalsIgnoreCase (highlighted);
        g.setColour (on ? theme::highlight() : theme::text().withAlpha (0.14f));
        g.fillRoundedRectangle (chip, chip.getHeight() * 0.5f);
        g.setColour (on ? theme::onAccent() : theme::text().withAlpha (0.85f));
        g.drawText (tag, chip, juce::Justification::centred, false);
    }
}

inline int tagChipsWidth (const juce::StringArray& tags, int maxWidth)
{
    const juce::Font font (juce::FontOptions (12.0f));
    int total = 0;

    for (auto& tag : tags)
    {
        const auto w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, tag)) + 19;

        if (total + w > maxWidth)
            break;

        total += w;
    }

    return total;
}

/** Two-button confirmation: result 1 = confirm, 0 = cancel. */
inline juce::MessageBoxOptions confirmBox (juce::MessageBoxIconType icon, const juce::String& title, const juce::String& message,
                                    const juce::String& confirmText, juce::Component* associated)
{
    return juce::MessageBoxOptions{}.withIconType (icon).withTitle (title).withMessage (message)
                                    .withButton (confirmText, 1).withButton ("Cancel", 0)
                                    .withAssociatedComponent (associated);
}

inline void styleEditor (juce::TextEditor& e)
{
    e.setColour (juce::TextEditor::backgroundColourId, theme::panel());
    e.setColour (juce::TextEditor::outlineColourId, theme::text().withAlpha (0.15f));
    e.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent());
}

inline juce::Label& styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (theme::headerFont());
    l.setColour (juce::Label::textColourId, theme::text().withAlpha (0.85f));
    return l;
}

//==============================================================================
/** One resource: badge, title, description, Open and a "..." menu (or "Move to topic..." in the Inbox). */
class ResourceRow final : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    ResourceRow (const Resource& r, bool missingFile, const juce::String& menuText, const juce::String& highlightedTag = {})
        : resource (r), missing (missingFile), highlight (highlightedTag), menuButton (menuText)
    {
        openButton.setEnabled (! missing);
        openButton.onClick = [this] { if (onOpen) onOpen(); };
        menuButton.onClick = [this] { if (onMenu) onMenu (menuButton); };
        addAndMakeVisible (openButton);
        addAndMakeVisible (menuButton);

        auto tip = resource.isLink() ? resource.url : resource.fileName;

        if (resource.description.isNotEmpty())
            tip << "\n\n" << resource.description;

        setTooltip (tip);
    }

    std::function<void()> onOpen;
    std::function<void (juce::Component&)> onMenu;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel());
        g.fillRoundedRectangle (r, 6.0f);

        auto inner = getLocalBounds().reduced (8);
        drawBadge (g, inner.removeFromLeft (inner.getHeight()).toFloat(), resource);
        inner.removeFromLeft (12);
        inner.removeFromRight (menuButton.getRight() - openButton.getX() + 12);

        auto titleArea = inner.removeFromTop (22);

        if (! resource.tags.isEmpty())
        {
            const auto chipsWidth = tagChipsWidth (resource.tags, titleArea.getWidth() / 2);
            drawTagChips (g, titleArea.removeFromRight (chipsWidth).reduced (0, 2), resource.tags, highlight);
        }

        g.setColour (theme::text());
        g.setFont (juce::FontOptions (16.0f).withStyle ("Bold"));
        g.drawText (resource.title, titleArea, juce::Justification::centredLeft, true);

        auto detail = missing ? juce::String ("File not found here yet (still syncing?)") : resource.description;

        if (detail.isEmpty())
            detail = resource.isLink() ? resource.url : resource.fileName;

        g.setColour (missing ? theme::highlight() : theme::text().withAlpha (0.6f));
        g.setFont (juce::FontOptions (13.5f));
        g.drawFittedText (detail.replaceCharacters ("\r\n", "  "), inner, juce::Justification::topLeft, 2, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        const auto h = juce::jmin (36, r.getHeight());
        const auto menuWidth = menuButton.getButtonText().length() > 3 ? 140 : 44;
        menuButton.setBounds (r.removeFromRight (menuWidth).withSizeKeepingCentre (menuWidth, h));
        r.removeFromRight (6);
        openButton.setBounds (r.removeFromRight (80).withSizeKeepingCentre (80, h));
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (! missing && onOpen)
            onOpen();
    }

private:
    Resource resource;
    bool missing;
    juce::String highlight;
    juce::TextButton openButton { "Open" }, menuButton;
};

//==============================================================================
/** Overlay form: title, URL (links only) and description of a resource. */
class ResourceEditor final : public juce::Component
{
public:
    ResourceEditor (const Resource& r, bool isLink)
    {
        auto setUp = [this] (juce::Label& label, const juce::String& text, juce::TextEditor& editor)
        {
            styleCaption (label, text);
            addAndMakeVisible (label);
            styleEditor (editor);
            editor.setFont (juce::FontOptions (15.0f));
            addAndMakeVisible (editor);
        };

        if (isLink)
        {
            setUp (urlLabel, "Link (URL)", urlEditor);
            urlEditor.setText (r.url, false);
            urlEditor.setTextToShowWhenEmpty ("https://www.youtube.com/watch?v=...", theme::text().withAlpha (0.35f));
        }

        setUp (titleLabel, "Title", titleEditor);
        titleEditor.setText (r.title, false);

        if (isLink)
            titleEditor.setTextToShowWhenEmpty ("(optional: taken from the link)", theme::text().withAlpha (0.35f));

        setUp (tagsLabel, "Tags", tagsEditor);
        tagsEditor.setText (joinTags (r.tags), false);
        tagsEditor.setTextToShowWhenEmpty ("separated by commas", theme::text().withAlpha (0.35f));

        setUp (descriptionLabel, "Description / instructions", descriptionEditor);
        descriptionEditor.setMultiLine (true, true);
        descriptionEditor.setReturnKeyStartsNewLine (true);
        descriptionEditor.setScrollbarsShown (true);
        descriptionEditor.setText (r.description, false);
        descriptionEditor.setTextToShowWhenEmpty ("e.g. play along 10 times a day, 60 -> 90 bpm", theme::text().withAlpha (0.35f));

        saveButton.onClick = [this]
        {
            if (urlEditor.isVisible() && urlEditor.getText().trim().isEmpty())
            {
                urlEditor.grabKeyboardFocus();
                return;
            }

            if (onSave)
                onSave (titleEditor.getText().trim(), urlEditor.getText().trim(), parseTags (tagsEditor.getText()), descriptionEditor.getText());
        };
        cancelButton.onClick = [this] { if (onCancel) onCancel(); };
        addAndMakeVisible (saveButton);
        addAndMakeVisible (cancelButton);
    }

    std::function<void (const juce::String& title, const juce::String& url, const juce::StringArray& tags, const juce::String& description)> onSave;
    std::function<void()> onCancel;

    void parentHierarchyChanged() override
    {
        if (isShowing())
            (urlEditor.isVisible() ? urlEditor : titleEditor).grabKeyboardFocus();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::background());
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        auto buttons = area.removeFromBottom (44);
        saveButton.setBounds (buttons.removeFromRight (120).reduced (3));
        cancelButton.setBounds (buttons.removeFromRight (120).reduced (3));

        if (urlEditor.isVisible())
        {
            urlLabel.setBounds (area.removeFromTop (28));
            urlEditor.setBounds (area.removeFromTop (34));
            area.removeFromTop (10);
        }

        titleLabel.setBounds (area.removeFromTop (28));
        titleEditor.setBounds (area.removeFromTop (34));
        area.removeFromTop (10);
        tagsLabel.setBounds (area.removeFromTop (28));
        tagsEditor.setBounds (area.removeFromTop (34));
        area.removeFromTop (10);
        descriptionLabel.setBounds (area.removeFromTop (28));
        descriptionEditor.setBounds (area.withTrimmedBottom (10));
    }

private:
    juce::Label urlLabel, titleLabel, tagsLabel, descriptionLabel;
    juce::TextEditor urlEditor, titleEditor, tagsEditor, descriptionEditor;
    juce::TextButton saveButton { "Save" }, cancelButton { "Cancel" };
};

} // namespace pv
