#include "FretboardPanel.h"
#include "Localisation.h"
#include "Theme.h"
#include "WebView.h"
#include "WebData.h"

namespace pv
{

namespace
{
    constexpr const char* webViewDownloadUrl = "https://developer.microsoft.com/microsoft-edge/webview2/";

    juce::String cssColour (juce::Colour c)
    {
        return "rgba(" + juce::String (c.getRed()) + "," + juce::String (c.getGreen()) + "," + juce::String (c.getBlue())
             + "," + juce::String (c.getFloatAlpha(), 3) + ")";
    }

    /** The page's colour variables set to the app's palette (the page's own names: --bg, --ink, --soft, --line, --panel). */
    juce::String themeCss()
    {
        return ":root{--bg:" + cssColour (theme::background())
             + ";--ink:" + cssColour (theme::text())
             + ";--soft:" + cssColour (theme::muted())
             + ";--line:" + cssColour (theme::line())
             + ";--panel:" + cssColour (theme::card())
             + ";--pv-raised:" + cssColour (theme::raised())
             + ";--pv-field:" + cssColour (theme::field())
             + ";--pv-accent:" + cssColour (theme::accent())
             + ";--pv-on-accent:" + cssColour (theme::onAccent()) + "}";
    }

    juce::String themeName()  { return theme::isDark() ? "dark" : "light"; }

    /** Added to every page: the bundled font, the app's buttons, and the page's own title and intro hidden
        (the tab bar names the page). Only page classes that change nothing when missing, so a new version of the
        page still works. */
    constexpr const char* appCss =
        "@font-face{font-family:Archivo;src:url(fonts/Archivo.ttf) format('truetype');font-weight:100 900;font-stretch:62% 125%;font-style:normal}"
        "header.band h1,header.band .lede,header.band .nav{display:none}"
        "header.band{padding-top:16px}"
        ".seg button,.seg .lab{background:var(--pv-field)}"
        ".seg button[aria-pressed=true]{background:var(--pv-accent);color:var(--pv-on-accent)}"
        "select{background:var(--pv-raised)}"
        ".play{background:var(--pv-accent);border-color:var(--pv-accent);color:var(--pv-on-accent)}"
        ".play.alt{background:var(--pv-raised);border-color:var(--line);color:var(--ink)}"
        ".card[aria-current=true]{border-color:var(--pv-accent);box-shadow:0 0 0 1px var(--pv-accent)}"
        "button:focus-visible,select:focus-visible,input:focus-visible{outline-color:var(--pv-accent)}"
        "input[type=checkbox]{accent-color:var(--pv-accent)}";

    /** The page as served: without the web-font links, with the app's style sheet and theme before </head>. */
    juce::String servedPage()
    {
        const auto source = juce::String::fromUTF8 (WebData::fretboard_html, WebData::fretboard_htmlSize);

        juce::StringArray lines;
        lines.addLines (source);

        juce::StringArray kept;

        for (auto& line : lines)
        {
            const auto t = line.trimStart();

            if (t.startsWithIgnoreCase ("<link") && (t.contains ("fonts.googleapis.com") || t.contains ("fonts.gstatic.com")))
                continue;

            kept.add (line);
        }

        auto page = kept.joinIntoString ("\n");

        const auto inject = "<style id=\"pv-app\">" + juce::String (appCss) + "</style>"
                          + "<style id=\"pv-colours\">" + themeCss() + "</style>"
                          + "<script>document.documentElement.setAttribute('data-theme','" + themeName() + "')</script>\n";

        const auto headEnd = page.indexOfIgnoreCase ("</head>");
        return headEnd >= 0 ? page.replaceSection (headEnd, 0, inject) : inject + page;
    }

    juce::WebBrowserComponent::Resource makeResource (const void* data, size_t size, const char* mimeType)
    {
        juce::WebBrowserComponent::Resource r;
        const auto* bytes = static_cast<const std::byte*> (data);
        r.data.assign (bytes, bytes + size);
        r.mimeType = mimeType;
        return r;
    }

    std::optional<juce::WebBrowserComponent::Resource> provide (const juce::String& path)
    {
        const auto p = path.upToFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("#", false, false);

        if (p == "/" || p == "/fretboard.html")
        {
            const auto page = servedPage().toStdString();
            return makeResource (page.data(), page.size(), "text/html");
        }

        if (p == "/fonts/Archivo.ttf")
            return makeResource (WebData::Archivo_ttf, (size_t) WebData::Archivo_ttfSize, "font/ttf");

        return std::nullopt;
    }

    juce::WebBrowserComponent::Options fretboardOptions()
    {
        auto options = webViewOptions();

       #if JUCE_WINDOWS
        // Same as webViewOptions(), plus the app's background while the page loads (no white flash).
        options = options.withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
                                                      .withUserDataFolder (getAppDataFolder().getChildFile ("WebView2"))
                                                      .withBackgroundColour (theme::background().withAlpha (1.0f))
                                                      .withStatusBarDisabled());
       #endif

        return options.withResourceProvider ([] (const juce::String& path) { return provide (path); });
    }
}

//==============================================================================
struct FretboardPanel::Browser final : public juce::WebBrowserComponent
{
    Browser() : juce::WebBrowserComponent (fretboardOptions()) {}

    bool pageAboutToLoad (const juce::String& url) override
    {
        if (url.startsWith (getResourceProviderRoot()) || url.startsWith ("about:") || url.startsWith ("data:") || url.startsWith ("blob:"))
            return true;

        juce::URL (url).launchInDefaultBrowser();   // a link to somewhere else: the user's browser
        return false;
    }

    void newWindowAttemptingToLoad (const juce::String& url) override
    {
        juce::URL (url).launchInDefaultBrowser();
    }
};

//==============================================================================
FretboardPanel::FretboardPanel()
{
    unavailableLabel.setJustificationType (juce::Justification::centred);
    unavailableLabel.setFont (theme::textFont (16.0f));
    unavailableLabel.setText (tr ("The Fretboard page needs Microsoft WebView2, which isn't installed on this computer.") + "\n"
                              + tr ("It is free from Microsoft: install it, then restart Practice Vibes."),
                              juce::dontSendNotification);
    addChildComponent (unavailableLabel);

    getWebViewButton.setButtonText (tr ("Get WebView2"));
    getWebViewButton.onClick = [] { juce::URL (webViewDownloadUrl).launchInDefaultBrowser(); };
    addChildComponent (getWebViewButton);

    const bool available = webViewAvailable();
    unavailableLabel.setVisible (! available);
    getWebViewButton.setVisible (! available);

    theme::addListener (this, [this] (const theme::Palette&) { applyTheme(); });
}

FretboardPanel::~FretboardPanel()
{
    theme::removeListener (this);
}

void FretboardPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
}

void FretboardPanel::resized()
{
    auto area = getLocalBounds();

    if (browser != nullptr)
        browser->setBounds (area);

    auto box = area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 32, 560), 140);
    unavailableLabel.setBounds (box.removeFromTop (88));
    getWebViewButton.setBounds (box.removeFromTop (40).withSizeKeepingCentre (180, 40));
}

void FretboardPanel::visibilityChanged()       { createBrowserIfShown(); }
void FretboardPanel::parentHierarchyChanged()  { createBrowserIfShown(); }

/** The web view starts the first time the tab is shown, not at app start (it takes memory and a moment to start). */
void FretboardPanel::createBrowserIfShown()
{
    if (browser != nullptr || ! isShowing() || ! webViewAvailable())
        return;

    browser = std::make_unique<Browser>();
    addAndMakeVisible (*browser);
    resized();
    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot() + "fretboard.html");
}

/** Theme switch while the page is open: swap its colour style sheet (the page keeps its state). */
void FretboardPanel::applyTheme()
{
    repaint();

    if (browser == nullptr)
        return;

    browser->evaluateJavascript ("(function(){var s=document.getElementById('pv-colours');if(s)s.textContent="
                                 + juce::JSON::toString (juce::var (themeCss()), true)
                                 + ";document.documentElement.setAttribute('data-theme','" + themeName() + "');})();");
}

} // namespace pv
