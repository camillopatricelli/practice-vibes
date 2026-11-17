#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "TopicShare.h"
#include "StudentGroups.h"
#include <optional>

namespace pv
{

/** Who is sharing, for the Share dialog. */
struct ShareContext
{
    bool teacher = false;                       // "I teach" is on: offers "as a new topic for another student"
    juce::String senderName;                    // "Your name" as last typed (kept on this device only)
    PracticeSummary summary;                    // the user's practice on this topic (empty: nothing to offer)
    StudentGroups groups;                       // teachers: groups to send to, and what went to them before
    std::function<void (const juce::String&)> onNameChanged;
};

/** Overlay: "Share topic". The sender ticks which files go in (every time), sees the running total against the
    email limit, and writes the .pvtopic. What happens to the file next (Explorer, share sheet) is up to onWritten. */
class SharePanel final : public juce::Component
{
public:
    SharePanel (const Topic&, juce::File outputFolder, juce::String shareButtonText, ShareContext);
    ~SharePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The .pvtopic, or (document) the page followed by the files that go with it. */
    std::function<void (const juce::Array<juce::File>&, bool isDocument)> onWritten;
    std::function<void()> onCancel;
    /** Sent to a group: what to remember (called just before onWritten). */
    std::function<void (const std::vector<GroupSend>&)> onGroupSent;

private:
    void updateTotal();
    bool isDocument() const    { return formatBox.getSelectedId() == 2; }
    const StudentGroup* chosenGroup() const;
    bool onePerMember() const  { return groupModeBox.getSelectedId() == 2; }
    const GroupSend* previousGroupSend() const;    // to the chosen group, in the chosen way (any member for "per member")
    void updateNewTopicDefault();
    void share();

    Topic topic;
    juce::File outputFolder;
    std::vector<TopicShare::FileItem> items;
    juce::String shareText;

    juce::ComboBox formatBox;
    juce::Label formatLabel, introLabel, totalLabel, leftOutLabel, noFilesLabel;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::Label> sizeLabels;
    juce::Component rowHolder;
    juce::Viewport viewport;
    ShareContext context;
    juce::Label nameLabel;
    juce::TextEditor nameEditor;
    juce::ToggleButton newTopicToggle { "As a new topic: the same lesson for another student (their own copy)" };
    juce::Label toLabel, newTopicHint;
    juce::ComboBox toBox, groupModeBox;
    juce::ToggleButton summaryToggle;
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
    ImportPanel (std::function<Library*()> getLibrary, std::unique_ptr<TopicShare::Package>, bool teacher);
    ~ImportPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const juce::String& topicId)> onImported;
    std::function<void()> onCancel;

private:
    void refresh();
    void locate (const juce::String& fileName);
    void import (TopicShare::Mode);
    TopicShare::ImportAs importAs() const;

    std::function<Library*()> getLibrary;
    std::unique_ptr<TopicShare::Package> package;
    TopicShare::LocatedFiles located;
    TopicShare::Preview current;

    bool teacher = false;
    std::optional<GroupSend> groupSend;        // the file went to a group: one of its members sent it back
    StudentGroup group;
    juce::Label memberLabel;
    juce::ComboBox memberBox;                  // "Who sent this?"
    juce::Label summaryLabel, missingHeader, studentLabel;
    juce::TextEditor studentEditor;
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
