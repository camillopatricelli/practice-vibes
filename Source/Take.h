#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <optional>
#include <vector>

namespace pv
{

/** What the song player was doing from a moment of a take on (until the next segment). */
struct TakeSegment
{
    double takeSeconds = 0.0;       // where in the take this segment starts
    double songSeconds = 0.0;       // song position heard at that moment
    double speed = 1.0, semitones = 0.0;
    bool playing = false;
    bool loopOn = false;
    double loopStart = 0.0, loopEnd = 0.0;

    /** Song position at take time t, if this segment's settings were in force then. */
    double songSecondsAt (double t) const noexcept
    {
        if (! playing)
            return songSeconds;

        auto position = songSeconds + (t - takeSeconds) * speed;

        if (loopOn && loopEnd - loopStart > 0.01 && position >= loopEnd)
            position = loopStart + std::fmod (position - loopStart, loopEnd - loopStart);

        return position;    // may be slightly negative right after a seek to 0 (stretcher latency)
    }

    bool sameSettings (const TakeSegment& o) const noexcept
    {
        return playing == o.playing && speed == o.speed && semitones == o.semitones && loopOn == o.loopOn
            && (! loopOn || (loopStart == o.loopStart && loopEnd == o.loopEnd));
    }
};

/** Maps take time to song position: the song moves at `speed` from each segment's start, wrapping around an
    active loop like the player does. Seeks, pauses and setting changes start a new segment. */
struct TakeTimeline
{
    std::vector<TakeSegment> segments;      // sorted by takeSeconds

    /** Index of the segment in force at take time t (the first one before it starts), -1 if none. */
    int segmentAt (double takeSeconds) const noexcept;

    /** Song position at take time t, following segment `index` (default: the one in force). */
    double songSecondsAt (double takeSeconds) const noexcept;
    double songSecondsAt (double takeSeconds, int index) const noexcept;

    /** The song played at some point of the take. */
    bool usesSong() const noexcept;

    void toXml (juce::XmlElement& parent) const;
    static TakeTimeline fromXml (const juce::XmlElement& parent);
};

/** A recorded take's details, kept next to its audio file in a hidden ".takes" folder
    (<folder>/.takes/<audio file name>.xml), so the library shows only the audio file. */
struct TakeInfo
{
    juce::Time recorded;
    double lengthSeconds = 0.0;
    juce::String topicId;
    juce::String songPath, songName;            // songPath: relative to the library (see PracticeSetup::portablePath)
    juce::String youTubeId, youTubeTitle;       // backing was a YouTube video (no sync, for information)
    TakeTimeline timeline;

    std::unique_ptr<juce::XmlElement> toXml() const;
    static TakeInfo fromXml (const juce::XmlElement&);

    static juce::File infoFileFor (const juce::File& audioFile);
    static std::optional<TakeInfo> load (const juce::File& audioFile);
    bool save (const juce::File& audioFile) const;

    /** "Take 2026-11-15 14.32 - Autumn Leaves" (no extension; characters a file name can't hold are removed). */
    static juce::String makeFileName (juce::Time when, const juce::String& what);

    static constexpr const char* folderName = ".takes";
};

} // namespace pv
