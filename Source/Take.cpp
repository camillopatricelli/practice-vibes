#include "Take.h"

namespace pv
{

//==============================================================================
int TakeTimeline::segmentAt (double t) const noexcept
{
    if (segments.empty())
        return -1;

    // Last segment starting at or before t; before the first one, the first one (extrapolated back).
    // (A microsecond of slack: take times are sums of block lengths, rounding must not pick the previous one.)
    const auto it = std::upper_bound (segments.begin(), segments.end(), t + 1.0e-6,
                                      [] (double value, const TakeSegment& s) { return value < s.takeSeconds; });
    return it == segments.begin() ? 0 : (int) std::distance (segments.begin(), it) - 1;
}

double TakeTimeline::songSecondsAt (double t) const noexcept
{
    return songSecondsAt (t, segmentAt (t));
}

double TakeTimeline::songSecondsAt (double t, int index) const noexcept
{
    return juce::isPositiveAndBelow (index, (int) segments.size()) ? segments[(size_t) index].songSecondsAt (t) : 0.0;
}

bool TakeTimeline::usesSong() const noexcept
{
    for (auto& s : segments)
        if (s.playing)
            return true;

    return false;
}

void TakeTimeline::toXml (juce::XmlElement& parent) const
{
    for (auto& s : segments)
    {
        auto* e = parent.createNewChildElement ("SEGMENT");
        e->setAttribute ("take", s.takeSeconds);
        e->setAttribute ("song", s.songSeconds);
        e->setAttribute ("playing", s.playing);
        e->setAttribute ("speed", s.speed);
        e->setAttribute ("semitones", s.semitones);

        if (s.loopOn)
        {
            e->setAttribute ("loopStart", s.loopStart);
            e->setAttribute ("loopEnd", s.loopEnd);
        }
    }
}

TakeTimeline TakeTimeline::fromXml (const juce::XmlElement& parent)
{
    TakeTimeline timeline;

    for (auto* e : parent.getChildWithTagNameIterator ("SEGMENT"))
    {
        TakeSegment s;
        s.takeSeconds = e->getDoubleAttribute ("take");
        s.songSeconds = e->getDoubleAttribute ("song");
        s.playing = e->getBoolAttribute ("playing");
        s.speed = juce::jlimit (0.25, 2.0, e->getDoubleAttribute ("speed", 1.0));
        s.semitones = juce::jlimit (-12.0, 12.0, e->getDoubleAttribute ("semitones"));
        s.loopOn = e->hasAttribute ("loopEnd");
        s.loopStart = e->getDoubleAttribute ("loopStart");
        s.loopEnd = e->getDoubleAttribute ("loopEnd");

        if (! timeline.segments.empty() && s.takeSeconds < timeline.segments.back().takeSeconds)
            continue;   // keep them sorted

        timeline.segments.push_back (s);
    }

    return timeline;
}

//==============================================================================
std::unique_ptr<juce::XmlElement> TakeInfo::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("TAKE");
    xml->setAttribute ("recorded", recorded.toISO8601 (true));
    xml->setAttribute ("length", lengthSeconds);

    if (topicId.isNotEmpty())       xml->setAttribute ("topic", topicId);
    if (songPath.isNotEmpty())      xml->setAttribute ("song", songPath);
    if (songName.isNotEmpty())      xml->setAttribute ("songName", songName);
    if (youTubeId.isNotEmpty())     xml->setAttribute ("youTube", youTubeId);
    if (youTubeTitle.isNotEmpty())  xml->setAttribute ("youTubeTitle", youTubeTitle);

    timeline.toXml (*xml);
    return xml;
}

TakeInfo TakeInfo::fromXml (const juce::XmlElement& xml)
{
    TakeInfo info;
    info.recorded = juce::Time::fromISO8601 (xml.getStringAttribute ("recorded"));
    info.lengthSeconds = xml.getDoubleAttribute ("length");
    info.topicId = xml.getStringAttribute ("topic");
    info.songPath = xml.getStringAttribute ("song");
    info.songName = xml.getStringAttribute ("songName");
    info.youTubeId = xml.getStringAttribute ("youTube");
    info.youTubeTitle = xml.getStringAttribute ("youTubeTitle");
    info.timeline = TakeTimeline::fromXml (xml);
    return info;
}

juce::File TakeInfo::infoFileFor (const juce::File& audioFile)
{
    return audioFile.getParentDirectory().getChildFile (folderName).getChildFile (audioFile.getFileName() + ".xml");
}

std::optional<TakeInfo> TakeInfo::load (const juce::File& audioFile)
{
    if (auto xml = juce::parseXMLIfTagMatches (infoFileFor (audioFile), "TAKE"))
        return fromXml (*xml);

    return std::nullopt;
}

bool TakeInfo::save (const juce::File& audioFile) const
{
    const auto file = infoFileFor (audioFile);
    file.getParentDirectory().createDirectory();
    return toXml()->writeTo (file);
}

juce::String TakeInfo::makeFileName (juce::Time when, const juce::String& what)
{
    auto name = "Take " + when.formatted ("%Y-%m-%d %H.%M");
    // "Autumn Leaves.mp3" -> "Autumn Leaves" (a short extension only: titles may contain dots).
    auto base = what.trim();
    const auto dot = base.lastIndexOfChar ('.');

    if (dot > 0 && base.length() - dot <= 5)
        base = base.substring (0, dot);

    const auto clean = juce::File::createLegalFileName (base).trim();

    if (clean.isNotEmpty())
        name << " - " << clean.substring (0, 60).trim();

    return name;
}

} // namespace pv
