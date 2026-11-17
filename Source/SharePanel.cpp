#include "SharePanel.h"
#include "ResourceWidgets.h"
#include "StoragePanel.h"
#include "Theme.h"

namespace pv
{

namespace
{
    void styleInfo (juce::Label& l, float alpha = 0.7f)
    {
        l.setColour (juce::Label::textColourId, theme::text().withAlpha (alpha));
        l.setJustificationType (juce::Justification::topLeft);
        l.setMinimumHorizontalScale (1.0f);
    }
}

//==============================================================================
SharePanel::SharePanel (const Topic& t, juce::File folder, juce::String shareButtonText, ShareContext ctx)
    : topic (t), outputFolder (std::move (folder)), items (TopicShare::listFiles (t)), shareText (shareButtonText),
      context (std::move (ctx)), shareButton (shareButtonText)
{
    getProperties().set ("overlayWidth", 680);
    getProperties().set ("overlayHeight", 680);

    nameLabel.setText ("Your name", juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (nameLabel);
    nameEditor.setText (context.senderName, false);
    nameEditor.setTextToShowWhenEmpty ("Optional: a first name or nickname, shown to the person you send it to",
                                       theme::text().withAlpha (0.35f));
    nameEditor.setTooltip ("Kept only on this device and put only in the files you send");
    styleEditor (nameEditor);
    addAndMakeVisible (nameEditor);

    newTopicToggle.setTooltip ("Gets a new topic id: importing it never updates the topic you sent to someone else");
    newTopicToggle.onClick = [this] { updateTotal(); };
    addChildComponent (newTopicToggle);

    if (! context.summary.isEmpty() && context.summary.sessions > 0)
        summaryToggle.setButtonText ("Include my practice on this topic: " + context.summary.describe());

    summaryToggle.setTooltip ("Totals only (time per week, tempo, days in a row): no times of day, nothing else from your log");
    addChildComponent (summaryToggle);

    formatLabel.setText ("Send as", juce::dontSendNotification);
    formatLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (formatLabel);
    formatBox.addItem ("Practice Vibes topic (.pvtopic): opens in the app, updates the topic", 1);
    formatBox.addItem ("Document for anyone: a web page + the files (no app needed)", 2);
    formatBox.setSelectedId (1, juce::dontSendNotification);
    formatBox.setTooltip ("Not everyone has the app: the document opens in any browser or phone (and prints to PDF), "
                          "with the notes, the links under their titles and the list of files");
    formatBox.onChange = [this] { updateTotal(); resized(); };
    addAndMakeVisible (formatBox);

    styleInfo (introLabel);
    addAndMakeVisible (introLabel);

    for (auto& item : items)
    {
        juce::String text = item.title;

        if (item.isTake)
            text << "  (take)";
        else if (item.isSong)
            text << "  (song)";

        auto* toggle = toggles.add (new juce::ToggleButton (text));
        toggle->setToggleState (item.include, juce::dontSendNotification);
        toggle->setTooltip (item.isSong ? "Leave commercial recordings out: the receiver connects their own copy (same file name)"
                                        : item.fileName);
        toggle->onClick = [this] { updateTotal(); };
        rowHolder.addAndMakeVisible (toggle);

        auto* size = sizeLabels.add (new juce::Label());
        size->setText (StorageScan::formatBytes (item.bytes), juce::dontSendNotification);
        size->setJustificationType (juce::Justification::centredRight);
        size->setColour (juce::Label::textColourId, theme::text().withAlpha (0.55f));
        rowHolder.addAndMakeVisible (size);
    }

    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    noFilesLabel.setText ("This topic has no files: only the notes and links go in.", juce::dontSendNotification);
    styleInfo (noFilesLabel, 0.5f);
    addChildComponent (noFilesLabel);
    noFilesLabel.setVisible (items.empty());

    const bool anyShrinkable = std::any_of (items.begin(), items.end(), [] (auto& i) { return i.canShrink; });
    shrinkToggle.setVisible (anyShrinkable);
    shrinkToggle.setToggleState (anyShrinkable, juce::dontSendNotification);
    shrinkToggle.onClick = [this] { updateTotal(); };
    addChildComponent (shrinkToggle);

    styleInfo (leftOutLabel, 0.55f);
    addAndMakeVisible (leftOutLabel);

    totalLabel.setFont (theme::textFont (16.0f, "SemiBold"));
    styleInfo (totalLabel, 0.9f);
    addAndMakeVisible (totalLabel);

    shareButton.setColour (juce::TextButton::buttonColourId, theme::accent());
    shareButton.onClick = [this] { share(); };
    addAndMakeVisible (shareButton);

    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (cancelButton);

    updateTotal();
}

SharePanel::~SharePanel() = default;

void SharePanel::updateTotal()
{
    juce::int64 total = 4096;   // topic.xml and the zip's own bookkeeping
    int leftOut = 0;

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (toggles[(int) i]->getToggleState())
            total += shrinkToggle.isVisible() && shrinkToggle.getToggleState() ? TopicShare::shrunkBytes (items[i]) : items[i].bytes;
        else
            ++leftOut;
    }

    newTopicToggle.setVisible (context.teacher && ! isDocument());
    summaryToggle.setVisible (! context.teacher && ! isDocument() && ! context.summary.isEmpty() && context.summary.sessions > 0);
    nameLabel.setVisible (! isDocument());
    nameEditor.setVisible (! isDocument());

    introLabel.setText (isDocument() ? "Choose which files go along with the page. The notes and links are on the page; "
                                       "your private notes and practice log are never included."
                                     : "Choose which files go in. The notes, links and resource details always travel; "
                                       "your private notes and practice log never do.",
                        juce::dontSendNotification);
    totalLabel.setText ("Total: " + TopicShare::sizeHint (total), juce::dontSendNotification);
    totalLabel.setColour (juce::Label::textColourId, total <= TopicShare::emailLimitBytes ? theme::text().withAlpha (0.9f) : theme::highlight());

    leftOutLabel.setText (leftOut == 0 ? juce::String()
                                       : (leftOut == 1 ? juce::String ("The file left out stays") : juce::String (leftOut) + " files left out stay")
                                             + (isDocument() ? juce::String (" listed on the page as not included.")
                                                             : juce::String (" listed: the receiver connects their own copy (found by file name), or locates it once.")),
                          juce::dontSendNotification);
}

void SharePanel::share()
{
    if (writing)
        return;

    TopicShare::ExportOptions options;
    options.shrinkAudio = shrinkToggle.isVisible() && shrinkToggle.getToggleState();
    options.asNewTopic = newTopicToggle.isVisible() && newTopicToggle.getToggleState();
    options.senderName = nameEditor.getText().trim().substring (0, 60);

    if (summaryToggle.isVisible() && summaryToggle.getToggleState())
        options.summary = context.summary;

    if (options.senderName != context.senderName.trim() && context.onNameChanged)
        context.onNameChanged (options.senderName);

    for (size_t i = 0; i < items.size(); ++i)
        if (toggles[(int) i]->getToggleState())
            options.includedFiles.add (items[i].fileName);

    outputFolder.createDirectory();
    const auto target = TopicShare::fileNameFor (topic, outputFolder);
    const auto document = isDocument();

    writing = true;
    shareButton.setEnabled (false);
    shareButton.setButtonText ("Packing...");

    juce::Thread::launch ([safeThis = juce::WeakReference<SharePanel> (this), topicCopy = topic, options, target, document,
                           folder = outputFolder]
    {
        juce::Array<juce::File> written;
        const auto result = document ? TopicShare::writeDocument (topicCopy, options, folder, written)
                                     : TopicShare::write (topicCopy, options, target);

        if (! document)
            written.add (target);

        juce::MessageManager::callAsync ([safeThis, result, written, document]
        {
            const auto target = written.isEmpty() ? juce::File() : written.getFirst();

            if (result.failed())
                juce::Logger::writeToLog ("Could not share the topic: " + result.getErrorMessage());
            else
                juce::Logger::writeToLog ("Shared topic written to " + target.getFullPathName() + " ("
                                          + StorageScan::formatBytes (target.getSize()) + ")");

            if (safeThis == nullptr)
                return;

            safeThis->writing = false;
            safeThis->shareButton.setEnabled (true);
            safeThis->shareButton.setButtonText (safeThis->shareText);

            if (result.failed())
            {
                safeThis->totalLabel.setText ("Could not write the file: " + result.getErrorMessage(), juce::dontSendNotification);
                return;
            }

            if (safeThis->onWritten)
                safeThis->onWritten (written, document);
        });
    });
}

void SharePanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
}

void SharePanel::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto buttons = area.removeFromBottom (40);
    shareButton.setBounds (buttons.removeFromRight (190).reduced (2, 3));
    cancelButton.setBounds (buttons.removeFromRight (110).reduced (2, 3));

    area.removeFromBottom (6);
    totalLabel.setBounds (area.removeFromBottom (44));
    leftOutLabel.setBounds (area.removeFromBottom (leftOutLabel.getText().isEmpty() ? 0 : 40));

    if (shrinkToggle.isVisible())
        shrinkToggle.setBounds (area.removeFromBottom (32));

    if (summaryToggle.isVisible())
        summaryToggle.setBounds (area.removeFromBottom (32));

    if (newTopicToggle.isVisible())
        newTopicToggle.setBounds (area.removeFromBottom (32));

    if (nameEditor.isVisible())
    {
        auto row = area.removeFromBottom (38).reduced (0, 4);
        nameLabel.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (8);
        nameEditor.setBounds (row);
    }

    auto formatRow = area.removeFromTop (38).reduced (0, 3);
    formatLabel.setBounds (formatRow.removeFromLeft (70));
    formatRow.removeFromLeft (8);
    formatBox.setBounds (formatRow);
    area.removeFromTop (8);
    introLabel.setBounds (area.removeFromTop (48));
    area.removeFromTop (4);
    viewport.setBounds (area);
    noFilesLabel.setBounds (area.removeFromTop (40));

    constexpr int rowHeight = 32;
    const auto width = juce::jmax (1, viewport.getMaximumVisibleWidth());
    rowHolder.setSize (width, juce::jmax (1, toggles.size() * rowHeight));

    for (int i = 0; i < toggles.size(); ++i)
    {
        auto row = juce::Rectangle<int> (0, i * rowHeight, width, rowHeight);
        sizeLabels[i]->setBounds (row.removeFromRight (90));
        toggles[i]->setBounds (row);
    }
}

//==============================================================================
ImportPanel::ImportPanel (std::function<Library*()> libraryFn, std::unique_ptr<TopicShare::Package> p, bool isTeacher)
    : getLibrary (std::move (libraryFn)), package (std::move (p)), teacher (isTeacher)
{
    studentLabel.setText ("Student", juce::dontSendNotification);
    studentLabel.setJustificationType (juce::Justification::centredRight);
    addChildComponent (studentLabel);
    studentEditor.setTextToShowWhenEmpty ("Optional: who this topic is for (a first name or nickname)", theme::text().withAlpha (0.35f));
    studentEditor.setTooltip ("Groups your topics by student (Tags menu). Stays on your devices: never sent to anyone");
    styleEditor (studentEditor);
    addChildComponent (studentEditor);

    if (teacher && package != nullptr)
    {
        juce::String initial = package->senderName;

        if (auto* library = getLibrary ? getLibrary() : nullptr)
            if (auto* existing = library->findTopic (package->topic.id); existing != nullptr && existing->student.isNotEmpty())
                initial = existing->student;

        studentEditor.setText (initial, false);
        studentLabel.setVisible (true);
        studentEditor.setVisible (true);
    }

    getProperties().set ("overlayWidth", 640);
    getProperties().set ("overlayHeight", 520);

    summaryLabel.setFont (theme::textFont (16.0f));
    styleInfo (summaryLabel, 0.9f);
    addAndMakeVisible (summaryLabel);

    details.setMultiLine (true, true);
    details.setReadOnly (true);
    details.setScrollbarsShown (true);
    details.setCaretVisible (false);
    styleEditor (details);
    addAndMakeVisible (details);

    styleCaption (missingHeader, "Not on this device");
    addChildComponent (missingHeader);
    missingViewport.setViewedComponent (&missingHolder, false);
    missingViewport.setScrollBarsShown (true, false);
    addChildComponent (missingViewport);

    applyButton.setColour (juce::TextButton::buttonColourId, theme::accent());
    applyButton.onClick = [this] { import (TopicShare::Mode::update); };
    addAndMakeVisible (applyButton);

    copyButton.setTooltip ("Keeps your topic as it is and adds this one as a separate topic");
    copyButton.onClick = [this] { import (TopicShare::Mode::copy); };
    addChildComponent (copyButton);

    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (cancelButton);

    refresh();
}

ImportPanel::~ImportPanel() = default;

void ImportPanel::refresh()
{
    auto* library = getLibrary ? getLibrary() : nullptr;

    if (library == nullptr || package == nullptr)
        return;

    current = TopicShare::preview (*library, *package, TopicShare::Mode::update, located);
    summaryLabel.setText (current.summary(), juce::dontSendNotification);

    juce::String text;
    auto listLine = [&text] (const juce::String& heading, const juce::StringArray& names)
    {
        if (! names.isEmpty())
            text << heading << ":\n    " << names.joinIntoString ("\n    ") << "\n\n";
    };

    if (package->senderName.isNotEmpty())
        text << "From " << package->senderName << ". ";

    if (package->sharedAt != juce::Time())
        text << "Shared on " << package->sharedAt.formatted ("%d %b %Y at %H:%M") << ".";

    text << "\n\n";

    if (! package->summary.isEmpty())
        text << "Practice on this topic: " << package->summary.describe() << "\n\n";

    if (! current.updatesExisting && package->topic.description.isNotEmpty())
        text << "Notes:\n" << package->topic.description.trim() << "\n\n";

    listLine (current.updatesExisting ? "New resources" : "Resources", current.newResources);
    listLine ("Updated files", current.updatedFiles);
    listLine ("Found on this device", current.reconnected);
    details.setText (text.trim(), false);

    applyButton.setButtonText (current.updatesExisting ? "Apply update" : "Import");
    applyButton.setEnabled (current.changesAnything());
    copyButton.setVisible (current.updatesExisting);
    cancelButton.setButtonText (current.changesAnything() ? "Cancel" : "Close");

    missingLabels.clear();
    locateButtons.clear();

    for (auto& name : current.missing)
    {
        auto* label = missingLabels.add (new juce::Label ({}, name));
        label->setColour (juce::Label::textColourId, theme::text().withAlpha (0.8f));
        missingHolder.addAndMakeVisible (label);

        auto* button = locateButtons.add (new juce::TextButton ("Locate..."));
        button->setTooltip ("Point to your copy of " + name + ": it is copied into the topic");
        button->onClick = [this, name] { locate (name); };
        missingHolder.addAndMakeVisible (button);
    }

    missingHeader.setVisible (! current.missing.isEmpty());
    missingViewport.setVisible (! current.missing.isEmpty());
    resized();
}

void ImportPanel::locate (const juce::String& fileName)
{
    const auto ext = juce::File::createFileWithoutCheckingPath (fileName).getFileExtension();
    chooser = std::make_unique<juce::FileChooser> ("Where is " + quoted (fileName) + "?", juce::File(),
                                                   ext.isNotEmpty() ? "*" + ext : "*");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis = juce::WeakReference<ImportPanel> (this), fileName] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr || fc.getURLResult().isEmpty() || safeThis->package == nullptr)
            return;

        // Copied next to the unpacked topic right away: on the iPad the picked file is only readable for a moment.
        const auto copy = safeThis->package->stage.getChildFile (".located").getChildFile (fileName);
        copy.getParentDirectory().createDirectory();
        bool ok = false;

        if (std::unique_ptr<juce::InputStream> in { fc.getURLResult().createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)) })
        {
            juce::FileOutputStream out (copy);

            if (out.openedOk())
            {
                out.setPosition (0);
                out.truncate();
                ok = out.writeFromInputStream (*in, -1) >= 0;
            }
        }

        if (! ok)
        {
            juce::Logger::writeToLog ("Could not read the located file for " + fileName);
            return;
        }

        safeThis->located[fileName] = copy;
        safeThis->refresh();
    });
}

void ImportPanel::import (TopicShare::Mode mode)
{
    auto* library = getLibrary ? getLibrary() : nullptr;

    if (library == nullptr || package == nullptr)
        return;

    juce::String topicId;
    const auto result = TopicShare::apply (*library, *package, mode, located, topicId,
                                           teacher ? studentEditor.getText() : juce::String());

    if (result.failed())
    {
        summaryLabel.setText ("Could not import the topic: " + result.getErrorMessage(), juce::dontSendNotification);
        juce::Logger::writeToLog ("Topic import failed: " + result.getErrorMessage());
        return;
    }

    juce::Logger::writeToLog ("Imported shared topic \"" + package->topic.title + "\" ("
                              + (mode == TopicShare::Mode::copy ? "copy" : current.updatesExisting ? "update" : "new") + ")");

    if (onImported)
        onImported (topicId);
}

void ImportPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
}

void ImportPanel::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto buttons = area.removeFromBottom (40);
    applyButton.setBounds (buttons.removeFromRight (160).reduced (2, 3));

    if (copyButton.isVisible())
        copyButton.setBounds (buttons.removeFromRight (150).reduced (2, 3));

    cancelButton.setBounds (buttons.removeFromRight (110).reduced (2, 3));
    area.removeFromBottom (8);

    if (studentEditor.isVisible())
    {
        auto row = area.removeFromBottom (38).reduced (0, 4);
        studentLabel.setBounds (row.removeFromLeft (70));
        row.removeFromLeft (8);
        studentEditor.setBounds (row);
        area.removeFromBottom (4);
    }

    summaryLabel.setBounds (area.removeFromTop (juce::jmax (48, 22 * (summaryLabel.getText().length() / 60 + 1 + summaryLabel.getText().containsChar ('\n')))));
    area.removeFromTop (6);

    if (missingViewport.isVisible())
    {
        constexpr int rowHeight = 34;
        const auto listHeight = juce::jmin (4, missingLabels.size()) * rowHeight;
        auto missingArea = area.removeFromBottom (listHeight + 30);
        missingHeader.setBounds (missingArea.removeFromTop (28));
        missingViewport.setBounds (missingArea);

        const auto width = juce::jmax (1, missingViewport.getMaximumVisibleWidth());
        missingHolder.setSize (width, juce::jmax (1, missingLabels.size() * rowHeight));

        for (int i = 0; i < missingLabels.size(); ++i)
        {
            auto row = juce::Rectangle<int> (0, i * rowHeight, width, rowHeight);
            locateButtons[i]->setBounds (row.removeFromRight (110).reduced (2, 3));
            missingLabels[i]->setBounds (row);
        }

        area.removeFromBottom (6);
    }

    details.setBounds (area);
}

} // namespace pv
