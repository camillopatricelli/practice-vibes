#include "WarmLookAndFeel.h"
#include "FontData.h"

namespace pv
{

namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }

    /** The given text colour, or a dark / light one if it wouldn't be readable on `fill` (amber buttons want dark text). */
    juce::Colour readableOn (juce::Colour fill, juce::Colour wanted)
    {
        if (fill.getAlpha() < 80)
            return wanted;

        const float fb = fill.getPerceivedBrightness(), tb = wanted.getPerceivedBrightness();

        if (std::abs (fb - tb) >= 0.38f)
            return wanted;

        return fb > 0.55f ? theme::onAccent() : juce::Colour (0xfff6ebe1);
    }

    bool isBright (juce::Colour c)  { return c.getPerceivedBrightness() > 0.55f; }
}

struct WarmLookAndFeel::Faces
{
    juce::Typeface::Ptr textRegular  = load (FontData::DMSansRegular_ttf,   FontData::DMSansRegular_ttfSize),
                        textItalic   = load (FontData::DMSansItalic_ttf,    FontData::DMSansItalic_ttfSize),
                        textMedium   = load (FontData::DMSansMedium_ttf,    FontData::DMSansMedium_ttfSize),
                        textSemiBold = load (FontData::DMSansSemiBold_ttf,  FontData::DMSansSemiBold_ttfSize),
                        textBold     = load (FontData::DMSansBold_ttf,      FontData::DMSansBold_ttfSize),
                        displayRegular  = load (FontData::OutfitRegular_ttf,  FontData::OutfitRegular_ttfSize),
                        displayMedium   = load (FontData::OutfitMedium_ttf,   FontData::OutfitMedium_ttfSize),
                        displaySemiBold = load (FontData::OutfitSemiBold_ttf, FontData::OutfitSemiBold_ttfSize),
                        displayBold     = load (FontData::OutfitBold_ttf,     FontData::OutfitBold_ttfSize);
};

WarmLookAndFeel::WarmLookAndFeel()
    : faces (std::make_unique<Faces>())
{
    applyPalette();
    theme::addListener (this, [this] (const theme::Palette& old) { paletteChanged (old); });
}

WarmLookAndFeel::~WarmLookAndFeel()
{
    theme::removeListener (this);
}

//==============================================================================
juce::Typeface::Ptr WarmLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    const auto name = font.getTypefaceName();
    const auto style = font.getTypefaceStyle().toLowerCase().removeCharacters (" -");
    const bool display = name == theme::displayFamily;

    if (display || name == theme::textFamily || name == juce::Font::getDefaultSansSerifFontName())
    {
        const bool semi = style.contains ("semibold") || style.contains ("demibold");
        const bool bold = ! semi && (style.contains ("bold") || style.contains ("black") || style.contains ("heavy"));
        const bool medium = style.contains ("medium");
        const bool italic = style.contains ("italic") || style.contains ("oblique");

        juce::Typeface::Ptr face;

        if (display)
            face = bold ? faces->displayBold : semi ? faces->displaySemiBold : medium ? faces->displayMedium : faces->displayRegular;
        else
            face = bold ? faces->textBold : semi ? faces->textSemiBold : medium ? faces->textMedium
                 : italic ? faces->textItalic : faces->textRegular;

        if (face != nullptr)
            return face;
    }

    return LookAndFeel_V4::getTypefaceForFont (font);
}

//==============================================================================
void WarmLookAndFeel::applyPalette()
{
    const auto& p = theme::palette();
    const auto none = juce::Colours::transparentBlack;

    setColourScheme ({ p.background, p.card, p.card, p.line, p.text, p.accent, p.onAccent, p.accent, p.text });

    const std::pair<int, juce::Colour> entries[]
    {
        { juce::ResizableWindow::backgroundColourId,       p.background },
        { juce::DocumentWindow::textColourId,              p.text },

        { juce::TextButton::buttonColourId,                p.raised },
        { juce::TextButton::buttonOnColourId,              p.accent },
        { juce::TextButton::textColourOffId,               p.text },
        { juce::TextButton::textColourOnId,                p.onAccent },
        { juce::ToggleButton::textColourId,                p.text },
        { juce::ToggleButton::tickColourId,                p.accent },
        { juce::ToggleButton::tickDisabledColourId,        p.track },
        { juce::HyperlinkButton::textColourId,             p.highlight },

        { juce::Label::textColourId,                       p.text },
        { juce::Label::backgroundColourId,                 none },
        { juce::Label::outlineColourId,                    none },
        { juce::Label::textWhenEditingColourId,            p.text },
        { juce::Label::backgroundWhenEditingColourId,      p.field },
        { juce::Label::outlineWhenEditingColourId,         p.accent },

        { juce::ComboBox::backgroundColourId,              p.raised },
        { juce::ComboBox::textColourId,                    p.text },
        { juce::ComboBox::outlineColourId,                 p.line },
        { juce::ComboBox::buttonColourId,                  p.raised },
        { juce::ComboBox::arrowColourId,                   p.muted },
        { juce::ComboBox::focusedOutlineColourId,          p.accent },

        { juce::PopupMenu::backgroundColourId,             p.card },
        { juce::PopupMenu::textColourId,                   p.text },
        { juce::PopupMenu::headerTextColourId,             p.muted },
        { juce::PopupMenu::highlightedBackgroundColourId,  p.accent },
        { juce::PopupMenu::highlightedTextColourId,        p.onAccent },

        { juce::Slider::backgroundColourId,                p.track },
        { juce::Slider::trackColourId,                     p.accent },
        { juce::Slider::thumbColourId,                     p.accent },
        { juce::Slider::textBoxTextColourId,               p.text },
        { juce::Slider::textBoxBackgroundColourId,         p.field },
        { juce::Slider::textBoxHighlightColourId,          p.accent.withAlpha (0.4f) },
        { juce::Slider::textBoxOutlineColourId,            none },

        { juce::TextEditor::backgroundColourId,            p.field },
        { juce::TextEditor::textColourId,                  p.text },
        { juce::TextEditor::highlightColourId,             p.accent.withAlpha (0.35f) },
        { juce::TextEditor::highlightedTextColourId,       p.text },
        { juce::TextEditor::outlineColourId,               p.line },
        { juce::TextEditor::focusedOutlineColourId,        p.accent },
        { juce::TextEditor::shadowColourId,                none },
        { juce::CaretComponent::caretColourId,             p.accent },

        { juce::ListBox::backgroundColourId,               p.card },
        { juce::ListBox::outlineColourId,                  none },
        { juce::ListBox::textColourId,                     p.text },

        { juce::ScrollBar::thumbColourId,                  p.muted },
        { juce::ScrollBar::trackColourId,                  none },
        { juce::ScrollBar::backgroundColourId,             none },

        { juce::TabbedButtonBar::tabOutlineColourId,       none },
        { juce::TabbedButtonBar::frontOutlineColourId,     none },
        { juce::TabbedButtonBar::tabTextColourId,          p.muted },
        { juce::TabbedButtonBar::frontTextColourId,        p.text },
        { juce::TabbedComponent::backgroundColourId,       p.background },
        { juce::TabbedComponent::outlineColourId,          none },

        { juce::AlertWindow::backgroundColourId,           p.card },
        { juce::AlertWindow::textColourId,                 p.text },
        { juce::AlertWindow::outlineColourId,              p.line },

        { juce::ProgressBar::backgroundColourId,           p.track },
        { juce::ProgressBar::foregroundColourId,           p.accent },

        { juce::TooltipWindow::backgroundColourId,         p.raised },
        { juce::TooltipWindow::textColourId,               p.text },
        { juce::TooltipWindow::outlineColourId,            p.line },

        { juce::GroupComponent::outlineColourId,           p.line },
        { juce::GroupComponent::textColourId,              p.muted },

        { juce::DirectoryContentsDisplayComponent::highlightColourId,      p.accent },
        { juce::DirectoryContentsDisplayComponent::textColourId,           p.text },
        { juce::DirectoryContentsDisplayComponent::highlightedTextColourId, p.onAccent },
    };

    for (auto& [id, colour] : entries)
        setColour (id, colour);
}

void WarmLookAndFeel::paletteChanged (const theme::Palette& oldPalette)
{
    applyPalette();

    auto& desktop = juce::Desktop::getInstance();

    for (int i = desktop.getNumComponents(); --i >= 0;)
    {
        if (auto* window = desktop.getComponent (i))
        {
            theme::refreshComponents (*window, oldPalette);
            window->sendLookAndFeelChange();
        }
    }
}

//==============================================================================
juce::Font WarmLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme::textFont (juce::jmin (15.0f, (float) buttonHeight * 0.55f), "Medium");
}

void WarmLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                            bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = juce::jmin (bounds.getHeight() * 0.5f, 22.0f);

    auto base = backgroundColour;

    if (! button.isEnabled())
        base = base.withMultipliedAlpha (0.45f);
    else if (isDown)
        base = isBright (base) ? base.darker (0.12f) : base.brighter (0.18f);
    else if (isHighlighted)
        base = isBright (base) ? base.brighter (0.06f) : base.brighter (0.09f);

    const bool flatLeft   = button.isConnectedOnLeft(),  flatRight  = button.isConnectedOnRight(),
               flatTop    = button.isConnectedOnTop(),   flatBottom = button.isConnectedOnBottom();

    juce::Path shape;
    shape.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), corner, corner,
                               ! (flatLeft || flatTop), ! (flatRight || flatTop),
                               ! (flatLeft || flatBottom), ! (flatRight || flatBottom));

    g.setColour (base);
    g.fillPath (shape);

    // A hairline on the plain (raised) buttons; coloured ones stand out on their own.
    if (std::abs (base.getPerceivedBrightness() - theme::raised().getPerceivedBrightness()) < 0.08f)
    {
        g.setColour (theme::line());
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }
}

void WarmLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const bool on = button.getToggleState();
    const auto fill = button.findColour (on ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
    auto colour = readableOn (fill, button.findColour (on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId));

    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.5f);

    const auto font = getTextButtonFont (button, button.getHeight());
    g.setFont (font);
    g.setColour (colour);

    const int yIndent = juce::jmin (4, button.proportionOfHeight (0.3f));
    const int pad = juce::jmin (16, juce::roundToInt ((float) button.getHeight() * 0.42f));
    const int leftIndent  = button.isConnectedOnLeft()  ? 6 : pad;
    const int rightIndent = button.isConnectedOnRight() ? 6 : pad;
    const int textWidth = button.getWidth() - leftIndent - rightIndent;

    if (textWidth > 0)
        g.drawFittedText (button.getButtonText(), leftIndent, yIndent, textWidth, button.getHeight() - yIndent * 2,
                          juce::Justification::centred, 2, 0.85f);
}

//==============================================================================
// Toggle buttons: a switch (track + knob) left of the text.
namespace
{
    struct SwitchMetrics { float fontSize, w, h; };

    SwitchMetrics switchMetrics (const juce::Component& b)
    {
        const float fontSize = juce::jmin (15.0f, (float) b.getHeight() * 0.72f);
        const float h = juce::jmin (fontSize * 1.25f, (float) b.getHeight() - 2.0f);
        return { fontSize, h * 1.7f, h };
    }
}

void WarmLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isHighlighted, bool isDown)
{
    const auto m = switchMetrics (button);

    drawTickBox (g, button, 2.0f, ((float) button.getHeight() - m.h) * 0.5f, m.w, m.h,
                 button.getToggleState(), button.isEnabled(), isHighlighted, isDown);

    if (button.getButtonText().isEmpty())
        return;

    g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.setFont (theme::textFont (m.fontSize));
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().withTrimmedLeft (juce::roundToInt (m.w) + 10).withTrimmedRight (2),
                      juce::Justification::centredLeft, 10);
}

void WarmLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component, float x, float y, float w, float h,
                                   bool ticked, bool isEnabled, bool isHighlighted, bool)
{
    // Callers other than drawToggleButton pass a square: make it a switch of the same height.
    if (w < h * 1.5f)
        w = h * 1.7f;

    const juce::Rectangle<float> track (x, y, w, h);
    auto trackColour = ticked ? component.findColour (juce::ToggleButton::tickColourId) : theme::track();

    if (! isEnabled)
        trackColour = trackColour.withMultipliedAlpha (0.45f);
    else if (isHighlighted)
        trackColour = trackColour.brighter (0.06f);

    g.setColour (trackColour);
    g.fillRoundedRectangle (track, h * 0.5f);

    const float knob = h - 4.0f;
    const auto knobArea = juce::Rectangle<float> (knob, knob)
                              .withCentre ({ ticked ? track.getRight() - 2.0f - knob * 0.5f : track.getX() + 2.0f + knob * 0.5f,
                                             track.getCentreY() });

    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillEllipse (knobArea.translated (0.0f, 1.0f));
    g.setColour (isEnabled ? juce::Colours::white : juce::Colours::white.withAlpha (0.6f));
    g.fillEllipse (knobArea);
}

void WarmLookAndFeel::changeToggleButtonWidthToFitText (juce::ToggleButton& button)
{
    const auto m = switchMetrics (button);
    const auto font = juce::Font (theme::textFont (m.fontSize));
    button.setSize (juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, button.getButtonText()) + m.w + 16.0f),
                    button.getHeight());
}

//==============================================================================
void WarmLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const float corner = juce::jmin (bounds.getHeight() * 0.5f, 22.0f);

    auto fill = box.findColour (juce::ComboBox::backgroundColourId);
    if (! box.isEnabled())
        fill = fill.withMultipliedAlpha (0.5f);
    else if (box.isMouseOver (true))
        fill = fill.brighter (0.06f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (box.hasKeyboardFocus (true) ? box.findColour (juce::ComboBox::focusedOutlineColourId)
                                             : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    // Chevron
    const float s = juce::jmin (9.0f, (float) height * 0.28f);
    const float cx = (float) width - corner * 0.5f - s * 0.9f, cy = (float) height * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath (cx - s * 0.5f, cy - s * 0.25f);
    chevron.lineTo (cx, cy + s * 0.25f);
    chevron.lineTo (cx + s * 0.5f, cy - s * 0.25f);

    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font WarmLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return theme::textFont (juce::jmin (15.0f, (float) box.getHeight() * 0.55f));
}

void WarmLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const int h = box.getHeight();
    const int left = juce::jmin (14, h / 2);
    const int arrowZone = juce::jmin (h / 2, 22) + 14;

    label.setBounds (left - 3, 1, box.getWidth() - left - arrowZone + 3, h - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font WarmLookAndFeel::getPopupMenuFont()
{
    return theme::textFont (15.5f);
}

void WarmLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (theme::line());
    g.drawRect (0, 0, width, height);
}

//==============================================================================
int WarmLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    if (slider.isBar() || slider.isRotary())
        return LookAndFeel_V4::getSliderThumbRadius (slider);

    return 10;
}

void WarmLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float minSliderPos, float maxSliderPos,
                                        juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const bool horizontal = slider.isHorizontal();
    const float alpha = slider.isEnabled() ? 1.0f : 0.4f;
    const float trackThickness = 6.0f;

    const juce::Point<float> start = horizontal ? juce::Point<float> ((float) x, (float) y + (float) height * 0.5f)
                                                : juce::Point<float> ((float) x + (float) width * 0.5f, (float) (y + height));
    const juce::Point<float> end   = horizontal ? juce::Point<float> ((float) (x + width), start.y)
                                                : juce::Point<float> (start.x, (float) y);
    const juce::Point<float> thumb = horizontal ? juce::Point<float> (sliderPos, start.y)
                                                : juce::Point<float> (start.x, sliderPos);

    juce::Path background;
    background.startNewSubPath (start);
    background.lineTo (end);
    g.setColour (slider.findColour (juce::Slider::backgroundColourId).withMultipliedAlpha (alpha));
    g.strokePath (background, { trackThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    const auto accent = slider.findColour (juce::Slider::trackColourId).withMultipliedAlpha (alpha);

    juce::Path value;
    value.startNewSubPath (start);
    value.lineTo (thumb);
    g.setColour (accent);
    g.strokePath (value, { trackThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    // Ringed thumb with a soft halo.
    const float r = (float) getSliderThumbRadius (slider) - 1.0f;
    const bool active = slider.isMouseButtonDown() || slider.isMouseOverOrDragging();
    g.setColour (accent.withMultipliedAlpha (active ? 0.25f : 0.15f));
    g.fillEllipse (juce::Rectangle<float> ((r + 4.0f) * 2.0f, (r + 4.0f) * 2.0f).withCentre (thumb));
    g.setColour (theme::palette().thumb.withMultipliedAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (thumb));
    g.setColour (accent);
    g.drawEllipse (juce::Rectangle<float> ((r - 1.5f) * 2.0f, (r - 1.5f) * 2.0f).withCentre (thumb), 3.0f);
}

//==============================================================================
void WarmLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (dynamic_cast<juce::AlertWindow*> (editor.getParentComponent()) != nullptr || editor.isMultiLine())
    {
        const float corner = editor.isMultiLine() ? 10.0f : 0.0f;
        g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height), corner);
        return;
    }

    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height), juce::jmin (10.0f, (float) height * 0.5f));
}

void WarmLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    const float corner = editor.isMultiLine() ? 10.0f : juce::jmin (10.0f, (float) height * 0.5f);
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);

    if (editor.hasKeyboardFocus (true) && ! editor.isReadOnly())
    {
        g.setColour (editor.findColour (juce::TextEditor::focusedOutlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.6f);
    }
    else
    {
        g.setColour (editor.findColour (juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle (bounds, corner, 1.0f);
    }
}

//==============================================================================
int WarmLookAndFeel::getTabButtonBestWidth (juce::TabBarButton& button, int tabDepth)
{
    const auto font = juce::Font (theme::displayFont (juce::jmin (16.0f, (float) tabDepth * 0.42f), "SemiBold"));
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, button.getButtonText().trim())) + 28;
}

void WarmLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool)
{
    const auto area = button.getActiveArea().toFloat();
    const bool front = button.isFrontTab();
    auto& bar = button.getTabbedButtonBar();

    const auto colour = front ? bar.findColour (juce::TabbedButtonBar::frontTextColourId)
                              : isMouseOver ? theme::text().withAlpha (0.85f)
                                            : bar.findColour (juce::TabbedButtonBar::tabTextColourId);

    g.setColour (colour);
    g.setFont (theme::displayFont (juce::jmin (16.0f, area.getHeight() * 0.42f), front ? "SemiBold" : "Medium"));
    g.drawText (button.getButtonText().trim(), area, juce::Justification::centred, true);

    if (front)
    {
        g.setColour (theme::accent());
        g.fillRoundedRectangle (area.getX() + 12.0f, area.getBottom() - 3.0f, area.getWidth() - 24.0f, 2.5f, 1.25f);
    }
}

void WarmLookAndFeel::drawTabAreaBehindFrontButton (juce::TabbedButtonBar& bar, juce::Graphics& g, int w, int h)
{
    g.setColour (theme::line());

    if (bar.isVertical())
        g.fillRect (bar.getOrientation() == juce::TabbedButtonBar::TabsAtLeft ? w - 1 : 0, 0, 1, h);
    else
        g.fillRect (0, bar.getOrientation() == juce::TabbedButtonBar::TabsAtTop ? h - 1 : 0, w, 1);
}

//==============================================================================
void WarmLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height, bool isVertical,
                                     int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    juce::Rectangle<int> thumb;

    if (isVertical)
        thumb = { x, thumbStart, width, thumbSize };
    else
        thumb = { thumbStart, y, thumbSize, height };

    const float inset = isMouseOver || isMouseDown ? 2.0f : 3.0f;
    auto r = thumb.toFloat().reduced (inset);
    const float corner = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;

    g.setColour (bar.findColour (juce::ScrollBar::thumbColourId).withAlpha (isMouseDown ? 0.75f : isMouseOver ? 0.6f : 0.4f));
    g.fillRoundedRectangle (r, corner);
}

//==============================================================================
juce::Font WarmLookAndFeel::getAlertWindowTitleFont()    { return theme::displayFont (19.0f, "SemiBold"); }
juce::Font WarmLookAndFeel::getAlertWindowMessageFont()  { return theme::textFont (15.0f); }
juce::Font WarmLookAndFeel::getAlertWindowFont()         { return theme::textFont (14.0f); }

} // namespace pv
