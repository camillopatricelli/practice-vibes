#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "TopicShare.h"

namespace pv
{

/** Overlay: "Share topic". The sender ticks which files go in (every time), sees the running total against the
    email limit, and writes the .pvtopic. What happens to the file next (Explorer, share sheet) is up to onWritten. */
class SharePanel final : public juce::Component
{
public:
    SharePanel (const Topic&, juce::File outputFolder, juce::String shareButtonText);
    ~SharePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const juce::File&)> onWritten;
    std::function<void()> onCancel;

private:
    void updateTotal();
    void share();

    Topic topic;
    juce::File outputFolder;
    std::vector<TopicShare::FileItem> items;
    juce::String shareText;

    juce::Label introLabel, totalLabel, leftOutLabel, noFilesLabel;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::Label> sizeLabels;
    juce::Component rowHolder;
    juce::Viewport viewport;
    juce::ToggleButton shrinkToggle { "Make WAV / AIFF files smaller (AAC, 192 kbit/s)" };
    juce::TextButton shareButton, cancelButton { "Cancel" };
    bool writing = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SharePanel)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SharePanel)
};

/** Overlay: what importing a .pvtopic will do ("Updates your topic: 2 new resources, notes changed"), with Locate...
    for files the sender left out that aren't on this device, then Apply / Import as copy. */
class ImportPanel final : public juce::Component
{
public:
    ImportPanel (std::function<Library*()> getLibrary, std::unique_ptr<TopicShare::Package>);
    ~ImportPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const juce::String& topicId)> onImported;
    std::function<void()> onCancel;

private:
    void refresh();
    void locate (const juce::String& fileName);
    void import (TopicShare::Mode);

    std::function<Library*()> getLibrary;
    std::unique_ptr<TopicShare::Package> package;
    TopicShare::LocatedFiles located;
    TopicShare::Preview current;

    juce::Label summaryLabel, missingHeader;
    juce::TextEditor details;
    juce::OwnedArray<juce::Label> missingLabels;
    juce::OwnedArray<juce::TextButton> locateButtons;
    juce::Component missingHolder;
    juce::Viewport missingViewport;
    juce::TextButton applyButton, copyButton { "Import as copy" }, cancelButton { "Cancel" };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ImportPanel)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImportPanel)
};

} // namespace pv
