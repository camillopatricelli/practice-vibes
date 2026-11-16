#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Line icons for buttons (shapes from the approved mock-up, Lucide style: 24-unit grid, 2-unit round strokes).

    A TextButton gets an icon with setIcon(); WarmLookAndFeel then draws the icon alone (the text becomes the
    tooltip) or before the text. setRound() makes the big round primary button (Play).
*/
namespace pv::icons
{
    enum class Icon
    {
        none, back, play, pause, stop, loop, zoomIn, zoomOut, fit, folder, stems,
        tuner, gear, bluetooth, save, magnet, metronome, record
    };

    /** Draws `icon` fitted into `area` (square, centred). */
    void draw (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour);

    /** iconOnly: the text isn't drawn; it becomes the tooltip unless the button has one already. */
    void setIcon (juce::Button&, Icon, bool iconOnly);
    Icon getIcon (const juce::Button&);
    bool isIconOnly (const juce::Button&);

    /** The big round primary button (the mock-up's Play): amber gradient circle, icon only. */
    void setRound (juce::Button&);
    bool isRound (const juce::Button&);
}
