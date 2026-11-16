#include "MainComponent.h"
#include "LibraryPanel.h"
#include "LinksPanel.h"
#include "LooperPanel.h"
#include "PracticePanel.h"
#include "PracticeViews.h"
#include "RigPanel.h"
#include "TodayPanel.h"
#include "TunerPanel.h"
#include "YouTubePlayer.h"
#include "Theme.h"
#include "WebView.h"
#include "AppPaths.h"
#include <AppIconData.h>

namespace pv
{

//==============================================================================
/** The "Log" tab: everything the app writes to its log, plus a copy button
    (handy on the iPad, where there's no debugger).
*/
class MainComponent::LogPanel final : public juce::Component,
                                      private juce::ChangeListener,
                                      private juce::Timer
{
public:
    explicit LogPanel (AppLog& l) : appLog (l)
    {
        logView.setMultiLine (true);
        logView.setReadOnly (true);
        logView.setScrollbarsShown (true);
        logView.setCaretVisible (false);
        logView.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        logView.setColour (juce::TextEditor::backgroundColourId, theme::panel);
        addAndMakeVisible (logView);

        pathLabel.setText ("Log file: " + appLog.getFile().getFullPathName(), juce::dontSendNotification);
        pathLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        pathLabel.setMinimumHorizontalScale (0.5f);
        addAndMakeVisible (pathLabel);

        copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (appLog.getText()); };
        addAndMakeVisible (copyButton);

        appLog.addChangeListener (this);
        startTimerHz (10);
    }

    ~LogPanel() override
    {
        appLog.removeChangeListener (this);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::background); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 8);
        auto bottom = area.removeFromBottom (44);
        copyButton.setBounds (bottom.removeFromRight (160).reduced (3));
        pathLabel.setBounds (bottom);
        logView.setBounds (area);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { dirty = true; }

    void timerCallback() override
    {
        if (dirty && isShowing())
        {
            dirty = false;
            logView.setText (appLog.getText(), false);
            logView.moveCaretToEnd();
        }
    }

    AppLog& appLog;
    juce::TextEditor logView;
    juce::Label pathLabel;
    juce::TextButton copyButton { "Copy log" };
    bool dirty = true;
};

//==============================================================================
/** Full-window panel used for plugin UIs and the audio settings. */
class MainComponent::Overlay final : public juce::Component,
                                     private juce::ComponentListener
{
public:
    Overlay()
    {
        title.setFont (juce::FontOptions (20.0f).withStyle ("Bold"));
        title.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (title);
        addAndMakeVisible (closeButton);
        closeButton.onClick = [this] { if (onClose) onClose(); };
    }

    ~Overlay() override { hide(); }

    void show (const juce::String& titleText, std::unique_ptr<juce::Component> newContent)
    {
        hide();
        content = std::move (newContent);
        title.setText (titleText, juce::dontSendNotification);
        addAndMakeVisible (*content);
        content->addComponentListener (this);
        setVisible (true);
        toFront (true);
        resized();
    }

    void hide()
    {
        if (content != nullptr)
            content->removeComponentListener (this);

        content.reset();
        setVisible (false);
    }

    std::function<void()> onClose;

    juce::Component* getContent() const noexcept    { return content.get(); }

    void paint (juce::Graphics& g) override
    {
        const auto opaque = content != nullptr && (bool) content->getProperties()["fillOverlay"];
        g.fillAll (juce::Colours::black.withAlpha (opaque ? 1.0f : 0.88f));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        auto header = area.removeFromTop (44);
        closeButton.setBounds (header.removeFromRight (110).reduced (2));
        title.setBounds (header);
        area.removeFromTop (8);
        contentArea = area;
        layoutContent();
    }

private:
    void layoutContent()
    {
        if (content == nullptr || laying)
            return;

        const juce::ScopedValueSetter<bool> svs (laying, true);

        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (content.get()))
        {
            if (editor->isResizable())
                editor->setBounds (contentArea);
            else
                editor->setCentrePosition (contentArea.getCentre());
        }
        else if (content->getProperties()["fillOverlay"])
        {
            content->setBounds (contentArea);   // document / web viewer
        }
        else
        {
            content->setBounds (contentArea.withSizeKeepingCentre (juce::jmin (720, contentArea.getWidth()),
                                                                   juce::jmin (640, contentArea.getHeight())));
        }
    }

    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
    {
        if (wasResized)
            layoutContent();   // a plugin UI resized itself: re-centre it
    }

    juce::Label title;
    juce::TextButton closeButton { "Close" };
    std::unique_ptr<juce::Component> content;
    juce::Rectangle<int> contentArea;
    bool laying = false;
};

//==============================================================================
MainComponent::MainComponent (AppEngine& e, AppLog& appLog)
    : engine (e)
{
    titleLabel.setText (JUCE_APPLICATION_NAME_STRING, juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f).withStyle ("Bold"));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    // Scaled down once (high-quality resampling) to 4x the drawn size: sharp on every display density.
    {
        auto full = juce::ImageCache::getFromMemory (AppIconData::AppIcondesktop1024_png, AppIconData::AppIcondesktop1024_pngSize);
        const int px = iconSize * 4;
        if (full.isValid())
            appIcon = full.rescaled (px, px, juce::Graphics::highResamplingQuality);
    }

    for (auto* b : { &recordButton, &tunerButton, &audioSettingsButton, &bluetoothButton, &saveButton, &practisingButton })
        addAndMakeVisible (*b);

    practisingButton.setTooltip ("What you're practising: played time is logged to this topic (see the Today tab)");
    practisingButton.onClick = [this] { showPractisingMenu(); };

    audioSettingsButton.onClick = [this] { openAudioSettings(); };
    tunerButton.onClick = [this] { toggleTuner(); };
    tunerButton.setTooltip ("Chromatic tuner (a footswitch can open and close it too)");
    engine.onToggleTuner = [this] { toggleTuner(); };
    recordButton.onClick = [this] { toggleRecording(); };
    recordButton.setTooltip ("Record yourself (the guitar after the rig) into the topic you're practising. "
                             "Open the take later to hear it over the song, in step. A footswitch can do this too.");
    engine.onToggleRecording = [this] { toggleRecording(); };
    engine.onToggleGuitarStem = [this]
    {
        if (practicePanel != nullptr)
            juce::Logger::writeToLog ("Footswitch: " + practicePanel->toggleGuitarStem());
    };
    engine.onSaveState = [this]
    {
        if (libraryPanel != nullptr)
            libraryPanel->flush();

        tracker.flush();
    };

    bluetoothButton.onClick = []
    {
        if (juce::BluetoothMidiDevicePairingDialogue::isAvailable())
            juce::BluetoothMidiDevicePairingDialogue::open();
        else
            juce::Logger::writeToLog ("Bluetooth MIDI pairing is done by the operating system here: "
                                      "pair the footswitch in Windows Settings > Bluetooth & devices.");
    };

    saveButton.onClick = [this] { engine.saveState(); };

    todayPanel = std::make_unique<TodayPanel> (tracker, session);
    practicePanel = std::make_unique<PracticePanel> (*engine.player, *engine.metronome);
    libraryPanel = std::make_unique<LibraryPanel>();
    linksPanel = std::make_unique<LinksPanel>();
    looperPanel = std::make_unique<LooperPanel> (engine);
    rigPanel = std::make_unique<RigPanel> (engine);
    logPanel = std::make_unique<LogPanel> (appLog);

    rigPanel->showOverlay = [this] (const juce::String& title, std::unique_ptr<juce::Component> c) { showOverlay (title, std::move (c)); };
    rigPanel->hideOverlay = [this] { closeOverlay(); };
    looperPanel->showOverlay = [this] (const juce::String& title, std::unique_ptr<juce::Component> c) { showOverlay (title, std::move (c)); };
    looperPanel->hideOverlay = [this] { closeOverlay(); };
    libraryPanel->showOverlay = [this] (const juce::String& title, std::unique_ptr<juce::Component> c) { showOverlay (title, std::move (c)); };
    libraryPanel->hideOverlay = [this] { closeOverlay(); };
    libraryPanel->onOpenAudio = [this] (const juce::File& file) { openAudioFile (file); };
    libraryPanel->onOpenYouTube = [this] (const YouTubeLink& link, const juce::String& title) { openYouTube (link, title); };
    linksPanel->showOverlay = [this] (const juce::String& title, std::unique_ptr<juce::Component> c) { showOverlay (title, std::move (c)); };
    linksPanel->hideOverlay = [this] { closeOverlay(); };
    linksPanel->onOpenYouTube = [this] (const YouTubeLink& link, const juce::String& title) { openYouTube (link, title); };
    linksPanel->onChooseLibrary = [this] { showTab (libraryPanel.get()); };

    // Practice log: opening something from a topic makes it the topic you're practising.
    libraryPanel->onLibraryOpened = [this] { libraryOpened(); };
    libraryPanel->onTopicsChanged = [this] { todayPanel->refresh(); updatePractisingButton(); };
    libraryPanel->onPractise = [this] (const juce::String& id) { practiseTopic (id); };
    libraryPanel->onShowHistory = [this] (const juce::String& id) { showTopicHistory (id); };
    libraryPanel->onTopicOpened = [this] (const juce::String& id)
    {
        if (auto* lib = libraryPanel->getLibrary())
            if (auto* t = lib->findTopic (id))
                tracker.setActiveTopic (t->id, t->title);

        updatePractisingButton();
    };

    todayPanel->getLibrary = [this] { return libraryPanel->getLibrary(); };
    todayPanel->onPractise = [this] (const juce::String& id) { practiseTopic (id); };
    todayPanel->onShowHistory = [this] (const juce::String& id) { showTopicHistory (id); };
    todayPanel->onEditEntry = [this] (const juce::String& id) { editLogEntry (id, {}); };
    todayPanel->onAddTime = [this] (const juce::String& id) { addLogTime (id, {}); };
    practicePanel->onSongPicked = [this] (const juce::URL& url) { offerAddToInbox (url); };

    // Ear training, theory...: those topics are timed on the clock instead of from the guitar input.
    tracker.isNoGuitarTopic = [this] (const juce::String& id)
    {
        auto* lib = libraryPanel->getLibrary();
        auto* t = lib != nullptr ? lib->findTopic (id) : nullptr;
        return t != nullptr && t->noGuitar;
    };
    todayPanel->getCandidates = [this] (const juce::String& tag) { return rotationCandidates (tag); };
    todayPanel->getRotationTags = [this] { return rotationTags(); };
    todayPanel->onPlanSession = [this] (const juce::String& tag) { openSessionPlanner (tag); };
    todayPanel->getSavedPlans = [this]
    {
        std::vector<std::pair<juce::String, juce::String>> result;

        if (auto* plans = savedPlans())
            for (auto& p : plans->getPlans())
                result.push_back ({ p.id, p.name });

        return result;
    };
    todayPanel->onStartPlan = [this] (const juce::String& id) { startSavedPlan (id); };
    todayPanel->onEditPlan = [this] (const juce::String& id) { openSessionPlanner ({}, id); };
    todayPanel->onSessionNext = [this] { sessionNext(); };
    todayPanel->onSessionExtend = [this] { sessionExtend(); };
    todayPanel->onSessionEnd = [this] { sessionEnd(); };
    todayPanel->onSessionDismiss = [this] { session.dismiss(); updatePractisingButton(); };
    todayPanel->onShowTopic = [this] (const juce::String& id)
    {
        libraryPanel->selectTopic (id);
        showTab (libraryPanel.get());
    };
    todayPanel->onBecameVisible = [this]
    {
        libraryPanel->checkForChanges();

        if (practiceLog != nullptr)
            practiceLog->reload();
    };

    tracker.captureSetup = [this]
    {
        auto s = practicePanel->captureSetup();

        if (s.songPath.isNotEmpty())
            s.songPath = PracticeSetup::portablePath (juce::File (s.songPath),
                                                      libraryPanel->getLibrary() != nullptr ? libraryPanel->getLibrary()->getRoot() : juce::File());
        return s;
    };
    tracker.onSaved = [this] { if (todayPanel->isShowing()) todayPanel->refresh(); };

    tabs.setTabBarDepth (42);
    tabs.setOutline (0);
    tabs.addTab ("Today", theme::background, todayPanel.get(), false);
    tabs.addTab ("Practice", theme::background, practicePanel.get(), false);
    tabs.addTab ("Topics", theme::background, libraryPanel.get(), false);
    tabs.addTab ("Links", theme::background, linksPanel.get(), false);
    tabs.addTab ("Looper", theme::background, looperPanel.get(), false);
    tabs.addTab ("Guitar rig", theme::background, rigPanel.get(), false);
    tabs.addTab ("Log", theme::background, logPanel.get(), false);
    addAndMakeVisible (tabs);

    youtube = std::make_unique<YouTubePlayer>();
    addChildComponent (*youtube);
    practicePanel->setYouTubePlayer (youtube.get());
    practicePanel->onCompactRequested = [this]
    {
        youTubeAutoCompact = false;
        youtube->setCompact (true);
        youtube->setBounds (lastCompactBounds.isEmpty() ? defaultCompactBounds() : lastCompactBounds);
        layoutYouTube();
    };
    youtube->onExpand = [this]
    {
        lastCompactBounds = youtube->getBounds();
        youtube->setCompact (false);
        youTubeAutoCompact = false;
        closeOverlay();
        showTab (practicePanel.get());
        layoutYouTube();
    };
    youtube->onClose = [this]
    {
        practicePanel->closeYouTube();
        layoutYouTube();
    };

    overlay = std::make_unique<Overlay>();
    overlay->onClose = [this] { closeOverlay(); };
    addChildComponent (*overlay);

    libraryOpened();   // the Topics tab opened the saved library folder before the callbacks were set
    tabs.setCurrentTabIndex (0);
    startTimerHz (10);
}

MainComponent::~MainComponent()
{
    stopTimer();

    if (engine.recorder->isRecording())
        stopRecording (true);   // keep what was recorded

    engine.onToggleRecording = nullptr;
    engine.onToggleGuitarStem = nullptr;
    tracker.finish();
    tracker.onSaved = nullptr;
    tracker.captureSetup = nullptr;
    libraryPanel->onLibraryOpened = nullptr;
    libraryPanel->onTopicsChanged = nullptr;
    libraryPanel->onTopicOpened = nullptr;
    engine.onToggleTuner = nullptr;
    engine.onSaveState = nullptr;
    closeOverlay();
    tabs.clearTabs();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    if (appIcon.isValid() && ! iconBounds.isEmpty())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (appIcon, iconBounds.toFloat(), juce::RectanglePlacement::centred);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

   #if JUCE_IOS
    if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        area = display->safeAreaInsets.subtractedFrom (area);
   #endif

    overlay->setBounds (area);

    auto top = area.removeFromTop (52).reduced (12, 4);
    saveButton.setBounds (top.removeFromRight (100).reduced (4));
    bluetoothButton.setBounds (top.removeFromRight (150).reduced (4));
    audioSettingsButton.setBounds (top.removeFromRight (150).reduced (4));
    tunerButton.setBounds (top.removeFromRight (90).reduced (4));
    recordButton.setBounds (top.removeFromRight (juce::jlimit (90, 140, top.getWidth() / 6)).reduced (4));
    practisingButton.setBounds (top.removeFromRight (juce::jlimit (150, 320, top.getWidth() - 180)).reduced (4));
    iconBounds = top.removeFromLeft (iconSize + 6).withSizeKeepingCentre (iconSize, iconSize);
    titleLabel.setBounds (top);

    tabs.setBounds (area);
    layoutYouTube();
}

//==============================================================================
void MainComponent::timerCallback()
{
    layoutYouTube();
    tickTracker();
    updateRecordButton();
}

void MainComponent::showTab (juce::Component* content)
{
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (tabs.getTabContentComponent (i) == content)
            tabs.setCurrentTabIndex (i);
}

bool MainComponent::isTabShowing (juce::Component* content) const
{
    return tabs.getCurrentContentComponent() == content;
}

void MainComponent::openYouTube (const YouTubeLink& link, const juce::String& title)
{
    youtube->load (link, title);
    youtube->setCompact (false);
    youTubeAutoCompact = false;
    practicePanel->showYouTube();
    showTab (practicePanel.get());
    layoutYouTube();
}

//==============================================================================
void MainComponent::libraryOpened()
{
    tracker.setLog (nullptr);
    practiceLog.reset();
    tracker.setActiveTopic ({}, {});   // topic ids belong to the old library
    linksPanel->setLibraryRoot (libraryPanel->getLibrary() != nullptr ? libraryPanel->getLibrary()->getRoot() : juce::File());

    if (auto* lib = libraryPanel->getLibrary())
    {
        practiceLog = std::make_unique<PracticeLog> (lib->getRoot().getChildFile (".practice"), PracticeLog::getThisDeviceId());
        tracker.setLog (practiceLog.get());
    }

    todayPanel->refresh();
    updatePractisingButton();
}

void MainComponent::practiseTopic (const juce::String& topicId)
{
    auto* lib = libraryPanel->getLibrary();
    auto* topic = lib != nullptr ? lib->findTopic (topicId) : nullptr;

    if (topic == nullptr)
        return;

    tracker.setActiveTopic (topic->id, topic->title);
    updatePractisingButton();
    closeOverlay();

    // Bring back what was on screen the last time (on any device).
    if (const auto* last = practiceLog != nullptr ? practiceLog->latestSetupFor (topicId) : nullptr)
    {
        const auto setup = last->setup;

        if (setup.youTubeId.isNotEmpty() && webViewAvailable())
        {
            YouTubeLink link;
            link.videoId = setup.youTubeId;
            link.startSeconds = setup.loopOn && setup.loopEnd > setup.loopStart ? setup.loopStart : 0.0;
            openYouTube (link, setup.youTubeTitle);
            practicePanel->applySetup (setup);
            return;
        }

        if (setup.songPath.isNotEmpty())
        {
            const auto file = PracticeSetup::resolvePath (setup.songPath, lib->getRoot());

            if (file.existsAsFile())
            {
                lastSongFile = file;   // restoring: don't switch topic because of where the song lives

                if (practicePanel->getSongFile() == file && ! practicePanel->isYouTubeMode())
                    practicePanel->applySetup (setup);
                else
                    practicePanel->loadSong (juce::URL (file), setup);

                showTab (practicePanel.get());
                return;
            }
        }

        if (setup.metronome)
        {
            practicePanel->applySetup (setup);
            showTab (practicePanel.get());
            return;
        }
    }

    // First time: open its first song or video, or else show the topic itself.
    if (! libraryPanel->openFirstMedia (topicId))
    {
        libraryPanel->selectTopic (topicId);
        showTab (libraryPanel.get());
    }
}

void MainComponent::tickTracker()
{
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const auto seconds = lastTrackerTick > 0.0 ? (nowMs - lastTrackerTick) / 1000.0 : 0.0;
    lastTrackerTick = nowMs;

    // A song from a topic's folder (opened from anywhere) makes that topic the one you're practising.
    if (const auto songFile = practicePanel->getSongFile(); songFile != lastSongFile)
    {
        lastSongFile = songFile;

        if (auto* lib = libraryPanel->getLibrary(); lib != nullptr && songFile != juce::File())
            if (auto* topic = lib->findTopicForFile (songFile))
                tracker.setActiveTopic (topic->id, topic->title);
    }

    PracticeTracker::Activity activity;
    activity.songPlaying = practicePanel->isMediaPlaying();
    activity.speed = practicePanel->getMediaSpeed();
    activity.metronomeRunning = engine.metronome->isRunning();
    activity.bpm = engine.metronome->getCurrentBpm();
    activity.looperActive = ! engine.looper->isEmpty();
    activity.songName = practicePanel->getMediaName();

    tracker.tick (seconds, engine.chain->getInputStage().takeActivityPeak(), activity, juce::Time::getCurrentTime());

    // The slot's clock counts only time spent playing. Time's up: a soft chime (no forced move: you go on when ready).
    if (session.tick (tracker.getLastTickPlayedSeconds()))
    {
        engine.metronome->requestChime();
        updatePractisingButton();
    }

    if (--practisingButtonCountdown <= 0)
        updatePractisingButton();
}

void MainComponent::updatePractisingButton()
{
    practisingButtonCountdown = 10;   // once a second
    const auto active = tracker.getActiveTopicId().isNotEmpty();
    auto text = active ? tracker.getActiveTopicTitle() : juce::String ("Free practice");

    if (active && tracker.getActiveSeconds() >= 30.0)
        text << "  -  " << formatPracticeDuration (tracker.getActiveSeconds());

    if (const auto* slot = session.getCurrent())
    {
        const auto n = (int) session.getSlots().size();
        text = juce::String (session.getIndex() + 1) + "/" + juce::String (n) + "  " + slot->title + "  -  "
             + formatClock (juce::jmax (0.0, session.getRemainingSeconds()))
             + (session.isPaused() ? (tracker.clockMode() ? "  (paused: Resume on the Today tab)" : "  (paused: play to continue)") : "");

        if (session.isTimeUp())
            text = session.getNext() != nullptr ? "Time's up - next: " + session.getNext()->title
                                                : juce::String ("Time's up - finish the session");
    }

    juce::String prefix;

    if (tracker.clockMode())
        prefix = tracker.isPaused() ? "(paused) " : "(timing) ";
    else if (tracker.isPlaying() && ! session.isTimeUp())
        prefix = "(playing) ";

    practisingButton.setButtonText (prefix + text);

    if (session.isRunning() && session.isTimeUp())
    {
        practisingButton.setColour (juce::TextButton::buttonColourId, theme::highlight);
        practisingButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    }
    else
    {
        practisingButton.removeColour (juce::TextButton::textColourOffId);

        if (active)
            practisingButton.setColour (juce::TextButton::buttonColourId, theme::accent);
        else
            practisingButton.removeColour (juce::TextButton::buttonColourId);
    }
}

void MainComponent::showPractisingMenu()
{
    juce::PopupMenu menu;

    if (session.isRunning())
    {
        if (const auto* next = session.getNext())
            menu.addItem ("Next topic: " + next->title, [this] { sessionNext(); });
        else
            menu.addItem ("Finish the session", [this] { sessionNext(); });

        menu.addItem ("5 more minutes", [this] { sessionExtend(); });
        menu.addItem ("End the session", [this] { sessionEnd(); });
        menu.addSeparator();
    }

    menu.addItem ("Open the Today tab", [this] { showTab (todayPanel.get()); });

    if (tracker.getActiveTopicId().isNotEmpty())
        menu.addItem ("Stop practising \"" + tracker.getActiveTopicTitle() + "\"", [this]
        {
            tracker.setActiveTopic ({}, {});
            updatePractisingButton();
            todayPanel->refresh();
        });

    if (auto* lib = libraryPanel->getLibrary())
    {
        juce::PopupMenu topics;

        for (auto& t : lib->getTopics())
            if (t.inRotation)
                topics.addItem (t.title, true, t.id == tracker.getActiveTopicId(), [this, id = t.id] { practiseTopic (id); });

        menu.addSeparator();

        if (topics.getNumItems() > 0)
        {
            menu.addSubMenu ("Practise a rotation topic", topics);

            if (! session.isRunning())
            {
                menu.addItem ("Plan a session...", [this] { openSessionPlanner ({}); });

                if (auto* plans = savedPlans(); plans != nullptr && ! plans->getPlans().empty())
                {
                    juce::PopupMenu planMenu;

                    for (auto& p : plans->getPlans())
                        planMenu.addItem (p.name, [this, id = p.id] { startSavedPlan (id); });

                    menu.addSubMenu ("Start a saved plan", planMenu);
                }
            }
        }
        else
        {
            menu.addItem ("No topics in the practice rotation yet", false, false, [] {});
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&practisingButton));
}

juce::Rectangle<int> MainComponent::defaultCompactBounds() const
{
    const auto w = YouTubePlayer::compactVideoWidth + 2;
    const auto h = YouTubePlayer::compactVideoHeight + YouTubePlayer::compactBarHeight + 1;
    return { getWidth() - w - 16, getHeight() - h - 16, w, h };
}

//==============================================================================
std::vector<PlanCandidate> MainComponent::rotationCandidates (const juce::String& tag)
{
    std::vector<PlanCandidate> result;

    if (auto* lib = libraryPanel->getLibrary())
    {
        const auto now = juce::Time::getCurrentTime();

        for (auto& t : lib->getTopics())
            if (t.inRotation && t.matchesTag (tag))
                result.push_back ({ t.id, t.title.isNotEmpty() ? t.title : juce::String ("(untitled)"),
                                    neglectWeight (practiceLog != nullptr ? practiceLog->summarise (t.id).last : juce::Time(), now) });
    }

    return result;
}

juce::StringArray MainComponent::rotationTags()
{
    juce::StringArray tags;

    if (auto* lib = libraryPanel->getLibrary())
    {
        for (auto& t : lib->getTopics())
        {
            if (! t.inRotation)
                continue;

            for (auto& tag : t.tags)
                if (! hasTag (tags, tag))
                    tags.add (tag);

            for (auto& r : t.resources)
                for (auto& tag : r.tags)
                    if (! hasTag (tags, tag))
                        tags.add (tag);
        }
    }

    tags.sortNatural();
    return tags;
}

PlanList* MainComponent::savedPlans()
{
    auto* lib = libraryPanel->getLibrary();

    if (lib == nullptr)
    {
        planList.reset();
        return nullptr;
    }

    const auto file = lib->getRoot().getChildFile (PlanList::folderName).getChildFile (PlanList::fileName);

    if (planList == nullptr || planList->getFile() != file)
        planList = std::make_unique<PlanList> (lib->getRoot());
    else
        planList->reloadIfChanged();

    return planList.get();
}

juce::String MainComponent::topicTitle (const juce::String& topicId)
{
    auto* lib = libraryPanel->getLibrary();
    auto* topic = lib != nullptr && topicId.isNotEmpty() ? lib->findTopic (topicId) : nullptr;

    if (topic == nullptr)
        return {};

    return topic->title.isNotEmpty() ? topic->title : juce::String ("(untitled)");
}

void MainComponent::openSessionPlanner (const juce::String& tag, const juce::String& planId)
{
    auto planner = std::make_unique<SessionPlanner> ([this] (const juce::String& t) { return rotationCandidates (t); },
                                                     [this] (const juce::String& id) { return topicTitle (id); },
                                                     savedPlans(), rotationTags(), planMinutes, tag, planId);
    planner->onCancel = [this]
    {
        juce::MessageManager::callAsync ([safe = SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
            {
                safe->closeOverlay();
                safe->todayPanel->refresh();   // plans may have been saved, renamed or deleted
            }
        });
    };
    planner->onStart = [this] (std::vector<SessionSlot> slots, int minutes, const juce::String&)
    {
        planMinutes = minutes;

        // Not from inside the planner's own button click: starting closes (deletes) the planner.
        juce::MessageManager::callAsync ([safe = SafePointer<MainComponent> (this), slots]
        {
            if (safe != nullptr)
                safe->startSession (slots);
        });
    };
    showOverlay ("Plan a practice session", std::move (planner));
}

void MainComponent::startSavedPlan (const juce::String& planId)
{
    auto* plans = savedPlans();
    const auto* plan = plans != nullptr ? plans->find (planId) : nullptr;

    if (plan == nullptr)
        return;

    juce::Random random;
    const auto all = slotsFromPlan (*plan, [this] (const juce::String& id) { return topicTitle (id); },
                                    [this] (const juce::String& t) { return rotationCandidates (t); }, random);
    auto playable = playableSlots (all);

    if (playable.empty() || playable.size() < all.size())
    {
        // Something is missing: show the plan so it can be fixed (or started without those slots).
        openSessionPlanner ({}, planId);
        return;
    }

    startSession (playable);
}

void MainComponent::startSession (std::vector<SessionSlot> slots)
{
    closeOverlay();

    if (slots.empty())
        return;

    session.start (slots);
    practiseTopic (slots.front().topicId);
    updatePractisingButton();
    todayPanel->refresh();
}

void MainComponent::sessionNext()
{
    if (session.next())
    {
        if (const auto* slot = session.getCurrent())
            practiseTopic (slot->topicId);
    }
    else
    {
        showTab (todayPanel.get());   // the session is over: the Today tab shows the summary
    }

    updatePractisingButton();
    todayPanel->refresh();
}

void MainComponent::sessionExtend()
{
    session.extend (5);
    updatePractisingButton();
}

void MainComponent::sessionEnd()
{
    session.stop();
    updatePractisingButton();
    todayPanel->refresh();
}

void MainComponent::showTopicHistory (const juce::String& topicId)
{
    auto* lib = libraryPanel->getLibrary();
    auto* topic = lib != nullptr ? lib->findTopic (topicId) : nullptr;

    if (topic == nullptr || practiceLog == nullptr)
        return;

    tracker.flush();   // include what you're playing right now

    std::vector<TakeRow> takes;

    for (auto& r : topic->resources)
    {
        if (! r.isTake)
            continue;

        TakeRow row;
        row.file = topic->getFile (r);
        row.when = row.file.getCreationTime();
        row.title = "guitar only";

        if (const auto info = TakeInfo::load (row.file))
        {
            if (info->recorded != juce::Time())
                row.when = info->recorded;

            row.seconds = info->lengthSeconds;

            if (info->songName.isNotEmpty() && info->timeline.usesSong())
                row.title = info->songName;
            else if (info->youTubeTitle.isNotEmpty())
                row.title = info->youTubeTitle + " (YouTube)";
        }

        takes.push_back (row);
    }

    auto history = std::make_unique<TopicHistory> (*practiceLog, topicId, std::move (takes));
    history->onOpenTake = [this] (const juce::File& f)
    {
        // Not from inside the history's own click: opening closes (deletes) the overlay.
        juce::MessageManager::callAsync ([safe = SafePointer<MainComponent> (this), f]
        {
            if (safe != nullptr)
            {
                safe->closeOverlay();
                safe->openAudioFile (f);
            }
        });
    };
    history->onEditEntry = [this, topicId] (const juce::String& entryId)
    {
        // Not from inside the history's own click: showing the editor deletes the history overlay.
        juce::MessageManager::callAsync ([safe = SafePointer<MainComponent> (this), entryId, topicId]
        {
            if (safe != nullptr)
                safe->editLogEntry (entryId, topicId);
        });
    };
    history->onAddTime = [this, topicId]
    {
        juce::MessageManager::callAsync ([safe = SafePointer<MainComponent> (this), topicId]
        {
            if (safe != nullptr)
                safe->addLogTime (topicId, topicId);
        });
    };
    showOverlay ("History: " + topic->title, std::move (history));
}

void MainComponent::editLogEntry (const juce::String& entryId, const juce::String& historyTopicId)
{
    if (practiceLog == nullptr)
        return;

    tracker.flush();
    const auto* entry = practiceLog->find (entryId);

    if (entry == nullptr)
        return;

    const auto* current = tracker.getCurrent();
    auto editor = std::make_unique<EntryEditor> (*entry, current != nullptr && current->id == entryId);

    auto done = [this, historyTopicId]
    {
        juce::MessageManager::callAsync ([safe = SafePointer<MainComponent> (this), historyTopicId]
        {
            if (safe == nullptr)
                return;

            safe->todayPanel->refresh();

            if (historyTopicId.isNotEmpty())
                safe->showTopicHistory (historyTopicId);
            else
                safe->closeOverlay();
        });
    };

    editor->onCancel = done;
    editor->onSave = [this, entryId, done] (juce::Time start, double played, const juce::String& note)
    {
        if (practiceLog != nullptr)
            practiceLog->editEntry (entryId, start, played, note);

        juce::Logger::writeToLog ("Practice log: entry corrected (" + formatPracticeDuration (played) + ")");
        done();
    };
    editor->onDelete = [this, entryId, done]
    {
        if (practiceLog != nullptr)
            practiceLog->deleteEntry (entryId);

        juce::Logger::writeToLog ("Practice log: entry deleted");
        done();
    };

    showOverlay ("Practice entry", std::move (editor));
}

void MainComponent::offerAddToInbox (const juce::URL& song)
{
    auto* lib = libraryPanel->getLibrary();

    if (lib == nullptr)
        return;

    const auto original = song.isLocalFile() ? song.getLocalFile() : juce::File();

    if (original != juce::File() && original.isAChildOf (lib->getRoot()))
        return;   // already in the library

    const auto key = song.toString (false);

    if (inboxDeclined.contains (key))
        return;

    const auto name = original != juce::File() ? original.getFileName() : juce::URL::removeEscapeChars (song.getFileName());
    auto options = juce::MessageBoxOptions{}
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Add to your library?")
                       .withMessage ("\"" + name + "\" isn't in your library folder.\n\n"
                                     "Add a copy to the Topics Inbox? It then syncs to your other devices, and its stems "
                                     "are kept next to it (stems you already made for it move along).")
                       .withButton ("Add to Inbox", 1)
                       .withButton ("Not now", 0)
                       .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [safe = SafePointer<MainComponent> (this), song, original, key, name] (int result)
    {
        if (safe == nullptr)
            return;

        if (result != 1)
        {
            safe->inboxDeclined.add (key);
            return;
        }

        auto* library = safe->libraryPanel->getLibrary();

        if (library == nullptr)
            return;

        if (safe->practicePanel->isSeparatingStems())
            juce::Logger::writeToLog ("Stems are still being made: they stay in the app's data folder for the original file");

        const auto copy = library->addToInbox (song);

        if (copy == juce::File())
        {
            juce::Logger::writeToLog ("Could not copy " + name + " into the library");
            safe->practicePanel->showNote ("Could not copy the song into the library (see the Log tab)");
            return;
        }

        juce::Logger::writeToLog ("Song added to the Inbox: " + copy.getFullPathName());

        // The loaded song now is the copy (unless another song was opened meanwhile).
        if (safe->practicePanel->getSongFile() == original)
            safe->practicePanel->songCopiedTo (copy);

        safe->practicePanel->showNote ("Added to the Inbox: move it into a topic from the Topics tab");
        safe->libraryPanel->checkForChanges();
    });
}

void MainComponent::addLogTime (const juce::String& topicId, const juce::String& historyTopicId)
{
    auto* lib = libraryPanel->getLibrary();

    if (practiceLog == nullptr || lib == nullptr)
        return;

    PracticeEntry defaults;
    const auto now = juce::Time::getCurrentTime();
    defaults.start = juce::Time (now.getYear(), now.getMonth(), now.getDayOfMonth(), now.getHours(), now.getMinutes(), 0, 0, true);
    std::vector<std::pair<juce::String, juce::String>> choices;

    if (topicId.isNotEmpty())
    {
        if (auto* t = lib->findTopic (topicId))
        {
            defaults.topicId = t->id;
            defaults.topicTitle = t->title;
        }
    }
    else
    {
        defaults.topicId = tracker.getActiveTopicId();
        choices.push_back ({ {}, "Free practice" });

        std::vector<std::pair<juce::String, juce::String>> topics;

        for (auto& t : lib->getTopics())
            topics.push_back ({ t.id, t.title.isNotEmpty() ? t.title : juce::String ("(untitled)") });

        std::sort (topics.begin(), topics.end(), [] (const auto& a, const auto& b) { return a.second.compareNatural (b.second) < 0; });
        choices.insert (choices.end(), topics.begin(), topics.end());
    }

    auto editor = std::make_unique<EntryEditor> (EntryEditor::NewEntry {}, defaults, std::move (choices));
    auto* editorPtr = editor.get();

    auto done = [this, historyTopicId]
    {
        juce::MessageManager::callAsync ([safe = SafePointer<MainComponent> (this), historyTopicId]
        {
            if (safe == nullptr)
                return;

            safe->todayPanel->refresh();

            if (historyTopicId.isNotEmpty())
                safe->showTopicHistory (historyTopicId);
            else
                safe->closeOverlay();
        });
    };

    editor->onCancel = done;
    editor->onSave = [this, editorPtr, done] (juce::Time start, double played, const juce::String& note)
    {
        PracticeEntry e;
        e.id = juce::Uuid().toDashedString();
        e.topicId = editorPtr->getTopicId();
        e.topicTitle = e.topicId.isNotEmpty() ? editorPtr->getTopicTitle() : juce::String();
        e.start = start;
        e.end = start + juce::RelativeTime::seconds (played);
        e.playedSeconds = played;
        e.note = note;
        e.manual = true;

        if (auto* lib = libraryPanel->getLibrary())
            if (auto* t = lib->findTopic (e.topicId))
                e.noGuitar = t->noGuitar;

        if (practiceLog != nullptr)
            practiceLog->save (e);

        juce::Logger::writeToLog ("Practice log: " + formatPracticeDuration (played) + " added by hand"
                                  + (e.topicTitle.isNotEmpty() ? " to " + e.topicTitle : juce::String()));
        done();
    };

    showOverlay ("Add practice time", std::move (editor));
}

void MainComponent::layoutYouTube()
{
    if (youtube == nullptr || overlay == nullptr || practicePanel == nullptr)
        return;

    if (! youtube->hasVideo() || ! practicePanel->isYouTubeMode())
    {
        youtube->setVisible (false);
        return;
    }

    const auto practiceShowing = isTabShowing (practicePanel.get()) && ! overlay->isVisible();
    auto compact = youtube->isCompact();

    // Leaving the Practice tab while the video plays: keep it going in the floating player.
    if (! compact && ! practiceShowing && youtube->getState().playing)
    {
        compact = true;
        youTubeAutoCompact = true;
    }

    // ...and dock it again when coming back, unless you asked for the compact player yourself.
    if (compact && youTubeAutoCompact && practiceShowing)
    {
        compact = false;
        youTubeAutoCompact = false;
    }

    if (compact != youtube->isCompact())
    {
        if (! compact)
            lastCompactBounds = youtube->getBounds();

        youtube->setCompact (compact);

        if (compact)
            youtube->setBounds (lastCompactBounds.isEmpty() ? defaultCompactBounds() : lastCompactBounds);
    }

    if (compact)
    {
        // Keep it inside the window (after a resize, for instance).
        auto b = youtube->getBounds();
        b = b.constrainedWithin (getLocalBounds());

        if (b != youtube->getBounds())
            youtube->setBounds (b);

        if (! youtube->isVisible())
        {
            youtube->setVisible (true);
            youtube->toFront (false);
        }
    }
    else if (practiceShowing)
    {
        const auto area = getLocalArea (practicePanel.get(), practicePanel->getVideoArea());

        if (youtube->getBounds() != area)
            youtube->setBounds (area);

        youtube->setVisible (! area.isEmpty());
    }
    else
    {
        youtube->setVisible (false);   // paused on another tab or under an overlay
    }
}

//==============================================================================
juce::File MainComponent::takeFolder() const
{
    // The topic you're practising, else the library's Inbox, else a Recordings folder next to the Loops.
    if (auto* lib = libraryPanel->getLibrary())
    {
        if (auto* topic = lib->findTopic (tracker.getActiveTopicId()))
            return topic->folder;

        return lib->getRoot();
    }

   #if JUCE_IOS
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Recordings");
   #else
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Practice Vibes").getChildFile ("Recordings");
   #endif
}

void MainComponent::toggleRecording()
{
    if (engine.recorder->isRecording())
        stopRecording();
    else
        startRecording();
}

void MainComponent::startRecording()
{
    auto* device = engine.deviceManager.getCurrentAudioDevice();

    if (device == nullptr || device->getActiveInputChannels().isZero())
    {
        recordMessage = "No audio input";
        recordMessageCountdown = 30;
        juce::Logger::writeToLog ("Can't record a take: no audio input is open (see Audio settings)");
        return;
    }

    recordingFolder = takeFolder();
    recordingFile = recordingFolder.getChildFile (TakeInfo::folderName)
                                   .getChildFile ("recording-" + juce::Uuid().toDashedString().substring (0, 8) + ".flac");

    // What you're playing along to (for the file name and for replaying in step later).
    recordingInfo = {};
    recordingInfo.recorded = juce::Time::getCurrentTime();
    recordingInfo.topicId = tracker.getActiveTopicId();

    if (practicePanel->isYouTubeMode())
    {
        const auto setup = practicePanel->captureSetup();
        recordingInfo.youTubeId = setup.youTubeId;
        recordingInfo.youTubeTitle = setup.youTubeTitle;
    }
    else if (practicePanel->getSongFile() != juce::File())
    {
        auto* lib = libraryPanel->getLibrary();
        recordingInfo.songPath = PracticeSetup::portablePath (practicePanel->getSongFile(), lib != nullptr ? lib->getRoot() : juce::File());
        recordingInfo.songName = practicePanel->getSongFile().getFileName();
    }

    juce::String error;

    if (! engine.recorder->start (recordingFile, device->getCurrentSampleRate(), engine.looper->getCompensationSamples(), error))
    {
        recordMessage = "Can't record";
        recordMessageCountdown = 30;
        juce::Logger::writeToLog ("Can't record a take: " + error);
        return;
    }

    juce::Logger::writeToLog ("Recording a take into " + recordingFolder.getFullPathName()
                              + " (latency compensation " + juce::String (engine.looper->getCompensationSamples()) + " samples)");
    updateRecordButton();
}

void MainComponent::stopRecording (bool waitForFile)
{
    auto result = engine.recorder->stop();

    if (result.file == juce::File())
        return;

    if (engine.recorder->hasDroppedAudio())
        juce::Logger::writeToLog ("Warning: the disk didn't keep up while recording - the take has gaps");

    // Very short takes are usually accidental presses: not worth a resource in the topic.
    constexpr double minimumTakeSeconds = 10.0;

    if (result.seconds < minimumTakeSeconds)
    {
        result.file.deleteFile();
        recordMessage = "Too short: < " + juce::String ((int) minimumTakeSeconds) + " s";
        recordMessageCountdown = 30;
        juce::Logger::writeToLog ("Take not saved: " + juce::String (result.seconds, 1) + " s is under the "
                                  + juce::String ((int) minimumTakeSeconds) + " s minimum");
        updateRecordButton();
        return;
    }

    recordingInfo.lengthSeconds = result.seconds;
    recordingInfo.timeline = std::move (result.timeline);

    // Only name a song the take actually played along to.
    const auto what = recordingInfo.timeline.usesSong() ? recordingInfo.songName : recordingInfo.youTubeTitle;
    const auto base = TakeInfo::makeFileName (recordingInfo.recorded, what);
    const auto folder = recordingFolder;
    auto uniqueFile = [folder, base] (const juce::String& extension)
    {
        auto f = folder.getChildFile (base + extension);

        for (int n = 2; f.exists(); ++n)
            f = folder.getChildFile (base + " (" + juce::String (n) + ")" + extension);

        return f;
    };

    // Recorded as FLAC (safe in real time), kept as a small AAC .m4a: encoding takes a moment, so off the UI thread
    // (except when the app is closing).
    auto finish = [recorded = result.file, target = uniqueFile (".m4a"), flacTarget = uniqueFile (".flac"), info = recordingInfo]
    {
        auto saved = target;
        auto converted = Recorder::convertToAac (recorded, target);

        if (converted.wasOk())
        {
            recorded.deleteFile();
        }
        else
        {
            target.deleteFile();
            saved = recorded.moveFileTo (flacTarget) ? flacTarget : recorded;   // keep it rather than lose it
        }

        info.save (saved);
        return std::make_pair (saved, converted);
    };

    auto report = [this, seconds = result.seconds, usesSong = recordingInfo.timeline.usesSong(),
                   songName = recordingInfo.songName] (const juce::File& saved, const juce::Result& converted)
    {
        if (converted.failed())
            juce::Logger::writeToLog ("Could not make an .m4a of the take (" + converted.getErrorMessage() + "): kept as FLAC");

        juce::Logger::writeToLog ("Saved take " + saved.getFileName() + " (" + juce::String (seconds, 1) + " s, "
                                  + juce::String (saved.getSize() / 1024) + " KB"
                                  + (usesSong ? ", in step with " + songName : juce::String()) + ") to "
                                  + saved.getParentDirectory().getFullPathName());
        recordMessage = "Take saved";
        recordMessageCountdown = 30;
        libraryPanel->checkForChanges();
        updateRecordButton();
    };

    if (waitForFile)
    {
        const auto [saved, converted] = finish();
        report (saved, converted);
        return;
    }

    recordMessage = "Saving...";
    recordMessageCountdown = 600;
    updateRecordButton();

    juce::Thread::launch ([finish, report, safe = SafePointer<MainComponent> (this)]
    {
        const auto [saved, converted] = finish();

        juce::MessageManager::callAsync ([safe, report, saved = saved, converted = converted]
        {
            if (safe != nullptr)
                report (saved, converted);
        });
    });
}

void MainComponent::updateRecordButton()
{
    juce::String text;

    if (engine.recorder->isRecording())
    {
        const auto seconds = juce::roundToInt (engine.recorder->getRecordedSeconds());
        text = juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 ")) + juce::String (seconds / 60) + ":"
             + juce::String (seconds % 60).paddedLeft ('0', 2);
        recordButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffc62828));
        recordButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    }
    else
    {
        recordButton.removeColour (juce::TextButton::buttonColourId);
        recordButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffff7070));   // a red "Rec"

        if (recordMessageCountdown > 0)
        {
            --recordMessageCountdown;
            text = recordMessage;
        }
        else
        {
            text = juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f Rec"));
        }
    }

    if (recordButton.getButtonText() != text)
        recordButton.setButtonText (text);
}

void MainComponent::openAudioFile (const juce::File& file)
{
    if (const auto info = TakeInfo::load (file))
    {
        if (info->timeline.usesSong() && info->songPath.isNotEmpty())
        {
            auto* lib = libraryPanel->getLibrary();
            const auto song = PracticeSetup::resolvePath (info->songPath, lib != nullptr ? lib->getRoot() : juce::File());

            if (song.existsAsFile())
            {
                practicePanel->openTake (song, file, *info);
                showTab (practicePanel.get());
                return;
            }

            juce::Logger::writeToLog ("The take's song (" + info->songPath + ") wasn't found: playing the take on its own");
        }
    }

    practicePanel->loadSong (juce::URL (file));
    showTab (practicePanel.get());
}

//==============================================================================
void MainComponent::openAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (engine.deviceManager,
                                                                          0, 8,    // inputs
                                                                          0, 8,    // outputs
                                                                          true,    // MIDI inputs
                                                                          false, false, false);
    showOverlay ("Audio settings", std::move (selector));
}

void MainComponent::toggleTuner()
{
    if (overlay->isVisible() && dynamic_cast<TunerPanel*> (overlay->getContent()) != nullptr)
        closeOverlay();
    else
        showOverlay ("Tuner", std::make_unique<TunerPanel> (engine));
}

void MainComponent::showOverlay (const juce::String& title, std::unique_ptr<juce::Component> content)
{
    overlay->show (title, std::move (content));
}

void MainComponent::closeOverlay()
{
    if (overlay != nullptr)
        overlay->hide();
}

} // namespace pv
