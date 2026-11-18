#include "AboutPanel.h"

#include "BuildInfo.h"
#include "Localisation.h"
#include "NoticesData.h"
#include "Theme.h"

namespace pv
{

namespace
{
    // Public details approved by the owner (2026-11-18). The guide isn't published yet: until it is, "User guide"
    // opens the README on GitHub.
    constexpr const char* developerName = "Camillo Patricelli";
    constexpr const char* contactAddress = "practicevibes.app@gmail.com";
    constexpr const char* repositoryUrl = "https://github.com/camillopatricelli/practice-vibes";
    constexpr const char* guideUrl = "https://github.com/camillopatricelli/practice-vibes#readme";
    constexpr const char* privacyUrl = "https://github.com/camillopatricelli/practice-vibes/blob/main/PRIVACY.md";
    constexpr const char* licenceUrl = "https://github.com/camillopatricelli/practice-vibes/blob/main/LICENSE";

    juce::String noticesText()
    {
        return juce::String::fromUTF8 (NoticesData::THIRD_PARTY_NOTICES_md, NoticesData::THIRD_PARTY_NOTICES_mdSize);
    }
}

AboutPanel::AboutPanel (std::function<juce::String()> info)
    : audioInfo (std::move (info))
{
    getProperties().set ("overlayWidth", 680);
    getProperties().set ("overlayHeight", 660);

    title.setText ("Practice Vibes", juce::dontSendNotification);
    title.setFont (theme::displayFont (30.0f));
    title.setJustificationType (juce::Justification::centred);

    version.setText (build::number().isNotEmpty() ? tr ("Version {version} (build {build})", build::version(), build::number())
                                                  : tr ("Version {version} (local build)", build::version()),
                     juce::dontSendNotification);
    platform.setText (build::platform(), juce::dontSendNotification);
    developer.setText (tr ("Made by {name}", developerName), juce::dontSendNotification);
    contact.setButtonText (contactAddress);
    contact.setURL (juce::URL ("mailto:" + juce::String (contactAddress)));
    contact.setFont (theme::textFont (15.0f), false, juce::Justification::centred);
    contact.setTooltip (tr ("Write to the developer"));

    licenceNote.setText (tr ("Copyright 2026 {name}. Free software under the GNU AGPLv3: you may use, study, share and change it. "
                             "It comes with no warranty.", developerName),
                         juce::dontSendNotification);
    credit.setText (tr ("Written with Claude Code (Anthropic's AI). Built with JUCE."), juce::dontSendNotification);

    for (auto* l : { &version, &platform, &developer, &licenceNote, &credit })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (theme::textFont (15.0f));
    }

    for (auto* l : { &platform, &licenceNote, &credit })
        l->setColour (juce::Label::textColourId, theme::text().withAlpha (0.65f));

    for (auto* c : std::initializer_list<juce::Component*> { &title, &version, &platform, &developer, &contact, &licenceNote, &credit })
        addAndMakeVisible (c);

    const auto link = [this] (juce::TextButton& b, const juce::String& text, const char* url)
    {
        b.setButtonText (text);
        b.setTooltip (url);
        b.onClick = [url] { juce::URL (url).launchInDefaultBrowser(); };
        addAndMakeVisible (b);
    };

    link (guideButton, tr ("User guide"), guideUrl);
    link (privacyButton, tr ("Privacy"), privacyUrl);
    link (licenceButton, tr ("Licence"), licenceUrl);
    link (sourceButton, tr ("Source code"), repositoryUrl);

    noticesButton.setButtonText (tr ("Open source licences"));
    noticesButton.setTooltip (tr ("The licences of the libraries, fonts and sounds the app is built with"));
    noticesButton.onClick = [this] { showLicences (true); };
    addAndMakeVisible (noticesButton);

    copyButton.setButtonText (tr ("Copy diagnostic info"));
    copyButton.setTooltip (tr ("Copies the version, build and device details, to paste into a bug report"));
    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (diagnosticInfo (audioInfo != nullptr ? audioInfo() : juce::String()));
        copyButton.setButtonText (tr ("Copied"));
    };
    addAndMakeVisible (copyButton);

    backButton.setButtonText (tr ("Back"));
    backButton.onClick = [this] { showLicences (false); };
    addChildComponent (backButton);

    notices.setMultiLine (true);
    notices.setReadOnly (true);
    notices.setScrollbarsShown (true);
    notices.setCaretVisible (false);
    notices.setFont (theme::textFont (14.0f));
    notices.setText (noticesText(), false);
    addChildComponent (notices);
}

juce::String AboutPanel::diagnosticInfo (const juce::String& audio)
{
    juce::StringArray lines;
    lines.add ("Practice Vibes " + build::versionLine());
    lines.add ("Platform: " + build::platform());
    lines.add ("Device: " + juce::SystemStats::getDeviceDescription()
               + (juce::SystemStats::getDeviceManufacturer().isNotEmpty() ? " (" + juce::SystemStats::getDeviceManufacturer() + ")" : juce::String()));
    lines.add ("Language: " + i18n::currentLanguage());

    if (audio.isNotEmpty())
        lines.add (audio);

    return lines.joinIntoString ("\n");
}

void AboutPanel::showLicences (bool show)
{
    showingLicences = show;

    for (auto* c : std::initializer_list<juce::Component*> { &title, &version, &platform, &developer, &contact, &licenceNote, &credit,
                                                             &guideButton, &privacyButton, &licenceButton, &sourceButton,
                                                             &noticesButton, &copyButton })
        c->setVisible (! show);

    notices.setVisible (show);
    backButton.setVisible (show);
    resized();
}

void AboutPanel::paint (juce::Graphics& g)
{
    theme::paintCard (g, getLocalBounds().toFloat().reduced (1.0f));
}

void AboutPanel::resized()
{
    auto r = getLocalBounds().reduced (24, 20);

    if (showingLicences)
    {
        backButton.setBounds (r.removeFromTop (36).removeFromLeft (120));
        r.removeFromTop (10);
        notices.setBounds (r);
        return;
    }

    // The block below is ~420 px tall: centred, so the card can stay tall enough for the licences view.
    r.removeFromTop (juce::jmax (0, (r.getHeight() - 420) / 2));

    title.setBounds (r.removeFromTop (48));
    version.setBounds (r.removeFromTop (26));
    platform.setBounds (r.removeFromTop (24));
    r.removeFromTop (12);
    developer.setBounds (r.removeFromTop (26));
    contact.setBounds (r.removeFromTop (26).withSizeKeepingCentre (juce::jmin (360, r.getWidth()), 26));
    r.removeFromTop (16);

    // Two rows of two buttons, then the licences and diagnostic buttons.
    const auto buttonRow = [&r] (juce::Button& a, juce::Button& b)
    {
        auto row = r.removeFromTop (40).reduced (0, 3).withSizeKeepingCentre (juce::jmin (440, r.getWidth()), 34);
        a.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (4, 0));
        b.setBounds (row.reduced (4, 0));
    };

    buttonRow (guideButton, privacyButton);
    buttonRow (licenceButton, sourceButton);
    buttonRow (noticesButton, copyButton);

    r.removeFromTop (14);
    licenceNote.setBounds (r.removeFromTop (48));
    licenceNote.setMinimumHorizontalScale (1.0f);
    r.removeFromTop (8);
    credit.setBounds (r.removeFromTop (24));
}

} // namespace pv
