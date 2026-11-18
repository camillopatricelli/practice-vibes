#include "TodayPanel.h"
#include "Theme.h"
#include "Localisation.h"

namespace pv
{

namespace
{
    juce::Time startOfToday()
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::Time (now.getYear(), now.getMonth(), now.getDayOfMonth(), 0, 0, 0, 0, true);
    }

    juce::String progressText (const PracticeLog::TopicSummary& s)
    {
        juce::StringArray parts;

        if (s.sessions > 0)
            parts.add (tr ("{duration} in total", formatPracticeDuration (s.totalSeconds)));

        if (s.lastBpm > 0.0)
            parts.add (tr ("last {bpm} bpm", juce::roundToInt (s.lastBpm)));

        if (s.lastSpeed > 0.0)
            parts.add (tr ("song at {percent} %", juce::roundToInt (s.lastSpeed * 100.0)));

        return parts.joinIntoString ("  -  ");
    }
}

//==============================================================================
/** One rotation topic: title, when you last practised it, progress, and a Practise button. */
class TodayPanel::RotationRow final : public juce::Component,
                                      public juce::SettableTooltipClient
{
public:
    RotationRow (const Topic& topic, const PracticeLog::TopicSummary& s, bool isActive)
        : topicId (topic.id), title (topic.title.isNotEmpty() ? topic.title : juce::String ("(untitled)")),
          tags (topic.tags), summary (s), active (isActive)
    {
        practiseButton.setColour (juce::TextButton::buttonColourId, theme::accent());
        practiseButton.onClick = [this] { if (onPractise) onPractise (topicId); };
        practiseButton.setButtonText (active ? tr ("Practising") : tr ("Practise"));
        practiseButton.setEnabled (! active);
        addAndMakeVisible (practiseButton);
        historyButton.onClick = [this] { if (onHistory) onHistory (topicId); };
        historyButton.setTooltip (tr ("Tempo, song speed and minutes over the weeks, and every session"));
        historyButton.setEnabled (s.sessions > 0);
        addAndMakeVisible (historyButton);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (tr ("Click to open the topic; Practise brings back last time's song, loop, speed and tempo"));
    }

    std::function<void (const juce::String&)> onPractise, onShow, onHistory;
    juce::String assignmentText;    // the teacher's assignment and today's progress, if the topic has one

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel());
        g.fillRoundedRectangle (r, 6.0f);

        if (active)
        {
            g.setColour (theme::accent());
            g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 2.0f);
        }

        auto inner = getLocalBounds().reduced (12, 8);
        inner.removeFromRight (getWidth() - historyButton.getX() + 12);

        g.setColour (theme::text());
        g.setFont (juce::FontOptions (16.0f).withStyle ("Bold"));
        g.drawText (title, inner.removeFromTop (22), juce::Justification::centredLeft, true);

        const auto now = juce::Time::getCurrentTime();
        const auto days = summary.last == juce::Time() ? 1000.0 : (now - summary.last).inDays();
        const auto ago = summary.last == juce::Time() ? tr ("Not practised yet")
                                                      : tr ("Last practised {when}", formatPracticeAgo (summary.last, now));
        const juce::Font agoFont (juce::FontOptions (13.5f).withStyle ("Bold"));
        g.setFont (agoFont);
        g.setColour (days >= 7.0 ? theme::highlight() : theme::text().withAlpha (0.75f));
        const auto agoWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (agoFont, ago)) + 16;
        auto line = inner.removeFromTop (20);
        g.drawText (ago, line.removeFromLeft (agoWidth), juce::Justification::centredLeft, true);

        g.setFont (juce::FontOptions (13.5f));
        g.setColour (theme::text().withAlpha (0.6f));
        auto detail = progressText (summary);

        if (assignmentText.isNotEmpty())
        {
            g.setColour (theme::good());
            detail = assignmentText;
        }
        else if (! tags.isEmpty())
            detail << (detail.isNotEmpty() ? "     " : "") << "#" << tags.joinIntoString ("  #");

        g.drawText (detail, line, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        practiseButton.setBounds (r.removeFromRight (110).withSizeKeepingCentre (110, 34));
        r.removeFromRight (6);
        historyButton.setBounds (r.removeFromRight (84).withSizeKeepingCentre (84, 34));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onShow)
            onShow (topicId);
    }

private:
    juce::String topicId, title;
    juce::StringArray tags;
    PracticeLog::TopicSummary summary;
    bool active;
    juce::TextButton practiseButton, historyButton { tr ("History") };
};

//==============================================================================
/** The last two weeks, newest first, grouped by day. */
class TodayPanel::RecentView final : public juce::Component,
                                     public juce::SettableTooltipClient
{
public:
    void setEntries (const std::vector<PracticeEntry>& all)
    {
        lines.clear();
        const auto today = startOfToday();
        const auto from = today - juce::RelativeTime::days (13);
        juce::Time shownDay;

        for (auto it = all.rbegin(); it != all.rend(); ++it)
        {
            if (it->start < from)
                break;

            const auto day = juce::Time (it->start.getYear(), it->start.getMonth(), it->start.getDayOfMonth(), 0, 0, 0, 0, true);

            if (day != shownDay)
            {
                shownDay = day;
                double total = 0.0;

                for (auto& e : all)
                    if (e.start >= day && e.start < day + juce::RelativeTime::days (1))
                        total += e.playedSeconds;

                const auto name = day == today ? tr ("Today")
                                : day == today - juce::RelativeTime::days (1) ? tr ("Yesterday")
                                : i18n::date (day, "%a %d %b");
                lines.push_back ({ Line::day, name, formatPracticeDuration (total) });
            }

            auto what = it->isFree() ? tr ("Free practice") : it->topicTitle;

            if (it->isFree() && it->songName.isNotEmpty())
                what << ": " << it->songName;

            juce::StringArray detail { formatPracticeDuration (it->playedSeconds) };

            if (it->maxBpm > 0.0)    detail.add (juce::String (juce::roundToInt (it->maxBpm)) + " bpm");
            if (it->maxSpeed > 0.0)  detail.add (juce::String (juce::roundToInt (it->maxSpeed * 100.0)) + " %");
            if (it->looper)          detail.add (tr ("looper"));
            if (it->noGuitar)        detail.add (tr ("clock time"));
            if (it->manual)          detail.add (tr ("added by hand"));

            lines.push_back ({ Line::entry, what, detail.joinIntoString ("  -  "), it->id });

            if (it->note.isNotEmpty())
                lines.push_back ({ Line::note, it->note.replaceCharacters ("\r\n", "  "), {}, it->id });
        }

        setSize (getWidth(), getContentHeight());
        repaint();
    }

    int getContentHeight() const
    {
        int h = 4;

        for (auto& l : lines)
            h += heightOf (l);

        return juce::jmax (40, h);
    }

    void paint (juce::Graphics& g) override
    {
        if (lines.empty())
        {
            g.setColour (theme::text().withAlpha (0.45f));
            g.setFont (theme::textFont (15.5f));
            g.drawFittedText (tr ("Nothing yet. Play, and the time you spend on each topic is recorded here by itself."),
                              getLocalBounds().reduced (4), juce::Justification::topLeft, 3);
            return;
        }

        int y = 4;

        for (auto& l : lines)
        {
            const auto h = heightOf (l);
            auto r = juce::Rectangle<int> (0, y, getWidth(), h).reduced (4, 0);
            y += h;

            if (l.entryId.isNotEmpty() && l.entryId == hoverId)
            {
                g.setColour (theme::text().withAlpha (0.07f));
                g.fillRect (r);
            }

            switch (l.kind)
            {
                case Line::day:
                    r.removeFromTop (8);
                    g.setColour (theme::text().withAlpha (0.12f));
                    g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
                    g.setColour (theme::text());
                    g.setFont (theme::textFont (16.0f, "Bold"));
                    g.drawText (l.text, r, juce::Justification::centredLeft, true);
                    g.setColour (theme::text().withAlpha (0.7f));
                    g.drawText (l.detail, r, juce::Justification::centredRight, true);
                    break;

                case Line::entry:
                {
                    const juce::Font entryFont (theme::textFont (15.5f));
                    g.setFont (entryFont);
                    const auto detailWidth = juce::jmin (r.getWidth() / 2,
                                                         juce::roundToInt (juce::GlyphArrangement::getStringWidth (entryFont, l.detail)) + 8);
                    g.setColour (theme::text().withAlpha (0.6f));
                    g.drawText (l.detail, r.removeFromRight (detailWidth), juce::Justification::centredRight, true);
                    g.setColour (theme::text().withAlpha (0.9f));
                    g.drawText (l.text, r.withTrimmedLeft (10), juce::Justification::centredLeft, true);
                    break;
                }

                case Line::note:
                    g.setColour (theme::highlight().withAlpha (0.8f));
                    g.setFont (theme::textFont (14.5f, "Italic"));
                    g.drawText (l.text, r.withTrimmedLeft (22), juce::Justification::centredLeft, true);
                    break;
            }
        }
    }

    /** Tap an entry (or its note) to correct or delete it. */
    std::function<void (const juce::String& entryId)> onEdit;

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto id = idAt (e.y);
        setMouseCursor (id.isNotEmpty() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);

        if (id != hoverId)
        {
            hoverId = id;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override       { hoverId = {}; repaint(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto id = idAt (e.y);

        if (id.isNotEmpty() && e.mouseWasClicked() && onEdit)
            onEdit (id);
    }

private:
    struct Line
    {
        enum Kind { day, entry, note } kind;
        juce::String text, detail;
        juce::String entryId;
    };

    static int heightOf (const Line& l)     { return l.kind == Line::day ? 40 : (l.kind == Line::entry ? 30 : 24); }

    juce::String idAt (int y) const
    {
        int top = 4;

        for (auto& l : lines)
        {
            const auto h = heightOf (l);

            if (y >= top && y < top + h)
                return l.entryId;

            top += h;
        }

        return {};
    }

    std::vector<Line> lines;
    juce::String hoverId;
};

//==============================================================================
TodayPanel::TodayPanel (PracticeTracker& t, const PracticeSession& s) : tracker (t), session (s)
{
    titleLabel.setFont (theme::displayFont (22.0f, "SemiBold"));
    titleLabel.setColour (juce::Label::textColourId, theme::text());
    addAndMakeVisible (titleLabel);

    totalsLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.75f));
    totalsLabel.setJustificationType (juce::Justification::centredRight);
    totalsLabel.setFont (juce::FontOptions (15.0f));
    addAndMakeVisible (totalsLabel);

    nowTitle.setFont (theme::displayFont (19.0f, "SemiBold"));
    nowTitle.setColour (juce::Label::textColourId, theme::text());
    nowTitle.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (nowTitle);

    nowStatus.setColour (juce::Label::textColourId, theme::text().withAlpha (0.7f));
    nowStatus.setFont (juce::FontOptions (14.0f));
    nowStatus.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (nowStatus);

    noteEditor.setTextToShowWhenEmpty (tr ("Note for this session (optional): what went well, what to fix next time"),
                                       theme::text().withAlpha (0.35f));
    noteEditor.setFont (juce::FontOptions (14.5f));
    noteEditor.setColour (juce::TextEditor::backgroundColourId, theme::background());
    noteEditor.setColour (juce::TextEditor::outlineColourId, theme::text().withAlpha (0.15f));
    noteEditor.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent());
    noteEditor.setJustification (juce::Justification::centredLeft);
    noteEditor.onTextChange = [this] { tracker.setNote (noteEditor.getText().trim()); };
    noteEditor.onReturnKey = [this] { unfocusAllComponents(); };
    addAndMakeVisible (noteEditor);

    stopButton.setTooltip (tr ("Stop practising this topic (playing is then logged as free practice)"));
    stopButton.onClick = [this]
    {
        tracker.setActiveTopic ({}, {});
        refresh();
    };
    addAndMakeVisible (stopButton);

    pauseButton.setTooltip (tr ("This topic is timed without playing: pause the clock while you're away"));
    pauseButton.onClick = [this]
    {
        tracker.setPaused (! tracker.isPaused());
        updateNow();
    };
    addChildComponent (pauseButton);

    addTimeButton.setTooltip (tr ("Add practice time by hand (e.g. theory done away from the app)"));
    addTimeButton.onClick = [this] { if (onAddTime) onAddTime ({}); };
    addAndMakeVisible (addTimeButton);

    allHistoryButton.setTooltip (tr ("Your practice across all topics: minutes per week, time per topic and every session"));
    allHistoryButton.onClick = [this] { if (onShowAllHistory) onShowAllHistory(); };
    addAndMakeVisible (allHistoryButton);

    for (auto* l : { &rotationHeader, &recentHeader })
    {
        theme::styleHeader (*l);
        l->setColour (juce::Label::textColourId, theme::text().withAlpha (0.85f));
        addAndMakeVisible (*l);
    }

    rotationHeader.setText (tr ("Practice rotation"), juce::dontSendNotification);
    rotationHeader.setTooltip (tr ("Longest neglected first"));

    diceButton.setTooltip (tr ("Pick one rotation topic at random - the longer you've left one, the likelier it comes up"));
    diceButton.onClick = [this] { rollDice(); };
    planButton.setTooltip (tr ("Say how long you have: a few rotation topics with time slots, ready to start"));
    planButton.setColour (juce::TextButton::buttonColourId, theme::accent());
    planButton.onClick = [this] { if (onPlanSession) onPlanSession (focusTag()); };
    focusBox.setTooltip (tr ("Only topics with this tag, for the dice and the session planner"));
    focusBox.addItem (tr ("All tags"), 1);
    focusBox.setSelectedId (1, juce::dontSendNotification);

    plansButton.setTooltip (tr ("Your saved session plans: start one as it is, or load it to change today's session"));
    plansButton.onClick = [this] { showPlansMenu(); };

    for (auto* c : std::initializer_list<juce::Component*> { &diceButton, &planButton, &plansButton, &focusBox })
        addAndMakeVisible (*c);

    diceLabel.setFont (juce::FontOptions (16.0f).withStyle ("Bold"));
    diceLabel.setColour (juce::Label::textColourId, theme::onAccent());
    diceLabel.setMinimumHorizontalScale (0.7f);
    dicePractiseButton.onClick = [this]
    {
        const auto id = diceTopicId;
        diceTopicId = {};
        resized();

        if (onPractise)
            onPractise (id);
    };
    diceAgainButton.onClick = [this] { rollDice(); };
    diceCloseButton.onClick = [this] { diceTopicId = {}; resized(); repaint(); };

    for (auto* c : std::initializer_list<juce::Component*> { &diceLabel, &dicePractiseButton, &diceAgainButton, &diceCloseButton })
        addChildComponent (*c);

    sessionTitle.setFont (theme::displayFont (17.0f, "SemiBold"));
    sessionTitle.setColour (juce::Label::textColourId, theme::text());
    addChildComponent (sessionTitle);
    nextButton.setColour (juce::TextButton::buttonColourId, theme::accent());
    nextButton.onClick = [this] { if (onSessionNext) onSessionNext(); };
    extendButton.onClick = [this] { if (onSessionExtend) onSessionExtend(); };
    endButton.onClick = [this]
    {
        if (session.isRunning()) { if (onSessionEnd) onSessionEnd(); }
        else if (onSessionDismiss) onSessionDismiss();
    };

    for (auto* c : { &nextButton, &extendButton, &endButton })
        addChildComponent (*c);
    recentHeader.setText (tr ("Last two weeks"), juce::dontSendNotification);

    rotationEmpty.setJustificationType (juce::Justification::topLeft);
    rotationEmpty.setColour (juce::Label::textColourId, theme::text().withAlpha (0.55f));
    rotationEmpty.setFont (juce::FontOptions (15.0f));
    addChildComponent (rotationEmpty);

    rotationViewport.setViewedComponent (&rotationHolder, false);
    rotationViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (rotationViewport);

    recentView = std::make_unique<RecentView>();
    recentView->onEdit = [this] (const juce::String& id) { if (onEditEntry) onEditEntry (id); };
    recentView->setTooltip (tr ("Tap an entry to correct or delete it"));
    recentViewport.setViewedComponent (recentView.get(), false);
    recentViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (recentViewport);

    recentToggleButton.setTooltip (tr ("Show the last two weeks in place of the rotation list, or the other way round"));
    recentToggleButton.onClick = [this] { recentExpanded = ! recentExpanded; resized(); repaint(); };
    addChildComponent (recentToggleButton);

    startTimerHz (2);
}

TodayPanel::~TodayPanel()
{
    stopTimer();
}

//==============================================================================
void TodayPanel::refresh()
{
    auto* library = getLibrary ? getLibrary() : nullptr;
    auto* log = tracker.getLog();
    rows.clear();

    std::vector<std::pair<const Topic*, PracticeLog::TopicSummary>> rotation;

    if (library != nullptr)
        for (auto& t : library->getTopics())
            if (t.inRotation)
                rotation.emplace_back (&t, log != nullptr ? log->summarise (t.id) : PracticeLog::TopicSummary {});

    // Never practised first (newest topic first), then the longest ago.
    std::stable_sort (rotation.begin(), rotation.end(), [] (const auto& a, const auto& b)
    {
        const auto na = a.second.last == juce::Time(), nb = b.second.last == juce::Time();

        if (na != nb)
            return na;

        return na ? a.first->created > b.first->created : a.second.last < b.second.last;
    });

    for (auto& [topic, summary] : rotation)
    {
        auto row = std::make_unique<RotationRow> (*topic, summary, topic->id == tracker.getActiveTopicId());

        if (const auto& a = topic->assignment; ! a.isEmpty())
        {
            // "Teacher: 5 / 15 min today · target 96 bpm (last 88) · lesson Thu 20 Nov"
            juce::StringArray parts;

            if (a.minutesPerDay > 0)
            {
                const auto now = juce::Time::getCurrentTime();
                const juce::Time midnight (now.getYear(), now.getMonth(), now.getDayOfMonth(), 0, 0);
                double today = 0.0;

                if (log != nullptr)
                    for (auto* e : log->historyFor (topic->id))
                        if (e->start >= midnight)
                            today += e->playedSeconds;

                if (auto* current = tracker.getCurrent(); current != nullptr && current->topicId == topic->id && current->start >= midnight)
                    today += current->playedSeconds;

                parts.add (tr ("{done} / {goal} min today", juce::roundToInt (today / 60.0), a.minutesPerDay));
            }

            if (a.targetBpm > 0.0)
                parts.add (summary.lastBpm > 0.0 ? tr ("target {bpm} bpm (last {last})", juce::roundToInt (a.targetBpm), juce::roundToInt (summary.lastBpm))
                                                 : tr ("target {bpm} bpm", juce::roundToInt (a.targetBpm)));

            if (a.nextLesson != juce::Time())
                parts.add (tr ("lesson {date}", i18n::date (a.nextLesson, "%a %d %b")));

            if (parts.isEmpty())
                parts.add (a.goal);

            row->assignmentText = tr ("Teacher: {details}", parts.joinIntoString (middleDot()));
            row->setTooltip (tr ("Assignment: {details}", a.describe()));
        }
        row->onPractise = [this] (const juce::String& id) { if (onPractise) onPractise (id); };
        row->onShow = [this] (const juce::String& id) { if (onShowTopic) onShowTopic (id); };
        row->onHistory = [this] (const juce::String& id) { if (onShowHistory) onShowHistory (id); };
        rotationHolder.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    rotationEmpty.setText (library == nullptr
                               ? tr ("Set up your topics in the Topics tab first.")
                               : tr ("No topics in the rotation yet.\n\nOpen a topic in the Topics tab and tick \"In practice rotation\": "
                                     "it shows up here, with how long ago you last practised it."),
                           juce::dontSendNotification);
    rotationEmpty.setVisible (rows.empty());

    recentView->setEntries (log != nullptr ? log->getEntries() : std::vector<PracticeEntry> {});
    allHistoryButton.setEnabled (log != nullptr && ! log->getEntries().empty());
    shownActiveId = tracker.getActiveTopicId();

    // Focus tags: keep the choice if the tag still exists.
    const auto tags = getRotationTags ? getRotationTags() : juce::StringArray();
    const auto chosen = focusTag();
    focusBox.clear (juce::dontSendNotification);
    focusBox.addItem (tr ("All tags"), 1);

    for (int i = 0; i < tags.size(); ++i)
        focusBox.addItem ("#" + tags[i], i + 2);

    const auto index = tags.indexOf (chosen, true);
    focusBox.setSelectedId (index >= 0 ? index + 2 : 1, juce::dontSendNotification);

    diceButton.setEnabled (! rows.empty());
    planButton.setEnabled (! rows.empty());
    plansButton.setVisible (getSavedPlans && ! getSavedPlans().empty());

    if (diceTopicId.isNotEmpty() && (library == nullptr || library->findTopic (diceTopicId) == nullptr))
        diceTopicId = {};

    updateNow();
    updateSession();
    resized();
}

void TodayPanel::showPlansMenu()
{
    const auto plans = getSavedPlans ? getSavedPlans() : std::vector<std::pair<juce::String, juce::String>> {};
    juce::PopupMenu menu;

    for (auto& [id, name] : plans)
    {
        juce::PopupMenu sub;
        sub.addItem (tr ("Start"), ! session.isRunning(), false, [this, planId = id] { if (onStartPlan) onStartPlan (planId); });
        sub.addItem (tr ("Change for today..."), ! session.isRunning(), false, [this, planId = id] { if (onEditPlan) onEditPlan (planId); });
        menu.addSubMenu (name, sub);
    }

    if (session.isRunning())
    {
        menu.addSeparator();
        menu.addItem (tr ("A session is running - end it first"), false, false, [] {});
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&plansButton));
}

juce::String TodayPanel::focusTag() const
{
    return focusBox.getSelectedId() > 1 ? focusBox.getText().substring (1) : juce::String();
}

void TodayPanel::rollDice()
{
    auto candidates = getCandidates ? getCandidates (focusTag()) : std::vector<PlanCandidate> {};

    // Not what you're on now, nor the same pick twice in a row (when there's a choice).
    juce::StringArray exclude { tracker.getActiveTopicId(), diceTopicId };
    auto pick = pickWeighted (candidates, random, exclude);

    if (pick < 0)
        pick = pickWeighted (candidates, random);

    if (pick < 0)
    {
        diceTopicId = {};
        diceLabel.setText ({}, juce::dontSendNotification);
        resized();
        return;
    }

    const auto& c = candidates[(size_t) pick];
    diceTopicId = c.topicId;

    juce::String when = tr ("not practised yet");

    if (auto* log = tracker.getLog())
        if (const auto last = log->summarise (c.topicId).last; last != juce::Time())
            when = tr ("last practised {when}", formatPracticeAgo (last, juce::Time::getCurrentTime()));

    diceLabel.setText (tr ("The dice says: {title}   ({when})", c.title, when), juce::dontSendNotification);
    resized();
    repaint();
}

void TodayPanel::updateSession()
{
    const auto visible = sessionVisible();

    for (juce::Component* c : { (juce::Component*) &sessionTitle, (juce::Component*) &nextButton,
                                (juce::Component*) &extendButton, (juce::Component*) &endButton })
        c->setVisible (visible);

    if (visible)
    {
        if (session.isRunning())
        {
            const auto n = (int) session.getSlots().size();
            sessionTitle.setText (session.isTimeUp() ? tr ("Time's up!")
                                                     : tr ("Session: topic {index} of {count}", session.getIndex() + 1, n)
                                                          + (! session.isPaused() ? juce::String()
                                                              : tracker.isStartButtonMode() ? "  -  " + tr ("paused: press Start")
                                                              : tracker.clockMode() ? "  -  " + tr ("paused: press Resume")
                                                                                    : "  -  " + tr ("the clock runs while you play")),
                                  juce::dontSendNotification);
            sessionTitle.setColour (juce::Label::textColourId, session.isTimeUp() ? theme::highlight() : theme::text());
            nextButton.setButtonText (session.getNext() != nullptr ? tr ("Next topic") : tr ("Finish"));
            nextButton.setVisible (true);
            extendButton.setVisible (true);
            endButton.setButtonText (tr ("End"));
        }
        else
        {
            const auto reached = session.getTopicsReached();
            sessionTitle.setText (trn ("Session over: {duration}, {n} topic", "Session over: {duration}, {n} topics", reached,
                                       formatPracticeDuration (session.getElapsedSeconds())),
                                  juce::dontSendNotification);
            sessionTitle.setColour (juce::Label::textColourId, theme::text());
            nextButton.setVisible (false);
            extendButton.setVisible (false);
            endButton.setButtonText (tr ("Close"));
        }
    }

    if (visible != sessionShown)
    {
        sessionShown = visible;
        resized();
    }

    repaint (sessionCard);
}

void TodayPanel::updateNow()
{
    const auto now = juce::Time::getCurrentTime();
    titleLabel.setText (tr ("Today, {date}", i18n::date (now, "%A %d %B")), juce::dontSendNotification);

    double today = 0.0, week = 0.0;

    if (auto* log = tracker.getLog())
    {
        const auto start = startOfToday();
        today = log->playedBetween (start, start + juce::RelativeTime::days (1));
        week = log->playedBetween (start - juce::RelativeTime::days (6), start + juce::RelativeTime::days (1));
    }

    // The open entry may not be written yet.
    if (auto* current = tracker.getCurrent(); current != nullptr && current->playedSeconds < PracticeTracker::minimumSeconds)
    {
        today += current->playedSeconds;
        week += current->playedSeconds;
    }
    else if (current != nullptr && tracker.getLog() != nullptr)
    {
        const auto& entries = tracker.getLog()->getEntries();
        const auto inLog = std::find_if (entries.begin(), entries.end(), [&] (const PracticeEntry& e) { return e.id == current->id; });
        const auto extra = current->playedSeconds - (inLog != entries.end() ? inLog->playedSeconds : 0.0);
        today += extra;
        week += extra;
    }

    totalsLabel.setText (tr ("Played today: {today}     Last 7 days: {week}", formatPracticeDuration (today), formatPracticeDuration (week)),
                         juce::dontSendNotification);

    const auto active = tracker.getActiveTopicId().isNotEmpty();
    nowTitle.setText (active ? tr ("Practising: {title}", tracker.getActiveTopicTitle()) : tr ("Free practice"), juce::dontSendNotification);

    const auto onClock = tracker.isTimedByHand();
    const auto startMode = tracker.isStartButtonMode();
    juce::String status = startMode ? (tracker.isPaused() ? tr ("Not timing (microphone input): press Start to log practice")
                                                          : tr ("Timing: press Pause when you stop"))
                        : onClock ? (tracker.isPaused() ? tr ("Paused (timed without playing)") : tr ("Timing (without playing)"))
                                  : (tracker.isPlaying() ? tr ("Playing now") : tr ("Not playing"));

    if (active)
        status << "  -  " << tr ("{duration} on this topic so far", formatPracticeDuration (tracker.getActiveSeconds()));
    else
        status << "  -  " << (startMode ? tr ("without a topic it's logged as free practice")
                                        : tr ("pick a topic from the Practice rotation, or just play: it's logged as free practice"));

    nowStatus.setText (status, juce::dontSendNotification);
    nowStatus.setColour (juce::Label::textColourId, tracker.isPlaying() ? theme::good() : theme::text().withAlpha (0.65f));
    stopButton.setVisible (active);

    if (pauseButton.isVisible() != onClock)
    {
        pauseButton.setVisible (onClock);
        resized();
    }

    pauseButton.setButtonText (tracker.isPaused() ? (startMode ? tr ("Start") : tr ("Resume")) : tr ("Pause"));
    pauseButton.setTooltip (startMode ? tr ("The input is a microphone (it hears the room too): practice time counts between Start and Pause. "
                                            "Change this per input in the Audio settings.")
                                      : tr ("This topic is timed without playing: pause the clock while you're away"));

    if (! noteEditor.hasKeyboardFocus (true) && noteEditor.getText().trim() != tracker.getNote())
        noteEditor.setText (tracker.getNote(), false);
}

void TodayPanel::timerCallback()
{
    if (! isShowing())
        return;

    if (shownActiveId != tracker.getActiveTopicId())
        refresh();
    else
        updateNow();

    updateSession();
}

void TodayPanel::visibilityChanged()
{
    if (isVisible())
    {
        if (onBecameVisible)
            onBecameVisible();

        refresh();
    }
}

//==============================================================================
void TodayPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
    g.setColour (theme::panel());
    g.fillRoundedRectangle (nowCard.toFloat(), 8.0f);

    if (! recentCard.isEmpty())
        theme::paintCard (g, recentCard.toFloat(), 14.0f);

    if (! diceStrip.isEmpty())
    {
        g.setColour (theme::highlight());
        g.fillRoundedRectangle (diceStrip.toFloat(), 6.0f);
    }

    if (sessionShown && ! sessionCard.isEmpty())
    {
        g.setColour (theme::panel());
        g.fillRoundedRectangle (sessionCard.toFloat(), 8.0f);

        if (session.isRunning() && session.isTimeUp())
        {
            g.setColour (theme::highlight());
            g.drawRoundedRectangle (sessionCard.toFloat().reduced (1.0f), 8.0f, 2.0f);
        }

        const auto& slots = session.getSlots();
        auto r = sessionLines;

        for (int i = 0; i < (int) slots.size(); ++i)
        {
            auto line = r.removeFromTop (24);
            const auto current = session.isRunning() && i == session.getIndex();
            const auto done = i < session.getIndex() || (! session.isRunning() && i <= session.getIndex());

            juce::String right = tr ("{minutes} min", slots[(size_t) i].minutes);

            if (current)
                right = session.isTimeUp() ? tr ("over by {time}", formatClock (-session.getRemainingSeconds()))
                                           : session.isPaused() ? tr ("{time} left (paused)", formatClock (session.getRemainingSeconds()))
                                                                : tr ("{time} left", formatClock (session.getRemainingSeconds()));
            else if (done)
                right = tr ("done");

            g.setFont (current ? juce::FontOptions (15.0f).withStyle ("Bold") : juce::FontOptions (15.0f));
            g.setColour (current ? theme::highlight() : theme::text().withAlpha (done ? 0.45f : 0.8f));
            g.drawText (right, line, juce::Justification::centredRight, false);
            g.drawText (juce::String (i + 1) + ".  " + slots[(size_t) i].title,
                        line.withTrimmedRight (120), juce::Justification::centredLeft, true);
        }
    }
}

void TodayPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);

    auto header = area.removeFromTop (44);
    totalsLabel.setBounds (header.removeFromRight (juce::jmin (420, header.getWidth() / 2)));
    titleLabel.setBounds (header);
    area.removeFromTop (6);

    const auto wide = area.getWidth() >= 900;
    juce::Rectangle<int> left, right;

    if (wide)
    {
        left = area.removeFromLeft ((int) (area.getWidth() * 0.58f));
        area.removeFromLeft (16);
        right = area;
    }
    else
    {
        left = area;
    }

    // Now card (and the session card): on top of the right column (or of everything when narrow).
    auto& cardColumn = wide ? right : left;
    nowCard = cardColumn.removeFromTop (124);
    cardColumn.removeFromTop (12);

    sessionCard = {};

    if (sessionShown)
    {
        const auto lines = (int) session.getSlots().size();
        sessionCard = cardColumn.removeFromTop (44 + 24 * lines + 52);
        cardColumn.removeFromTop (12);

        auto sc = sessionCard.reduced (14, 10);
        sessionTitle.setBounds (sc.removeFromTop (28));
        auto buttons = sc.removeFromBottom (38);
        endButton.setBounds (buttons.removeFromRight (90).reduced (0, 2));
        buttons.removeFromRight (6);
        extendButton.setBounds (buttons.removeFromRight (90).reduced (0, 2));
        buttons.removeFromRight (6);
        nextButton.setBounds (buttons.removeFromRight (120).reduced (0, 2));
        sc.removeFromTop (4);
        sessionLines = sc;
    }

    auto card = nowCard.reduced (14, 10);
    auto cardTop = card.removeFromTop (28);
    stopButton.setBounds (cardTop.removeFromRight (90).reduced (0, 1));

    if (pauseButton.isVisible())
        pauseButton.setBounds (cardTop.removeFromRight (96).reduced (3, 1));

    nowTitle.setBounds (cardTop);
    nowStatus.setBounds (card.removeFromTop (26));
    card.removeFromTop (8);
    noteEditor.setBounds (card.removeFromTop (34));

    auto headerRow = left.removeFromTop (36);
    planButton.setBounds (headerRow.removeFromRight (150).reduced (0, 2));
    headerRow.removeFromRight (6);

    if (plansButton.isVisible())
    {
        plansButton.setBounds (headerRow.removeFromRight (110).reduced (0, 2));
        headerRow.removeFromRight (6);
    }

    diceButton.setBounds (headerRow.removeFromRight (130).reduced (0, 2));
    headerRow.removeFromRight (6);
    focusBox.setBounds (headerRow.removeFromRight (juce::jmin (150, juce::jmax (90, headerRow.getWidth() - 160))).reduced (0, 3));
    rotationHeader.setBounds (headerRow);
    left.removeFromTop (6);

    const auto showDice = diceTopicId.isNotEmpty();
    diceStrip = {};

    for (juce::Component* c : { (juce::Component*) &diceLabel, (juce::Component*) &dicePractiseButton,
                                (juce::Component*) &diceAgainButton, (juce::Component*) &diceCloseButton })
        c->setVisible (showDice);

    if (showDice)
    {
        diceStrip = left.removeFromTop (50);
        left.removeFromTop (8);
        auto d = diceStrip.reduced (10, 7);
        diceCloseButton.setBounds (d.removeFromRight (36));
        d.removeFromRight (6);
        diceAgainButton.setBounds (d.removeFromRight (110));
        d.removeFromRight (6);
        dicePractiseButton.setBounds (d.removeFromRight (100));
        d.removeFromRight (8);
        diceLabel.setBounds (d);
    }

    // Narrow: the last two weeks are either just their header row at the bottom, or take the rotation list's place.
    recentToggleButton.setVisible (! wide);

    if (! wide)
    {
        recentToggleButton.setButtonText (recentExpanded ? tr ("Hide") : tr ("Show"));

        if (recentExpanded)
        {
            right = left;
            left = left.withHeight (0);
        }
        else
        {
            right = left.removeFromBottom (30);
            left.removeFromBottom (12);
        }
    }

    rotationViewport.setBounds (left);
    rotationEmpty.setBounds (left.withHeight (juce::jmin (left.getHeight(), 120)));

    constexpr int rowHeight = 66, gap = 6;
    const auto width = rotationViewport.getMaximumVisibleWidth() - (rotationViewport.canScrollVertically() ? 4 : 0);
    rotationHolder.setSize (juce::jmax (1, width), juce::jmax (1, (int) rows.size() * (rowHeight + gap)));

    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, (int) i * (rowHeight + gap), rotationHolder.getWidth(), rowHeight);

    auto recentTop = right.removeFromTop (30);
    addTimeButton.setBounds (recentTop.removeFromRight (110).reduced (0, 2));
    recentTop.removeFromRight (6);
    allHistoryButton.setBounds (recentTop.removeFromRight (84).reduced (0, 2));
    recentTop.removeFromRight (6);

    if (recentToggleButton.isVisible())
    {
        recentToggleButton.setBounds (recentTop.removeFromRight (76).reduced (0, 2));
        recentTop.removeFromRight (6);
    }

    recentHeader.setBounds (recentTop);

    // The list sits in a card, clearly apart from the rotation rows above it.
    right.removeFromTop (6);
    recentCard = right.getHeight() > 20 ? right : juce::Rectangle<int>();
    recentViewport.setBounds (recentCard.reduced (theme::listCardPadX + 6, theme::listCardPadY + 2));
    recentView->setSize (juce::jmax (1, recentViewport.getMaximumVisibleWidth()), recentView->getContentHeight());
}

} // namespace pv
