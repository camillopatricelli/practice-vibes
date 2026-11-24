#include "Footswitch.h"
#include "Stems.h"
#include "Localisation.h"

namespace pv
{

//==============================================================================
MidiTrigger MidiTrigger::fromMessage (const juce::MidiMessage& m)
{
    MidiTrigger t;

    if (m.isController())
        t = { Type::controller, m.getChannel(), m.getControllerNumber() };
    else if (m.isNoteOnOrOff())
        t = { Type::note, m.getChannel(), m.getNoteNumber() };
    else if (m.isProgramChange())
        t = { Type::program, m.getChannel(), m.getProgramChangeNumber() };

    return t;
}

juce::String MidiTrigger::describe() const
{
    const auto ch = " (ch " + juce::String (channel) + ")";

    switch (type)
    {
        case Type::controller: return "CC " + juce::String (number) + ch;
        case Type::note:       return tr ("Note {note}", juce::MidiMessage::getMidiNoteName (number, true, true, 3)) + ch;
        case Type::program:    return tr ("Program {number}", number) + ch;
        case Type::none:       break;
    }

    return tr ("not learned");
}

juce::String MidiTrigger::toString() const
{
    switch (type)
    {
        case Type::controller: return "cc:" + juce::String (channel) + ":" + juce::String (number);
        case Type::note:       return "note:" + juce::String (channel) + ":" + juce::String (number);
        case Type::program:    return "pc:" + juce::String (channel) + ":" + juce::String (number);
        case Type::none:       break;
    }

    return {};
}

MidiTrigger MidiTrigger::fromString (const juce::String& text)
{
    const auto parts = juce::StringArray::fromTokens (text, ":", {});

    if (parts.size() != 3)
        return {};

    MidiTrigger t;
    t.type = parts[0] == "cc" ? Type::controller : parts[0] == "note" ? Type::note : parts[0] == "pc" ? Type::program : Type::none;
    t.channel = juce::jlimit (1, 16, parts[1].getIntValue());
    t.number = juce::jlimit (0, 127, parts[2].getIntValue());
    return t;
}

//==============================================================================
juce::String FootswitchAction::describe() const
{
    switch (kind)
    {
        case Kind::trackPress: return tr ("Track {n}: record / overdub / play", track + 1);
        case Kind::trackClear: return tr ("Track {n}: clear", track + 1);
        case Kind::trackMute:  return tr ("Track {n}: mute / unmute", track + 1);
        case Kind::undo:       return tr ("Undo last overdub");
        case Kind::muteAll:    return tr ("Mute / unmute all tracks");
        case Kind::clearAll:   return tr ("Clear all tracks");
        case Kind::metronome:  return tr ("Start / stop metronome");
        case Kind::bypass:     return tr ("Rig: bypass selected plugin");
        case Kind::tuner:      return tr ("Tuner on / off");
        case Kind::record:     return tr ("Record a take (start / stop)");
        case Kind::stem:       return tr ("Mute / unmute the song's {stem} stem", Stems::displayName (track).toLowerCase());
        case Kind::scene:      return tr ("Rig: scene {n}", track + 1);
        case Kind::sceneNext:  return tr ("Rig: next scene");
        case Kind::scenePrevious: return tr ("Rig: previous scene");
        case Kind::switchMode: return tr ("Switch footswitch mode (looper / scenes)");
        case Kind::none:       break;
    }

    return tr ("Nothing");
}

juce::String FootswitchAction::toString() const
{
    switch (kind)
    {
        case Kind::trackPress: return "press:" + juce::String (track);
        case Kind::trackClear: return "clear:" + juce::String (track);
        case Kind::trackMute:  return "mute:" + juce::String (track);
        case Kind::undo:       return "undo";
        case Kind::muteAll:    return "muteAll";
        case Kind::clearAll:   return "clearAll";
        case Kind::metronome:  return "metronome";
        case Kind::bypass:     return "bypass";
        case Kind::tuner:      return "tuner";
        case Kind::record:     return "record";
        case Kind::stem:       return "stem:" + Stems::fileStem (track);
        case Kind::scene:      return "scene:" + juce::String (track);
        case Kind::sceneNext:  return "sceneNext";
        case Kind::scenePrevious: return "scenePrevious";
        case Kind::switchMode: return "switchMode";
        case Kind::none:       break;
    }

    return "none";
}

FootswitchAction FootswitchAction::fromString (const juce::String& text)
{
    const auto name = text.upToFirstOccurrenceOf (":", false, false);
    const auto track = juce::jlimit (0, 15, text.fromFirstOccurrenceOf (":", false, false).getIntValue());

    if (name == "press")     return { Kind::trackPress, track };
    if (name == "clear")     return { Kind::trackClear, track };
    if (name == "mute")      return { Kind::trackMute, track };
    if (name == "undo")      return { Kind::undo, 0 };
    if (name == "muteAll")   return { Kind::muteAll, 0 };
    if (name == "clearAll")  return { Kind::clearAll, 0 };
    if (name == "metronome") return { Kind::metronome, 0 };
    if (name == "bypass")    return { Kind::bypass, 0 };
    if (name == "tuner")     return { Kind::tuner, 0 };
    if (name == "record")    return { Kind::record, 0 };
    if (name == "scene")     return { Kind::scene, track };
    if (name == "sceneNext") return { Kind::sceneNext, 0 };
    if (name == "scenePrevious") return { Kind::scenePrevious, 0 };
    if (name == "switchMode") return { Kind::switchMode, 0 };
    if (name == "guitarStem") return { Kind::stem, Stems::guitar };     // before the stem could be chosen

    if (name == "stem")
        for (int s = 0; s < Stems::count; ++s)
            if (Stems::fileStem (s) == text.fromFirstOccurrenceOf (":", false, false))
                return { Kind::stem, s };

    return {};
}

std::vector<FootswitchAction> FootswitchAction::all (int numTracks, int numScenes)
{
    std::vector<FootswitchAction> list { { Kind::none, 0 } };

    for (auto kind : { Kind::trackPress, Kind::trackClear, Kind::trackMute })
        for (int t = 0; t < numTracks; ++t)
            list.push_back ({ kind, t });

    for (auto kind : { Kind::undo, Kind::muteAll, Kind::clearAll, Kind::metronome, Kind::bypass, Kind::tuner, Kind::record })
        list.push_back ({ kind, 0 });

    for (auto s : Stems::displayOrder)
        list.push_back ({ Kind::stem, s });

    for (int s = 0; s < numScenes; ++s)
        list.push_back ({ Kind::scene, s });

    for (auto kind : { Kind::sceneNext, Kind::scenePrevious, Kind::switchMode })
        list.push_back ({ kind, 0 });

    return list;
}

//==============================================================================
Footswitch::Footswitch()
{
    setDefaultActions();
}

Footswitch::~Footswitch()
{
    stopTimer();
}

FootswitchAction Footswitch::defaultTap (Mode m, int index, bool momentary)
{
    using Kind = FootswitchAction::Kind;

    if (index == numSwitches - 1 && ! momentary)
        return { Kind::switchMode, 0 };     // a latching switch can't hold: the last one changes the mode

    return m == sceneMode ? FootswitchAction { Kind::scene, index } : FootswitchAction { Kind::trackPress, index };
}

FootswitchAction Footswitch::defaultHold (Mode m, int index, bool momentary)
{
    using Kind = FootswitchAction::Kind;

    if (! momentary)
        return {};

    if (index == numSwitches - 1)
        return { Kind::switchMode, 0 };

    return m == sceneMode ? FootswitchAction {} : FootswitchAction { Kind::trackClear, index };
}

void Footswitch::setDefaultActions()
{
    for (int i = 0; i < numSwitches; ++i)
    {
        auto& sw = switches[(size_t) i];

        for (int m = 0; m < numModes; ++m)
        {
            sw.tap[(size_t) m]  = defaultTap ((Mode) m, i, sw.momentary);
            sw.hold[(size_t) m] = defaultHold ((Mode) m, i, sw.momentary);
        }
    }

    changed();
}

void Footswitch::adaptDefaults (int index, bool wasMomentary)
{
    auto& sw = switches[(size_t) index];

    if (sw.momentary == wasMomentary)
        return;

    for (int m = 0; m < numModes; ++m)
    {
        const auto page = (Mode) m;

        if (sw.tap[(size_t) m] == defaultTap (page, index, wasMomentary)
            && sw.hold[(size_t) m] == defaultHold (page, index, wasMomentary))
        {
            sw.tap[(size_t) m]  = defaultTap (page, index, sw.momentary);
            sw.hold[(size_t) m] = defaultHold (page, index, sw.momentary);
        }
    }
}

void Footswitch::setTapAction (int index, FootswitchAction action, Mode m)
{
    switches[(size_t) index].tap[(size_t) m] = action;
    changed();
}

void Footswitch::setHoldAction (int index, FootswitchAction action, Mode m)
{
    switches[(size_t) index].hold[(size_t) m] = action;
    changed();
}

void Footswitch::forget (int index)
{
    if (pending == index)
        pending = -1;

    auto& sw = switches[(size_t) index];
    const auto wasMomentary = sw.momentary;
    sw.trigger = {};
    sw.momentary = true;
    adaptDefaults (index, wasMomentary);
    runtime[(size_t) index] = {};
    changed();
}

void Footswitch::setMode (Mode newMode)
{
    if (newMode == mode || ! juce::isPositiveAndBelow ((int) newMode, (int) numModes))
        return;

    mode = newMode;
    juce::Logger::writeToLog ("Footswitch mode: " + juce::String (mode == sceneMode ? "scenes" : "looper"));
}

juce::String Footswitch::modeName (Mode m)
{
    return m == sceneMode ? tr ("Scene mode") : tr ("Looper mode");
}

int Footswitch::encode (const MidiTrigger& t) noexcept
{
    return t.isValid() ? ((int) t.type << 16) | (t.channel << 8) | t.number : 0;
}

void Footswitch::publish()
{
    for (int i = 0; i < numSwitches; ++i)
        publishedTriggers[(size_t) i] = encode (switches[(size_t) i].trigger);

    publishedLearning = learning >= 0 || pending >= 0;
}

bool Footswitch::isFootswitchMessage (const juce::MidiMessage& message) const noexcept
{
    const auto code = encode (MidiTrigger::fromMessage (message));

    if (code == 0)
        return false;

    if (publishedLearning.load())
        return true;

    for (auto& published : publishedTriggers)
        if (published.load() == code)
            return true;

    return false;
}

void Footswitch::changed()
{
    publish();

    if (onChange)
        onChange();
}

//==============================================================================
double Footswitch::timeNow()
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

bool Footswitch::handleMidi (const juce::MidiMessage& message)
{
    // MIDI input timestamps use the same clock; a message that waited in the queue is dated
    // from its arrival, so holds are measured from the real press.
    const auto now = timeNow();
    const auto stamp = message.getTimeStamp();
    return handleMidi (message, stamp > 0.0 && stamp <= now && now - stamp < 2.0 ? stamp : now);
}

bool Footswitch::handleMidi (const juce::MidiMessage& message, double now)
{
    lastNow = now;
    const auto trigger = MidiTrigger::fromMessage (message);

    if (! trigger.isValid())
        return false;

    const auto pressed = message.isController() ? message.getControllerValue() >= 64
                       : message.isNoteOnOrOff() ? message.isNoteOn()
                                                 : true;

    // Just learned a CC switch: a quick release means it is momentary.
    if (pending >= 0 && switches[(size_t) pending].trigger == trigger)
    {
        if (! pressed)
            finishPending (true);

        return true;
    }

    if (learning >= 0)
    {
        const auto index = learning;
        learning = -1;

        for (int i = 0; i < numSwitches; ++i)
            if (i != index && switches[(size_t) i].trigger == trigger)
                switches[(size_t) i].trigger = {};      // one MIDI message drives one switch

        auto& sw = switches[(size_t) index];
        const auto wasMomentary = sw.momentary;
        sw.trigger = trigger;
        runtime[(size_t) index] = {};
        runtime[(size_t) index].activity = now;

        if (trigger.type == MidiTrigger::Type::controller && pressed)
        {
            sw.momentary = false;       // until a release shows up
            pendingWasMomentary = wasMomentary;
            pending = index;
            pendingSince = now;
            startTimerHz (30);
        }
        else
        {
            // A CC that starts at 0 is a latching switch that was "on"; notes have note-offs.
            sw.momentary = trigger.type == MidiTrigger::Type::note;
            adaptDefaults (index, wasMomentary);
            juce::Logger::writeToLog ("Footswitch " + switchName (index) + " learned: " + trigger.describe()
                                      + (sw.momentary ? " (momentary)" : " (one message per stomp)"));
        }

        changed();
        return true;
    }

    for (int i = 0; i < numSwitches; ++i)
    {
        auto& sw = switches[(size_t) i];

        if (sw.trigger != trigger)
            continue;

        auto& rt = runtime[(size_t) i];

        if (! sw.momentary)
        {
            rt.activity = now;
            fire (i, sw.tap[(size_t) mode], "tap", now);
        }
        else if (pressed)
        {
            const auto& hold = sw.hold[(size_t) mode];
            rt.down = true;
            rt.holdFired = false;
            rt.pressMode = mode;
            rt.downTime = now;
            rt.activity = now;
            rt.tapOnRelease = hold.kind == FootswitchAction::Kind::switchMode;

            if (! rt.tapOnRelease)
                fire (i, sw.tap[(size_t) mode], "tap", now);

            if (hold.kind != FootswitchAction::Kind::none)
                startTimerHz (30);
        }
        else
        {
            if (rt.down && rt.tapOnRelease && ! rt.holdFired)
                fire (i, sw.tap[(size_t) rt.pressMode], "tap", rt.downTime);

            rt.down = false;
            rt.tapOnRelease = false;
        }

        return true;
    }

    return false;
}

void Footswitch::update (double now)
{
    lastNow = now;
    bool waiting = false;

    if (pending >= 0)
    {
        if (now - pendingSince >= learnReleaseWindow)
            finishPending (false);
        else
            waiting = true;
    }

    for (int i = 0; i < numSwitches; ++i)
    {
        auto& rt = runtime[(size_t) i];
        const auto& sw = switches[(size_t) i];

        const auto& hold = sw.hold[(size_t) rt.pressMode];

        if (! rt.down || rt.holdFired || ! sw.momentary || hold.kind == FootswitchAction::Kind::none)
            continue;

        if (now - rt.downTime >= holdSeconds)
        {
            rt.holdFired = true;
            rt.activity = now;
            fire (i, hold, "hold", 0.0);
        }
        else
        {
            waiting = true;
        }
    }

    if (! waiting)
        stopTimer();
}

void Footswitch::timerCallback()
{
    update (timeNow());
}

void Footswitch::finishPending (bool momentary)
{
    const auto index = pending;
    pending = -1;

    if (index < 0)
        return;

    auto& sw = switches[(size_t) index];
    sw.momentary = momentary;
    adaptDefaults (index, pendingWasMomentary);
    juce::Logger::writeToLog ("Footswitch " + switchName (index) + " learned: " + sw.trigger.describe()
                              + (momentary ? " (momentary: tap + hold)" : " (latching: every stomp is a tap)"));
    changed();
}

void Footswitch::fire (int index, const FootswitchAction& action, const char* gesture, double pressSeconds)
{
    if (action.kind == FootswitchAction::Kind::none)
        return;

    juce::Logger::writeToLog ("Footswitch " + switchName (index) + " " + gesture + ": " + action.describe());

    if (action.kind == FootswitchAction::Kind::switchMode)
        setMode (mode == looperMode ? sceneMode : looperMode);
    else if (perform)
        perform (action, pressSeconds);
}

//==============================================================================
void Footswitch::startLearning (int index)
{
    if (pending >= 0)
        finishPending (false);

    learning = juce::isPositiveAndBelow (index, numSwitches) ? index : -1;
    changed();
}

void Footswitch::cancelLearning()
{
    if (learning >= 0)
    {
        learning = -1;
        changed();
    }
}

double Footswitch::secondsSinceActivity (int index) const
{
    return timeNow() - runtime[(size_t) index].activity;
}

std::unique_ptr<juce::XmlElement> Footswitch::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("PRACTICE_VIBES_FOOTSWITCH");
    xml->setAttribute ("version", 2);

    for (int i = 0; i < numSwitches; ++i)
    {
        const auto& sw = switches[(size_t) i];
        auto* e = xml->createNewChildElement ("SWITCH");
        e->setAttribute ("index", i);
        e->setAttribute ("trigger", sw.trigger.toString());
        e->setAttribute ("momentary", sw.momentary);
        e->setAttribute ("tap", sw.tap[looperMode].toString());
        e->setAttribute ("hold", sw.hold[looperMode].toString());
        e->setAttribute ("sceneTap", sw.tap[sceneMode].toString());
        e->setAttribute ("sceneHold", sw.hold[sceneMode].toString());
    }

    return xml;
}

void Footswitch::fromXml (const juce::XmlElement& xml)
{
    // Version 1 (before scenes) had only the Looper mode actions.
    const auto beforeModes = xml.getIntAttribute ("version", 1) < 2;

    for (auto* e : xml.getChildWithTagNameIterator ("SWITCH"))
    {
        const auto i = e->getIntAttribute ("index", -1);

        if (! juce::isPositiveAndBelow (i, numSwitches))
            continue;

        auto& sw = switches[(size_t) i];
        sw.trigger = MidiTrigger::fromString (e->getStringAttribute ("trigger"));
        sw.momentary = e->getBoolAttribute ("momentary", true);

        for (int m = 0; m < numModes; ++m)
        {
            sw.tap[(size_t) m]  = defaultTap ((Mode) m, i, sw.momentary);
            sw.hold[(size_t) m] = defaultHold ((Mode) m, i, sw.momentary);
        }

        const auto tap  = FootswitchAction::fromString (e->getStringAttribute ("tap", sw.tap[looperMode].toString()));
        const auto hold = FootswitchAction::fromString (e->getStringAttribute ("hold", sw.hold[looperMode].toString()));

        // The old defaults on the last switch (track 4, hold = clear) make way for the mode switch.
        const auto oldDefaults = beforeModes && i == numSwitches - 1
                              && tap == FootswitchAction { FootswitchAction::Kind::trackPress, i }
                              && (hold == FootswitchAction { FootswitchAction::Kind::trackClear, i } || ! sw.momentary);

        if (! oldDefaults)
        {
            sw.tap[looperMode] = tap;
            sw.hold[looperMode] = hold;
        }

        if (! beforeModes)
        {
            sw.tap[sceneMode]  = FootswitchAction::fromString (e->getStringAttribute ("sceneTap", sw.tap[sceneMode].toString()));
            sw.hold[sceneMode] = FootswitchAction::fromString (e->getStringAttribute ("sceneHold", sw.hold[sceneMode].toString()));
        }
    }

    publish();
}

} // namespace pv
