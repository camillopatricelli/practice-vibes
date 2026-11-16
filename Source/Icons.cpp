#include "Icons.h"

namespace pv::icons
{
namespace
{
    const juce::Identifier iconId ("pvIcon"), iconOnlyId ("pvIconOnly"), roundId ("pvRound");

    /** One icon on the 24-unit grid: outlines (stroked) and solid parts (filled). */
    struct Shape
    {
        juce::Path stroke, fill;
    };

    void svg (juce::Path& p, const char* data)
    {
        p.addPath (juce::Drawable::parseSVGPath (data));
    }

    void circle (juce::Path& p, float cx, float cy, float r)
    {
        p.addEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
    }

    Shape make (Icon icon)
    {
        Shape s;

        switch (icon)
        {
            case Icon::back:      svg (s.fill, "M19 20 9 12l10-8z"); svg (s.stroke, "M5 19V5"); break;
            case Icon::play:      svg (s.fill, "M7 4.5v15a1 1 0 0 0 1.5.86l12-7.5a1 1 0 0 0 0-1.72l-12-7.5A1 1 0 0 0 7 4.5z"); break;
            case Icon::pause:     s.fill.addRoundedRectangle (6.0f, 4.0f, 4.0f, 16.0f, 1.2f);
                                  s.fill.addRoundedRectangle (14.0f, 4.0f, 4.0f, 16.0f, 1.2f); break;
            case Icon::stop:      s.fill.addRoundedRectangle (5.5f, 5.5f, 13.0f, 13.0f, 2.0f); break;
            case Icon::loop:      svg (s.stroke, "m17 2 4 4-4 4M3 11v-1a4 4 0 0 1 4-4h14M7 22l-4-4 4-4M21 13v1a4 4 0 0 1-4 4H3"); break;
            case Icon::zoomIn:    circle (s.stroke, 11, 11, 7); svg (s.stroke, "m20 20-3.5-3.5M11 8v6M8 11h6"); break;
            case Icon::zoomOut:   circle (s.stroke, 11, 11, 7); svg (s.stroke, "m20 20-3.5-3.5M8 11h6"); break;
            case Icon::fit:       svg (s.stroke, "M8 3H5a2 2 0 0 0-2 2v3M21 8V5a2 2 0 0 0-2-2h-3M3 16v3a2 2 0 0 0 2 2h3M16 21h3a2 2 0 0 0 2-2v-3"); break;
            case Icon::folder:    svg (s.stroke, "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"); break;
            case Icon::stems:     svg (s.stroke, "m12 3 9 5-9 5-9-5zM3 13l9 5 9-5"); break;
            case Icon::tuner:     svg (s.stroke, "M4 15a8 8 0 1 1 16 0M12 15l4-5"); circle (s.fill, 12, 15, 1.6f); break;
            case Icon::gear:      svg (s.stroke, "M4 7h10M18 7h2M4 17h4M12 17h8"); circle (s.stroke, 16, 7, 2); circle (s.stroke, 10, 17, 2); break;
            case Icon::bluetooth: svg (s.stroke, "m7 7 10 10-5 5V2l5 5L7 17"); break;
            case Icon::save:      svg (s.stroke, "M5 3h11l3 3v13a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM7 3v5h8V3M7 21v-7h10v7"); break;
            case Icon::magnet:    svg (s.stroke, "M6 3v8a6 6 0 0 0 12 0V3M6 7h4M14 7h4"); break;
            case Icon::metronome: svg (s.stroke, "M9 3h6l4 18H5zM12 15l6-9"); break;
            case Icon::record:    circle (s.fill, 12, 12, 5); break;
            case Icon::none:      break;
        }

        return s;
    }

    const Shape& shapeFor (Icon icon)
    {
        static std::map<Icon, Shape> cache;     // message thread only
        auto it = cache.find (icon);

        if (it == cache.end())
            it = cache.emplace (icon, make (icon)).first;

        return it->second;
    }
}

void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    if (icon == Icon::none || area.isEmpty())
        return;

    const auto& shape = shapeFor (icon);
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const float scale = size / 24.0f;
    const auto transform = juce::AffineTransform::scale (scale).translated (area.getCentreX() - size * 0.5f,
                                                                            area.getCentreY() - size * 0.5f);
    g.setColour (colour);

    if (! shape.fill.isEmpty())
        g.fillPath (shape.fill, transform);

    if (! shape.stroke.isEmpty())
        g.strokePath (shape.stroke, juce::PathStrokeType (juce::jmax (1.2f, 2.0f * scale), juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded), transform);
}

void setIcon (juce::Button& button, Icon icon, bool iconOnly)
{
    auto& props = button.getProperties();

    if ((int) props[iconId] == (int) icon && (bool) props[iconOnlyId] == iconOnly)
        return;

    props.set (iconId, (int) icon);
    props.set (iconOnlyId, iconOnly);

    if (iconOnly && button.getTooltip().isEmpty())
        button.setTooltip (button.getButtonText());

    button.repaint();
}

Icon getIcon (const juce::Button& button)     { return (Icon) (int) button.getProperties()[iconId]; }
bool isIconOnly (const juce::Button& button)  { return (bool) button.getProperties()[iconOnlyId]; }

void setRound (juce::Button& button)
{
    button.getProperties().set (roundId, true);
    button.repaint();
}

bool isRound (const juce::Button& button)     { return (bool) button.getProperties()[roundId]; }

} // namespace pv::icons
