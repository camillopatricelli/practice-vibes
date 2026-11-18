#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pv
{

/** The About card (from Audio settings): name, version and build, platform, developer and contact, links to the guide,
    privacy statement, licence and source code, the open source licences (THIRD_PARTY_NOTICES.md, embedded) and a
    "Copy diagnostic info" button. */
class AboutPanel final : public juce::Component
{
public:
    /** audioInfo: one or more plain-English lines about the current audio setup, for the diagnostic info. */
    explicit AboutPanel (std::function<juce::String()> audioInfo);

    /** The text "Copy diagnostic info" puts on the clipboard (plain English, also handy for the log). */
    static juce::String diagnosticInfo (const juce::String& audioInfo);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showLicences (bool);

    std::function<juce::String()> audioInfo;
    juce::Label title, version, platform, developer, licenceNote, credit;
    juce::HyperlinkButton contact;
    juce::TextButton guideButton, privacyButton, licenceButton, sourceButton, noticesButton, copyButton, backButton;
    juce::TextEditor notices;
    bool showingLicences = false;
};

} // namespace pv
