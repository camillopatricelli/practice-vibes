#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/** The app's colours and fonts ("Warm stage": espresso / warm paper surfaces, amber accent, Outfit + DM Sans).

    Colours are read through functions so a switch between the dark and light set takes effect at the next repaint.
    Colours stored on components with setColour() are re-mapped by refreshComponents(): a stored colour whose RGB is
    one of the old palette's roles becomes the same role in the new palette (alpha kept). So: set colours from
    the plain role functions (`theme::text().withAlpha (0.6f)` is fine); derived shades (`brighter()`) are only
    safe in paint code, which reads the palette live.
*/
namespace pv::theme
{
    enum class Mode { system, dark, light };

    struct Palette
    {
        juce::Colour background,    // window / tab background
                     card,          // panels, lists, cards
                     raised,        // buttons, combo boxes
                     field,         // text fields, segmented-control background
                     line,          // hairlines, outlines
                     text,          // main text
                     muted,         // secondary text
                     accent,        // amber: active buttons, filled tracks, loop
                     highlight,     // emphasis text / marks (current chord, warnings, BPM)
                     onAccent,      // text on accent / highlight fills
                     track,         // empty slider track, switch off
                     wave,          // waveform (not played)
                     wavePlayed,    // waveform gradient (played part), start ...
                     wavePlayed2,   // ... and end
                     waveBackground,
                     playhead,
                     record,        // recording, mute, danger
                     good,          // in tune, playing
                     thumb,         // slider thumb centre
                     shadow;        // shading over content (black in dark, warm brown in light)
    };

    const Palette& palette();
    bool isDark();

    Mode getMode();
    /** Changes the mode, updates the palette and tells the listeners (components then repaint). */
    void setMode (Mode);
    juce::String modeName (Mode);
    Mode modeFromName (const juce::String&);

    /** Called (on the message thread) after the palette changed, with the palette that was in use before. */
    void addListener (void* owner, std::function<void (const Palette& oldPalette)>);
    void removeListener (void* owner);

    /** Re-maps colours stored on `root` and its children from `oldPalette` to the current one, and repaints. */
    void refreshComponents (juce::Component& root, const Palette& oldPalette);

    // Shorthands (live values).
    inline juce::Colour background()  { return palette().background; }
    inline juce::Colour panel()       { return palette().card; }
    inline juce::Colour card()        { return palette().card; }
    inline juce::Colour raised()      { return palette().raised; }
    inline juce::Colour field()       { return palette().field; }
    inline juce::Colour line()        { return palette().line; }
    inline juce::Colour text()        { return palette().text; }
    inline juce::Colour muted()       { return palette().muted; }
    inline juce::Colour accent()      { return palette().accent; }
    inline juce::Colour highlight()   { return palette().highlight; }
    inline juce::Colour onAccent()    { return palette().onAccent; }
    inline juce::Colour track()       { return palette().track; }
    inline juce::Colour record()      { return palette().record; }
    inline juce::Colour good()        { return palette().good; }
    inline juce::Colour shadow()      { return palette().shadow; }
    /** Darkening over content (lanes, meter wells): black in dark, a light warm wash in light. */
    inline juce::Colour shade (float darkAlpha)  { return isDark() ? juce::Colours::black.withAlpha (darkAlpha) : palette().shadow.withAlpha (darkAlpha * 0.25f); }
    /** Selected list rows: translucent amber, so the row's own text colours stay readable. */
    inline juce::Colour selection()   { return palette().accent.withAlpha (isDark() ? 0.22f : 0.30f); }

    // Fonts: DM Sans for text (the default typeface), Outfit for headings and big numbers.
    inline const char* const displayFamily = "Outfit";
    inline const char* const textFamily    = "DM Sans";

    inline juce::FontOptions textFont (float size, const char* style = "Regular")    { return juce::FontOptions (textFamily, size, juce::Font::plain).withStyle (style); }
    inline juce::FontOptions displayFont (float size, const char* style = "SemiBold") { return juce::FontOptions (displayFamily, size, juce::Font::plain).withStyle (style); }
    inline juce::FontOptions headerFont()  { return displayFont (18.0f, "SemiBold"); }

    /** A section heading: Outfit, with the mock-up's small amber dot before it (drawn by WarmLookAndFeel). */
    inline void styleHeader (juce::Label& label)
    {
        label.setFont (headerFont());
        label.getProperties().set ("pvSection", true);
        label.repaint();
    }

    /** A rounded card (the mock-up's sections): card colour, hairline, soft depth in light. */
    inline void paintCard (juce::Graphics& g, juce::Rectangle<float> r, float corner = 18.0f)
    {
        if (r.isEmpty())
            return;

        if (! isDark())
        {
            g.setColour (palette().shadow.withAlpha (0.07f));
            g.fillRoundedRectangle (r.translated (0.0f, 3.0f).expanded (1.0f), corner + 1.0f);
        }

        g.setColour (card());
        g.fillRoundedRectangle (r, corner);
        g.setColour (line());
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    }

    /** Padding between a list card's edge and the ListBox inside it (see paintListCard). */
    inline constexpr int listCardPadX = 4, listCardPadY = 6;

    /** Lists sit in a rounded card: give the ListBox a transparent background, place it at
        `cardArea.reduced (listCardPadX, listCardPadY)` and call this from the parent's paint(). */
    inline void paintListCard (juce::Graphics& g, const juce::Component& list)
    {
        if (list.isVisible())
            paintCard (g, list.getBounds().expanded (listCardPadX, listCardPadY).toFloat(), 14.0f);
    }

    /** A list row: a rounded pill inset from the list's edges, translucent amber with an amber rim when selected.
        Returns the area for the row's content. */
    inline juce::Rectangle<int> paintListRow (juce::Graphics& g, int width, int height, bool selected)
    {
        const auto pill = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (2.0f, 1.5f);

        if (selected)
        {
            g.setColour (selection());
            g.fillRoundedRectangle (pill, 10.0f);
            g.setColour (accent().withAlpha (0.45f));
            g.drawRoundedRectangle (pill.reduced (0.5f), 10.0f, 1.0f);
        }

        return pill.toNearestInt().reduced (10, 4);
    }
}
