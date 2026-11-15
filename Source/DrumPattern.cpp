#include "DrumPattern.h"

namespace pv
{

namespace
{
    size_t cellIndex (int bar, int step, Drum drum) noexcept
    {
        return (size_t) ((bar * DrumPattern::maxSteps + step) * numDrums + (int) drum);
    }

    // Short keys for the text form: K kick, S snare, H closed hat, O open hat, R ride, C crash, T high tom, L low tom.
    const char* drumKey (Drum drum)
    {
        switch (drum)
        {
            case Drum::crash:     return "crash";
            case Drum::ride:      return "ride";
            case Drum::openHat:   return "openHat";
            case Drum::closedHat: return "closedHat";
            case Drum::tomHigh:   return "tomHigh";
            case Drum::tomLow:    return "tomLow";
            case Drum::snare:     return "snare";
            case Drum::kick:      return "kick";
        }

        return "";
    }

    Drum drumForLetter (char letter)
    {
        switch (letter)
        {
            case 'C': return Drum::crash;
            case 'R': return Drum::ride;
            case 'O': return Drum::openHat;
            case 'H': return Drum::closedHat;
            case 'T': return Drum::tomHigh;
            case 'L': return Drum::tomLow;
            case 'S': return Drum::snare;
            default:  return Drum::kick;
        }
    }

    DrumPattern make (const char* name, int beats, int ticks, double swing,
                      std::initializer_list<std::pair<char, const char*>> rows)
    {
        DrumPattern p;
        p.name = name;
        p.builtIn = true;
        p.beatsPerBar = beats;
        p.ticks = ticks;
        p.swing = swing;

        for (const auto& [letter, text] : rows)
        {
            const auto bars = juce::StringArray::fromTokens (text, "|", {}).size();
            p.bars = juce::jmax (p.bars, bars);
        }

        for (const auto& [letter, text] : rows)
            p.rowFromText (drumForLetter (letter), text);

        return p;
    }
}

DrumPattern::DrumPattern() = default;

juce::uint8 DrumPattern::get (int bar, int step, Drum drum) const noexcept
{
    if (! juce::isPositiveAndBelow (bar, maxBars) || ! juce::isPositiveAndBelow (step, maxSteps))
        return off;

    return cells[cellIndex (bar, step, drum)];
}

void DrumPattern::set (int bar, int step, Drum drum, juce::uint8 level) noexcept
{
    if (juce::isPositiveAndBelow (bar, maxBars) && juce::isPositiveAndBelow (step, maxSteps))
        cells[cellIndex (bar, step, drum)] = (juce::uint8) juce::jlimit (0, 3, (int) level);
}

void DrumPattern::clear() noexcept
{
    cells.fill (off);
}

bool DrumPattern::isEmpty() const noexcept
{
    for (int bar = 0; bar < bars; ++bar)
        for (int step = 0; step < stepsPerBar(); ++step)
            for (int d = 0; d < numDrums; ++d)
                if (get (bar, step, (Drum) d) != off)
                    return false;

    return true;
}

void DrumPattern::reshape (int newBeats, int newTicks, int newBars)
{
    newBeats = juce::jlimit (1, maxBeats, newBeats);
    newTicks = juce::jlimit (2, maxTicks, newTicks);
    newBars = juce::jlimit (1, maxBars, newBars);

    const auto old = cells;
    const auto oldBars = bars, oldTicks = ticks, oldBeats = beatsPerBar;
    cells.fill (off);

    for (int bar = 0; bar < newBars; ++bar)
    {
        const auto from = bar % oldBars;    // new bars repeat the old ones

        for (int beat = 0; beat < juce::jmin (newBeats, oldBeats); ++beat)
        {
            for (int tick = 0; tick < oldTicks; ++tick)
            {
                // Keep a step only if it lands exactly on the new grid (8ths <-> 16ths, downbeats always).
                if ((tick * newTicks) % oldTicks != 0)
                    continue;

                const auto newTick = tick * newTicks / oldTicks;

                for (int d = 0; d < numDrums; ++d)
                    cells[cellIndex (bar, beat * newTicks + newTick, (Drum) d)] = old[cellIndex (from, beat * oldTicks + tick, (Drum) d)];
            }
        }
    }

    beatsPerBar = newBeats;
    ticks = newTicks;
    bars = newBars;
}

void DrumPattern::copyBarToAll (int from) noexcept
{
    for (int bar = 0; bar < bars; ++bar)
        if (bar != from)
            for (int step = 0; step < maxSteps; ++step)
                for (int d = 0; d < numDrums; ++d)
                    cells[cellIndex (bar, step, (Drum) d)] = cells[cellIndex (from, step, (Drum) d)];
}

juce::String DrumPattern::rowToText (Drum drum) const
{
    static constexpr char symbols[] { '.', 'g', 'x', 'X' };
    juce::String text;

    for (int bar = 0; bar < bars; ++bar)
    {
        if (bar > 0)
            text << "|";

        for (int step = 0; step < stepsPerBar(); ++step)
        {
            if (step > 0 && step % ticks == 0)
                text << " ";

            text << symbols[get (bar, step, drum)];
        }
    }

    return text;
}

void DrumPattern::rowFromText (Drum drum, const juce::String& text)
{
    const auto barTexts = juce::StringArray::fromTokens (text, "|", {});

    for (int bar = 0; bar < juce::jmin (bars, barTexts.size()); ++bar)
    {
        const auto steps = barTexts[bar].removeCharacters (" \t");

        for (int step = 0; step < juce::jmin (stepsPerBar(), steps.length()); ++step)
        {
            const auto c = steps[step];
            set (bar, step, drum, c == 'X' ? accent : c == 'x' ? normal : c == 'g' ? ghost : off);
        }
    }
}

std::unique_ptr<juce::XmlElement> DrumPattern::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("DRUM_PATTERN");
    xml->setAttribute ("name", name);
    xml->setAttribute ("beatsPerBar", beatsPerBar);
    xml->setAttribute ("ticks", ticks);
    xml->setAttribute ("bars", bars);
    xml->setAttribute ("swing", swing);

    for (int d = 0; d < numDrums; ++d)
        xml->createNewChildElement ("ROW")->setAttribute (drumKey ((Drum) d), rowToText ((Drum) d));

    return xml;
}

DrumPattern DrumPattern::fromXml (const juce::XmlElement& xml)
{
    DrumPattern p;
    p.name = xml.getStringAttribute ("name", "Pattern");
    p.beatsPerBar = juce::jlimit (1, maxBeats, xml.getIntAttribute ("beatsPerBar", 4));
    p.ticks = juce::jlimit (2, maxTicks, xml.getIntAttribute ("ticks", 4));
    p.bars = juce::jlimit (1, maxBars, xml.getIntAttribute ("bars", 1));
    p.swing = juce::jlimit (0.5, 0.75, xml.getDoubleAttribute ("swing", 0.5));

    for (auto* row : xml.getChildWithTagNameIterator ("ROW"))
        for (int d = 0; d < numDrums; ++d)
            if (row->hasAttribute (drumKey ((Drum) d)))
                p.rowFromText ((Drum) d, row->getStringAttribute (drumKey ((Drum) d)));

    return p;
}

bool DrumPattern::sameNotes (const DrumPattern& other) const noexcept
{
    if (beatsPerBar != other.beatsPerBar || ticks != other.ticks || bars != other.bars || std::abs (swing - other.swing) > 1.0e-6)
        return false;

    for (int bar = 0; bar < bars; ++bar)
        for (int step = 0; step < stepsPerBar(); ++step)
            for (int d = 0; d < numDrums; ++d)
                if (get (bar, step, (Drum) d) != other.get (bar, step, (Drum) d))
                    return false;

    return true;
}

//==============================================================================
const std::vector<DrumPattern>& DrumPatterns::builtIn()
{
    static const std::vector<DrumPattern> patterns {
        make ("Kick on the beat", 4, 2, 0.5, {
            { 'K', "X. x. x. x." } }),

        make ("Basic beat", 4, 2, 0.5, {
            { 'H', "x. x. x. x." },
            { 'S', ".. X. .. X." },
            { 'K', "X. .. X. .." } }),

        make ("Rock", 4, 2, 0.5, {
            { 'H', "xx xx xx xx" },
            { 'S', ".. X. .. X." },
            { 'K', "X. .. Xx .." } }),

        make ("Rock with a fill (2 bars)", 4, 4, 0.5, {
            { 'C', "X... .... .... ....|.... .... .... ...." },
            { 'H', "..x. x.x. x.x. x.x.|x.x. x.x. x.x. ...." },
            { 'T', ".... .... .... ....|.... .... .... XxX." },
            { 'L', ".... .... .... ....|.... .... .... ...x" },
            { 'S', ".... X... .... X...|.... X... .... ...." },
            { 'K', "X... .... X.X. ....|X... .... X.X. ...." } }),

        make ("Four on the floor", 4, 4, 0.5, {
            { 'O', "..x. ..x. ..x. ..x." },
            { 'H', "x... x... x... x..." },
            { 'S', ".... X... .... X..." },
            { 'K', "X... X... X... X..." } }),

        make ("Funk", 4, 4, 0.5, {
            { 'H', "Xxxx Xxxx Xxxx Xxxx" },
            { 'S', ".... X..g .g.. X..g" },
            { 'K', "X.x. .... ..x. .x.." } }),

        make ("Shuffle", 4, 3, 0.5, {
            { 'H', "x.x x.x x.x x.x" },
            { 'S', "... X.. ... X.." },
            { 'K', "X.. ... X.x ..." } }),

        make ("Slow blues (12/8 feel)", 4, 3, 0.5, {
            { 'H', "Xxx Xxx Xxx Xxx" },
            { 'S', "... X.. ... X.." },
            { 'K', "X.. ..x X.. ..." } }),

        make ("Jazz swing", 4, 3, 0.5, {
            { 'R', "X.. x.x X.. x.x" },
            { 'H', "... x.. ... x.." },
            { 'K', "g.. g.. g.. g.." } }),

        make ("Reggae one drop", 4, 4, 0.5, {
            { 'H', "x.x. x.x. x.x. x.x." },
            { 'S', ".... .... X... ...." },
            { 'K', ".... .... X... ...." } }),

        make ("Bossa nova (2 bars)", 4, 2, 0.5, {
            { 'H', "xx xx xx xx|xx xx xx xx" },
            { 'S', "x. .x .. x.|.. x. .x .." },
            { 'K', "X. .x X. .x|X. .x X. .x" } }),

        make ("Waltz (3/4)", 3, 2, 0.5, {
            { 'H', "x. x. x." },
            { 'S', ".. x. x." },
            { 'K', "X. .. .." } }),

        make ("Ballad (6/8)", 2, 3, 0.5, {
            { 'H', "xxx xxx" },
            { 'S', "... X.." },
            { 'K', "X.. ..x" } }),
    };

    return patterns;
}

juce::File DrumPatterns::getUserFolder()
{
   #if JUCE_IOS
    auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Drum patterns");
   #else
    auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Practice Vibes").getChildFile ("Drum patterns");
   #endif

    return folder;
}

juce::File DrumPatterns::fileFor (const juce::String& name)
{
    auto safe = juce::File::createLegalFileName (name.trim());

    if (safe.isEmpty())
        safe = "Pattern";

    return getUserFolder().getChildFile (safe + ".xml");
}

std::vector<DrumPattern> DrumPatterns::loadUserPatterns()
{
    std::vector<DrumPattern> patterns;

    for (const auto& entry : juce::RangedDirectoryIterator (getUserFolder(), false, "*.xml"))
        if (auto xml = juce::parseXML (entry.getFile()))
            if (xml->hasTagName ("DRUM_PATTERN"))
                patterns.push_back (DrumPattern::fromXml (*xml));

    std::sort (patterns.begin(), patterns.end(), [] (const DrumPattern& a, const DrumPattern& b)
    {
        return a.name.compareNatural (b.name) < 0;
    });

    return patterns;
}

juce::Result DrumPatterns::saveUserPattern (const DrumPattern& pattern)
{
    const auto folder = getUserFolder();

    if (! folder.createDirectory())
        return juce::Result::fail ("Can't create " + folder.getFullPathName());

    auto copy = pattern;
    copy.builtIn = false;

    if (! copy.toXml()->writeTo (fileFor (pattern.name)))
        return juce::Result::fail ("Can't write " + fileFor (pattern.name).getFullPathName());

    return juce::Result::ok();
}

bool DrumPatterns::deleteUserPattern (const juce::String& name)
{
    return fileFor (name).deleteFile();
}

} // namespace pv
