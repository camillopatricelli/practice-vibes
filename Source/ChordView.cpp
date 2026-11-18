#include "ChordView.h"
#include "Theme.h"
#include "Localisation.h"

namespace pv
{

void ChordView::setTrack (std::shared_ptr<const ChordTrack> newTrack, const juce::String& newStatus)
{
    track = std::move (newTrack);
    status = newStatus;
    chordIndex = beatIndex = -1;
    repaint();
}

void ChordView::update (double positionSeconds, int semitones)
{
    if (track == nullptr)
        return;

    const auto newChord = track->chordIndexAt (positionSeconds);
    const auto& beats = track->beats;
    const auto newBeat = (int) std::distance (beats.begin(), std::upper_bound (beats.begin(), beats.end(), positionSeconds)) - 1;

    if (newChord != chordIndex || newBeat != beatIndex || semitones != transpose)
    {
        chordIndex = newChord;
        beatIndex = newBeat;
        transpose = semitones;
        repaint();
    }
}

void ChordView::paint (juce::Graphics& g)
{
    theme::paintCard (g, getLocalBounds().toFloat());

    auto area = getLocalBounds().reduced (18, 14);

    // Header: "Chords" with the section dot, key / tempo / meter on the right ---------------------------
    auto header = area.removeFromTop (26);
    {
        const float dot = 8.0f;
        g.setColour (theme::accent());
        g.fillEllipse ((float) header.getX() + 1.0f, (float) header.getCentreY() - dot * 0.5f, dot, dot);
        auto title = header;
        title.removeFromLeft (16);
        g.setColour (theme::text().withAlpha (0.85f));
        g.setFont (theme::headerFont());
        g.drawText (tr ("Chords"), title.removeFromLeft (90), juce::Justification::centredLeft);

        if (track != nullptr && ! track->chords.empty())
        {
            const auto sep = juce::String ("  ") + juce::String::charToString ((juce::juce_wchar) 0x00b7) + "  ";
            g.setColour (theme::muted());
            g.setFont (theme::textFont (13.0f, "Medium"));
            const auto keyText = track->keyIsMinor ? tr ("{note} minor", track->keyRootName (transpose))
                                                   : tr ("{note} major", track->keyRootName (transpose));
            g.drawText (tr ("Key {key}", keyText) + sep + "~" + juce::String (juce::roundToInt (track->bpm)) + " bpm" + sep
                            + track->timeSignatureAt (juce::jmax (0, beatIndex)) + (track->hasMeterChanges() ? " " + tr ("(changes)") : juce::String()),
                        title, juce::Justification::centredRight, true);
        }
    }

    if (track == nullptr || track->chords.empty())
    {
        g.setColour (theme::muted());
        g.setFont (theme::textFont (16.0f));
        g.drawFittedText (status.isNotEmpty() ? status : tr ("Chords appear here after you open a song."),
                          area.reduced (0, 6), juce::Justification::centred, 3);
        return;
    }

    const auto& chords = track->chords;
    const auto current = juce::jlimit (0, (int) chords.size() - 1, chordIndex < 0 ? 0 : chordIndex);
    const auto& now = chords[(size_t) current];
    const auto started = chordIndex >= 0;
    const auto nextIndex = started ? current + 1 : current;

    auto footer = area.removeFromBottom (16);
    area.removeFromBottom (6);
    auto strip = area.removeFromBottom (juce::jmin (30, area.getHeight() / 4));
    area.removeFromBottom (10);
    auto main = area.withTrimmedTop (4);

    // Beat dots: one per beat of the current chord, lit up to the current beat ----------------------------
    const auto numDots = juce::jlimit (1, 16, now.numBeats);
    const float dotSize = 10.0f, dotGap = 6.0f;
    const auto dotsWidth = (float) numDots * dotSize + (float) (numDots - 1) * dotGap;

    auto paintDots = [&] (float x, float centreY)
    {
        const auto done = started ? juce::jlimit (0, numDots, beatIndex - now.startBeat + 1) : 0;

        for (int i = 0; i < numDots; ++i)
        {
            const auto isDownbeat = track->isDownbeat (now.startBeat + i);
            g.setColour (i < done ? (isDownbeat ? theme::highlight() : theme::accent())
                                  : (isDownbeat ? theme::muted().withAlpha (0.45f) : theme::track()));
            g.fillEllipse (x + (float) i * (dotSize + dotGap), centreY - dotSize * 0.5f, dotSize, dotSize);
        }
    };

    // Now: the big chord name ------------------------------------------------------------------------------
    const auto bigSize = juce::jlimit (28.0f, 76.0f, (float) main.getHeight() * 0.95f);
    const juce::Font bigFont (theme::displayFont (bigSize, "Bold"));
    const auto nowName = track->chordName (now, transpose);
    const auto nowWidth = juce::jmin ((float) main.getWidth() * 0.5f,
                                      juce::jmax (bigSize * 1.2f, juce::GlyphArrangement::getStringWidth (bigFont, nowName)));
    g.setColour (now.root < 0 ? theme::muted() : theme::highlight().withAlpha (started ? 1.0f : 0.5f));
    g.setFont (bigFont);
    g.drawFittedText (nowName, main.removeFromLeft (juce::roundToInt (nowWidth) + 2), juce::Justification::bottomLeft, 1, 0.6f);
    main.removeFromLeft (22);

    // Next: caption, name and countdown, bottom-aligned with the big chord ----------------------------------
    auto nextArea = main;
    const auto dotsFitRight = (float) main.getWidth() > dotsWidth + 120.0f;

    if (dotsFitRight)
        nextArea.removeFromRight (juce::roundToInt (dotsWidth) + 12);

    if (nextIndex < (int) chords.size())
    {
        const auto& next = chords[(size_t) nextIndex];
        auto block = nextArea.removeFromBottom (juce::jmin (nextArea.getHeight(), 70));
        g.setColour (theme::muted());
        g.setFont (theme::textFont (12.0f, "SemiBold").withKerningFactor (0.08f));
        g.drawText (tr ("NEXT"), block.removeFromTop (16), juce::Justification::bottomLeft);

        auto countRow = block.removeFromBottom (18);
        g.setColour (theme::text().withAlpha (0.8f));
        g.setFont (theme::displayFont (juce::jlimit (18.0f, 30.0f, (float) block.getHeight() * 0.9f), "SemiBold"));
        g.drawFittedText (track->chordName (next, transpose), block, juce::Justification::bottomLeft, 1, 0.6f);

        const auto beatsLeft = next.startBeat - juce::jmax (0, beatIndex);
        g.setColour (theme::muted());
        g.setFont (theme::textFont (13.0f));
        g.drawText (beatsLeft <= 1 ? tr ("next beat") : tr ("in {n} beats", beatsLeft),
                    countRow, juce::Justification::centredLeft);
    }

    if (dotsFitRight)
        paintDots ((float) main.getRight() - dotsWidth, (float) main.getBottom() - 12.0f);
    else
        paintDots ((float) main.getX(), (float) strip.getY() - 6.0f);

    // Then: the chords after "next" as pills, each with its length in beats ---------------------------------
    {
        const juce::Font nameFont (theme::textFont (14.0f, "SemiBold"));
        const juce::Font beatsFont (theme::textFont (11.0f, "Medium"));
        auto x = (float) strip.getX();
        const auto pillHeight = juce::jmin (28.0f, (float) strip.getHeight());
        const auto y = (float) strip.getCentreY() - pillHeight * 0.5f;

        for (auto i = nextIndex + 1; i < (int) chords.size(); ++i)
        {
            const auto& c = chords[(size_t) i];
            const auto name = track->chordName (c, transpose);
            const auto beats = juce::String (c.numBeats);
            const auto nameWidth = juce::GlyphArrangement::getStringWidth (nameFont, name);
            const auto beatsWidth = juce::GlyphArrangement::getStringWidth (beatsFont, beats);
            const auto pillWidth = 12.0f + nameWidth + 5.0f + beatsWidth + 12.0f;

            if (x + pillWidth > (float) strip.getRight())
                break;

            const juce::Rectangle<float> pill (x, y, pillWidth, pillHeight);
            g.setColour (theme::field());
            g.fillRoundedRectangle (pill, pillHeight * 0.5f);

            g.setColour (theme::text().withAlpha (0.75f));
            g.setFont (nameFont);
            g.drawText (name, pill.withTrimmedLeft (12.0f).withWidth (nameWidth + 1.0f), juce::Justification::centredLeft, false);
            g.setColour (theme::muted());
            g.setFont (beatsFont);
            g.drawText (beats, pill.withTrimmedLeft (12.0f + nameWidth + 5.0f).withWidth (beatsWidth + 1.0f).translated (0.0f, 1.0f),
                        juce::Justification::centredLeft, false);
            x += pillWidth + 8.0f;
        }
    }

    // Footer ------------------------------------------------------------------------------------------------
    g.setColour (theme::muted().withAlpha (0.7f));
    g.setFont (theme::textFont (12.0f));
    g.drawText (tr ("Detected automatically, may contain mistakes"), footer, juce::Justification::centredLeft, true);
}

} // namespace pv
