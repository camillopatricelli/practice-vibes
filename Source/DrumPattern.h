#pragma once

#include <juce_core/juce_core.h>
#include "DrumKit.h"

namespace pv
{

/** A drum pattern: up to maxBars bars of beatsPerBar beats, each beat split into `ticks` steps
    (2 = 8ths, 3 = triplets, 4 = 16ths). Every step holds a level per drum.

    Text form (files, starter patterns): one line per drum, one character per step:
    '.' off, 'g' ghost, 'x' normal, 'X' accent; bars separated by '|', spaces ignored.
*/
struct DrumPattern
{
    enum Level : juce::uint8 { off = 0, ghost = 1, normal = 2, accent = 3 };

    static constexpr int maxBars = 8;
    static constexpr int maxBeats = 12;
    static constexpr int maxTicks = 4;
    static constexpr int maxSteps = maxBeats * maxTicks;

    juce::String name { "New pattern" };
    bool builtIn = false;
    int beatsPerBar = 4, ticks = 4, bars = 1;
    double swing = 0.5;         // 0.5 straight .. 0.75 (8ths and 16ths only), as the metronome's

    DrumPattern();

    int stepsPerBar() const noexcept                        { return beatsPerBar * ticks; }
    juce::uint8 get (int bar, int step, Drum drum) const noexcept;
    void set (int bar, int step, Drum drum, juce::uint8 level) noexcept;
    void clear() noexcept;
    bool isEmpty() const noexcept;

    /** Changes the shape, keeping what still fits: steps stay at the same place in the beat
        (16ths that fall between 8ths are dropped), new bars repeat the existing ones. */
    void reshape (int newBeats, int newTicks, int newBars);

    /** Copies bar `from` over every other bar. */
    void copyBarToAll (int from) noexcept;

    juce::String rowToText (Drum) const;
    void rowFromText (Drum, const juce::String&);

    std::unique_ptr<juce::XmlElement> toXml() const;
    static DrumPattern fromXml (const juce::XmlElement&);

    bool sameNotes (const DrumPattern& other) const noexcept;

    // [bar][step][drum], fixed size so the sequencer can mirror it lock-free.
    std::array<juce::uint8, (size_t) (maxBars * maxSteps * numDrums)> cells {};
};

namespace DrumPatterns
{
    /** The patterns that come with the app, simplest first. */
    const std::vector<DrumPattern>& builtIn();

    /** Desktop: Documents/Practice Vibes/Drum patterns. iOS: "Drum patterns" in the app's Documents. */
    juce::File getUserFolder();

    /** The user's saved patterns, by name. */
    std::vector<DrumPattern> loadUserPatterns();

    /** Saves (or replaces) a pattern under its name. */
    juce::Result saveUserPattern (const DrumPattern&);
    bool deleteUserPattern (const juce::String& name);
    juce::File fileFor (const juce::String& name);
}

} // namespace pv
