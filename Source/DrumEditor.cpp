#include "DrumEditor.h"
#include "Theme.h"
#include "Icons.h"
#include "Localisation.h"

namespace pv
{

namespace
{
    constexpr int controlRowHeight = 44;
    constexpr int toolbarHeight = 40;
    constexpr int gridRowHeight = 23;
    constexpr int hintHeight = 22;

    void styleLabel (juce::Label& label, const juce::String& text, float alpha = 0.85f)
    {
        label.setText (text, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, theme::text().withAlpha (alpha));
    }

    int ticksForId (int id)     { return id == 2 ? 3 : id == 3 ? 4 : 2; }
    int idForTicks (int ticks)  { return ticks == 3 ? 2 : ticks == 4 ? 3 : 1; }
}

//==============================================================================
/** The steps of one bar: a row per drum (name, mute, volume), a column per step. */
class DrumSection::Grid final : public juce::Component
{
public:
    explicit Grid (DrumSection& o) : owner (o)
    {
        for (int d = 0; d < numDrums; ++d)
        {
            auto& row = rows[(size_t) d];
            row.mute.setButtonText (tr ("M"));
            row.mute.setClickingTogglesState (true);
            row.mute.setColour (juce::TextButton::buttonOnColourId, theme::record());
            row.mute.setTooltip (tr ("Mute the {drum}", tr (drumName ((Drum) d)).toLowerCase()));
            row.mute.onClick = [this, d] { owner.drums.drumMuted[(size_t) d] = rows[(size_t) d].mute.getToggleState(); };

            row.volume.setSliderStyle (juce::Slider::LinearHorizontal);
            row.volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            row.volume.setRange (0.0, 1.5, 0.01);
            row.volume.setDoubleClickReturnValue (true, DrumSequencer::defaultDrumGain ((Drum) d));
            row.volume.setTooltip (tr ("{drum} volume", tr (drumName ((Drum) d))));
            row.volume.onValueChange = [this, d] { owner.drums.drumGains[(size_t) d] = (float) rows[(size_t) d].volume.getValue(); };

            addAndMakeVisible (row.mute);
            addAndMakeVisible (row.volume);
        }

        refresh();
    }

    void refresh()
    {
        for (int d = 0; d < numDrums; ++d)
        {
            rows[(size_t) d].mute.setToggleState (owner.drums.drumMuted[(size_t) d].load(), juce::dontSendNotification);
            rows[(size_t) d].volume.setValue (owner.drums.drumGains[(size_t) d].load(), juce::dontSendNotification);
        }

        repaint();
    }

    void resized() override
    {
        for (int d = 0; d < numDrums; ++d)
        {
            auto r = rowArea (d);
            r.removeFromLeft (labelWidth());
            rows[(size_t) d].mute.setBounds (r.removeFromLeft (30).reduced (2, 2));
            rows[(size_t) d].volume.setBounds (r.removeFromLeft (volumeWidth()).reduced (2, 0));
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& pattern = owner.drums.getPattern();
        const auto steps = pattern.stepsPerBar();
        const auto cells = cellArea();
        const auto playingStep = owner.drums.getPlayingBar() == owner.shownBar ? owner.drums.getPlayingStep() : -1;

        g.setFont (juce::FontOptions (13.0f));

        for (int d = 0; d < numDrums; ++d)
        {
            const auto r = rowArea (d);
            g.setColour (theme::text().withAlpha (owner.drums.drumMuted[(size_t) d].load() ? 0.35f : 0.8f));
            g.drawText (tr (drumName ((Drum) d)), r.withWidth (labelWidth() - 4), juce::Justification::centredLeft, true);

            for (int step = 0; step < steps; ++step)
            {
                const auto cell = cellBounds (cells, d, step, steps);
                const auto beat = step / pattern.ticks;
                auto base = theme::text().withAlpha (beat % 2 == 0 ? 0.08f : 0.04f);

                if (step == playingStep)
                    base = theme::text().withAlpha (0.22f);

                g.setColour (base);
                g.fillRoundedRectangle (cell, 3.0f);

                switch (pattern.get (owner.shownBar, step, (Drum) d))
                {
                    case DrumPattern::ghost:  g.setColour (theme::accent().withAlpha (0.45f)); g.fillRoundedRectangle (cell.reduced (cell.getWidth() * 0.22f, cell.getHeight() * 0.22f), 2.0f); break;
                    case DrumPattern::normal: g.setColour (theme::accent()); g.fillRoundedRectangle (cell, 3.0f); break;
                    case DrumPattern::accent: g.setColour (theme::palette().wavePlayed2); g.fillRoundedRectangle (cell, 3.0f); break;
                    default: break;
                }
            }
        }

        // Beat numbers under the grid.
        g.setColour (theme::text().withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f));

        for (int beat = 0; beat < pattern.beatsPerBar; ++beat)
        {
            const auto cell = cellBounds (cells, numDrums - 1, beat * pattern.ticks, steps);
            g.drawText (juce::String (beat + 1), juce::Rectangle<float> (cell.getX(), cell.getBottom() + 1.0f, 30.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        downCell = cellAt (e.position);
        downTime = juce::Time::getMillisecondCounterHiRes();
        handled = false;

        if (downCell.first >= 0 && e.mods.isPopupMenu())
        {
            toggleGhost (downCell);
            handled = true;
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (handled || downCell.first < 0 || cellAt (e.position) != downCell)
            return;

        if (juce::Time::getMillisecondCounterHiRes() - downTime > 450.0)
        {
            toggleGhost (downCell);     // long press (touch screens)
            return;
        }

        const auto [drum, step] = downCell;
        const auto level = owner.drums.getPattern().get (owner.shownBar, step, (Drum) drum);
        const auto next = level == DrumPattern::off ? DrumPattern::normal
                        : level == DrumPattern::normal ? DrumPattern::accent
                                                       : DrumPattern::off;
        owner.drums.setCell (owner.shownBar, step, (Drum) drum, next);
        owner.refreshControls();
        repaint();
    }

    static int preferredHeight()    { return numDrums * gridRowHeight + 14; }

private:
    struct Row
    {
        juce::TextButton mute;
        juce::Slider volume;
    };

    int labelWidth() const      { return getWidth() < 700 ? 82 : 100; }
    int volumeWidth() const     { return getWidth() < 700 ? 50 : 70; }

    juce::Rectangle<int> rowArea (int d) const
    {
        return { 0, d * gridRowHeight, getWidth(), gridRowHeight };
    }

    juce::Rectangle<float> cellArea() const
    {
        return getLocalBounds().withTrimmedLeft (labelWidth() + 30 + volumeWidth() + 8).withHeight (numDrums * gridRowHeight).toFloat();
    }

    static juce::Rectangle<float> cellBounds (juce::Rectangle<float> area, int drum, int step, int steps)
    {
        const auto w = area.getWidth() / (float) juce::jmax (1, steps);
        return { area.getX() + step * w + 1.0f, area.getY() + drum * (float) gridRowHeight + 2.0f, w - 2.0f, (float) gridRowHeight - 4.0f };
    }

    std::pair<int, int> cellAt (juce::Point<float> p) const
    {
        const auto area = cellArea();
        const auto steps = owner.drums.getPattern().stepsPerBar();

        if (! area.contains (p) || steps <= 0)
            return { -1, -1 };

        const auto drum = juce::jlimit (0, numDrums - 1, (int) ((p.y - area.getY()) / (float) gridRowHeight));
        const auto step = juce::jlimit (0, steps - 1, (int) ((p.x - area.getX()) / (area.getWidth() / (float) steps)));
        return { drum, step };
    }

    void toggleGhost (std::pair<int, int> cell)
    {
        const auto level = owner.drums.getPattern().get (owner.shownBar, cell.second, (Drum) cell.first);
        owner.drums.setCell (owner.shownBar, cell.second, (Drum) cell.first, level == DrumPattern::ghost ? DrumPattern::off : DrumPattern::ghost);
        owner.refreshControls();
        repaint();
    }

    DrumSection& owner;
    std::array<Row, numDrums> rows;
    std::pair<int, int> downCell { -1, -1 };
    double downTime = 0.0;
    bool handled = false;
};

//==============================================================================
DrumSection::DrumSection (AppEngine& e)
    : engine (e), drums (*e.drums), metronome (*e.metronome)
{
    savedVersion = drums.getPattern();

    styleLabel (title, tr ("Drums"));
    theme::styleHeader (title);
    styleLabel (status, {}, 0.6f);
    status.setMinimumHorizontalScale (0.6f);

    playButton.setClickingTogglesState (false);
    playButton.setColour (juce::TextButton::buttonOnColourId, theme::accent());
    playButton.setTooltip (tr ("Play the pattern with the metronome (starts it if needed): same tempo and bars as the loops."));
    playButton.onClick = [this] { togglePlay(); };

    patternButton.setTooltip (tr ("Choose a pattern: the starter ones or your own"));
    patternButton.onClick = [this] { showPatternMenu(); };

    kitBox.addItem (tr ("Acoustic kit"), 1);
    kitBox.addItem (tr ("Electronic kit"), 2);
    kitBox.setTooltip (tr ("Acoustic: a recorded drum kit. Electronic: a drum machine."));
    kitBox.onChange = [this]
    {
        drums.setKitType (kitBox.getSelectedId() == 2 ? DrumKitType::electronic : DrumKitType::acoustic);
        refreshControls();
    };

    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volume.setRange (0.0, 1.5, 0.01);
    volume.setDoubleClickReturnValue (true, 0.8);
    volume.setTooltip (tr ("Drums volume"));
    volume.onValueChange = [this] { drums.gain = (float) volume.getValue(); };

    clicksMutedButton.setTooltip (tr ("Silence the metronome clicks (the clock keeps running, so the drums and loops stay in time)."));
    clicksMutedButton.onClick = [this]
    {
        metronome.clicksMuted = clicksMutedButton.getToggleState();

        if (onClicksMutedChange)
            onClicksMutedChange();
    };

    showButton.setTooltip (tr ("Show or hide the pattern editor"));
    showButton.onClick = [this] { setEditorShown (! editorShown); };

    for (auto* c : std::initializer_list<juce::Component*> { &title, &playButton, &patternButton, &kitBox, &volume,
                                                             &clicksMutedButton, &showButton, &status })
        addAndMakeVisible (*c);

    // Editor
    styleLabel (stepsLabel, tr ("Steps"));
    stepsBox.addItem (tr ("8ths"), 1);
    stepsBox.addItem (tr ("Triplets"), 2);
    stepsBox.addItem (tr ("16ths"), 3);
    stepsBox.onChange = [this]
    {
        const auto& p = drums.getPattern();
        reshape (p.beatsPerBar, ticksForId (stepsBox.getSelectedId()), p.bars);
    };

    styleLabel (swingLabel, tr ("Swing"));
    swingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    swingSlider.setRange (0.5, 0.75, 0.01);
    swingSlider.setDoubleClickReturnValue (true, 0.5);
    swingSlider.setTooltip (tr ("Delays every second 8th (or 16th): left = straight, middle = shuffle"));
    swingSlider.onValueChange = [this]
    {
        auto p = drums.getPattern();
        p.swing = swingSlider.getValue();
        drums.setPattern (p);
        refreshControls();
    };

    styleLabel (barsLabel, tr ("Bars"));

    for (auto bars : { 1, 2, 4, 8 })
        barsBox.addItem (juce::String (bars), bars);

    barsBox.setTooltip (tr ("Pattern length. It repeats over the loops; it starts again with the first loop."));
    barsBox.onChange = [this]
    {
        const auto& p = drums.getPattern();
        reshape (p.beatsPerBar, p.ticks, barsBox.getSelectedId());
    };

    copyBarButton.setTooltip (tr ("Copy the bar shown to every other bar of the pattern"));
    copyBarButton.onClick = [this]
    {
        auto p = drums.getPattern();
        p.copyBarToAll (shownBar);
        drums.setPattern (p);
        refreshControls();
    };

    clearButton.setTooltip (tr ("Remove every note of the bar shown"));
    clearButton.onClick = [this]
    {
        for (int step = 0; step < DrumPattern::maxSteps; ++step)
            for (int d = 0; d < numDrums; ++d)
                drums.setCell (shownBar, step, (Drum) d, DrumPattern::off);

        refreshControls();
    };

    saveButton.setTooltip (tr ("Save this pattern under a name (Documents > Practice Vibes > Drum patterns)"));
    saveButton.onClick = [this] { saveAs(); };
    deleteButton.setTooltip (tr ("Delete this saved pattern"));
    deleteButton.onClick = [this] { deleteCurrent(); };

    styleLabel (hint, tr ("Tap a step: on, accent, off. Right-click or hold: ghost note."), 0.5f);
    hint.setFont (juce::FontOptions (12.0f));

    grid = std::make_unique<Grid> (*this);

    for (auto* c : std::initializer_list<juce::Component*> { &stepsLabel, &stepsBox, &swingLabel, &swingSlider, &barsLabel, &barsBox,
                                                             &copyBarButton, &clearButton, &saveButton, &deleteButton, &hint, grid.get() })
        addChildComponent (*c);

    refreshControls();
    startTimerHz (30);
}

DrumSection::~DrumSection() = default;

void DrumSection::setEditorShown (bool shouldShow)
{
    editorShown = shouldShow;
    refreshControls();
    resized();

    if (onLayoutChange)
        onLayoutChange();
}

int DrumSection::getPreferredHeight() const
{
    return controlRowHeight + (editorShown ? toolbarHeight + Grid::preferredHeight() + hintHeight : 0);
}

void DrumSection::patternChangedElsewhere()
{
    savedVersion = drums.getPattern();
    shownBar = 0;
    grid->refresh();
    refreshControls();
}

juce::String DrumSection::displayName() const
{
    const auto& p = drums.getPattern();
    const auto edited = ! p.sameNotes (savedVersion) || p.name != savedVersion.name;
    const auto& starters = DrumPatterns::builtIn();
    const auto isStarter = p.builtIn || std::any_of (starters.begin(), starters.end(), [&] (const DrumPattern& s) { return s.name == p.name; });
    const auto name = isStarter ? tr (p.name) : p.name;
    return edited ? tr ("{name} (edited)", name) : name;
}

//==============================================================================
void DrumSection::refreshControls()
{
    const auto& p = drums.getPattern();
    shownBar = juce::jlimit (0, p.bars - 1, shownBar);

    playButton.setToggleState (drums.enabled.load(), juce::dontSendNotification);
    playButton.setButtonText (drums.enabled.load() ? tr ("Stop drums") : tr ("Play drums"));
    icons::setIcon (playButton, drums.enabled.load() ? icons::Icon::stop : icons::Icon::play, false);
    patternButton.setButtonText (tr ("Pattern: {name}", displayName()));
    kitBox.setSelectedId (drums.getKitType() == DrumKitType::electronic ? 2 : 1, juce::dontSendNotification);

    if (! volume.isMouseButtonDown())
        volume.setValue (drums.gain.load(), juce::dontSendNotification);

    clicksMutedButton.setToggleState (metronome.clicksMuted.load(), juce::dontSendNotification);
    showButton.setButtonText (editorShown ? tr ("Hide pattern") : tr ("Edit pattern"));

    stepsBox.setSelectedId (idForTicks (p.ticks), juce::dontSendNotification);
    barsBox.setSelectedId (p.bars, juce::dontSendNotification);
    swingSlider.setEnabled (p.ticks != 3);

    if (! swingSlider.isMouseButtonDown())
        swingSlider.setValue (p.swing, juce::dontSendNotification);

    const auto isSaved = DrumPatterns::fileFor (p.name).existsAsFile() && ! savedVersion.builtIn && p.name == savedVersion.name;
    deleteButton.setEnabled (isSaved);
    copyBarButton.setEnabled (p.bars > 1);

    // One tab per bar (none for a one-bar pattern).
    if (barTabs.size() != (p.bars > 1 ? p.bars : 0))
    {
        barTabs.clear();

        for (int bar = 0; p.bars > 1 && bar < p.bars; ++bar)
        {
            auto* tab = barTabs.add (new juce::TextButton (tr ("Bar {n}", bar + 1)));
            tab->setColour (juce::TextButton::buttonOnColourId, theme::accent());
            tab->onClick = [this, bar] { setShownBar (bar); };
            addChildComponent (*tab);
        }

        resized();
    }

    for (int bar = 0; bar < barTabs.size(); ++bar)
    {
        barTabs[bar]->setToggleState (bar == shownBar, juce::dontSendNotification);
        barTabs[bar]->setVisible (editorShown);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &stepsLabel, &stepsBox, &swingLabel, &swingSlider, &barsLabel, &barsBox,
                                                             &copyBarButton, &clearButton, &saveButton, &deleteButton, &hint, grid.get() })
        c->setVisible (editorShown);

    grid->repaint();
}

void DrumSection::setShownBar (int bar)
{
    shownBar = bar;
    refreshControls();
}

void DrumSection::reshape (int beats, int ticks, int bars)
{
    auto p = drums.getPattern();
    p.reshape (beats, ticks, bars);
    drums.setPattern (p);
    refreshControls();
}

void DrumSection::togglePlay()
{
    if (! drums.enabled.load())
    {
        drums.enabled = true;

        if (! metronome.isRunning())
        {
            metronome.start();
            startedMetronome = true;
        }
    }
    else
    {
        drums.enabled = false;

        // Stop the metronome if the drums started it, unless loops need it.
        if (startedMetronome && engine.looper->isEmpty())
            metronome.stop();

        startedMetronome = false;
    }

    refreshControls();
}

//==============================================================================
void DrumSection::showPatternMenu()
{
    const auto& builtIn = DrumPatterns::builtIn();
    const auto user = DrumPatterns::loadUserPatterns();
    juce::PopupMenu menu;

    menu.addSectionHeader (tr ("Starter patterns"));

    for (size_t i = 0; i < builtIn.size(); ++i)
        menu.addItem ((int) i + 1, tr (builtIn[i].name));

    menu.addSectionHeader (tr ("My patterns"));

    if (user.empty())
        menu.addItem (-1, tr ("None saved yet: edit a pattern, then Save as..."), false);

    for (size_t i = 0; i < user.size(); ++i)
        menu.addItem (1000 + (int) i, user[i].name);

    menu.addSeparator();
    menu.addItem (2000, tr ("New empty pattern"));

   #if ! JUCE_IOS
    menu.addItem (2001, tr ("Show the patterns folder"));
   #endif

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (patternButton),
                        [safeThis = SafePointer<DrumSection> (this), user] (int chosen)
    {
        if (safeThis == nullptr || chosen == 0)
            return;

        const auto& starters = DrumPatterns::builtIn();

        if (juce::isPositiveAndBelow (chosen - 1, (int) starters.size()))
        {
            safeThis->choosePattern (starters[(size_t) chosen - 1]);
        }
        else if (juce::isPositiveAndBelow (chosen - 1000, (int) user.size()))
        {
            safeThis->choosePattern (user[(size_t) chosen - 1000]);
        }
        else if (chosen == 2000)
        {
            DrumPattern empty;
            empty.beatsPerBar = juce::jlimit (1, DrumPattern::maxBeats, safeThis->metronome.beatsPerBar.load());
            empty.ticks = 4;
            const auto fixed = safeThis->engine.looper->fixedBars.load();
            empty.bars = fixed == 1 || fixed == 2 || fixed == 4 || fixed == 8 ? fixed : 1;
            safeThis->choosePattern (empty);

            if (! safeThis->editorShown)
                safeThis->showButton.triggerClick();
        }
        else if (chosen == 2001)
        {
            const auto folder = DrumPatterns::getUserFolder();
            folder.createDirectory();
            folder.revealToUser();
        }
    });
}

void DrumSection::choosePattern (const DrumPattern& chosen)
{
    auto p = chosen;

    // Same meter as the metronome: the pattern sets it, unless the loops hold it (then the pattern adapts).
    if (metronome.isTempoLocked())
        p.reshape (metronome.getCurrentBeatsPerBar(), p.ticks, p.bars);
    else
        metronome.beatsPerBar = p.beatsPerBar;

    drums.setPattern (p);
    savedVersion = p;
    shownBar = 0;
    refreshControls();
}

void DrumSection::saveAs()
{
    auto* window = new juce::AlertWindow (tr ("Save pattern"), tr ("Name:"), juce::MessageBoxIconType::NoIcon, this);
    const auto& p = drums.getPattern();
    window->addTextEditor ("name", p.builtIn || savedVersion.builtIn ? tr ("{name} (mine)", tr (p.name)) : p.name);
    window->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis = SafePointer<DrumSection> (this), window] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();

        if (safeThis == nullptr || result != 1 || name.isEmpty())
            return;

        auto pattern = safeThis->drums.getPattern();
        pattern.name = name;
        pattern.builtIn = false;

        auto save = [safeThis, pattern]
        {
            if (safeThis == nullptr)
                return;

            const auto saved = DrumPatterns::saveUserPattern (pattern);

            if (saved.failed())
            {
                safeThis->status.setText (tr ("Not saved: {error}", saved.getErrorMessage()), juce::dontSendNotification);
                safeThis->showsSaveError = true;
                return;
            }

            safeThis->drums.setPattern (pattern);
            safeThis->savedVersion = pattern;
            safeThis->refreshControls();
            juce::Logger::writeToLog ("Saved drum pattern " + DrumPatterns::fileFor (pattern.name).getFullPathName());
        };

        if (DrumPatterns::fileFor (name).existsAsFile() && name != safeThis->savedVersion.name)
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions{}.withIconType (juce::MessageBoxIconType::QuestionIcon)
                                              .withTitle (tr ("Save pattern")).withMessage (tr ("A pattern called \"{name}\" exists. Replace it?", name))
                                              .withButton (tr ("Replace"), 1).withButton (tr ("Cancel"), 0).withAssociatedComponent (safeThis.getComponent()),
                                          [save] (int r) { if (r == 1) save(); });
            return;
        }

        save();
    }), true);
}

void DrumSection::deleteCurrent()
{
    const auto name = drums.getPattern().name;

    juce::AlertWindow::showAsync (juce::MessageBoxOptions{}.withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (tr ("Delete pattern")).withMessage (tr ("Delete your pattern \"{name}\"? It keeps playing until you choose another.", name))
                                      .withButton (tr ("Delete"), 1).withButton (tr ("Cancel"), 0).withAssociatedComponent (this),
                                  [safeThis = SafePointer<DrumSection> (this), name] (int result)
    {
        if (safeThis == nullptr || result != 1)
            return;

        DrumPatterns::deleteUserPattern (name);
        safeThis->savedVersion = DrumPattern();     // now unsaved
        safeThis->savedVersion.name = {};
        safeThis->refreshControls();
    });
}

//==============================================================================
void DrumSection::timerCallback()
{
    const auto running = metronome.isRunning();

    // The drums play with the metronome: when it stops (from any tab), they switch off.
    if (metronomeWasRunning && ! running && drums.enabled.load())
    {
        drums.enabled = false;
        startedMetronome = false;
        refreshControls();
    }

    metronomeWasRunning = running;

    // Same meter as the metronome: follow its "beats per bar" (or the loops', while they hold it).
    const auto beats = juce::jlimit (1, DrumPattern::maxBeats, metronome.isTempoLocked() ? metronome.getCurrentBeatsPerBar()
                                                                                         : metronome.beatsPerBar.load());
    const auto& p = drums.getPattern();

    if (beats != p.beatsPerBar)
        reshape (beats, p.ticks, p.bars);

    if (playButton.getToggleState() != drums.enabled.load() || clicksMutedButton.getToggleState() != metronome.clicksMuted.load())
        refreshControls();

    // Status
    juce::String text;

    if (! drums.isKitReady())
        text = tr ("Loading the kit...");
    else if (drums.enabled.load())
        text = p.bars > 1 ? tr ("Bar {n} of {total}", juce::jmax (0, drums.getPlayingBar()) + 1, p.bars) : juce::String();

    if (! showsSaveError || drums.enabled.load())
    {
        showsSaveError = false;
        status.setText (text, juce::dontSendNotification);
    }

    // Playhead
    const auto playing = drums.getPlayingBar() * 1000 + drums.getPlayingStep();

    if (playing != shownPlayingBar && editorShown)
    {
        shownPlayingBar = playing;
        grid->repaint();
    }
}

//==============================================================================
void DrumSection::paint (juce::Graphics& g)
{
    if (editorShown)
    {
        g.setColour (theme::panel());
        g.fillRoundedRectangle (getLocalBounds().withTrimmedTop (controlRowHeight).toFloat(), 6.0f);
    }
}

void DrumSection::resized()
{
    auto area = getLocalBounds();
    auto row = area.removeFromTop (controlRowHeight);
    const auto narrow = getWidth() < 900;

    title.setVisible (! narrow);

    if (! narrow)
        title.setBounds (row.removeFromLeft (70));

    playButton.setBounds (row.removeFromLeft (130).reduced (2, 4));
    showButton.setBounds (row.removeFromRight (narrow ? 120 : 140).reduced (2, 4));
    clicksMutedButton.setBounds (row.removeFromRight (narrow ? 110 : 120).reduced (2, 4));
    volume.setBounds (row.removeFromRight (narrow ? 80 : 140).reduced (4, 8));
    kitBox.setBounds (row.removeFromRight (narrow ? 130 : 150).reduced (2, 6));
    patternButton.setBounds (row.removeFromLeft (juce::jmin (260, row.getWidth())).reduced (2, 4));
    status.setBounds (row.reduced (6, 0));

    if (! editorShown)
        return;

    area.reduce (8, 2);
    auto tools = area.removeFromTop (toolbarHeight);
    stepsLabel.setBounds (tools.removeFromLeft (44));
    stepsBox.setBounds (tools.removeFromLeft (100).reduced (2, 6));
    swingLabel.setBounds (tools.removeFromLeft (50).withTrimmedLeft (6));
    swingSlider.setBounds (tools.removeFromLeft (narrow ? 70 : 110).reduced (2, 8));
    barsLabel.setBounds (tools.removeFromLeft (42).withTrimmedLeft (6));
    barsBox.setBounds (tools.removeFromLeft (64).reduced (2, 6));
    deleteButton.setBounds (tools.removeFromRight (70).reduced (2, 5));
    saveButton.setBounds (tools.removeFromRight (90).reduced (2, 5));
    clearButton.setBounds (tools.removeFromRight (60).reduced (2, 5));
    copyBarButton.setBounds (tools.removeFromRight (narrow ? 110 : 130).reduced (2, 5));
    tools.removeFromLeft (8);

    const auto tabWidth = barTabs.isEmpty() ? 0 : juce::jmin (64, tools.getWidth() / barTabs.size());

    for (auto* tab : barTabs)
        tab->setBounds (tools.removeFromLeft (tabWidth).reduced (1, 6));

    hint.setBounds (area.removeFromBottom (hintHeight));
    grid->setBounds (area);
}

} // namespace pv
