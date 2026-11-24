#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

namespace pv
{

/** The Fretboard tab: the chord-chart page (Resources/Web/fretboard.html) in a web view, served from inside the app.

    The HTML file is used as it is: when it is served, the Google Fonts links are dropped (the page gets the bundled
    Archivo instead) and a small style sheet is added that gives it the app's colours and hides the page's own title
    and intro. So a new version of the page can simply replace the file.
*/
class FretboardPanel final : public juce::Component
{
public:
    FretboardPanel();
    ~FretboardPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

private:
    struct Browser;

    void createBrowserIfShown();
    void applyTheme();

    std::unique_ptr<Browser> browser;
    juce::Label unavailableLabel;
    juce::TextButton getWebViewButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FretboardPanel)
};

} // namespace pv
