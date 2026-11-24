#include "LooperPanel.h"
#include "FootswitchPanel.h"
#include "AppPaths.h"
#include "LoopFiles.h"
#include "Theme.h"
#include "Icons.h"
#include "Localisation.h"

namespace pv
{

namespace
{
    const juce::Colour recordColour  { 0xffd64545 };
    const juce::Colour armedColour   { 0xffe0a030 };
    const juce::Colour playColour    { 0xff3f9e5a };
    const juce::Colour overdubColour { 0xffb455c8 };

    void styleLabel (juce::Label& label, const juce::String& text, float alpha = 0.85f)
    {
        label.setText (text, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, theme::text().withAlpha (alpha));
    }
}

//==============================================================================
/** Beat dots for the current bar (same look as the Practice tab's). */
class LooperPanel::BeatDots final : public juce::Component
{
public:
    explicit BeatDots (MetronomeProcessor& m) : metronome (m) {}

    void update()
    {
        const auto beat = metronome.isRunning() ? metronome.getCurrentBeat() : -1;
        const auto beats = beat >= 0 ? metronome.getCurrentBeatsPerBar() : metronome.beatsPerBar.load();

        if (beat != lastBeat || beats != lastBeats)
        {
            lastBeat = beat;
            lastBeats = beats;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto beats = juce::jmax (1, lastBeats);
        const auto size = juce::jmin (18.0f, (float) getHeight(), (float) getWidth() / (float) beats - 6.0f);   // small dots (mock-up)

        for (int i = 0; i < beats; ++i)
        {
            const auto x = (float) i * ((float) getWidth() / (float) beats);
            auto circle = juce::Rectangle<float> (x, 0.0f, size, size).withY (((float) getHeight() - size) * 0.5f);
            g.setColour (i == lastBeat ? (i == 0 ? theme::highlight() : theme::accent())
                                       : theme::track());
            g.fillEllipse (circle.reduced (2.0f));
        }
    }

private:
    MetronomeProcessor& metronome;
    int lastBeat = -1, lastBeats = 4;
};

//==============================================================================
/** One track: the pedal button, mute, clear, volume, and a loop progress bar. */
class LooperPanel::TrackRow final : public juce::Component
{
public:
    TrackRow (Looper& l, int index) : looper (l), track (index)
    {
        nameLabel.setText (juce::String (track + 1), juce::dontSendNotification);
        nameLabel.setFont (theme::displayFont (26.0f, "SemiBold"));
        nameLabel.setJustificationType (juce::Justification::centred);
        nameLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.7f));
        addAndMakeVisible (nameLabel);

        pedalLabel.setFont (juce::FontOptions (12.0f));
        pedalLabel.setJustificationType (juce::Justification::centred);
        pedalLabel.setColour (juce::Label::textColourId, theme::highlight());
        pedalLabel.setMinimumHorizontalScale (0.6f);
        addAndMakeVisible (pedalLabel);

        pedalButton.onClick = [this] { looper.pressTrack (track); };
        pedalButton.setTooltip (tr ("Record, then overdub / play (key {key})", track + 1));
        addAndMakeVisible (pedalButton);

        muteButton.onClick = [this] { looper.toggleMute (track); };
        addAndMakeVisible (muteButton);

        clearButton.onClick = [this] { looper.clearTrack (track); };
        addAndMakeVisible (clearButton);

        volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
        volumeSlider.setRange (-40.0, 6.0, 0.5);
        volumeSlider.setTextValueSuffix (" dB");
        volumeSlider.setValue (juce::Decibels::gainToDecibels (looper.getGain (track)), juce::dontSendNotification);
        volumeSlider.setDoubleClickReturnValue (true, 0.0);
        volumeSlider.onValueChange = [this]
        {
            looper.setTrackGain (track, juce::Decibels::decibelsToGain ((float) volumeSlider.getValue(), -40.0f));
        };
        addAndMakeVisible (volumeSlider);
    }

    /** The footswitch(es) whose tap plays this track, e.g. "A"; empty = none. */
    void setPedal (const juce::String& switches, const juce::String& tooltip)
    {
        if (pedalLabel.getText() != switches)
        {
            pedalLabel.setText (switches, juce::dontSendNotification);
            pedalLabel.setTooltip (tooltip);
            resized();
        }
    }

    void update()
    {
        const auto state = looper.getState (track);
        const auto muted = looper.isMuted (track);
        const auto bars = looper.getBars (track);
        const auto position = looper.getPosition (track);
        const auto peak = looper.takePeak (track);

        juce::String text;
        juce::Colour colour;

        switch (state)
        {
            case Looper::State::empty:       text = tr ("Record");                              colour = theme::raised(); break;
            case Looper::State::armed:       text = tr ("Waiting for bar line...");             colour = armedColour;   break;
            case Looper::State::recording:   text = tr ("Recording - tap to stop");             colour = recordColour;  break;
            case Looper::State::playing:     text = muted ? tr ("Muted - tap to play") : tr ("Playing - tap to overdub");
                                             colour = muted ? theme::track() : playColour; break;
            case Looper::State::overdubbing: text = tr ("Overdubbing - tap to stop");           colour = overdubColour; break;
        }

        pedalButton.setButtonText (text);
        icons::setIcon (pedalButton, state == Looper::State::empty ? icons::Icon::record : icons::Icon::none, false);
        pedalButton.setColour (juce::TextButton::buttonColourId, colour);
        muteButton.setButtonText (muted ? tr ("Unmute") : tr ("Mute"));
        muteButton.setEnabled (state == Looper::State::playing || state == Looper::State::overdubbing);
        clearButton.setEnabled (state != Looper::State::empty);

        const auto level = juce::jmax (peak, meterLevel * 0.85f);

        if (state != lastState || bars != lastBars || std::abs (position - lastPosition) > 0.002f
            || muted != lastMuted || std::abs (level - meterLevel) > 0.01f)
        {
            lastState = state;
            lastBars = bars;
            lastPosition = position;
            lastMuted = muted;
            meterLevel = level;
            repaint (progressArea);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panel());
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f);

        auto area = progressArea.toFloat();
        g.setColour (theme::shade (0.35f));
        g.fillRoundedRectangle (area, 4.0f);

        const auto colour = lastState == Looper::State::recording   ? recordColour
                          : lastState == Looper::State::overdubbing ? overdubColour
                          : lastState == Looper::State::armed       ? armedColour
                          : lastMuted                               ? juce::Colours::grey
                                                                    : playColour;

        if (lastPosition >= 0.0f)
        {
            g.setColour (colour.withAlpha (0.55f));
            g.fillRoundedRectangle (area.withWidth (area.getWidth() * juce::jlimit (0.0f, 1.0f, lastPosition)), 4.0f);
        }

        if (lastBars > 1 && lastState != Looper::State::recording)
        {
            g.setColour (theme::text().withAlpha (0.25f));

            for (int b = 1; b < lastBars; ++b)
            {
                const auto x = area.getX() + area.getWidth() * (float) b / (float) lastBars;
                g.drawVerticalLine (juce::roundToInt (x), area.getY() + 3.0f, area.getBottom() - 3.0f);
            }
        }

        // Output level along the bottom edge
        g.setColour (theme::text().withAlpha (0.5f));
        g.fillRect (area.removeFromBottom (3.0f).withWidth (area.getWidth() * juce::jlimit (0.0f, 1.0f, meterLevel)));

        juce::String info;

        if (lastState == Looper::State::empty)
            info = tr ("empty");
        else if (lastState == Looper::State::armed)
            info = tr ("starts on the next bar line");
        else if (lastState == Looper::State::recording)
            info = lastBars > 0 ? tr ("bar {n}", lastBars) : juce::String();
        else
            info = trn ("{n} bar", "{n} bars", lastBars);

        g.setColour (theme::text().withAlpha (0.85f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (info, progressArea.reduced (10, 0), juce::Justification::centredLeft);
    }

    void resized() override
    {
        const auto slim = getHeight() < 50;     // the drum pattern editor is open
        auto area = getLocalBounds().reduced (8, slim ? 2 : 6);
        auto name = area.removeFromLeft (44);

        if (pedalLabel.getText().isNotEmpty())
            pedalLabel.setBounds (name.removeFromBottom (juce::jmin (18, name.getHeight() / 3)));
        else
            pedalLabel.setBounds ({});

        nameLabel.setBounds (name);
        const auto gap = slim ? 1 : 3;
        pedalButton.setBounds (area.removeFromLeft (juce::jmin (260, area.getWidth() / 3)).reduced (3, gap));
        clearButton.setBounds (area.removeFromRight (80).reduced (3, gap));
        muteButton.setBounds (area.removeFromRight (90).reduced (3, gap));
        volumeSlider.setBounds (area.removeFromRight (juce::jmin (280, area.getWidth() / 3)).reduced (3, 0)
                                    .withSizeKeepingCentre (juce::jmin (280, area.getWidth() / 3) - 6, juce::jmin (32, area.getHeight())));
        progressArea = area.reduced (8, slim ? 1 : 6);
    }

private:
    Looper& looper;
    const int track;

    juce::Label nameLabel, pedalLabel;
    juce::TextButton pedalButton, muteButton { tr ("Mute") }, clearButton { tr ("Clear") };
    juce::Slider volumeSlider;
    juce::Rectangle<int> progressArea;

    Looper::State lastState = Looper::State::empty;
    int lastBars = 0;
    float lastPosition = -1.0f, meterLevel = 0.0f;
    bool lastMuted = false;
};

//==============================================================================
LooperPanel::LooperPanel (AppEngine& e)
    : engine (e), looper (*e.looper), metronome (*e.metronome)
{
    loadSettings();
    setWantsKeyboardFocus (true);

    header.setText (tr ("Looper"), juce::dontSendNotification);
    theme::styleHeader (header);
    header.setColour (juce::Label::textColourId, theme::text().withAlpha (0.9f));
    addAndMakeVisible (header);

    styleLabel (sourceLabel, tr ("Record"));
    addAndMakeVisible (sourceLabel);

    for (auto* b : { &afterFxButton, &beforeFxButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent());
        addAndMakeVisible (*b);
    }

    afterFxButton.setTooltip (tr ("Record your instrument as you hear it, with the rig's effects."));
    beforeFxButton.setTooltip (tr ("Record your dry instrument; loops are played through the rig, so changing the tone changes them too."));
    (looper.source.load() == (int) Looper::Source::beforeFx ? beforeFxButton : afterFxButton).setToggleState (true, juce::dontSendNotification);
    afterFxButton.onClick = [this] { looper.source = (int) Looper::Source::afterFx; saveSettings(); };
    beforeFxButton.onClick = [this] { looper.source = (int) Looper::Source::beforeFx; saveSettings(); };

    styleLabel (lengthLabel, tr ("Length"));
    addAndMakeVisible (lengthLabel);
    lengthBox.addItem (tr ("Free (stop on a bar line)"), 100);

    for (auto bars : { 1, 2, 3, 4, 6, 8, 12, 16 })
        lengthBox.addItem (trn ("{n} bar", "{n} bars", bars), bars);

    lengthBox.setSelectedId (looper.fixedBars.load() > 0 ? looper.fixedBars.load() : 100, juce::dontSendNotification);
    lengthBox.onChange = [this]
    {
        const auto id = lengthBox.getSelectedId();
        looper.fixedBars = id == 100 ? 0 : id;
        saveSettings();
    };
    addAndMakeVisible (lengthBox);

    undoButton.onClick = [this] { looper.undo(); };
    clearAllButton.onClick = [this] { looper.clearAll(); };
    muteAllButton.onClick = [this] { looper.toggleMuteAll(); };

    footswitchButton.setTooltip (tr ("Control the looper with a MIDI pedal: learn its switches, choose tap and hold actions."));
    footswitchButton.onClick = [this]
    {
        if (showOverlay)
            showOverlay (tr ("Footswitch"), std::make_unique<FootswitchPanel> (engine));
    };

    for (auto* b : { &undoButton, &clearAllButton, &muteAllButton, &metronomeButton, &footswitchButton })
        addAndMakeVisible (*b);

    // Metronome (shared with the Practice tab)
    styleLabel (tempoLabel, tr ("Tempo"));
    tempoSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    tempoSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 26);
    tempoSlider.setRange (30.0, 300.0, 1.0);
    tempoSlider.setTextValueSuffix (" bpm");
    tempoSlider.setValue (metronome.startBpm.load(), juce::dontSendNotification);
    tempoSlider.onValueChange = [this]
    {
        metronome.startBpm = tempoSlider.getValue();
        metronome.tempoChangedByUser();
    };

    styleLabel (beatsLabel, tr ("Beats per bar"));

    for (int i = 1; i <= 12; ++i)
        beatsBox.addItem (juce::String (i), i);

    beatsBox.setSelectedId (metronome.beatsPerBar.load(), juce::dontSendNotification);
    beatsBox.onChange = [this] { metronome.beatsPerBar = juce::jmax (1, beatsBox.getSelectedId()); };

    icons::setIcon (metronomeButton, icons::Icon::metronome, false);
    metronomeButton.onClick = [this]
    {
        if (metronome.isRunning())
            metronome.stop();
        else
            metronome.start();
    };

    tempoDisplay.setFont (theme::displayFont (30.0f, "SemiBold"));
    tempoDisplay.setColour (juce::Label::textColourId, theme::highlight());
    tempoDisplay.setJustificationType (juce::Justification::centredRight);

    beatDots = std::make_unique<BeatDots> (metronome);

    for (auto* c : std::initializer_list<juce::Component*> { &tempoLabel, &tempoSlider, &beatsLabel, &beatsBox, &tempoDisplay, beatDots.get() })
        addAndMakeVisible (*c);

    // Latency
    styleLabel (offsetLabel, tr ("Latency fine-tune"));
    offsetSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    offsetSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 26);
    offsetSlider.setRange (-100.0, 100.0, 0.5);
    offsetSlider.setTextValueSuffix (" ms");
    offsetSlider.setValue (looper.latencyOffsetMs.load(), juce::dontSendNotification);
    offsetSlider.setDoubleClickReturnValue (true, 0.0);
    offsetSlider.setTooltip (tr ("If overdubs land late, move this up; if early, down."));
    offsetSlider.onValueChange = [this] { looper.latencyOffsetMs = (float) offsetSlider.getValue(); };
    offsetSlider.onDragEnd = [this] { saveSettings(); };
    styleLabel (latencyLabel, {}, 0.6f);
    latencyLabel.setMinimumHorizontalScale (0.5f);
    styleLabel (hintLabel, {}, 0.7f);
    hintLabel.setJustificationType (juce::Justification::topLeft);

    calibrateButton.setTooltip (tr ("Measure the latency: play short muted notes (or strums) right on the clicks "
                                "(or connect the audio output to the input with a cable)."));
    calibrateButton.onClick = [this]
    {
        if (calibrating)
        {
            calibrationMessage = tr ("Calibration cancelled.");
            endCalibration();
        }
        else
        {
            startCalibration();
        }
    };

    saveButton.setTooltip (tr ("Save every track as a WAV (plus a mix) in Documents > Practice Vibes > Loops, to load again later or use in a DAW."));
    saveButton.onClick = [this] { saveLoops(); };
    loadButton.setTooltip (tr ("Load saved loops (the looper must be empty)."));
    loadButton.onClick = [this] { showLoadMenu(); };

    for (auto* c : std::initializer_list<juce::Component*> { &offsetLabel, &offsetSlider, &calibrateButton, &latencyLabel, &hintLabel,
                                                             &saveButton, &loadButton })
        addAndMakeVisible (*c);

    for (int t = 0; t < Looper::numTracks; ++t)
    {
        rows[(size_t) t] = std::make_unique<TrackRow> (looper, t);
        addAndMakeVisible (*rows[(size_t) t]);
    }

    // Drum machine
    drumSection = std::make_unique<DrumSection> (engine);
    drumSection->onClicksMutedChange = [this] { saveSettings(); };
    addAndMakeVisible (*drumSection);

    if (auto xml = juce::parseXML (getLooperSettingsFile()))
        drumSection->setEditorShown (xml->getBoolAttribute ("drumEditorShown", false));

    drumSection->onLayoutChange = [this] { resized(); saveSettings(); };

    startTimerHz (30);
}

LooperPanel::~LooperPanel()
{
    if (calibrating)
        endCalibration();

    saveSettings();
}

//==============================================================================
void LooperPanel::updatePedalBadges()
{
    auto& footswitch = *engine.footswitch;

    for (int t = 0; t < Looper::numTracks; ++t)
    {
        juce::StringArray letters, details;

        for (int i = 0; i < Footswitch::numSwitches; ++i)
        {
            // The Looper mode actions (in Scene mode the pedal plays scenes until switched back).
            const auto& sw = footswitch.getSwitch (i);
            const auto& tap = sw.tap[Footswitch::looperMode];
            const auto& hold = sw.hold[Footswitch::looperMode];
            const auto tapsThis = tap.kind == FootswitchAction::Kind::trackPress && tap.track == t;

            if (! sw.trigger.isValid() || ! tapsThis)
                continue;

            letters.add (Footswitch::switchName (i));
            details.add (sw.momentary && hold.kind != FootswitchAction::Kind::none
                             ? tr ("Footswitch {name}: tap = record / overdub / play, hold = {action}", Footswitch::switchName (i), hold.describe())
                             : tr ("Footswitch {name}: tap = record / overdub / play", Footswitch::switchName (i)));
        }

        rows[(size_t) t]->setPedal (letters.isEmpty() ? juce::String() : tr ("pedal {letters}", letters.joinIntoString ("+")),
                                    details.joinIntoString ("\n"));
    }
}

//==============================================================================
void LooperPanel::showFileMessage (const juce::String& text)
{
    fileMessage = text;
    fileMessageMs = juce::Time::getMillisecondCounterHiRes();
}

void LooperPanel::saveLoops()
{
    LoopSet set;
    juce::String error;

    if (! looper.copyLoops (set, error))
    {
        showFileMessage (tr ("Not saved: {error}", error));
        return;
    }

    set.drums = std::shared_ptr<juce::XmlElement> (engine.drums->createStateXml().release());
    const auto folder = LoopFiles::createSessionFolder (set);
    const auto result = LoopFiles::save (set, folder);

    if (result.failed())
    {
        showFileMessage (tr ("Not saved: {error}", result.getErrorMessage()));
        juce::Logger::writeToLog ("Saving loops failed: " + result.getErrorMessage());
        return;
    }

    juce::Logger::writeToLog ("Saved " + juce::String (set.getNumLoops()) + " loop(s) to " + folder.getFullPathName());
    showFileMessage (trn ("Saved {n} loop as \"{name}\" (Loops folder: track WAVs + mix.wav).",
                          "Saved {n} loops as \"{name}\" (Loops folder: track WAVs + mix.wav).", set.getNumLoops(), folder.getFileName()));
}

void LooperPanel::showLoadMenu()
{
    const auto sessions = LoopFiles::listSessions();
    juce::PopupMenu menu;

    if (sessions.isEmpty())
        menu.addItem (-1, tr ("No saved loops yet"), false);

    if (! looper.isEmpty())
        menu.addSectionHeader (tr ("Clear all loops before loading"));

    for (int i = 0; i < juce::jmin (30, sessions.size()); ++i)
        menu.addItem (i + 1, LoopFiles::describe (sessions[i]), looper.isEmpty() && ! looper.isLoading());

   #if ! JUCE_IOS
    menu.addSeparator();
    menu.addItem (1000, tr ("Show the Loops folder"));
   #endif

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (loadButton),
                        [safeThis = SafePointer<LooperPanel> (this), sessions] (int chosen)
    {
        if (safeThis == nullptr || chosen == 0)
            return;

        if (chosen == 1000)
            LoopFiles::getLoopsFolder().revealToUser();
        else if (juce::isPositiveAndBelow (chosen - 1, sessions.size()))
            safeThis->loadLoops (sessions[chosen - 1]);
    });
}

void LooperPanel::loadLoops (const juce::File& folder)
{
    LoopSet set;
    const auto result = LoopFiles::load (folder, set);
    juce::String error;

    if (result.failed())
        error = result.getErrorMessage();
    else
        looper.loadLoops (set, error);

    if (error.isNotEmpty())
    {
        showFileMessage (tr ("Not loaded: {error}", error));
        juce::Logger::writeToLog ("Loading loops from " + folder.getFullPathName() + " failed: " + error);
        return;
    }

    // Record mode, tempo and drums come from the session.
    if (set.drums != nullptr)
    {
        engine.drums->applyStateXml (*set.drums);
        drumSection->patternChangedElsewhere();
    }

    (set.source == Looper::Source::beforeFx ? beforeFxButton : afterFxButton).setToggleState (true, juce::dontSendNotification);
    saveSettings();
    juce::Logger::writeToLog ("Loaded loops from " + folder.getFullPathName());
    showFileMessage (tr ("Loaded \"{name}\": metronome set to {bpm} bpm, {beats} beats; the loops play from its first downbeat.",
                         folder.getFileName(), i18n::number (set.getBpm(), 1), set.beatsPerBar));
}

//==============================================================================
namespace
{
    constexpr int warmUpHits = 2;           // the first strums are often off: ignored
    constexpr int calibrationHits = 12;
    constexpr double calibrationTimeoutMs = 45000.0;
}

void LooperPanel::startCalibration()
{
    calibrating = true;
    calibrationLags.clear();
    calibrationStartMs = juce::Time::getMillisecondCounterHiRes();
    calibrationMessage.clear();

    // The clicks must be audible and follow the bar clock.
    followSongBefore = metronome.followSong.load();
    clicksMutedBefore = metronome.clicksMuted.load();
    metronome.followSong = false;
    metronome.clicksMuted = false;
    stopMetronomeAfterCalibration = ! metronome.isRunning();

    if (stopMetronomeAfterCalibration)
        metronome.start();

    looper.calibrator.start();
    calibrateButton.setButtonText (tr ("Cancel"));
}

void LooperPanel::endCalibration()
{
    looper.calibrator.stop();
    calibrating = false;
    calibrationMessageMs = juce::Time::getMillisecondCounterHiRes();
    metronome.followSong = followSongBefore;
    metronome.clicksMuted = clicksMutedBefore;

    if (stopMetronomeAfterCalibration)
        metronome.stop();

    calibrateButton.setButtonText (tr ("Calibrate..."));
}

void LooperPanel::updateCalibration()
{
    if (! calibrating)
        return;

    looper.calibrator.readLags (calibrationLags);
    const auto hits = (int) calibrationLags.size() - warmUpHits;

    if (hits < calibrationHits)
    {
        if (juce::Time::getMillisecondCounterHiRes() - calibrationStartMs > calibrationTimeoutMs)
        {
            calibrationMessage = calibrationLags.empty()
                                   ? tr ("Calibration stopped: no notes detected. Check that your instrument input is working (meter in the Rig tab).")
                                   : tr ("Calibration stopped: only {count} notes detected in 45 s. Try again.", (int) calibrationLags.size());
            endCalibration();
        }

        return;
    }

    const auto result = LatencyCalibrator::analyse ({ calibrationLags.begin() + warmUpHits, calibrationLags.end() });
    const auto rate = juce::jmax (1.0, looper.getSampleRate());
    const auto deviceMs = looper.deviceLatencySamples.load() * 1000.0 / rate;

    if (result.ok)
    {
        const auto offset = juce::jlimit (-100.0, 100.0, result.lagMs - deviceMs);
        offsetSlider.setValue (offset, juce::sendNotificationSync);
        saveSettings();
        calibrationMessage = tr ("Calibrated: your notes arrive {lag} ms after the click (spread {spread} ms); the audio device reports {device} ms, "
                                 "so the fine-tune is now {offset} ms.",
                                 i18n::decimal (result.lagMs, 1), i18n::decimal (result.spreadMs, 1), i18n::decimal (deviceMs, 1), i18n::decimal (offset, 1));
    }
    else
    {
        calibrationMessage = tr ("Calibration failed: {problem}. Try again with short muted notes right on the clicks.", result.problem);
    }

    endCalibration();
}

//==============================================================================
void LooperPanel::loadSettings()
{
    if (auto xml = juce::parseXML (getLooperSettingsFile()))
    {
        looper.source = juce::jlimit (0, 1, xml->getIntAttribute ("source", 0));
        looper.fixedBars = juce::jlimit (0, 16, xml->getIntAttribute ("fixedBars", 0));
        looper.latencyOffsetMs = (float) juce::jlimit (-100.0, 100.0, xml->getDoubleAttribute ("latencyOffsetMs", 0.0));
        metronome.clicksMuted = xml->getBoolAttribute ("clicksMuted", false);
    }
}

void LooperPanel::saveSettings() const
{
    juce::XmlElement xml ("PRACTICE_VIBES_LOOPER");
    xml.setAttribute ("source", looper.source.load());
    xml.setAttribute ("fixedBars", looper.fixedBars.load());
    xml.setAttribute ("latencyOffsetMs", (double) looper.latencyOffsetMs.load());
    xml.setAttribute ("clicksMuted", metronome.clicksMuted.load());
    xml.setAttribute ("drumEditorShown", drumSection != nullptr && drumSection->isEditorShown());
    xml.writeTo (getLooperSettingsFile());
}

bool LooperPanel::keyPressed (const juce::KeyPress& key)
{
    const auto c = key.getTextCharacter();

    if (c >= '1' && c < '1' + Looper::numTracks)
    {
        looper.pressTrack (c - '1');
        return true;
    }

    return false;
}

void LooperPanel::timerCallback()
{
    for (auto& row : rows)
        row->update();

    updatePedalBadges();
    beatDots->update();

    const auto empty = looper.isEmpty();
    const auto locked = metronome.isTempoLocked();
    const auto beforeFx = looper.getActiveSource() == Looper::Source::beforeFx;

    // The record mode can only change while the looper is empty.
    afterFxButton.setEnabled (empty);
    beforeFxButton.setEnabled (empty);

    if (! empty)
        (beforeFx ? beforeFxButton : afterFxButton).setToggleState (true, juce::dontSendNotification);

    tempoSlider.setEnabled (! locked);
    beatsBox.setEnabled (! locked);
    tempoLabel.setEnabled (! locked);
    beatsLabel.setEnabled (! locked);

    // The Practice tab edits the same metronome.
    if (! tempoSlider.isMouseButtonDown())
        tempoSlider.setValue (metronome.startBpm.load(), juce::dontSendNotification);

    if (beatsBox.getSelectedId() != metronome.beatsPerBar.load() && ! beatsBox.isPopupActive())
        beatsBox.setSelectedId (metronome.beatsPerBar.load(), juce::dontSendNotification);

    undoButton.setEnabled (looper.canUndo());
    clearAllButton.setEnabled (! empty);
    muteAllButton.setEnabled (! empty);
    metronomeButton.setButtonText (metronome.isRunning() ? tr ("Stop metronome") : tr ("Start metronome"));

    const auto running = metronome.isRunning();
    tempoDisplay.setText (juce::String (juce::roundToInt (running ? metronome.getCurrentBpm() : metronome.startBpm.load())) + " bpm",
                          juce::dontSendNotification);

    const auto rate = juce::jmax (1.0, looper.getSampleRate());
    latencyLabel.setText (tr ("Compensation {ms} ms (device + plugins + fine-tune)", i18n::decimal (looper.getCompensationSamples() * 1000.0 / rate, 1)),
                          juce::dontSendNotification);

    updateCalibration();

    if (! calibrating && calibrationMessage.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() - calibrationMessageMs > 30000.0)
        calibrationMessage.clear();

    if (fileMessage.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() - fileMessageMs > 15000.0)
        fileMessage.clear();

    saveButton.setEnabled (! empty);

    juce::String hint;

    if (calibrating)
    {
        const auto hits = juce::jmax (0, (int) calibrationLags.size() - warmUpHits);
        hint = tr ("Calibrating: play a short muted note (or strum) exactly on every click (or connect the output to the input with a cable).")
               + " " + juce::String (juce::jmin (hits, calibrationHits)) + " / " + juce::String (calibrationHits)
               + (calibrationLags.size() < (size_t) warmUpHits ? " " + tr ("(warming up)") : juce::String());
    }
    else if (fileMessage.isNotEmpty())
    {
        hint = fileMessage;
    }
    else if (calibrationMessage.isNotEmpty())
    {
        hint = calibrationMessage;
    }
    else if (looper.isLoading())
    {
        hint = tr ("Loaded loops start with the metronome.");
    }
    else if (locked)
        hint = tr ("Tempo and bar length are locked while there are loops. Clear all to change them.");
    else if (empty)
        hint = tr ("Tap Record (or press 1-4): recording starts on the nearest bar line. "
                   "If the metronome is stopped it starts, with one bar of count-in.");

    if (beforeFx && engine.chain->getOutputStage().muted.load())
        hint << (hint.isEmpty() ? "" : "\n")
             << tr ("Before-FX loops play through the rig: switch Monitor on in the Rig tab to hear them.");

    if (metronome.followSong.load() && locked)
        hint << (hint.isEmpty() ? "" : "\n") << tr ("\"Follow song beats\" is paused while there are loops.");

    hintLabel.setText (hint, juce::dontSendNotification);

}

//==============================================================================
void LooperPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
}

void LooperPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);

    auto top = area.removeFromTop (44);
    header.setBounds (top.removeFromLeft (90));
    sourceLabel.setBounds (top.removeFromLeft (64));
    afterFxButton.setBounds (top.removeFromLeft (100).reduced (2, 4));
    beforeFxButton.setBounds (top.removeFromLeft (100).reduced (2, 4));
    top.removeFromLeft (16);
    lengthLabel.setBounds (top.removeFromLeft (60));
    lengthBox.setBounds (top.removeFromLeft (220).reduced (2, 6));

    // Undo / Mute all / Clear all / Footswitch need 450 px: when the first row can't give them that
    // (iPad portrait), they get a row of their own instead of being squeezed into unreadable slivers.
    constexpr int actionsWidth = 130 + 100 + 100 + 120;
    const bool narrow = top.getWidth() < actionsWidth;
    auto actions = top;

    if (narrow)
    {
        area.removeFromTop (2);
        actions = area.removeFromTop (44);
        actions = actions.removeFromRight (juce::jmin (actions.getWidth(), 4 * 150));
    }

    const int unit = narrow ? actions.getWidth() / 4 : 0;
    footswitchButton.setBounds (actions.removeFromRight (narrow ? unit : 120).reduced (3, 4));
    clearAllButton.setBounds (actions.removeFromRight (narrow ? unit : 100).reduced (3, 4));
    muteAllButton.setBounds (actions.removeFromRight (narrow ? unit : 100).reduced (3, 4));
    undoButton.setBounds (actions.removeFromRight (narrow ? unit : 130).reduced (3, 4));

    area.removeFromTop (4);
    auto metro = area.removeFromTop (44);
    metronomeButton.setBounds (metro.removeFromLeft (170).reduced (2, 4));
    tempoDisplay.setBounds (metro.removeFromRight (narrow ? 110 : 130));
    beatDots->setBounds (metro.removeFromRight (narrow ? 110 : 160).reduced (4, 8));
    tempoLabel.setBounds (metro.removeFromLeft (70).withTrimmedLeft (12));
    beatsBox.setBounds (metro.removeFromRight (70).reduced (2, 6));
    beatsLabel.setBounds (metro.removeFromRight (110));
    tempoSlider.setBounds (metro.reduced (0, 6));

    area.removeFromTop (4);
    area.removeFromTop (4);
    drumSection->setBounds (area.removeFromTop (drumSection->getPreferredHeight()));
    area.removeFromTop (8);

    auto bottom = area.removeFromBottom (36);
    offsetLabel.setBounds (bottom.removeFromLeft (140));
    offsetSlider.setBounds (bottom.removeFromLeft (juce::jmin (360, bottom.getWidth() / 2)).reduced (0, 4));
    calibrateButton.setBounds (bottom.removeFromLeft (120).reduced (6, 3));
    loadButton.setBounds (bottom.removeFromRight (120).reduced (3));
    saveButton.setBounds (bottom.removeFromRight (110).reduced (3));
    latencyLabel.setBounds (bottom.reduced (12, 0));

    hintLabel.setBounds (area.removeFromBottom (drumSection->isEditorShown() ? 36 : 54));

    // The pattern editor takes room from the tracks: they get slimmer, but all four stay visible.
    const auto rowHeight = juce::jlimit (drumSection->isEditorShown() ? 34 : 60, 110, area.getHeight() / Looper::numTracks);

    for (auto& row : rows)
        row->setBounds (area.removeFromTop (rowHeight).reduced (0, 3));
}

} // namespace pv
