#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace pv
{

/** "1:23.4 / 3:45.0" in Outfit, right-aligned. Every digit gets the same width (Outfit's digits are proportional,
    so a plain label would jitter as the time runs). The part after " / " is drawn muted.
*/
class TimeDisplay final : public juce::Component
{
public:
    TimeDisplay()  { setInterceptsMouseClicks (false, false); }

    void setText (const juce::String& newText, juce::NotificationType = juce::dontSendNotification)
    {
        if (newText != text)
        {
            text = newText;
            repaint();
        }
    }

    const juce::String& getText() const noexcept  { return text; }

    void setFontSize (float newSize)  { size = newSize; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const juce::Font font (theme::displayFont (size, "Medium"));
        g.setFont (font);

        float digitWidth = 0.0f;

        for (auto c = (juce::juce_wchar) '0'; c <= '9'; ++c)
            digitWidth = juce::jmax (digitWidth, juce::GlyphArrangement::getStringWidth (font, juce::String::charToString (c)));

        auto widthOf = [&] (juce::juce_wchar c)
        {
            return juce::CharacterFunctions::isDigit (c) ? digitWidth
                                                         : juce::GlyphArrangement::getStringWidth (font, juce::String::charToString (c));
        };

        float total = 0.0f;

        for (auto c : text)
            total += widthOf (c);

        const auto split = text.indexOf (" / ");
        auto x = (float) getWidth() - total;
        int index = 0;

        for (auto c : text)
        {
            const auto w = widthOf (c);
            g.setColour (split >= 0 && index >= split ? theme::muted() : theme::text());
            g.drawText (juce::String::charToString (c), juce::Rectangle<float> (x, 0.0f, w, (float) getHeight()),
                        juce::Justification::centred, false);
            x += w;
            ++index;
        }
    }

private:
    juce::String text;
    float size = 18.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimeDisplay)
};

} // namespace pv
