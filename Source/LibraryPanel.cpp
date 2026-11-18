#include "LibraryPanel.h"
#include "ResourceWidgets.h"
#include "AppPaths.h"
#include "FolderAccess.h"
#include "LoopFiles.h"
#include "SharePanel.h"
#include "SharePicker.h"
#include "StoragePanel.h"
#include "Theme.h"
#include "WebView.h"
#include "Localisation.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace pv
{

namespace
{
    const juce::String inboxId = "inbox";

    juce::String formatDate (juce::Time t)
    {
        return t == juce::Time() ? juce::String() : i18n::date (t, "%d %b %Y");
    }

    /** Desktop: where shared topics are written before they're attached to a mail or a message. */
    juce::File sharedTopicsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Practice Vibes").getChildFile ("Shared topics");
    }

    /** Files the in-app viewer can show on this platform (others open in their own app on desktop). */
    bool viewerCanShow (const juce::File& f)
    {
        if (! webViewAvailable())
            return false;

        const auto ext = f.getFileExtension().toLowerCase();

       #if JUCE_IOS
        // WKWebView shows PDFs, images, plain text, Office/iWork documents and video.
        return classifyFileName (f.getFileName()) != ResourceKind::audio && ext != ".gdoc" && ext != ".gsheet" && ext != ".gslides";
       #else
        return juce::StringArray { ".pdf", ".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".svg",
                                   ".txt", ".htm", ".html", ".mp4", ".webm", ".m4v" }.contains (ext);
       #endif
    }

}

//==============================================================================
/** Overlay: the teacher's assignment on a topic (goal, target tempo, minutes a day, next lesson). */
class AssignmentEditor final : public juce::Component
{
public:
    AssignmentEditor (const Assignment& a, std::function<void (Assignment)> onSave, std::function<void()> onCancel)
        : saved (std::move (onSave)), cancelled (std::move (onCancel))
    {
        getProperties().set ("overlayWidth", 560);
        getProperties().set ("overlayHeight", 380);

        auto label = [this] (juce::Label& l, const juce::String& text)
        {
            l.setText (text, juce::dontSendNotification);
            l.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (l);
        };

        label (goalLabel, tr ("Goal"));
        label (bpmLabel, tr ("Target tempo"));
        label (minutesLabel, tr ("Minutes a day"));
        label (lessonLabel, tr ("Next lesson"));

        goalEditor.setMultiLine (true, true);
        goalEditor.setReturnKeyStartsNewLine (true);
        goalEditor.setText (a.goal, false);
        goalEditor.setTextToShowWhenEmpty (tr ("e.g. Bars 1-16 clean, then the solo"), theme::text().withAlpha (0.35f));

        bpmEditor.setInputRestrictions (3, "0123456789");
        bpmEditor.setText (a.targetBpm > 0.0 ? juce::String (juce::roundToInt (a.targetBpm)) : juce::String(), false);
        bpmEditor.setTextToShowWhenEmpty (tr ("bpm (optional)"), theme::text().withAlpha (0.35f));

        minutesEditor.setInputRestrictions (3, "0123456789");
        minutesEditor.setText (a.minutesPerDay > 0 ? juce::String (a.minutesPerDay) : juce::String(), false);
        minutesEditor.setTextToShowWhenEmpty (tr ("optional"), theme::text().withAlpha (0.35f));

        for (auto* e : { &goalEditor, &bpmEditor, &minutesEditor })
        {
            styleEditor (*e);
            addAndMakeVisible (*e);
        }

        // The next 8 weeks, day by day (id = days from today + 2; 1 = none).
        lessonBox.addItem (tr ("None"), 1);
        const auto now = juce::Time::getCurrentTime();
        const juce::Time today (now.getYear(), now.getMonth(), now.getDayOfMonth(), 12, 0);
        int selected = 1;

        for (int d = 0; d < 56; ++d)
        {
            const auto day = today + juce::RelativeTime::days (d);
            lessonBox.addItem (d == 0 ? tr ("Today") : d == 1 ? tr ("Tomorrow") : i18n::date (day, "%a %d %b"), d + 2);

            if (a.nextLesson != juce::Time() && day.formatted ("%Y-%m-%d") == a.nextLesson.formatted ("%Y-%m-%d"))
                selected = d + 2;
        }

        lessonBox.setSelectedId (selected, juce::dontSendNotification);
        addAndMakeVisible (lessonBox);

        saveButton.setColour (juce::TextButton::buttonColourId, theme::accent());
        saveButton.onClick = [this]
        {
            Assignment result;
            result.goal = goalEditor.getText().trim().substring (0, 500);
            result.targetBpm = juce::jlimit (0.0, 400.0, bpmEditor.getText().getDoubleValue());
            result.minutesPerDay = juce::jlimit (0, 600, minutesEditor.getText().getIntValue());

            if (lessonBox.getSelectedId() >= 2)
            {
                const auto now2 = juce::Time::getCurrentTime();
                result.nextLesson = juce::Time (now2.getYear(), now2.getMonth(), now2.getDayOfMonth(), 12, 0)
                                    + juce::RelativeTime::days (lessonBox.getSelectedId() - 2);
            }

            if (saved) saved (result);
        };
        clearButton.onClick = [this] { if (saved) saved ({}); };
        cancelButton.onClick = [this] { if (cancelled) cancelled(); };

        for (auto* b : { &saveButton, &clearButton, &cancelButton })
            addAndMakeVisible (*b);
    }

    void paint (juce::Graphics& g) override    { g.fillAll (theme::background()); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);
        auto buttons = area.removeFromBottom (40);
        saveButton.setBounds (buttons.removeFromRight (120).reduced (2, 3));
        cancelButton.setBounds (buttons.removeFromRight (110).reduced (2, 3));
        clearButton.setBounds (buttons.removeFromLeft (150).reduced (2, 3));
        area.removeFromBottom (10);

        auto row = [&area] (juce::Label& l, juce::Component& c, int height)
        {
            auto r = area.removeFromTop (height).reduced (0, 4);
            l.setBounds (r.removeFromLeft (120).withHeight (30));
            r.removeFromLeft (10);
            c.setBounds (r);
        };

        row (goalLabel, goalEditor, 110);
        row (bpmLabel, bpmEditor, 40);
        row (minutesLabel, minutesEditor, 40);
        row (lessonLabel, lessonBox, 40);
    }

private:
    std::function<void (Assignment)> saved;
    std::function<void()> cancelled;
    juce::Label goalLabel, bpmLabel, minutesLabel, lessonLabel;
    juce::TextEditor goalEditor, bpmEditor, minutesEditor;
    juce::ComboBox lessonBox;
    juce::TextButton saveButton { tr ("Save") }, clearButton { tr ("No assignment") }, cancelButton { tr ("Cancel") };
};

//==============================================================================
/** Overlay: the teacher's groups (ensembles): a name and its members, one row per group. */
class GroupEditor final : public juce::Component
{
public:
    GroupEditor (const StudentGroups& g, const juce::StringArray& knownStudents, bool addNew,
                 std::function<void (StudentGroups)> onSave, std::function<void()> onCancel)
        : groups (g), saved (std::move (onSave)), cancelled (std::move (onCancel))
    {
        getProperties().set ("overlayWidth", 680);
        getProperties().set ("overlayHeight", 560);

        introLabel.setText (tr ("A group gets one topic in one go (Share > To). Members are first names or nicknames, separated "
                            "by commas, as in the Student boxes. Groups stay on your devices: never sent to anyone."),
                            juce::dontSendNotification);
        introLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.7f));
        introLabel.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (introLabel);

        studentsLabel.setText (knownStudents.isEmpty() ? juce::String()
                                                       : tr ("Your students: {names}", knownStudents.joinIntoString (", ")),
                               juce::dontSendNotification);
        studentsLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.5f));
        studentsLabel.setJustificationType (juce::Justification::topLeft);
        studentsLabel.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (studentsLabel);

        viewport.setViewedComponent (&rowHolder, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        addButton.onClick = [this] { readRows(); groups.add ({}); buildRows(); nameEditors.getLast()->grabKeyboardFocus(); };
        saveButton.setColour (juce::TextButton::buttonColourId, theme::accent());
        saveButton.onClick = [this]
        {
            readRows();
            auto result = groups;
            result.groups.erase (std::remove_if (result.groups.begin(), result.groups.end(),
                                                 [] (const StudentGroup& sg) { return sg.name.isEmpty() && sg.members.isEmpty(); }),
                                 result.groups.end());

            for (auto& sg : result.groups)
                if (sg.name.isEmpty())
                    sg.name = tr ("Group");

            if (saved) saved (std::move (result));
        };
        cancelButton.onClick = [this] { if (cancelled) cancelled(); };

        for (auto* b : { &addButton, &saveButton, &cancelButton })
            addAndMakeVisible (*b);

        if (groups.groups.empty() || addNew)
            groups.add ({});

        buildRows();

        // The new (empty) row is ready to type in.
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<GroupEditor> (this)]
        {
            if (safeThis != nullptr && safeThis->isShowing() && ! safeThis->nameEditors.isEmpty()
                && safeThis->nameEditors.getLast()->isEmpty())
                safeThis->nameEditors.getLast()->grabKeyboardFocus();
        });
    }

    void paint (juce::Graphics& g) override    { g.fillAll (theme::background()); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);
        auto buttons = area.removeFromBottom (40);
        saveButton.setBounds (buttons.removeFromRight (120).reduced (2, 3));
        cancelButton.setBounds (buttons.removeFromRight (110).reduced (2, 3));
        addButton.setBounds (buttons.removeFromLeft (140).reduced (2, 3));
        area.removeFromBottom (8);
        introLabel.setBounds (area.removeFromTop (48));
        studentsLabel.setBounds (area.removeFromTop (studentsLabel.getText().isEmpty() ? 0 : 26));
        area.removeFromTop (6);
        viewport.setBounds (area);

        constexpr int rowHeight = 40;
        const auto width = juce::jmax (1, viewport.getMaximumVisibleWidth());
        rowHolder.setSize (width, juce::jmax (1, nameEditors.size() * rowHeight));

        for (int i = 0; i < nameEditors.size(); ++i)
        {
            auto row = juce::Rectangle<int> (0, i * rowHeight, width, rowHeight).reduced (0, 4);
            removeButtons[i]->setBounds (row.removeFromRight (90).reduced (2, 0));
            nameEditors[i]->setBounds (row.removeFromLeft (190));
            row.removeFromLeft (8);
            memberEditors[i]->setBounds (row.withTrimmedRight (6));
        }
    }

private:
    void readRows()
    {
        for (int i = 0; i < nameEditors.size() && i < (int) groups.groups.size(); ++i)
        {
            auto& sg = groups.groups[(size_t) i];
            sg.name = nameEditors[i]->getText().trim().substring (0, 60);
            sg.members.clear();

            for (auto m : juce::StringArray::fromTokens (memberEditors[i]->getText(), ",", ""))
                if (m = m.trim().substring (0, 60); m.isNotEmpty())
                    sg.members.addIfNotAlreadyThere (m, true);
        }
    }

    void buildRows()
    {
        nameEditors.clear();
        memberEditors.clear();
        removeButtons.clear();

        for (size_t i = 0; i < groups.groups.size(); ++i)
        {
            const auto& sg = groups.groups[i];
            auto* name = nameEditors.add (new juce::TextEditor());
            name->setText (sg.name, false);
            name->setTextToShowWhenEmpty (tr ("Group name"), theme::text().withAlpha (0.35f));
            styleEditor (*name);
            rowHolder.addAndMakeVisible (name);

            auto* members = memberEditors.add (new juce::TextEditor());
            members->setText (sg.members.joinIntoString (", "), false);
            members->setTextToShowWhenEmpty (tr ("Members, e.g. Anna, Marco, Luca"), theme::text().withAlpha (0.35f));
            styleEditor (*members);
            rowHolder.addAndMakeVisible (members);

            auto* remove = removeButtons.add (new juce::TextButton (tr ("Remove")));
            remove->setTooltip (tr ("Removes the group (its members' topics stay)"));
            remove->onClick = [this, i]
            {
                readRows();
                groups.groups.erase (groups.groups.begin() + (std::ptrdiff_t) i);
                buildRows();
            };
            rowHolder.addAndMakeVisible (remove);
        }

        resized();
    }

    StudentGroups groups;
    std::function<void (StudentGroups)> saved;
    std::function<void()> cancelled;
    juce::Label introLabel, studentsLabel;
    juce::Component rowHolder;
    juce::Viewport viewport;
    juce::OwnedArray<juce::TextEditor> nameEditors, memberEditors;
    juce::OwnedArray<juce::TextButton> removeButtons;
    juce::TextButton addButton { tr ("+ Add group") }, saveButton { tr ("Save") }, cancelButton { tr ("Cancel") };
};

//==============================================================================
/** Overlay after "one file per member" (iPad / Mac): one row per member with its own Share... button, so each file
    goes to the right chat (a share sheet doesn't say whose file it is). */
class MemberSharePanel final : public juce::Component
{
public:
    using ShareFn = std::function<void (const juce::File&, juce::Component& anchor, std::function<void (bool shared)> done)>;

    MemberSharePanel (const juce::StringArray& members, const juce::Array<juce::File>& filesToShare, ShareFn shareFn,
                      std::function<void()> onDone)
        : files (filesToShare), share (std::move (shareFn)), finished (std::move (onDone))
    {
        getProperties().set ("overlayWidth", 560);
        getProperties().set ("overlayHeight", juce::jlimit (260, 640, 170 + files.size() * 44));

        introLabel.setText (tr ("Each member has their own file. Tap Share... on a row and send it to that member "
                            "(their chat, mail or AirDrop), then the next one."), juce::dontSendNotification);
        introLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.7f));
        introLabel.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (introLabel);

        for (int i = 0; i < files.size(); ++i)
        {
            auto* label = names.add (new juce::Label ({}, i < members.size() ? members[i] : files[i].getFileNameWithoutExtension()));
            label->setFont (theme::textFont (16.0f));
            addAndMakeVisible (label);

            auto* button = buttons.add (new juce::TextButton (tr ("Share...")));
            button->setColour (juce::TextButton::buttonColourId, theme::accent());
            button->onClick = [this, i] { shareRow (i); };
            addAndMakeVisible (button);
        }

        doneButton.onClick = [this] { if (finished) finished(); };
        addAndMakeVisible (doneButton);
    }

    void paint (juce::Graphics& g) override    { g.fillAll (theme::background()); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);
        doneButton.setBounds (area.removeFromBottom (40).removeFromRight (120).reduced (2, 3));
        area.removeFromBottom (8);
        introLabel.setBounds (area.removeFromTop (48));
        area.removeFromTop (6);

        for (int i = 0; i < names.size(); ++i)
        {
            auto row = area.removeFromTop (44).reduced (0, 4);
            buttons[i]->setBounds (row.removeFromRight (150));
            names[i]->setBounds (row);
        }
    }

private:
    void shareRow (int i)
    {
        if (! share)
            return;

        // Marked as soon as the sheet opens: the Mac menu doesn't report back, and the iPad's "done" can't tell a send
        // from a cancel for every app. The button stays, to send it again.
        buttons[i]->setButtonText (tr ("Shared - again?"));
        buttons[i]->setColour (juce::TextButton::buttonColourId, getLookAndFeel().findColour (juce::TextButton::buttonColourId));
        share (files[i], *buttons[i], [] (bool) {});
    }

    juce::Array<juce::File> files;
    ShareFn share;
    std::function<void()> finished;
    juce::Label introLabel;
    juce::OwnedArray<juce::Label> names;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::TextButton doneButton { tr ("Done") };
};

//==============================================================================
/** Title, created date, notes and the resource list of one topic. Files can be dropped onto it (desktop). */
class LibraryPanel::TopicView final : public juce::Component,
                                      public juce::FileDragAndDropTarget
{
public:
    explicit TopicView (LibraryPanel& p) : owner (p)
    {
        titleEditor.setFont (theme::displayFont (22.0f, "SemiBold"));
        titleEditor.setTextToShowWhenEmpty (tr ("Topic title"), theme::text().withAlpha (0.35f));
        titleEditor.setJustification (juce::Justification::centredLeft);
        styleEditor (titleEditor);
        titleEditor.onTextChange = [this] { edited(); };
        titleEditor.onReturnKey = [this] { descriptionEditor.grabKeyboardFocus(); };
        titleEditor.onFocusLost = [this] { owner.titleSettled (topicId); };
        addAndMakeVisible (titleEditor);

        tagsEditor.setTextToShowWhenEmpty (tr ("Tags, separated by commas (e.g. technique, jazz, lesson)"), theme::text().withAlpha (0.35f));
        tagsEditor.setFont (juce::FontOptions (14.0f));
        styleEditor (tagsEditor);
        tagsEditor.onTextChange = [this] { edited(); };
        tagsEditor.onReturnKey = [this] { descriptionEditor.grabKeyboardFocus(); };
        tagsEditor.onFocusLost = [this]
        {
            owner.flush();


            // Show the cleaned-up list (duplicates and stray commas removed).
            if (auto* t = owner.library->findTopic (topicId))
                tagsEditor.setText (joinTags (t->tags), false);
        };
        addAndMakeVisible (tagsEditor);

        createdLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.55f));
        createdLabel.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (createdLabel);

        studentEditor.setTextToShowWhenEmpty (tr ("Student"), theme::text().withAlpha (0.35f));
        studentEditor.setTooltip (tr ("Who this topic is for: a first name or nickname. Groups topics by student (Tags menu). "
                                  "Stays on your devices: never sent with the topic"));
        studentEditor.setFont (juce::FontOptions (14.0f));
        styleEditor (studentEditor);
        studentEditor.onTextChange = [this] { edited(); };
        studentEditor.onFocusLost = [this] { owner.flush(); };
        addChildComponent (studentEditor);

        assignmentLabel.setColour (juce::Label::textColourId, theme::accent());
        assignmentLabel.setMinimumHorizontalScale (0.6f);
        addChildComponent (assignmentLabel);
        assignmentButton.setTooltip (tr ("Goal, target tempo, minutes a day and the next lesson: they go along when you share "
                                     "the topic, and show on the student's Today tab"));
        assignmentButton.onClick = [this] { owner.editAssignment (topicId); };
        addChildComponent (assignmentButton);

        summaryLabel.setColour (juce::Label::textColourId, theme::good());
        summaryLabel.setMinimumHorizontalScale (0.6f);
        addChildComponent (summaryLabel);

        groupLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.7f));
        groupLabel.setMinimumHorizontalScale (0.6f);
        groupLabel.setJustificationType (juce::Justification::centredLeft);
        addChildComponent (groupLabel);

        deleteButton.onClick = [this] { owner.deleteTopic (topicId); };
        addAndMakeVisible (deleteButton);

        practiseButton.onClick = [this] { if (owner.onPractise) owner.onPractise (topicId); };
        practiseButton.setTooltip (tr ("Make this the topic you're practising, and bring back the song, loop, speed and tempo you used last time"));
        practiseButton.setColour (juce::TextButton::buttonColourId, theme::accent());
        addAndMakeVisible (practiseButton);

        shareButton.onClick = [this] { owner.shareTopic (topicId); };
        shareButton.setTooltip (tr ("Send this topic to a teacher, a student or a bandmate as one .pvtopic file: you choose which files go in"));
        addAndMakeVisible (shareButton);

        historyButton.onClick = [this] { if (owner.onShowHistory) owner.onShowHistory (topicId); };
        historyButton.setTooltip (tr ("Tempo, song speed and minutes over the weeks, and every session"));
        addAndMakeVisible (historyButton);

        rotationToggle.setTooltip (tr ("Topics in the rotation are listed on the Today tab, most neglected first"));
        rotationToggle.onClick = [this] { owner.setRotation (topicId, rotationToggle.getToggleState()); };
        addAndMakeVisible (rotationToggle);

        noGuitarToggle.setTooltip (tr ("For ear training, theory...: while you practise this topic the time is counted on the clock "
                                   "(with Pause on the Today tab) instead of from your instrument input"));
        noGuitarToggle.onClick = [this] { owner.setNoGuitar (topicId, noGuitarToggle.getToggleState()); };
        addAndMakeVisible (noGuitarToggle);

        addAndMakeVisible (styleCaption (notesLabel, tr ("Notes")));
        notesLabel.setTooltip (tr ("These notes travel with the topic when you share it"));
        addAndMakeVisible (styleCaption (privateNotesLabel, tr ("Private notes")));
        privateNotesLabel.setTooltip (tr ("Only for you: never included when you share the topic"));
        privateNotesEditor.setMultiLine (true, true);
        privateNotesEditor.setReturnKeyStartsNewLine (true);
        privateNotesEditor.setScrollbarsShown (true);
        privateNotesEditor.setFont (juce::FontOptions (15.0f));
        privateNotesEditor.setTextToShowWhenEmpty (tr ("Only for you: never shared"), theme::text().withAlpha (0.35f));
        styleEditor (privateNotesEditor);
        privateNotesEditor.onTextChange = [this] { edited(); };
        privateNotesEditor.onFocusLost = [this] { owner.flush(); };
        addAndMakeVisible (privateNotesEditor);
        descriptionEditor.setMultiLine (true, true);
        descriptionEditor.setReturnKeyStartsNewLine (true);
        descriptionEditor.setScrollbarsShown (true);
        descriptionEditor.setFont (juce::FontOptions (15.0f));
        descriptionEditor.setTextToShowWhenEmpty (tr ("What to practise, how, lesson notes..."), theme::text().withAlpha (0.35f));
        styleEditor (descriptionEditor);
        descriptionEditor.onTextChange = [this] { edited(); };
        descriptionEditor.onFocusLost = [this] { owner.flush(); };
        addAndMakeVisible (descriptionEditor);

        conflictLabel.setColour (juce::Label::textColourId, theme::highlight());
        conflictLabel.setMinimumHorizontalScale (0.6f);
        addChildComponent (conflictLabel);
        conflictButton.setTooltip (tr ("Shows each copy's title and when it was saved; the ones you don't keep go to the bin"));
        conflictButton.onClick = [this] { owner.chooseTopicVersion (topicId, conflictButton); };
        addChildComponent (conflictButton);

        addAndMakeVisible (styleCaption (resourcesLabel, tr ("Resources")));
        addFilesButton.onClick = [this] { owner.addFiles (topicId); };
        addLinkButton.onClick = [this] { owner.editResource (topicId, {}, true); };
       #if JUCE_IOS
        addFilesButton.setTooltip (tr ("Copies documents, images or audio files into this topic's folder"));
       #else
        addFilesButton.setTooltip (tr ("Copies documents, images or audio files into this topic's folder (you can also drop files here)"));
       #endif
        addLinkButton.setTooltip (tr ("YouTube video, Google document or any web page"));
        addAndMakeVisible (addFilesButton);
        addAndMakeVisible (addLinkButton);

        noResourcesLabel.setText (tr ("No resources yet: add documents, audio files or links."), juce::dontSendNotification);
        noResourcesLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.45f));
        addChildComponent (noResourcesLabel);

        viewport.setViewedComponent (&rowHolder, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
    }

    juce::String topicId;

    void load (const Topic& topic, bool keepFocusedEditors)
    {
        const auto sameTopic = topic.id == topicId;
        topicId = topic.id;

        if (! (sameTopic && keepFocusedEditors && titleEditor.hasKeyboardFocus (true)) && titleEditor.getText() != topic.title)
            titleEditor.setText (topic.title, false);

        if (! (sameTopic && keepFocusedEditors && descriptionEditor.hasKeyboardFocus (true)) && descriptionEditor.getText() != topic.description)
            descriptionEditor.setText (topic.description, false);

        if (! (sameTopic && keepFocusedEditors && privateNotesEditor.hasKeyboardFocus (true)) && privateNotesEditor.getText() != topic.privateNotes)
            privateNotesEditor.setText (topic.privateNotes, false);

        if (! (sameTopic && keepFocusedEditors && tagsEditor.hasKeyboardFocus (true)) && parseTags (tagsEditor.getText()) != topic.tags)
            tagsEditor.setText (joinTags (topic.tags), false);

        juce::String dates = tr ("Created {date}", formatDate (topic.created));

        if (topic.lastSent != juce::Time())     dates << "\n" << tr ("Sent {date}", formatDate (topic.lastSent));
        if (topic.lastReceived != juce::Time()) dates << (topic.lastSent != juce::Time() ? ", " + tr ("received {date}", formatDate (topic.lastReceived))
                                                                                         : "\n" + tr ("Received {date}", formatDate (topic.lastReceived)));

        createdLabel.setText (dates, juce::dontSendNotification);
        createdLabel.setTooltip (dates.replace ("\n", "  "));

        const bool teacher = owner.isTeacher && owner.isTeacher();
        studentEditor.setVisible (teacher || topic.student.isNotEmpty());

        if (! (sameTopic && keepFocusedEditors && studentEditor.hasKeyboardFocus (true)) && studentEditor.getText() != topic.student)
            studentEditor.setText (topic.student, false);

        assignmentButton.setVisible (teacher);
        assignmentLabel.setVisible (! topic.assignment.isEmpty() || teacher);
        assignmentLabel.setText (topic.assignment.isEmpty() ? tr ("No assignment yet")
                                                            : tr ("Assignment: {details}", topic.assignment.describe()), juce::dontSendNotification);
        assignmentLabel.setTooltip (topic.assignment.describe());

        // Groups: who got this topic and who sent something back; a member's copy names its group.
        juce::String groupText = owner.groups.describeSends (*owner.library, topic.id);

        if (const auto* g = owner.groups.find (topic.group); g != nullptr && topic.group.isNotEmpty())
            groupText = (tr ("In the group {name}", g->name) + (groupText.isNotEmpty() ? "\n" : "")) + groupText;

        groupLabel.setText (groupText, juce::dontSendNotification);
        groupLabel.setTooltip (groupText.replace ("\n", "  "));

        if (groupLabel.isVisible() != groupText.isNotEmpty())
        {
            groupLabel.setVisible (groupText.isNotEmpty());
            resized();
        }

        summaryLabel.setVisible (! topic.studentSummary.isEmpty());
        summaryLabel.setText (tr ("Student's practice (sent {date}): {summary}", formatDate (topic.studentSummary.made), topic.studentSummary.describe()),
                              juce::dontSendNotification);
        rotationToggle.setToggleState (topic.inRotation, juce::dontSendNotification);
        noGuitarToggle.setToggleState (topic.noGuitar, juce::dontSendNotification);

        conflictLabel.setVisible (! topic.conflictFiles.isEmpty());
        conflictButton.setVisible (! topic.conflictFiles.isEmpty());
        conflictLabel.setText (trn ("The cloud sync made an extra copy of this topic's file ({files}). Choose which version to keep.",
                                    "The cloud sync made extra copies of this topic's file ({files}). Choose which version to keep.",
                                    topic.conflictFiles.size(), topic.conflictFiles.joinIntoString (", ")),
                               juce::dontSendNotification);

        rows.clear();

        for (auto& r : topic.resources)
        {
            auto row = std::make_unique<ResourceRow> (r, topic.isMissing (r), "...", owner.tagFilter);
            const auto resourceId = r.id;
            row->onOpen = [this, resourceId]
            {
                if (auto* t = owner.library->findTopic (topicId))
                    if (auto* res = t->findResource (resourceId))
                        owner.openResource (*t, *res);
            };
            row->onMenu = [this, resourceId] (juce::Component& anchor) { owner.showResourceMenu (topicId, resourceId, anchor); };
            rowHolder.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }

        noResourcesLabel.setVisible (rows.empty());
        resized();
    }

    void focusTitle()
    {
        titleEditor.grabKeyboardFocus();
        titleEditor.selectAll();
    }

    void paint (juce::Graphics& g) override
    {
        if (dragOver)
        {
            g.setColour (theme::accent().withAlpha (0.25f));
            g.fillRoundedRectangle (viewport.getBounds().toFloat().expanded (4.0f), 8.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();

        // Narrow (iPad portrait): the created date and the tags get a row of their own, the switches another,
        // so the title and the tags box aren't squeezed to a few letters.
        const bool narrow = getWidth() < 780;

        auto top = area.removeFromTop (44);
        deleteButton.setBounds (top.removeFromRight (120).reduced (2, 4));
        shareButton.setBounds (top.removeFromRight (90).reduced (2, 4));
        practiseButton.setBounds (top.removeFromRight (100).reduced (2, 4));
        top.removeFromRight (8);
        if (! narrow)
            createdLabel.setBounds (top.removeFromRight (170));
        titleEditor.setBounds (top.reduced (0, 2));

        area.removeFromTop (6);

        if (narrow)
        {
            auto tagsRow = area.removeFromTop (30);
            createdLabel.setBounds (tagsRow.removeFromRight (150).withTrimmedLeft (8));

            if (studentEditor.isVisible())
            {
                studentEditor.setBounds (tagsRow.removeFromLeft (130));
                tagsRow.removeFromLeft (8);
            }

            tagsEditor.setBounds (tagsRow);
            area.removeFromTop (6);

            auto toggleRow = area.removeFromTop (30);
            historyButton.setBounds (toggleRow.removeFromRight (90).reduced (0, 1));
            noGuitarToggle.setBounds (toggleRow.removeFromLeft (toggleRow.getWidth() / 2));
            rotationToggle.setBounds (toggleRow);
        }
        else
        {
            auto tagsRow = area.removeFromTop (30);
            historyButton.setBounds (tagsRow.removeFromRight (90).reduced (0, 1));
            rotationToggle.setBounds (tagsRow.removeFromRight (190).withTrimmedLeft (12));
            noGuitarToggle.setBounds (tagsRow.removeFromRight (220).withTrimmedLeft (12));

            if (studentEditor.isVisible())
            {
                studentEditor.setBounds (tagsRow.removeFromLeft (140));
                tagsRow.removeFromLeft (8);
            }

            tagsEditor.setBounds (tagsRow);
        }
        area.removeFromTop (4);
        const auto notesHeight = juce::jlimit (110, 360, (int) (area.getHeight() * 0.38f));

        if (narrow)
        {
            // Shared notes on top, private notes under them (smaller).
            notesLabel.setBounds (area.removeFromTop (26));
            descriptionEditor.setBounds (area.removeFromTop (notesHeight * 3 / 5));
            privateNotesLabel.setBounds (area.removeFromTop (26));
            privateNotesEditor.setBounds (area.removeFromTop (notesHeight * 2 / 5));
        }
        else
        {
            auto labels = area.removeFromTop (26);
            auto editors = area.removeFromTop (notesHeight);
            const auto privateWidth = (int) (labels.getWidth() * 0.36f);
            privateNotesLabel.setBounds (labels.removeFromRight (privateWidth));
            privateNotesEditor.setBounds (editors.removeFromRight (privateWidth));
            labels.removeFromRight (10);
            editors.removeFromRight (10);
            notesLabel.setBounds (labels);
            descriptionEditor.setBounds (editors);
        }

        area.removeFromTop (6);

        if (assignmentLabel.isVisible())
        {
            auto row = area.removeFromTop (30);

            if (assignmentButton.isVisible())
                assignmentButton.setBounds (row.removeFromRight (130).reduced (2, 2));

            assignmentLabel.setBounds (row);
        }

        if (summaryLabel.isVisible())
            summaryLabel.setBounds (area.removeFromTop (26));

        if (groupLabel.isVisible())
            groupLabel.setBounds (area.removeFromTop (juce::jmin (3, juce::StringArray::fromLines (groupLabel.getText()).size()) * 20 + 6));

        if (conflictLabel.isVisible())
        {
            auto row = area.removeFromTop (28);
            conflictButton.setBounds (row.removeFromRight (150).reduced (2, 2));
            conflictLabel.setBounds (row);
        }

        auto header = area.removeFromTop (40);
        addLinkButton.setBounds (header.removeFromRight (120).reduced (2, 4));
        addFilesButton.setBounds (header.removeFromRight (120).reduced (2, 4));
        resourcesLabel.setBounds (header);

        area.removeFromTop (4);
        viewport.setBounds (area);
        noResourcesLabel.setBounds (area.removeFromTop (40));

        constexpr int rowHeight = 64, gap = 6;
        const auto width = viewport.getMaximumVisibleWidth() - (viewport.canScrollVertically() ? 4 : 0);
        rowHolder.setSize (juce::jmax (1, width), juce::jmax (1, (int) rows.size() * (rowHeight + gap)));

        for (size_t i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, (int) i * (rowHeight + gap), rowHolder.getWidth(), rowHeight);
    }

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray&) override    { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override  { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override             { dragOver = false; repaint(); }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver = false;
        repaint();

        juce::Array<juce::URL> urls;

        for (auto& f : files)
            if (juce::File (f).existsAsFile())
                urls.add (juce::URL (juce::File (f)));

        owner.addUrls (topicId, urls);
    }

private:
    void edited()
    {
        if (auto* t = owner.library->findTopic (topicId))
        {
            t->title = titleEditor.getText().trim();
            t->description = descriptionEditor.getText();
            t->privateNotes = privateNotesEditor.getText();

            if (studentEditor.isVisible())
                t->student = studentEditor.getText().trim().substring (0, 60);
            t->tags = parseTags (tagsEditor.getText());
            owner.editedTopic();
        }
    }

    LibraryPanel& owner;
    juce::TextEditor titleEditor, tagsEditor, descriptionEditor, privateNotesEditor, studentEditor;
    juce::Label groupLabel;
    juce::Label assignmentLabel, summaryLabel, createdLabel, notesLabel, privateNotesLabel, resourcesLabel, conflictLabel, noResourcesLabel;
    juce::TextButton deleteButton { tr ("Delete topic") }, addFilesButton { tr ("Add files...") }, addLinkButton { tr ("Add link...") },
                     practiseButton { tr ("Practise") }, shareButton { tr ("Share...") }, assignmentButton { tr ("Assignment...") }, historyButton { tr ("History") }, conflictButton { tr ("Choose version...") };
    juce::ToggleButton rotationToggle { tr ("In practice rotation") }, noGuitarToggle { tr ("Timed without playing") };
    juce::Viewport viewport;
    juce::Component rowHolder;
    std::vector<std::unique_ptr<ResourceRow>> rows;
    bool dragOver = false;
};

//==============================================================================
/** Loose files at the top of the library folder, waiting to be moved into a topic. */
class LibraryPanel::InboxView final : public juce::Component
{
public:
    explicit InboxView (LibraryPanel& p) : owner (p)
    {
        addAndMakeVisible (styleCaption (header, tr ("Inbox")));
        info.setText (tr ("Files placed directly in the library folder (for example from another device). "
                      "Move each one into a topic."), juce::dontSendNotification);
        info.setColour (juce::Label::textColourId, theme::text().withAlpha (0.6f));
        info.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (info);
        viewport.setViewedComponent (&rowHolder, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
    }

    void load (const juce::Array<juce::File>& files)
    {
        rows.clear();

        for (auto& f : files)
        {
            Resource r;
            r.id = r.fileName = f.getFileName();
            r.title = f.getFileName();
            auto row = std::make_unique<ResourceRow> (r, false, tr ("Move to topic..."));
            row->onOpen = [this, f] { owner.openFile (f, f.getFileName()); };
            row->onMenu = [this, f] (juce::Component& anchor) { owner.showInboxMenu (f, anchor); };
            rowHolder.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }

        resized();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        header.setBounds (area.removeFromTop (30));
        info.setBounds (area.removeFromTop (44));
        viewport.setBounds (area);

        constexpr int rowHeight = 64, gap = 6;
        const auto width = viewport.getMaximumVisibleWidth();
        rowHolder.setSize (juce::jmax (1, width), juce::jmax (1, (int) rows.size() * (rowHeight + gap)));

        for (size_t i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, (int) i * (rowHeight + gap), rowHolder.getWidth(), rowHeight);
    }

private:
    LibraryPanel& owner;
    juce::Label header, info;
    juce::Viewport viewport;
    juce::Component rowHolder;
    std::vector<std::unique_ptr<ResourceRow>> rows;
};

//==============================================================================
/** In-app viewer for documents and web pages (WebView2 on Windows, WKWebView on iOS). */
class LibraryPanel::Viewer final : public juce::Component
{
public:
    Viewer (const juce::String& urlToShow, std::function<void()> openOutsideFn)
        : browser (webViewOptions()), openOutside (std::move (openOutsideFn))
    {
        getProperties().set ("fillOverlay", true);
        addAndMakeVisible (browser);

        if (openOutside != nullptr)
        {
            outsideButton.onClick = [this] { openOutside(); };
            addAndMakeVisible (outsideButton);
        }

        browser.goToURL (urlToShow);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        if (outsideButton.isVisible())
            outsideButton.setBounds (area.removeFromBottom (40).removeFromRight (220).reduced (2, 4));

        browser.setBounds (area);
    }

private:
    /** Links that want a new window open in the same view. */
    struct Browser final : public juce::WebBrowserComponent
    {
        using juce::WebBrowserComponent::WebBrowserComponent;
        void newWindowAttemptingToLoad (const juce::String& url) override    { goToURL (url); }
    };

    Browser browser;
    std::function<void()> openOutside;
    juce::TextButton outsideButton {
       #if JUCE_IOS
        tr ("Open in Safari / app")
       #else
        tr ("Open outside the app")
       #endif
    };
};

//==============================================================================
LibraryPanel::LibraryPanel()
{
    setupLabel.setJustificationType (juce::Justification::centred);
    setupLabel.setFont (juce::FontOptions (16.0f));
    // iPadOS only lets apps use whole folders from iCloud Drive (Google Drive and others are greyed out in the
    // picker), so iCloud Drive is the one way to share a library between an iPad/iPhone and another device.
    setupLabel.setText (tr ("Where should Practice Vibes keep your topics?") + "\n\n"
                       #if JUCE_IOS
                        + tr ("Keep them on this iPad, or pick a folder in iCloud Drive to share the same topics with "
                              "your other devices (on a Windows PC: install iCloud for Windows).") + "\n"
                        + tr ("iCloud Drive is the only cloud drive that can keep topics in sync with an iPad or iPhone."),
                       #else
                        + tr ("Pick a folder on this computer, or one inside a cloud drive (OneDrive, Google Drive, "
                              "Dropbox, iCloud Drive) to share the same topics with your other computers.") + "\n"
                        + tr ("To share them with an iPad or iPhone too, use iCloud Drive (install iCloud for Windows): "
                              "it is the only cloud drive iPadOS lets apps use for a whole folder."),
                       #endif
                        juce::dontSendNotification);
    addChildComponent (setupLabel);

    useDefaultButton.setButtonText (tr ("Use {folder}", LibraryLocation::getDefaultFolder().getFullPathName()));
   #if JUCE_IOS
    useDefaultButton.setButtonText (tr ("Keep them on this iPad"));
   #endif
    useDefaultButton.onClick = [this]
    {
        LibraryLocation::save (LibraryLocation::getDefaultFolder());
        openLibrary (LibraryLocation::getDefaultFolder());
    };
    addChildComponent (useDefaultButton);

    chooseFolderButton.onClick = [this] { chooseFolder(); };
   #if JUCE_IOS
    chooseFolderButton.setButtonText (tr ("Choose a folder in iCloud Drive"));
   #endif
    addChildComponent (chooseFolderButton);

    searchBox.setTextToShowWhenEmpty (tr ("Search topics"), theme::text().withAlpha (0.4f));
    searchBox.setFont (juce::FontOptions (15.0f));
    searchBox.setJustification (juce::Justification::centredLeft);
    styleEditor (searchBox);
    searchBox.onTextChange = [this] { rebuildList (selectedId); };
    searchBox.onEscapeKey = [this] { searchBox.clear(); rebuildList (selectedId); };

    newTopicButton.onClick = [this] { newTopic(); };
    folderButton.onClick = [this] { showFolderMenu(); };
    tagsButton.onClick = [this] { showTagMenu(); };
    tagsButton.setTooltip (tr ("Show only topics with a tag (on the topic or one of its resources)"));

    topicList.setRowHeight (54);
    topicList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    topicList.setOutlineThickness (0);

    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.5f));

    topicView = std::make_unique<TopicView> (*this);
    inboxView = std::make_unique<InboxView> (*this);

    for (juce::Component* c : { (juce::Component*) &searchBox, (juce::Component*) &newTopicButton, (juce::Component*) &folderButton,
                                (juce::Component*) &tagsButton, (juce::Component*) &topicList, (juce::Component*) &emptyLabel,
                                (juce::Component*) topicView.get(), (juce::Component*) inboxView.get() })
        addChildComponent (c);

    const auto location = LibraryLocation::load();

    if (location != juce::File())
        openLibrary (location);
    else
        resized();

    startTimerHz (4);
}

LibraryPanel::~LibraryPanel()
{
    flush();
}

//==============================================================================
void LibraryPanel::openLibrary (const juce::File& folder)
{
    flush();
    library = std::make_unique<Library> (folder);
    signature = library->computeSignature();
    folderButton.setTooltip (tr ("Library folder: {folder}", folder.getFullPathName()));
    juce::Logger::writeToLog ("Library: " + folder.getFullPathName() + " (" + juce::String ((int) library->getTopics().size())
                              + " topics, " + juce::String (library->getInbox().size()) + " in the Inbox)");
    selectedId = {};
    rebuildList (library->getTopics().empty() ? juce::String() : library->getTopics().front().id);
    resized();

    if (onLibraryOpened)
        onLibraryOpened();

    requestDownloads();
}

void LibraryPanel::requestDownloads()
{
   #if JUCE_IOS
    static std::atomic<bool> busy { false };

    if (library == nullptr || busy.exchange (true))
        return;

    lastDownloadCheck = juce::Time::getMillisecondCounter();

    juce::Thread::launch ([root = library->getRoot()]
    {
        if (const auto n = FolderAccess::requestDownloads (root); n > 0)
            juce::Logger::writeToLog ("Library: asked the cloud drive to download " + juce::String (n) + " file(s)");

        busy = false;
    });
   #endif
}

void LibraryPanel::chooseFolder()
{
    const auto start = library != nullptr ? library->getRoot() : LibraryLocation::getDefaultFolder().getParentDirectory();
    fileChooser = std::make_unique<juce::FileChooser> (tr ("Choose the library folder"), start);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto url = chooser.getURLResult();

        if (url.isEmpty())
            return;

        const auto folder = LibraryLocation::saveChosen (url);

        if (folder == juce::File())
        {
            showError (tr ("Practice Vibes can't use that folder. Try another one, or keep the topics on this device."));
            return;
        }

        openLibrary (folder);
    });
}

void LibraryPanel::showFolderMenu()
{
    if (library == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader (library->getRoot().getFullPathName());
    const auto isDefault = library->getRoot() == LibraryLocation::getDefaultFolder();
   #if ! JUCE_IOS
    menu.addItem (tr ("Show the folder"), [this] { library->getRoot().revealToUser(); });
    menu.addItem (tr ("To share topics with an iPad or iPhone, use a folder in iCloud Drive"), false, false, [] {});
   #else
    if (isDefault)
        menu.addItem (tr ("Files app: On My iPad > Practice Vibes > Library"), false, false, [] {});

    menu.addItem (tr ("Only iCloud Drive can keep topics in sync with your other devices"), false, false, [] {});
   #endif
    menu.addItem (tr ("Use a different folder..."), [this] { chooseFolder(); });

    if (! isDefault)
        menu.addItem (
                     #if JUCE_IOS
                      tr ("Use the folder on this iPad"),
                     #else
                      tr ("Use the default folder"),
                     #endif
                      [this]
        {
            LibraryLocation::save (LibraryLocation::getDefaultFolder());
            openLibrary (LibraryLocation::getDefaultFolder());
        });
    menu.addItem (tr ("Check for changes now"), [this]
    {
        flush();
        library->rescan();
        signature = library->computeSignature();
        refreshAfterRescan();
    });
    menu.addSeparator();
    menu.addItem (tr ("Storage and clean-up..."), [this] { showStorage(); });
    menu.addSeparator();
    menu.addItem (tr ("Import a shared topic (.pvtopic)..."), [this] { chooseTopicToImport(); });
   #if ! JUCE_IOS
    menu.addItem (tr ("Show the shared topics folder"), [] { auto f = sharedTopicsFolder(); f.createDirectory(); f.revealToUser(); });
   #endif

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&folderButton));
}

void LibraryPanel::showStorage()
{
    if (library == nullptr || ! showOverlay)
        return;

    flush();
    auto panel = std::make_unique<StoragePanel> (library.get(), LoopFiles::getLoopsFolder(), getAppDataFolder());
    panel->onRemoved = [this] { checkForChanges(); };
    showOverlay (tr ("Storage and clean-up"), std::move (panel));
}

void LibraryPanel::shareTopic (const juce::String& topicId)
{
    if (library == nullptr || ! showOverlay)
        return;

    flush();
    auto* t = library->findTopic (topicId);

    if (t == nullptr)
        return;

   #if JUCE_WINDOWS
    const auto folder = sharedTopicsFolder();
    const juce::String buttonText = tr ("Save and show in Explorer");
   #elif JUCE_MAC
    const auto folder = sharedTopicsFolder();
    const juce::String buttonText = tr ("Share...");
   #else
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Shared topics");
    const juce::String buttonText = tr ("Share...");
   #endif

    auto context = getShareContext ? getShareContext (topicId) : ShareContext();

    if (context.teacher)
        context.groups = StudentGroups::load (library->getRoot());

    auto panel = std::make_unique<SharePanel> (*t, folder, buttonText, std::move (context));
    panel->onCancel = [this] { if (hideOverlay) hideOverlay(); };
    panel->onGroupSent = [this] (const std::vector<GroupSend>& sends)
    {
        if (library == nullptr)
            return;

        auto current = StudentGroups::load (library->getRoot());   // fresh: another device may have changed it
        current.sends.insert (current.sends.end(), sends.begin(), sends.end());

        if (! current.save (library->getRoot()))
            juce::Logger::writeToLog ("Could not save the group sends");

        groups = std::move (current);
        lastSentMembers.clear();

        for (auto& s : sends)
            if (s.member.isNotEmpty())
                lastSentMembers.add (s.member);
    };
    lastSentMembers.clear();
    panel->onWritten = [this, topicId] (const juce::Array<juce::File>& files, bool isDocument)
    {
        // Closing the overlay deletes the panel, and this lambda with it: the rest runs from a copy on the stack.
        auto rest = [this, topicId, files, isDocument]
        {
            juce::ignoreUnused (isDocument);   // used on iPad / Mac only

            if (files.isEmpty())
                return;

            if (auto* shared = library != nullptr ? library->findTopic (topicId) : nullptr)
            {
                shared->lastSent = juce::Time::getCurrentTime();
                library->saveTopic (*shared);
                signature = library->computeSignature();
                showSelection();
            }

           #if JUCE_IOS || JUCE_MAC
            // One file per member: a share sheet each, from a list that says whose file it is.
            if (! isDocument && files.size() > 1 && lastSentMembers.size() == files.size())
            {
                shareOnePerMember (files);
                return;
            }
           #endif

           #if JUCE_IOS
            juce::Array<juce::URL> urls;

            for (auto& f : files)
                urls.add (juce::URL (f));

            shareBox = juce::ContentSharer::shareFilesScoped (urls, [] (bool, const juce::String&) {}, this);
           #elif JUCE_MAC
            showSystemSharePicker (files, *this);
           #else
            // Explorer with the file selected (a document: in its folder, with its files): drag them into a mail or WhatsApp.
            files.getFirst().revealToUser();
           #endif
        };

        if (hideOverlay)
            hideOverlay();

        rest();
    };
    showOverlay (tr ("Share {title}", quoted (t->title)), std::move (panel));
}

void LibraryPanel::editAssignment (const juce::String& topicId)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr || ! showOverlay)
        return;

    flush();
    showOverlay (tr ("Assignment: {title}", t->title), std::make_unique<AssignmentEditor> (t->assignment,
        [this, topicId] (Assignment a)
        {
            if (auto* topic = library != nullptr ? library->findTopic (topicId) : nullptr)
            {
                topic->assignment = a;
                library->saveTopic (*topic);
                signature = library->computeSignature();
                showSelection();

                if (onTopicsChanged)
                    onTopicsChanged();
            }

            if (hideOverlay)
                hideOverlay();
        },
        [this] { if (hideOverlay) hideOverlay(); }));
}

void LibraryPanel::chooseTopicToImport()
{
   #if JUCE_IOS
    const juce::String pattern = "*";      // the iPad picker doesn't know the .pvtopic type by pattern
   #else
    const juce::String pattern = juce::String ("*") + TopicShare::extension;
   #endif
    fileChooser = std::make_unique<juce::FileChooser> (tr ("Import a shared topic"), juce::File(), pattern);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis = SafePointer<LibraryPanel> (this)] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr || fc.getURLResult().isEmpty())
            return;

        const auto url = fc.getURLResult();

        if (url.isLocalFile() && url.getLocalFile().existsAsFile())
        {
            safeThis->importSharedTopic (url.getLocalFile());
            return;
        }

        // iPad: a security-scoped URL; copy it while it can be read.
        const auto copy = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("import.pvtopic");

        if (std::unique_ptr<juce::InputStream> in { url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)) })
        {
            copy.deleteFile();
            juce::FileOutputStream out (copy);

            if (out.openedOk() && out.writeFromInputStream (*in, -1) > 0)
            {
                out.flush();
                safeThis->importSharedTopic (copy);
                return;
            }
        }

        safeThis->showError (tr ("Could not read the chosen file."));
    });
}

void LibraryPanel::importSharedTopic (const juce::File& file)
{
    if (library == nullptr)
    {
        showError (tr ("Choose where your library lives first (Topics tab), then open the shared topic again."));
        return;
    }

    if (! showOverlay)
        return;

    flush();
    checkForChanges();

    juce::String error;
    auto package = TopicShare::open (file, error);

    if (package == nullptr)
    {
        juce::Logger::writeToLog ("Could not open shared topic " + file.getFileName() + ": " + error);
        showError (error);
        return;
    }

    juce::Logger::writeToLog ("Opening shared topic " + file.getFileName());
    auto panel = std::make_unique<ImportPanel> ([this] { return library.get(); }, std::move (package), isTeacher && isTeacher());
    panel->onCancel = [this] { if (hideOverlay) hideOverlay(); };
    panel->onImported = [this] (const juce::String& topicId)
    {
        // Closing the overlay deletes the panel, and this lambda with it: the rest runs from a copy on the stack.
        auto rest = [this, id = topicId]
        {
            library->rescan();
            signature = library->computeSignature();
            refreshAfterRescan();
            selectTopic (id);
        };

        if (hideOverlay)
            hideOverlay();

        rest();
    };
    showOverlay (tr ("Import shared topic"), std::move (panel));
}

//==============================================================================
void LibraryPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
    theme::paintListCard (g, topicList);
}

void LibraryPanel::resized()
{
    const auto hasLibrary = library != nullptr;

    setupLabel.setVisible (! hasLibrary);
    useDefaultButton.setVisible (! hasLibrary);
    chooseFolderButton.setVisible (! hasLibrary && chooseFolderButton.getParentComponent() == this);

    for (juce::Component* c : { (juce::Component*) &searchBox, (juce::Component*) &newTopicButton,
                                (juce::Component*) &folderButton, (juce::Component*) &tagsButton, (juce::Component*) &topicList })
        c->setVisible (hasLibrary);

    auto area = getLocalBounds().reduced (12, 8);

    if (! hasLibrary)
    {
        auto box = area.withSizeKeepingCentre (juce::jmin (640, area.getWidth()), 300);
        setupLabel.setBounds (box.removeFromTop (170));
        box.removeFromTop (10);
        useDefaultButton.setBounds (box.removeFromTop (44).reduced (40, 2));
        box.removeFromTop (8);
        chooseFolderButton.setBounds (box.removeFromTop (44).reduced (40, 2));
        topicView->setVisible (false);
        inboxView->setVisible (false);
        emptyLabel.setVisible (false);
        return;
    }

    auto left = area.removeFromLeft (juce::jlimit (220, 340, area.getWidth() / 3));
    area.removeFromLeft (12);

    auto leftTop = left.removeFromTop (40);
    newTopicButton.setBounds (leftTop.removeFromRight (120).reduced (2, 3));
    searchBox.setBounds (leftTop.reduced (0, 4).withTrimmedRight (6));
    left.removeFromTop (4);
    tagsButton.setBounds (left.removeFromTop (34).reduced (0, 2));
    left.removeFromTop (4);
    folderButton.setBounds (left.removeFromBottom (40).reduced (0, 4));
    left.removeFromBottom (4);
    topicList.setBounds (left.reduced (theme::listCardPadX, theme::listCardPadY));

    topicView->setBounds (area);
    inboxView->setBounds (area);
    emptyLabel.setBounds (area);
}

void LibraryPanel::visibilityChanged()
{
    if (! isVisible())
        flush();
    else
        lastPoll = 0;   // check for changes from other devices right away
}

//==============================================================================
int LibraryPanel::getNumRows()
{
    return (int) rowIds.size();
}

void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (library == nullptr || ! juce::isPositiveAndBelow (row, (int) rowIds.size()))
        return;

    auto r = theme::paintListRow (g, width, height, selected).reduced (2, 2);
    const auto& id = rowIds[(size_t) row];

    if (id == inboxId)
    {
        g.setColour (theme::highlight());
        g.setFont (juce::FontOptions (15.0f).withStyle ("Bold"));
        g.drawText (tr ("Inbox"), r.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (theme::text().withAlpha (0.65f));
        g.setFont (juce::FontOptions (12.5f));
        const auto n = library->getInbox().size();
        g.drawText (trn ("{n} file to sort into topics", "{n} files to sort into topics", n), r, juce::Justification::centredLeft);
        return;
    }

    if (auto* t = library->findTopic (id))
    {
        g.setColour (theme::text());
        g.setFont (juce::FontOptions (15.0f).withStyle ("Bold"));
        g.drawText (t->title.isNotEmpty() ? t->title : tr ("(untitled)"), r.removeFromTop (22), juce::Justification::centredLeft, true);
        g.setColour (theme::text().withAlpha (0.6f));
        g.setFont (juce::FontOptions (12.5f));
        const auto n = (int) t->resources.size();
        auto info = formatDate (t->created) + (n > 0 ? "   " + trn ("{n} resource", "{n} resources", n) : juce::String())
                  + (t->student.isNotEmpty() ? "   " + t->student : juce::String());

        // When the search or tag filter matched something not shown in this row, say where instead.
        if (const auto why = t->describeMatch (searchBox.getText(), tagFilter); why.isNotEmpty())
        {
            g.setColour (selected ? theme::text().withAlpha (0.85f) : theme::highlight().withAlpha (0.9f));
            info = why;
        }

        const auto infoWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (12.5f)), info)) + 10;
        g.drawText (info, r.removeFromLeft (juce::jmin (infoWidth, r.getWidth())), juce::Justification::centredLeft, true);
        drawTagChips (g, r.reduced (0, 3), t->tags, tagFilter);
    }
}

void LibraryPanel::selectedRowsChanged (int lastRowSelected)
{
    const auto id = juce::isPositiveAndBelow (lastRowSelected, (int) rowIds.size()) ? rowIds[(size_t) lastRowSelected] : juce::String();

    if (rebuilding || id == selectedId)
        return;

    flush();
    selectedId = id;
    showSelection();
}

void LibraryPanel::showTagMenu()
{
    if (library == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem (tr ("All topics"), true, tagFilter.isEmpty(), [this] { tagFilter = {}; updateTagButton(); rebuildList (selectedId); });
    menu.addSeparator();

    const auto tags = library->getAllTags();

    if (tags.isEmpty())
        menu.addItem (tr ("No tags yet: type them under a topic's title or in a resource's details"), false, false, [] {});

    for (auto& tag : tags)
    {
        int count = 0;

        for (auto& t : library->getTopics())
            if (t.matchesTag (tag))
                ++count;

        menu.addItem (tag + "  (" + juce::String (count) + ")", true, tag.equalsIgnoreCase (tagFilter), [this, tag]
        {
            tagFilter = tag;
            updateTagButton();
            rebuildList (selectedId);
        });
    }

    // Teachers: topics by student.
    juce::StringArray students;

    for (auto& t : library->getTopics())
        if (t.student.isNotEmpty() && ! students.contains (t.student, true))
            students.add (t.student);

    students.sortNatural();
    groups = StudentGroups::load (library->getRoot());
    const bool teacher = isTeacher && isTeacher();

    if (! students.isEmpty() || ! groups.groups.empty() || teacher)
    {
        menu.addSectionHeader (tr ("Students"));

        for (auto& name : students)
        {
            const auto filter = Topic::studentFilter (name);
            int count = 0;

            for (auto& t : library->getTopics())
                if (t.matchesTag (filter))
                    ++count;

            menu.addItem (name + "  (" + juce::String (count) + ")", true, filter.equalsIgnoreCase (tagFilter), [this, filter]
            {
                tagFilter = filter;
                updateTagButton();
                rebuildList (selectedId);
            });
        }

        for (auto& g : groups.groups)
        {
            const auto filter = Topic::groupFilter (g.id);
            int count = 0;

            for (auto& t : library->getTopics())
                if (t.matchesTag (filter) || std::any_of (groups.sends.begin(), groups.sends.end(),
                                                          [&] (const GroupSend& s) { return s.topicId == t.id && s.groupId == g.id; }))
                    ++count;

            menu.addItem (tr ("Group: {name}  ({count})", g.name, count), true, filter == tagFilter, [this, filter]
            {
                tagFilter = filter;
                updateTagButton();
                rebuildList (selectedId);
            });
        }

        if (teacher)
        {
            menu.addItem (tr ("New group (ensemble)..."), [this] { editGroups (true); });

            if (! groups.groups.empty())
                menu.addItem (tr ("Edit groups..."), [this] { editGroups (false); });
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tagsButton));
}

void LibraryPanel::updateTagButton()
{
    const auto* group = tagFilter.startsWith ("@group:") ? groups.find (tagFilter.fromFirstOccurrenceOf (":", false, false)) : nullptr;
    tagsButton.setButtonText (tagFilter.isEmpty() ? tr ("Tags")
                              : tagFilter.startsWith ("@student:") ? tr ("Student: {name}   (change / clear)", tagFilter.fromFirstOccurrenceOf (":", false, false))
                              : tagFilter.startsWith ("@group:") ? tr ("Group: {name}   (change / clear)", group != nullptr ? group->name : juce::String ("?"))
                              : tr ("Tag: {tag}   (change / clear)", tagFilter));
    tagsButton.setColour (juce::TextButton::buttonColourId,
                          tagFilter.isEmpty() ? getLookAndFeel().findColour (juce::TextButton::buttonColourId) : theme::accent());
}

bool LibraryPanel::passesTagFilter (const Topic& t) const
{
    if (t.matchesTag (tagFilter))
        return true;

    // A group: its members' copies (above) and the topics sent to it.
    if (! tagFilter.startsWith ("@group:"))
        return false;

    const auto groupId = tagFilter.fromFirstOccurrenceOf (":", false, false);
    return std::any_of (groups.sends.begin(), groups.sends.end(),
                        [&] (const GroupSend& s) { return s.topicId == t.id && s.groupId == groupId; });
}

void LibraryPanel::shareOnePerMember (const juce::Array<juce::File>& files)
{
    if (! showOverlay)
        return;

    auto shareOne = [this] (const juce::File& file, juce::Component& anchor, std::function<void (bool)> done)
    {
       #if JUCE_IOS
        juce::ignoreUnused (anchor);
        shareBox = juce::ContentSharer::shareFilesScoped ({ juce::URL (file) },
                                                         [done] (bool ok, const juce::String&) { if (done) done (ok); }, this);
       #elif JUCE_MAC
        showSystemSharePicker ({ file }, anchor);
        if (done) done (true);
       #else
        juce::ignoreUnused (anchor);
        file.revealToUser();
        if (done) done (true);
       #endif
    };

    showOverlay (tr ("Send to each member"), std::make_unique<MemberSharePanel> (lastSentMembers, files, shareOne,
                                                                           [this] { if (hideOverlay) hideOverlay(); }));
}

void LibraryPanel::editGroups (bool addNew)
{
    if (library == nullptr || ! showOverlay)
        return;

    flush();
    juce::StringArray students;

    for (auto& t : library->getTopics())
        if (t.student.isNotEmpty())
            students.addIfNotAlreadyThere (t.student, true);

    students.sortNatural();

    showOverlay (tr ("Groups"), std::make_unique<GroupEditor> (StudentGroups::load (library->getRoot()), students, addNew,
        [this] (StudentGroups edited)
        {
            if (library != nullptr)
            {
                // Keep sends recorded meanwhile (another device), take the groups from the editor.
                auto current = StudentGroups::load (library->getRoot());
                current.groups = std::move (edited.groups);

                if (! current.save (library->getRoot()))
                    showError (tr ("Could not save the groups."));

                groups = std::move (current);

                if (tagFilter.startsWith ("@group:") && groups.find (tagFilter.fromFirstOccurrenceOf (":", false, false)) == nullptr)
                    tagFilter = {};

                updateTagButton();
                rebuildList (selectedId);
            }

            if (hideOverlay)
                hideOverlay();
        },
        [this] { if (hideOverlay) hideOverlay(); }));
}

void LibraryPanel::rebuildList (const juce::String& idToSelect)
{
    const auto selectId = idToSelect;   // copy: callers often pass selectedId itself, which changes below
    const juce::ScopedValueSetter<bool> svs (rebuilding, true);
    rowIds.clear();

    if (library != nullptr)
    {
        const auto filtering = searchBox.getText().trim().isNotEmpty() || tagFilter.isNotEmpty();

        if (! library->getInbox().isEmpty() && ! filtering)
            rowIds.push_back (inboxId);

        groups = StudentGroups::load (library->getRoot());

        for (auto& t : library->getTopics())
            if (t.matches (searchBox.getText()) && passesTagFilter (t))
                rowIds.push_back (t.id);
    }

    topicList.updateContent();

    const auto it = std::find (rowIds.begin(), rowIds.end(), selectId);
    selectedId = it != rowIds.end() ? selectId : juce::String();

    if (it != rowIds.end())
        topicList.selectRow ((int) std::distance (rowIds.begin(), it), true, true);
    else
        topicList.deselectAllRows();

    topicList.repaint();
    showSelection();
}

void LibraryPanel::showSelection()
{
    if (library == nullptr)
        return;

    const auto* topic = library->findTopic (selectedId);
    const auto inbox = selectedId == inboxId;

    topicView->setVisible (topic != nullptr);
    inboxView->setVisible (inbox);
    emptyLabel.setVisible (topic == nullptr && ! inbox);

    if (topic != nullptr)
        topicView->load (*topic, true);
    else if (inbox)
        inboxView->load (library->getInbox());
    else
        emptyLabel.setText (library->getTopics().empty()
                                ? tr ("No topics yet.\n\nA topic is anything you practise: a technique, a song, a lesson.\n"
                                      "Press \"+ New topic\" to create one, then add notes, documents, audio files and links.")
                                : tr ("Select a topic on the left."),
                            juce::dontSendNotification);
}

void LibraryPanel::refreshAfterRescan()
{
    rebuildList (selectedId);

    if (onTopicsChanged)
        onTopicsChanged();
}

void LibraryPanel::checkForChanges()
{
    if (library == nullptr)
        return;

    flush();
    const auto newSignature = library->computeSignature();

    if (newSignature == signature)
        return;

    library->rescan();
    signature = library->computeSignature();
    refreshAfterRescan();
}

void LibraryPanel::selectTopic (const juce::String& topicId)
{
    if (library == nullptr || library->findTopic (topicId) == nullptr)
        return;

    flush();

    if (auto* t = library->findTopic (topicId); ! t->matches (searchBox.getText()) || ! passesTagFilter (*t))
    {
        searchBox.clear();
        tagFilter = {};
        updateTagButton();
    }

    rebuildList (topicId);
}

void LibraryPanel::setRotation (const juce::String& topicId, bool inRotation)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr || t->inRotation == inRotation)
        return;

    flush();
    t->inRotation = inRotation;

    if (! library->saveTopic (*t))
        showError ("Could not save " + t->folder.getChildFile (Library::topicFileName).getFullPathName());

    signature = library->computeSignature();

    if (onTopicsChanged)
        onTopicsChanged();
}

void LibraryPanel::setNoGuitar (const juce::String& topicId, bool noGuitar)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr || t->noGuitar == noGuitar)
        return;

    flush();
    t->noGuitar = noGuitar;

    if (! library->saveTopic (*t))
        showError ("Could not save " + t->folder.getChildFile (Library::topicFileName).getFullPathName());

    signature = library->computeSignature();

    if (onTopicsChanged)
        onTopicsChanged();
}

bool LibraryPanel::openFirstMedia (const juce::String& topicId)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr)
        return false;

    for (auto& r : t->resources)
    {
        const auto kind = kindOf (r);
        const auto playable = (kind == ResourceKind::audio && ! t->isMissing (r))
                           || (kind == ResourceKind::youtube && webViewAvailable() && parseYouTubeUrl (r.url).has_value());

        if (playable)
        {
            openResource (*t, r);
            return true;
        }
    }

    return false;
}

//==============================================================================
void LibraryPanel::editedTopic()
{
    dirty = true;
    lastEdit = juce::Time::getMillisecondCounter();
    topicList.repaint();
}

void LibraryPanel::flush()
{
    if (! dirty || library == nullptr)
        return;

    dirty = false;

    if (auto* t = library->findTopic (topicView->topicId))
    {
        if (! library->saveTopic (*t))
            juce::Logger::writeToLog ("Library: could not save " + t->folder.getChildFile (Library::topicFileName).getFullPathName());

        signature = library->computeSignature();

        if (onTopicsChanged)
            onTopicsChanged();
    }
}

void LibraryPanel::titleSettled (const juce::String& topicId)
{
    if (library == nullptr)
        return;

    flush();

    if (auto* t = library->findTopic (topicId); t != nullptr && library->renameFolderAfterTitle (*t))
    {
        signature = library->computeSignature();

        if (onTopicsChanged)
            onTopicsChanged();
    }
}

void LibraryPanel::chooseTopicVersion (const juce::String& topicId, juce::Component& anchor)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr)
        return;

    flush();
    const auto versions = Library::getTopicFileVersions (*t);
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Keep which version? The others go to the bin."));

    for (size_t i = 0; i < versions.size(); ++i)
    {
        const auto& v = versions[i];
        const auto isCurrent = v.fileName.equalsIgnoreCase (Library::topicFileName);
        const auto title = v.title.isNotEmpty() ? v.title : tr ("(no title)");
        const auto when = i18n::date (v.saved, "%d %b %H:%M:%S");
        menu.addItem ((int) i + 1, isCurrent ? trn ("\"{title}\", saved {when}, {n} resource (the one shown)",
                                                    "\"{title}\", saved {when}, {n} resources (the one shown)", v.resources, title, when)
                                             : trn ("\"{title}\", saved {when}, {n} resource ({file})",
                                                    "\"{title}\", saved {when}, {n} resources ({file})", v.resources, title, when, v.fileName));
    }

    juce::Component::SafePointer<LibraryPanel> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                        [safeThis, topicId, versions] (int result)
    {
        if (safeThis == nullptr || safeThis->library == nullptr || result <= 0 || result > (int) versions.size())
            return;

        auto* topic = safeThis->library->findTopic (topicId);

        if (topic == nullptr)
            return;

        if (! Library::keepTopicFileVersion (*topic, versions[(size_t) result - 1].fileName))
            safeThis->showError (tr ("Could not tidy up the copies in {folder}", topic->folder.getFullPathName()));

        safeThis->library->rescan();
        safeThis->signature = safeThis->library->computeSignature();
        safeThis->refreshAfterRescan();
    });
}

void LibraryPanel::timerCallback()
{
    if (library == nullptr)
        return;

    const auto now = juce::Time::getMillisecondCounter();

    // Not after every short pause: each save is an upload for a cloud drive, and quick rewrites while the
    // previous upload is still running is what makes cloud clients create duplicate copies.
    // Leaving the field (or the topic) saves at once.
    if (dirty && now - lastEdit > 2500)
        flush();

    if (! isShowing() || (lastPoll != 0 && now - lastPoll < 3000))
        return;

    if (now - lastDownloadCheck > 30000)
        requestDownloads();

    lastPoll = now;
    const auto newSignature = library->computeSignature();

    if (newSignature == signature)
        return;

    flush();
    library->rescan();
    signature = library->computeSignature();
    refreshAfterRescan();
}

//==============================================================================
void LibraryPanel::newTopic()
{
    if (library == nullptr)
        return;

    flush();

    if (auto* t = library->createTopic (tr ("New topic")))
    {
        const auto id = t->id;
        signature = library->computeSignature();
        searchBox.clear();
        tagFilter = {};
        updateTagButton();
        rebuildList (id);
        topicView->focusTitle();
    }
    else
    {
        showError (tr ("Could not create a topic folder in {folder}", library->getRoot().getFullPathName()));
    }
}

void LibraryPanel::deleteTopic (const juce::String& topicId)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr)
        return;

    flush();
    const auto files = (int) std::count_if (t->resources.begin(), t->resources.end(), [] (const Resource& r) { return ! r.isLink(); });

   #if JUCE_IOS
    const juce::String where = tr ("This can't be undone.");
   #else
    const juce::String where = tr ("The folder goes to the Recycle Bin.");
   #endif

    auto options = confirmBox (juce::MessageBoxIconType::WarningIcon, tr ("Delete topic"),
                               (files > 0 ? trn ("Delete \"{title}\" with its notes and {n} file?", "Delete \"{title}\" with its notes and {n} files?", files, t->title)
                                          : tr ("Delete \"{title}\" with its notes?", t->title))
                                   + "\n" + where,
                               tr ("Delete"), this);

    juce::AlertWindow::showAsync (options, [safeThis = SafePointer<LibraryPanel> (this), topicId] (int result)
    {
        if (safeThis == nullptr || result != 1 || safeThis->library == nullptr)
            return;

        if (! safeThis->library->deleteTopic (topicId))
            safeThis->showError (tr ("Could not delete the topic folder (is a file in it open in another app?)."));

        safeThis->signature = safeThis->library->computeSignature();
        safeThis->rebuildList (safeThis->library->getTopics().empty() ? juce::String() : safeThis->library->getTopics().front().id);
    });
}

void LibraryPanel::addFiles (const juce::String& topicId)
{
    fileChooser = std::make_unique<juce::FileChooser> (tr ("Add files to the topic"),
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this, topicId] (const juce::FileChooser& chooser)
    {
        addUrls (topicId, chooser.getURLResults());
    });
}

void LibraryPanel::addUrls (const juce::String& topicId, const juce::Array<juce::URL>& urls)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr || urls.isEmpty())
        return;

    flush();
    juce::StringArray failed;

    for (auto& url : urls)
        if (library->addFile (*t, url) == nullptr)
            failed.add (juce::URL::removeEscapeChars (url.getFileName()));

    signature = library->computeSignature();
    showSelection();
    topicList.repaint();

    if (! failed.isEmpty())
        showError (tr ("Could not copy: {files}", failed.joinIntoString (", ")));
}

void LibraryPanel::editResource (const juce::String& topicId, const juce::String& resourceId, bool isNewLink)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;

    if (t == nullptr || showOverlay == nullptr)
        return;

    Resource current;

    if (! isNewLink)
    {
        auto* r = t->findResource (resourceId);

        if (r == nullptr)
            return;

        current = *r;
    }

    auto editor = std::make_unique<ResourceEditor> (current, isNewLink || current.isLink());

    editor->onCancel = [this] { if (hideOverlay) hideOverlay(); };
    editor->onSave = [this, topicId, resourceId, isNewLink] (const juce::String& title, const juce::String& url,
                                                             const juce::StringArray& tags, const juce::String& description)
    {
        if (auto* topic = library->findTopic (topicId))
        {
            if (isNewLink)
            {
                if (auto* r = library->addLink (*topic, title, url))
                {
                    r->description = description;
                    r->tags = tags;
                    library->saveTopic (*topic);
                }
            }
            else if (auto* r = topic->findResource (resourceId))
            {
                r->title = title.isNotEmpty() ? title : (r->isLink() ? Library::titleFromUrl (url) : r->fileName);
                r->description = description;
                r->tags = tags;

                if (r->isLink() && url.isNotEmpty())
                    r->url = url.containsChar (':') ? url : "https://" + url;

                library->saveTopic (*topic);
            }

            signature = library->computeSignature();
            showSelection();
            topicList.repaint();
        }

        if (hideOverlay)
            hideOverlay();
    };

    showOverlay (isNewLink ? tr ("Add link") : tr ("Edit resource"), std::move (editor));
}

void LibraryPanel::showResourceMenu (const juce::String& topicId, const juce::String& resourceId, juce::Component& anchor)
{
    auto* t = library != nullptr ? library->findTopic (topicId) : nullptr;
    auto* r = t != nullptr ? t->findResource (resourceId) : nullptr;

    if (r == nullptr)
        return;

    const auto index = (int) (r - t->resources.data());
    const auto count = (int) t->resources.size();
    const auto isLink = r->isLink();
    const auto file = t->getFile (*r);

    auto move = [this, topicId, resourceId] (int delta)
    {
        if (auto* topic = library->findTopic (topicId))
        {
            library->moveResource (*topic, resourceId, delta);
            signature = library->computeSignature();
            showSelection();
        }
    };

    juce::PopupMenu menu;
    menu.addItem (tr ("Edit title and description..."), [this, topicId, resourceId] { editResource (topicId, resourceId, false); });
    menu.addItem (tr ("Move up"), index > 0, false, [move] { move (-1); });
    menu.addItem (tr ("Move down"), index < count - 1, false, [move] { move (1); });

   #if ! JUCE_IOS
    if (! isLink)
        menu.addItem (tr ("Show in folder"), file.existsAsFile(), false, [file] { file.revealToUser(); });
   #endif

    if (isLink && kindOf (*r) == ResourceKind::youtube)
        menu.addItem (tr ("Open in the browser"), [url = r->url] { juce::URL (url).launchInDefaultBrowser(); });

    menu.addSeparator();
    menu.addItem (isLink ? tr ("Remove link") : tr ("Remove (deletes the file)"), [this, topicId, resourceId, isLink, title = r->title]
    {
        auto doRemove = [safeThis = SafePointer<LibraryPanel> (this), topicId, resourceId]
        {
            if (safeThis == nullptr || safeThis->library == nullptr)
                return;

            if (auto* topic = safeThis->library->findTopic (topicId))
                if (! safeThis->library->removeResource (*topic, resourceId))
                    safeThis->showError (tr ("Could not remove the file (is it open in another app?)."));

            safeThis->signature = safeThis->library->computeSignature();
            safeThis->showSelection();
            safeThis->topicList.repaint();
        };

        if (isLink)
        {
            doRemove();
            return;
        }

        auto options = confirmBox (juce::MessageBoxIconType::QuestionIcon, tr ("Remove resource"),
                                   tr ("Remove \"{title}\" and delete its file from the topic folder?", title),
                                   tr ("Remove"), this);
        juce::AlertWindow::showAsync (options, [doRemove] (int result) { if (result == 1) doRemove(); });
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor));
}

void LibraryPanel::showInboxMenu (const juce::File& file, juce::Component& anchor)
{
    if (library == nullptr)
        return;

    auto moveTo = [safeThis = SafePointer<LibraryPanel> (this), file] (const juce::String& topicId)
    {
        if (safeThis == nullptr || safeThis->library == nullptr)
            return;

        if (auto* t = safeThis->library->findTopic (topicId))
            if (safeThis->library->moveInboxFile (*t, file) == nullptr)
                safeThis->showError (tr ("Could not move {file}", file.getFileName()));

        safeThis->signature = safeThis->library->computeSignature();
        safeThis->rebuildList (safeThis->library->getInbox().isEmpty() ? topicId : inboxId);
    };

    juce::PopupMenu menu;
    menu.addItem (tr ("New topic named \"{name}\"", file.getFileNameWithoutExtension()), [this, file, moveTo]
    {
        if (auto* t = library->createTopic (file.getFileNameWithoutExtension()))
            moveTo (t->id);
    });
    menu.addSeparator();

    for (auto& t : library->getTopics())
        menu.addItem (t.title.isNotEmpty() ? t.title : juce::String ("(untitled)"), [moveTo, id = t.id] { moveTo (id); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor));
}

//==============================================================================
void LibraryPanel::openResource (const Topic& topic, const Resource& r)
{
    if (onTopicOpened)
        onTopicOpened (topic.id);

    if (! r.isLink())
    {
        openFile (topic.getFile (r), r.title);
        return;
    }

    const auto url = r.url;

    if (kindOf (r) == ResourceKind::youtube && webViewAvailable() && onOpenYouTube != nullptr)
    {
        if (const auto link = parseYouTubeUrl (url))
        {
            onOpenYouTube (*link, r.title);
            return;
        }
    }

    // Websites open in the system browser (owner's choice); only YouTube stays in the app.
    juce::URL (url).launchInDefaultBrowser();
}

void LibraryPanel::openFile (const juce::File& file, const juce::String& title)
{
    if (! file.existsAsFile())
        return;

    if (classifyFileName (file.getFileName()) == ResourceKind::audio)
    {
        if (onOpenAudio)
            onOpenAudio (file);

        return;
    }

    if (viewerCanShow (file) && showOverlay != nullptr)
    {
       #if JUCE_IOS
        std::function<void()> outside;   // files have no "outside" on iOS without a share sheet
       #else
        std::function<void()> outside = [file] { file.startAsProcess(); };
       #endif
        showOverlay (title, std::make_unique<Viewer> (juce::URL (file).toString (false), outside));
        return;
    }

   #if JUCE_IOS
    showError (tr ("This kind of file can't be shown on the iPad."));
   #else
    if (! file.startAsProcess())
        showError (tr ("No app on this computer opens {file}", file.getFileName()));
   #endif
}

void LibraryPanel::showError (const juce::String& message)
{
    juce::Logger::writeToLog ("Library: " + message);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon, tr ("Topics"), message, tr ("OK"), this),
                                  nullptr);
}

} // namespace pv
