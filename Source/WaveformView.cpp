#include "WaveformView.h"
#include "Theme.h"

namespace pv
{

namespace
{
    constexpr int overviewHeight = 30;
    constexpr int rulerHeight = 18;
    constexpr int chordLaneHeight = 22;
    constexpr float edgeGrabDistance = 14.0f;

    juce::String formatRulerTime (double seconds, double step)
    {
        const auto minutes = (int) (seconds / 60.0);
        const auto rest = seconds - minutes * 60.0;
        const auto decimals = step < 0.1 ? 2 : (step < 1.0 ? 1 : 0);
        auto secondsText = juce::String (rest, decimals);

        if (rest < 10.0)
            secondsText = "0" + secondsText;

        return juce::String (minutes) + ":" + secondsText;
    }
}

WaveformView::WaveformView (PlayerProcessor& p)
    : player (p)
{
    setOpaque (true);
}

//==============================================================================
void WaveformView::songChanged()
{
    song = player.getSong();
    zoomToFit();
}

bool WaveformView::isZoomed() const
{
    return song != nullptr && viewLength < song->getLengthSeconds() - 1.0e-6;
}

double WaveformView::getMinViewLength() const
{
    return 0.02;   // 20 ms across the whole width: individual samples are visible
}

void WaveformView::setView (double start, double length)
{
    if (song == nullptr)
    {
        viewStart = 0.0;
        viewLength = 1.0;
        repaint();
        return;
    }

    const auto total = song->getLengthSeconds();
    viewLength = juce::jlimit (juce::jmin (getMinViewLength(), total), total, length);
    viewStart = juce::jlimit (0.0, total - viewLength, start);
    repaint();
}

void WaveformView::zoomAround (double factor, float x)
{
    const auto anchor = xToSeconds (x);
    const auto proportion = mainArea.getWidth() > 0 ? (double) (x - (float) mainArea.getX()) / mainArea.getWidth() : 0.5;
    const auto newLength = viewLength / factor;
    setView (anchor - proportion * newLength, newLength);
}

void WaveformView::zoomBy (double factor)
{
    zoomAround (factor, (float) mainArea.getCentreX());
}

void WaveformView::zoomToFit()
{
    setView (0.0, song != nullptr ? song->getLengthSeconds() : 1.0);
}

void WaveformView::zoomToRange (double startSeconds, double endSeconds)
{
    const auto length = juce::jmax (getMinViewLength(), endSeconds - startSeconds);
    const auto margin = length * 0.05;
    setView (startSeconds - margin, length + 2.0 * margin);
}

double WaveformView::xToSeconds (float x) const
{
    if (mainArea.getWidth() <= 0)
        return viewStart;

    return viewStart + (double) (x - (float) mainArea.getX()) / mainArea.getWidth() * viewLength;
}

float WaveformView::secondsToX (double seconds) const
{
    return (float) mainArea.getX() + (float) ((seconds - viewStart) / viewLength * mainArea.getWidth());
}

void WaveformView::refresh()
{
    if (song == nullptr)
        return;

    // Page-flip to follow the playhead, but only if it was on screen until now:
    // if the user scrolled elsewhere on purpose, leave the view alone.
    const auto position = player.getPositionSeconds();
    const auto visible = position >= viewStart && position <= viewStart + viewLength;

    if (player.isPlaying() && ! visible && playheadWasVisible && isZoomed()
         && gesture != Gesture::pinching && gesture != Gesture::overview)
    {
        setView (position - viewLength * 0.05, viewLength);
        playheadWasVisible = true;
    }
    else
    {
        playheadWasVisible = visible;
    }

    repaint();
}

//==============================================================================
void WaveformView::resized()
{
    auto area = getLocalBounds();
    overviewArea = area.removeFromBottom (overviewHeight);
    area.removeFromBottom (4);
    mainArea = area;
}

void WaveformView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
    g.setColour (theme::palette().waveBackground);
    g.fillRoundedRectangle (mainArea.toFloat(), 16.0f);
    g.setColour (theme::raised().withAlpha (theme::isDark() ? 0.7f : 1.0f));
    g.fillRoundedRectangle (overviewArea.toFloat(), 9.0f);

    if (song == nullptr)
    {
        g.setColour (theme::text().withAlpha (0.5f));
        g.setFont (juce::FontOptions (18.0f));
        g.drawText ("Open an audio file to start", mainArea, juce::Justification::centred);
        return;
    }

    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path rounded;
        rounded.addRoundedRectangle (mainArea.toFloat(), 16.0f);
        g.reduceClipRegion (rounded);
        paintWaveform (g, mainArea);
        paintChordLane (g, mainArea);
        paintTimeRuler (g, mainArea.withHeight (rulerHeight));
        paintMarks (g, mainArea);
    }

    paintOverview (g, overviewArea);
}

void WaveformView::setMarks (std::vector<SongMark> newMarks)
{
    marks = std::move (newMarks);
    repaint();
}

void WaveformView::paintMarks (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (marks.empty())
        return;

    const auto top = (float) area.getY() + rulerHeight + (chords != nullptr ? chordLaneHeight : 0);
    const auto bottom = (float) area.getBottom();
    const auto colour = theme::palette().highlight;
    g.setFont (theme::textFont (11.0f, "SemiBold"));

    // Each flag gets the first free row (of three) so neighbouring comments don't overlap.
    std::array<float, 3> rowEnds { -1.0e9f, -1.0e9f, -1.0e9f };

    for (auto& m : marks)
    {
        const auto x1 = secondsToX (m.start);
        const auto x2 = m.isLoop() ? secondsToX (m.end) : x1;

        if (x2 < (float) area.getX() - 2.0f || x1 > (float) area.getRight() + 2.0f)
            continue;

        g.setColour (colour.withAlpha (0.85f));
        g.drawLine (x1, top, x1, bottom, 1.5f);

        if (m.isLoop())
        {
            g.setColour (colour.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> (x1, top, x2 - x1, bottom - top));
            g.setColour (colour.withAlpha (0.6f));
            g.drawLine (x2, top, x2, bottom, 1.0f);
        }

        auto label = m.text.isNotEmpty() ? m.text : (m.isLoop() ? juce::String ("Loop") : formatMarkTime (m.start));

        if (label.length() > 28)
            label = label.substring (0, 27).trimEnd() + "...";

        const auto width = juce::jmin (220.0f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 12.0f);
        size_t row = 0;

        while (row < rowEnds.size() - 1 && rowEnds[row] > x1)
            ++row;

        rowEnds[row] = x1 + width + 4.0f;
        const auto flag = juce::Rectangle<float> (x1, top + 4.0f + (float) row * 18.0f, width, 16.0f);
        g.setColour (colour.withAlpha (0.9f));
        g.fillRoundedRectangle (flag, 4.0f);
        g.setColour (theme::onAccent());
        g.drawText (label, flag.reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}

void WaveformView::paintWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto top = (float) area.getY() + rulerHeight + (chords != nullptr ? chordLaneHeight : 0);
    const auto height = (float) area.getBottom() - top;
    const auto mid = top + height * 0.48f;
    const auto halfHeight = height * 0.46f - 4.0f;
    const auto& pal = theme::palette();

    // Loop region: a soft rounded band with amber edges and A / B tags.
    if (player.hasLoop())
    {
        const auto x1 = secondsToX (player.getLoopStartSeconds());
        const auto x2 = secondsToX (player.getLoopEndSeconds());
        const bool on = player.isLoopEnabled();
        g.setColour (pal.accent.withAlpha ((theme::isDark() ? 0.13f : 0.17f) * (on ? 1.0f : 0.5f)));
        g.fillRoundedRectangle (juce::Rectangle<float> (x1, top + 2.0f, x2 - x1, height - 4.0f), 10.0f);
        g.setFont (theme::textFont (10.5f, "Bold"));

        for (auto [x, tag] : { std::pair<float, const char*> { x1, "A" }, std::pair<float, const char*> { x2, "B" } })
        {
            g.setColour (pal.accent.withAlpha (on ? 1.0f : 0.45f));
            g.fillRect (juce::Rectangle<float> (x - 1.0f, top + 2.0f, 2.0f, height - 4.0f));
            const auto pill = juce::Rectangle<float> (20.0f, 15.0f).withCentre ({ x, (float) area.getBottom() - 11.0f });
            g.fillRoundedRectangle (pill, 7.5f);
            g.setColour (pal.onAccent);
            g.drawText (tag, pill, juce::Justification::centred, false);
        }
    }

    // Waveform
    const auto sampleRate = song->sampleRate;
    const auto numSamples = song->audio.getNumSamples();
    const auto width = area.getWidth();
    const auto samplesPerPixel = viewLength * sampleRate / juce::jmax (1, width);
    const auto* left = song->audio.getReadPointer (0);
    const auto* right = song->audio.getReadPointer (1);
    auto mono = [left, right] (int i) { return 0.5f * (left[i] + right[i]); };

    // The part already played: amber gradient with a soft glow; the rest in the quiet waveform colour.
    const auto playX = secondsToX (player.getPositionSeconds());
    const auto playedClip = juce::Rectangle<float> ((float) area.getX(), (float) area.getY(),
                                                    juce::jmax (0.0f, playX - (float) area.getX()), (float) area.getHeight()).toNearestInt();
    const juce::ColourGradient playedFill (pal.wavePlayed, 0.0f, mid - halfHeight, pal.wavePlayed2, 0.0f, mid + halfHeight * 0.8f, false);

    if (samplesPerPixel < 1.0)
    {
        // Very close: draw the actual sample curve.
        juce::Path path;
        const auto first = juce::jmax (0, (int) std::floor (viewStart * sampleRate));
        const auto last = juce::jmin (numSamples - 1, (int) std::ceil ((viewStart + viewLength) * sampleRate));

        for (int i = first; i <= last; ++i)
        {
            const auto x = secondsToX (i / sampleRate);
            const auto y = mid - mono (i) * halfHeight;

            if (i == first)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        g.setColour (pal.wave);
        g.strokePath (path, juce::PathStrokeType (1.8f));

        if (! playedClip.isEmpty())
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (playedClip);
            g.setGradientFill (playedFill);
            g.strokePath (path, juce::PathStrokeType (2.0f));
        }
    }
    else
    {
        // Pick the coarsest cache level that is still at least as fine as one pixel.
        const LoadedSong::PeakLevel* level = nullptr;

        for (auto& candidate : song->peakLevels)
            if (candidate.samplesPerBucket <= samplesPerPixel)
                level = &candidate;

        // A filled, mirrored shape (the lower half a little smaller, as in the mock-up).
        constexpr int stepPx = 2;
        std::vector<juce::Point<float>> points;
        points.reserve ((size_t) (width / stepPx + 2));

        for (int x = 0; x <= width; x += stepPx)
        {
            const auto s0 = (juce::int64) ((viewStart * sampleRate) + x * samplesPerPixel);
            const auto s1 = (juce::int64) ((viewStart * sampleRate) + (x + stepPx) * samplesPerPixel);
            auto peak = 0.0f;

            if (s0 < numSamples && s1 > 0)
            {
                if (level != nullptr)
                {
                    const auto b0 = (size_t) juce::jmax<juce::int64> (0, s0 / level->samplesPerBucket);
                    const auto b1 = (size_t) juce::jmax<juce::int64> ((juce::int64) b0 + 1, s1 / level->samplesPerBucket);

                    for (auto b = b0; b < b1 && b < level->mins.size(); ++b)
                        peak = juce::jmax (peak, level->maxs[b], -level->mins[b]);
                }
                else
                {
                    for (auto i = juce::jmax<juce::int64> (0, s0); i < juce::jmin<juce::int64> (numSamples, juce::jmax (s1, s0 + 1)); ++i)
                        peak = juce::jmax (peak, std::abs (mono ((int) i)));
                }
            }

            points.push_back ({ (float) (area.getX() + x), juce::jmax (0.75f, juce::jmin (1.0f, peak) * halfHeight) });
        }

        juce::Path shape;

        if (! points.empty())
        {
            shape.startNewSubPath (points.front().x, mid - points.front().y);

            for (auto& pt : points)
                shape.lineTo (pt.x, mid - pt.y);

            for (auto it = points.rbegin(); it != points.rend(); ++it)
                shape.lineTo (it->x, mid + it->y * 0.8f);

            shape.closeSubPath();
        }

        g.setColour (pal.wave);
        g.fillPath (shape);

        if (! playedClip.isEmpty())
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (playedClip);

            // Glow: a few wide, faint strokes of the outline under the fill.
            const float glowAlpha = theme::isDark() ? 1.0f : 0.45f;

            for (auto [w, a] : { std::pair<float, float> { 12.0f, 0.05f }, std::pair<float, float> { 7.0f, 0.08f }, std::pair<float, float> { 3.5f, 0.12f } })
            {
                g.setColour (pal.wavePlayed.withAlpha (a * glowAlpha));
                g.strokePath (shape, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            g.setGradientFill (playedFill);
            g.fillPath (shape);
        }

        g.setColour (theme::shade (0.18f));
        g.fillRect (juce::Rectangle<float> ((float) area.getX(), mid, (float) width, 1.0f));
    }

    // Selection being dragged
    if (gesture == Gesture::selecting)
    {
        const auto range = selectionRange();
        const auto x1 = secondsToX (range.first);
        const auto x2 = secondsToX (range.second);
        g.setColour (theme::text().withAlpha (0.14f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x1, top + 2.0f, x2 - x1, height - 4.0f), 8.0f);
    }

    // Playhead: a line with a dot on top.
    g.setColour (pal.playhead);
    g.fillRect (juce::Rectangle<float> (playX - 1.0f, top - 2.0f, 2.0f, height - 2.0f));
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ playX, top - 2.0f }));
}

double WaveformView::snap (double seconds) const
{
    if (chords == nullptr || snapMode == SnapMode::off)
        return seconds;

    return chords->snapToGrid (seconds, snapMode == SnapMode::bars ? ChordTrack::GridUnit::bar : ChordTrack::GridUnit::beat);
}

std::pair<double, double> WaveformView::snapLoop (double start, double end) const
{
    if (end < start)
        std::swap (start, end);

    if (chords == nullptr || snapMode == SnapMode::off)
        return { start, end };

    const auto unit = snapMode == SnapMode::bars ? ChordTrack::GridUnit::bar : ChordTrack::GridUnit::beat;
    const auto length = song != nullptr ? song->getLengthSeconds() : end;
    start = chords->snapToGrid (start, unit);
    end = chords->snapToGrid (end, unit);

    if (end <= start)
    {
        const auto next = chords->neighbourGridLine (start, unit, 1);
        end = next > start ? next : length;
    }

    return { start, juce::jmin (end, length) };
}

std::pair<double, double> WaveformView::selectionRange() const
{
    return snapLoop (xToSeconds (juce::jmin (dragStartX, dragEndX)), xToSeconds (juce::jmax (dragStartX, dragEndX)));
}

void WaveformView::setChords (std::shared_ptr<const ChordTrack> newChords)
{
    chords = std::move (newChords);
    repaint();
}

void WaveformView::setChordTranspose (int semitones)
{
    if (semitones != chordTranspose)
    {
        chordTranspose = semitones;
        repaint();
    }
}

void WaveformView::paintChordLane (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (chords == nullptr || chords->chords.empty())
        return;

    const auto viewEnd = viewStart + viewLength;
    const auto top = (float) area.getY() + rulerHeight;
    const auto lane = juce::Rectangle<float> ((float) area.getX(), top, (float) area.getWidth(), (float) chordLaneHeight);

    // Faint bar lines through the waveform, when they are not too dense.
    const auto& beats = chords->beats;
    const auto barPixels = chords->beatsPerBar * (beats.size() > 1 ? (beats.back() - beats.front()) / (double) (beats.size() - 1) : 1.0)
                               / viewLength * area.getWidth();

    if (barPixels > 12.0)
    {
        const auto first = (size_t) std::distance (beats.begin(), std::lower_bound (beats.begin(), beats.end(), viewStart));

        for (auto i = first; i < beats.size() && beats[i] <= viewEnd; ++i)
        {
            const auto downbeat = chords->isDownbeat ((int) i);

            if (! downbeat && barPixels < 120.0)
                continue;

            g.setColour (theme::text().withAlpha (downbeat ? 0.10f : 0.04f));
            g.fillRect (juce::Rectangle<float> (secondsToX (beats[i]), top, 1.0f, (float) area.getBottom() - top));
        }
    }

    const auto position = player.getPositionSeconds();
    g.setFont (theme::textFont (12.5f, "SemiBold"));

    for (const auto& chord : chords->chords)
    {
        if (chord.end < viewStart || chord.start > viewEnd)
            continue;

        const auto x1 = secondsToX (chord.start), x2 = secondsToX (chord.end);
        const auto isCurrent = position >= chord.start && position < chord.end;

        // A pill per chord: amber for the one playing now.
        const auto pill = juce::Rectangle<float> (x1 + 1.5f, lane.getY() + 2.0f, x2 - x1 - 3.0f, lane.getHeight() - 4.0f);

        if (pill.getWidth() >= 4.0f && chord.root >= 0)
        {
            g.setColour (isCurrent ? theme::accent() : theme::raised());
            g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        }

        // Keep the name visible while its chord scrolls off the left edge.
        const auto textX = juce::jmax (x1, lane.getX()) + 9.0f;

        if (x2 - textX < 16.0f || chord.root < 0)
            continue;

        g.setColour (isCurrent ? theme::onAccent() : theme::muted());
        g.drawText (chords->chordName (chord, chordTranspose), juce::Rectangle<float> (textX, lane.getY(), x2 - textX - 2.0f, lane.getHeight()),
                    juce::Justification::centredLeft, true);
    }
}

void WaveformView::paintTimeRuler (juce::Graphics& g, juce::Rectangle<int> area)
{
    // Choose a tick spacing that leaves at least ~90 px between labels.
    static const double steps[] = { 0.01, 0.02, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0 };
    auto step = steps[std::size (steps) - 1];

    for (auto candidate : steps)
    {
        if (candidate / viewLength * area.getWidth() >= 90.0)
        {
            step = candidate;
            break;
        }
    }

    g.setFont (juce::FontOptions (12.0f));

    for (auto t = std::ceil (viewStart / step) * step; t <= viewStart + viewLength; t += step)
    {
        const auto x = secondsToX (t);
        g.setColour (theme::text().withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x, (float) area.getBottom() - 5.0f, 1.0f, 5.0f));
        g.setColour (theme::text().withAlpha (0.6f));
        g.drawText (formatRulerTime (t, step), juce::Rectangle<float> (x + 3.0f, (float) area.getY(), 80.0f, (float) area.getHeight() - 2.0f),
                    juce::Justification::centredLeft, false);
    }
}

void WaveformView::paintOverview (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto total = song->getLengthSeconds();

    if (total <= 0.0 || song->peakLevels.empty())
        return;

    auto toX = [area, total] (double seconds) { return (float) area.getX() + (float) (seconds / total * area.getWidth()); };

    // Loop region
    if (player.hasLoop())
    {
        g.setColour (theme::accent().withAlpha (0.4f));
        const auto x1 = toX (player.getLoopStartSeconds());
        g.fillRect (juce::Rectangle<float> (x1, (float) area.getY(), toX (player.getLoopEndSeconds()) - x1, (float) area.getHeight()));
    }

    // Whole-song waveform from the coarsest cache level
    const auto& level = song->peakLevels.back();
    const auto buckets = level.maxs.size();
    const auto mid = (float) area.getCentreY();
    const auto halfHeight = area.getHeight() * 0.5f - 2.0f;
    const auto playX = toX (player.getPositionSeconds());

    for (int x = 0; x < area.getWidth(); ++x)
    {
        const auto b0 = (size_t) x * buckets / (size_t) area.getWidth();
        const auto b1 = juce::jmax (b0 + 1, (size_t) (x + 1) * buckets / (size_t) area.getWidth());
        auto peak = 0.0f;

        for (auto b = b0; b < b1 && b < buckets; ++b)
            peak = juce::jmax (peak, juce::jmax (level.maxs[b], -level.mins[b]));

        g.setColour ((float) (area.getX() + x) < playX ? theme::palette().wavePlayed.withAlpha (0.8f) : theme::palette().wave);
        g.fillRect (juce::Rectangle<float> ((float) (area.getX() + x), mid - peak * halfHeight, 1.0f, juce::jmax (1.0f, 2.0f * peak * halfHeight)));
    }

    // Visible window
    if (isZoomed())
    {
        const auto x1 = toX (viewStart);
        const auto x2 = juce::jmax (x1 + 3.0f, toX (viewStart + viewLength));
        auto window = juce::Rectangle<float> (x1, (float) area.getY(), x2 - x1, (float) area.getHeight());
        g.setColour (theme::palette().wavePlayed.withAlpha (0.12f));
        g.fillRoundedRectangle (window, 6.0f);
        g.setColour (theme::palette().wavePlayed);
        g.drawRoundedRectangle (window.reduced (0.75f), 6.0f, 1.5f);
    }

    // Playhead
    g.setColour (theme::palette().playhead);
    g.fillRect (juce::Rectangle<float> (toX (player.getPositionSeconds()) - 0.5f, (float) area.getY(), 1.5f, (float) area.getHeight()));
}

//==============================================================================
void WaveformView::startPinch()
{
    if (touches.size() < 2)
        return;

    auto it = touches.begin();
    const auto a = it->second;
    const auto b = (++it)->second;

    gesture = Gesture::pinching;
    pinchStartDistance = juce::jmax (10.0f, a.getDistanceFrom (b));
    pinchStartLength = viewLength;
    pinchAnchorSeconds = xToSeconds ((a.x + b.x) * 0.5f);
    repaint();
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (song == nullptr)
        return;

    touches[e.source.getIndex()] = e.position;

    if (touches.size() >= 2)
    {
        startPinch();   // a second finger cancels whatever the first one was doing
        return;
    }

    if (overviewArea.contains (e.getPosition()))
    {
        gesture = Gesture::overview;
        setView (e.position.x / juce::jmax (1, overviewArea.getWidth()) * song->getLengthSeconds() - viewLength * 0.5, viewLength);
        return;
    }

    dragStartX = dragEndX = e.position.x;
    gesture = Gesture::tapOrSelect;

    if (player.hasLoop())
    {
        const auto distanceToStart = std::abs (e.position.x - secondsToX (player.getLoopStartSeconds()));
        const auto distanceToEnd = std::abs (e.position.x - secondsToX (player.getLoopEndSeconds()));

        if (juce::jmin (distanceToStart, distanceToEnd) <= edgeGrabDistance)
            gesture = distanceToStart <= distanceToEnd ? Gesture::movingLoopStart : Gesture::movingLoopEnd;
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (song == nullptr)
        return;

    touches[e.source.getIndex()] = e.position;

    switch (gesture)
    {
        case Gesture::pinching:
        {
            if (touches.size() < 2)
                break;

            auto it = touches.begin();
            const auto a = it->second;
            const auto b = (++it)->second;
            const auto distance = juce::jmax (10.0f, a.getDistanceFrom (b));
            const auto midX = (a.x + b.x) * 0.5f;
            const auto newLength = pinchStartLength * pinchStartDistance / distance;
            const auto proportion = (double) (midX - (float) mainArea.getX()) / juce::jmax (1, mainArea.getWidth());
            setView (pinchAnchorSeconds - proportion * newLength, newLength);
            break;
        }

        case Gesture::overview:
            setView (e.position.x / juce::jmax (1, overviewArea.getWidth()) * song->getLengthSeconds() - viewLength * 0.5, viewLength);
            break;

        case Gesture::movingLoopStart:
        case Gesture::movingLoopEnd:
        {
            const auto t = snap (juce::jlimit (0.0, song->getLengthSeconds(), xToSeconds (e.position.x)));
            auto start = player.getLoopStartSeconds();
            auto end = player.getLoopEndSeconds();
            const auto unit = snapMode == SnapMode::bars ? ChordTrack::GridUnit::bar : ChordTrack::GridUnit::beat;
            const auto snapping = chords != nullptr && snapMode != SnapMode::off;

            // An edge can't cross the other one; when snapping it stops one grid step short.
            if (gesture == Gesture::movingLoopStart)
            {
                const auto limit = snapping ? chords->neighbourGridLine (end, unit, -1) : end - 0.01;
                start = juce::jmin (t, limit >= 0.0 ? limit : end - 0.01);
            }
            else
            {
                const auto limit = snapping ? chords->neighbourGridLine (start, unit, 1) : start + 0.01;
                end = juce::jmax (t, limit >= 0.0 ? limit : start + 0.01);
            }

            if (onLoopEdited)
                onLoopEdited (start, end);

            repaint();
            break;
        }

        case Gesture::tapOrSelect:
        case Gesture::selecting:
            dragEndX = juce::jlimit ((float) mainArea.getX(), (float) mainArea.getRight(), e.position.x);

            if (std::abs (dragEndX - dragStartX) > 10.0f)
                gesture = Gesture::selecting;

            repaint();
            break;

        case Gesture::none:
        case Gesture::consumed:
            break;
    }
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    touches.erase (e.source.getIndex());

    if (song == nullptr)
        return;

    if (gesture == Gesture::pinching)
    {
        // Lifting one finger ends the pinch; ignore the other finger until it lifts too.
        gesture = touches.empty() ? Gesture::none : Gesture::consumed;
        return;
    }

    if (gesture == Gesture::selecting && onLoopSelected)
    {
        const auto range = selectionRange();
        onLoopSelected (range.first, range.second);
    }
    else if (gesture == Gesture::tapOrSelect && onSeek)
        onSeek (juce::jlimit (0.0, song->getLengthSeconds(), xToSeconds (e.position.x)));

    if (touches.empty())
        gesture = Gesture::none;

    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (song == nullptr)
        return;

    if (e.mods.isCommandDown())
    {
        zoomAround (std::pow (2.0, wheel.deltaY * 4.0), e.position.x);
    }
    else
    {
        // Horizontal scroll (trackpad) or vertical wheel both pan.
        const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        setView (viewStart - delta * viewLength * 0.5, viewLength);
    }
}

void WaveformView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (song != nullptr && scaleFactor > 0.0f)
        zoomAround (scaleFactor, e.position.x);
}

} // namespace pv
