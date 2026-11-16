#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>
#include "Theme.h"

namespace pv
{

/** A row of pill segments (the mock-up's segmented control) with the ComboBox calls the panels use
    (addItem, setSelectedId, getSelectedId, onChange, isPopupActive).

    setSegments() picks the items shown as segments; the others are under a last "..." segment (a menu).
    When the control is too narrow for its segments it draws as one pill with a chevron and opens the menu.
*/
class SegmentedControl final : public juce::Component
{
public:
    SegmentedControl() = default;

    std::function<void()> onChange;

    void addItem (const juce::String& text, int id)
    {
        items.push_back ({ text, id });
        repaint();
    }

    /** The item ids shown as segments (default: all). */
    void setSegments (std::vector<int> ids)
    {
        segmentIds = std::move (ids);
        repaint();
    }

    void setSelectedId (int id, juce::NotificationType notification = juce::sendNotificationAsync)
    {
        if (id == selected)
            return;

        selected = id;
        repaint();

        if (notification != juce::dontSendNotification && onChange != nullptr)
            onChange();
    }

    int getSelectedId() const noexcept     { return selected; }
    bool isPopupActive() const noexcept    { return menuOpen; }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const float alpha = isEnabled() ? 1.0f : 0.45f;
        const auto font = juce::Font (theme::textFont (juce::jmin (13.5f, bounds.getHeight() * 0.55f), "SemiBold"));
        g.setFont (font);

        g.setColour (theme::field().withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

        if (isCompact())
        {
            g.setColour (theme::text().withMultipliedAlpha (alpha));
            auto text = bounds.reduced (bounds.getHeight() * 0.5f, 0.0f);
            g.drawFittedText (textOf (selected), text.withTrimmedRight (14.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

            const float s = 4.0f, cx = text.getRight() - 4.0f, cy = bounds.getCentreY();
            juce::Path chevron;
            chevron.startNewSubPath (cx - s, cy - s * 0.5f);
            chevron.lineTo (cx, cy + s * 0.5f);
            chevron.lineTo (cx + s, cy - s * 0.5f);
            g.setColour (theme::muted().withMultipliedAlpha (alpha));
            g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }

        for (auto& seg : layout())
        {
            const bool isMore = seg.id == moreId;
            const bool on = isMore ? isOverflow (selected) : seg.id == selected;
            const auto r = seg.area.reduced (1.0f);

            if (on)
            {
                g.setColour (theme::accent().withMultipliedAlpha (alpha));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            }
            else if (seg.index == hover && isEnabled())
            {
                g.setColour (theme::text().withAlpha (0.06f));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            }

            const auto label = isMore ? (on ? textOf (selected) : juce::String ("...")) : textOf (seg.id);
            g.setColour ((on ? theme::onAccent() : (seg.index == hover ? theme::text() : theme::muted())).withMultipliedAlpha (alpha));
            g.drawFittedText (label, r.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.75f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isEnabled())
            return;

        if (isCompact())
        {
            showMenu (false);
            return;
        }

        for (auto& seg : layout())
        {
            if (seg.area.contains (e.position))
            {
                if (seg.id == moreId)
                    showMenu (true);
                else
                    setSelectedId (seg.id, juce::sendNotificationSync);

                return;
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        int index = -1;

        if (! isCompact())
            for (auto& seg : layout())
                if (seg.area.contains (e.position))
                    index = seg.index;

        if (index != hover)
        {
            hover = index;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hover = -1;
        repaint();
    }

private:
    struct Item { juce::String text; int id; };
    struct Segment { juce::Rectangle<float> area; int id, index; };
    static constexpr int moreId = std::numeric_limits<int>::min();

    std::vector<Item> items;
    std::vector<int> segmentIds;
    int selected = 0, hover = -1;
    bool menuOpen = false;

    juce::String textOf (int id) const
    {
        for (auto& item : items)
            if (item.id == id)
                return item.text;

        return {};
    }

    std::vector<int> shownIds() const
    {
        if (! segmentIds.empty())
            return segmentIds;

        std::vector<int> all;
        for (auto& item : items)
            all.push_back (item.id);
        return all;
    }

    bool hasOverflow() const        { return shownIds().size() < items.size(); }

    bool isOverflow (int id) const
    {
        const auto shown = shownIds();
        return std::find (shown.begin(), shown.end(), id) == shown.end() && textOf (id).isNotEmpty();
    }

    std::vector<Segment> layout() const
    {
        auto ids = shownIds();

        if (hasOverflow())
            ids.push_back (moreId);

        std::vector<Segment> segs;
        const auto inner = getLocalBounds().toFloat().reduced (3.0f);

        if (ids.empty())
            return segs;

        // Width in proportion to each label (at least a square), so "Triplets" gets more room than "4".
        const auto font = juce::Font (theme::textFont (juce::jmin (13.5f, inner.getHeight() * 0.55f), "SemiBold"));
        std::vector<float> widths;
        float total = 0.0f;

        for (auto id : ids)
        {
            const auto text = id == moreId ? juce::String ("...") : textOf (id);
            const float w = juce::jmax (inner.getHeight(), juce::GlyphArrangement::getStringWidth (font, text) + 18.0f);
            widths.push_back (w);
            total += w;
        }

        const float scale = inner.getWidth() / total;
        float x = inner.getX();

        for (size_t i = 0; i < ids.size(); ++i)
        {
            const float w = widths[i] * scale;
            segs.push_back ({ { x, inner.getY(), w, inner.getHeight() }, ids[i], (int) i });
            x += w;
        }

        return segs;
    }

    /** Too narrow: the labels would be squeezed below ~80 %. */
    bool isCompact() const
    {
        auto ids = shownIds();
        const auto inner = getLocalBounds().toFloat().reduced (3.0f);
        const auto font = juce::Font (theme::textFont (juce::jmin (13.5f, inner.getHeight() * 0.55f), "SemiBold"));
        float needed = hasOverflow() ? inner.getHeight() : 0.0f;

        for (auto id : ids)
            needed += juce::jmax (inner.getHeight() * 0.9f, juce::GlyphArrangement::getStringWidth (font, textOf (id)) + 12.0f);

        return needed > inner.getWidth();
    }

    void showMenu (bool overflowOnly)
    {
        juce::PopupMenu menu;

        for (auto& item : items)
            if (! overflowOnly || isOverflow (item.id))
                menu.addItem (item.id, item.text, true, item.id == selected);

        menuOpen = true;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<SegmentedControl> (this)] (int result)
                            {
                                if (safe == nullptr)
                                    return;

                                safe->menuOpen = false;

                                if (result != 0)
                                    safe->setSelectedId (result, juce::sendNotificationSync);
                            });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentedControl)
};

} // namespace pv
