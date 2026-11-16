#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace pv
{

/** The app-wide look ("Warm stage"): pill buttons and combo boxes, slim tracks with a ringed thumb, switches instead of
    tick boxes, underlined tabs, Outfit + DM Sans (bundled). Follows theme::palette(): on a theme change it re-applies
    its colours, re-maps colours stored on every window's components and repaints them.
    Set as the default look before any component is created; reset the default before destroying it.
*/
class WarmLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    WarmLookAndFeel();
    ~WarmLookAndFeel() override;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    // Buttons
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isHighlighted, bool isDown) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                      bool ticked, bool isEnabled, bool isHighlighted, bool isDown) override;
    void changeToggleButtonWidthToFitText (juce::ToggleButton&) override;

    // Combo boxes and menus
    void drawComboBox (juce::Graphics&, int width, int height, bool isDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    // Sliders
    int getSliderThumbRadius (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    // Text fields
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    // Tabs
    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
    int getTabButtonOverlap (int) override  { return 0; }
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) override {}
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;

    // Scroll bars
    int getDefaultScrollbarWidth() override  { return 10; }
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isVertical,
                        int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown) override;

    // Labels: section headings get a dot; slider value boxes are rounded.
    void drawLabel (juce::Graphics&, juce::Label&) override;

    // Alerts
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;

private:
    void applyPalette();
    void paletteChanged (const theme::Palette& oldPalette);

    struct Faces;
    std::unique_ptr<Faces> faces;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WarmLookAndFeel)
};

} // namespace pv
