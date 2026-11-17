#include "PracticeViews.h"
#include "Theme.h"

namespace pv
{

namespace
{
    // Functions, not constants: they follow the theme.
    juce::Colour seriesColour()  { return theme::isDark() ? theme::accent() : theme::highlight(); }   // chart marks
    juce::Colour gridColour()    { return theme::text().withAlpha (0.08f); }
    juce::Colour axisText()      { return theme::text().withAlpha (0.55f); }
    juce::Colour primaryText()   { return theme::text().withAlpha (0.92f); }

    juce::Label& styleCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        theme::styleHeader (l);
        l.setColour (juce::Label::textColourId, theme::text().withAlpha (0.85f));
        return l;
    }

    /** Tick step of 1, 2 or 5 x 10^n giving about `count` intervals over `range`. */
    double niceStep (double range, int count)
    {
        const auto raw = juce::jmax (1.0e-9, range / juce::jmax (1, count));
        const auto magnitude = std::pow (10.0, std::floor (std::log10 (raw)));
        const auto f = raw / magnitude;
        return magnitude * (f <= 1.0 ? 1.0 : f <= 2.0 ? 2.0 : f <= 5.0 ? 5.0 : 10.0);
    }

    float textWidth (const juce::Font& f, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (f, s);
    }

    /** Small dark box with one line of text near `anchor`, kept inside `bounds`. */
    void drawTooltip (juce::Graphics& g, juce::Point<float> anchor, const juce::String& text, juce::Rectangle<int> bounds)
    {
        const juce::Font font (juce::FontOptions (13.0f));
        const auto w = textWidth (font, text) + 16.0f, h = 24.0f;
        auto box = juce::Rectangle<float> (anchor.x - w * 0.5f, anchor.y - h - 12.0f, w, h);

        if (box.getY() < (float) bounds.getY())
            box.setY (anchor.y + 12.0f);

        box = box.constrainedWithin (bounds.toFloat());
        g.setColour (theme::raised());
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (theme::text().withAlpha (0.25f));
        g.drawRoundedRectangle (box, 5.0f, 1.0f);
        g.setColour (primaryText());
        g.setFont (font);
        g.drawText (text, box, juce::Justification::centred, false);
    }
}

//==============================================================================
/** One planned slot: number, topic, minutes, Random, Re-roll, Remove. */
class SessionPlanner::SlotRow final : public juce::Component
{
public:
    SlotRow (int number, const SessionSlot& slot) : index (number), title (slot.title), tag (slot.tag), missing (slot.missing)
    {
        for (int m = 5; m <= 60; m += 5)
            minutesBox.addItem (juce::String (m) + " min", m);

        if (minutesBox.indexOfItemId (slot.minutes) < 0)
            minutesBox.addItem (juce::String (slot.minutes) + " min", slot.minutes);

        minutesBox.setSelectedId (slot.minutes, juce::dontSendNotification);
        minutesBox.onChange = [this] { if (onMinutes) onMinutes (minutesBox.getSelectedId()); };
        rerollButton.onClick = [this] { if (onReroll) onReroll(); };
        removeButton.onClick = [this] { if (onRemove) onRemove(); };
        rerollButton.setTooltip ("Another rotation topic (neglected ones come up more often)");

        randomButton.setClickingTogglesState (true);
        randomButton.setToggleState (slot.random, juce::dontSendNotification);
        randomButton.setColour (juce::TextButton::buttonOnColourId, theme::accent());
        randomButton.setTooltip ("On: in a saved plan this slot picks a rotation topic again every time the plan is used. "
                                 "Off: the plan keeps this topic.");
        randomButton.onClick = [this] { if (onRandom) onRandom (randomButton.getToggleState()); };

        for (auto* c : std::initializer_list<juce::Component*> { &minutesBox, &randomButton, &rerollButton, &removeButton })
            addAndMakeVisible (*c);
    }

    std::function<void (int)> onMinutes;
    std::function<void (bool)> onRandom;
    std::function<void()> onReroll, onRemove;

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panel());
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        auto r = getLocalBounds().reduced (12, 0).withRight (minutesBox.getX() - 8);
        g.setColour (axisText());
        g.setFont (juce::FontOptions (15.0f).withStyle ("Bold"));
        g.drawText (juce::String (index) + ".", r.removeFromLeft (28), juce::Justification::centredLeft);

        if (randomButton.getToggleState())
        {
            const juce::Font small (juce::FontOptions (12.0f));
            g.setFont (small);
            g.setColour (axisText());
            const auto note = tag.isNotEmpty() ? "random #" + tag : juce::String ("random");
            g.drawText (note, r.removeFromRight (juce::jmin (r.getWidth() / 2, (int) textWidth (small, note) + 8)),
                        juce::Justification::centredRight, true);
        }

        g.setColour (missing ? theme::record() : primaryText());
        g.setFont (juce::FontOptions (15.0f).withStyle (missing ? "Italic" : "Bold"));
        g.drawText (title, r, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        removeButton.setBounds (r.removeFromRight (40));
        r.removeFromRight (6);
        rerollButton.setBounds (r.removeFromRight (80));
        r.removeFromRight (6);
        randomButton.setBounds (r.removeFromRight (80));
        r.removeFromRight (6);
        minutesBox.setBounds (r.removeFromRight (96));
    }

private:
    int index;
    juce::String title, tag;
    bool missing;
    juce::ComboBox minutesBox;
    juce::TextButton randomButton { "Random" }, rerollButton { "Re-roll" }, removeButton { "X" };
};

SessionPlanner::SessionPlanner (CandidatesFn fn, TopicTitleFn titles, PlanList* planList, const juce::StringArray& tags,
                                int minutes, const juce::String& tag, const juce::String& planId)
    : candidatesFor (std::move (fn)), titleOf (std::move (titles)), plans (planList)
{
    addAndMakeVisible (styleCaption (planLabel, "Plan"));
    addAndMakeVisible (styleCaption (timeLabel, "I have"));
    addAndMakeVisible (styleCaption (focusLabel, "Focus"));

    planBox.setTooltip ("Plan on the spot, or start from one of your saved plans");
    planBox.onChange = [this]
    {
        const auto i = planBox.getSelectedId() - 100;

        if (plans != nullptr && juce::isPositiveAndBelow (i, (int) plans->getPlans().size()))
            loadPlan (plans->getPlans()[(size_t) i].id);
        else
            replan();
    };
    addAndMakeVisible (planBox);

    saveButton.setTooltip ("Keep this session as a named plan (stored with the library, so your other devices get it too)");
    saveButton.onClick = [this] { showSaveMenu(); };
    addAndMakeVisible (saveButton);

    planMenuButton.setTooltip ("Rename, duplicate or delete the saved plan");
    planMenuButton.onClick = [this] { showPlanMenu(); };
    addAndMakeVisible (planMenuButton);

    for (int m : { 15, 20, 30, 45, 60, 75, 90, 120 })
        timeBox.addItem (juce::String (m) + " minutes", m);

    timeBox.setSelectedId (timeBox.indexOfItemId (minutes) >= 0 ? minutes : 45, juce::dontSendNotification);
    timeBox.onChange = [this] { replan(); };
    addAndMakeVisible (timeBox);

    focusBox.addItem ("All rotation topics", 1);

    for (int i = 0; i < tags.size(); ++i)
        focusBox.addItem ("#" + tags[i], i + 2);

    const auto tagIndex = tags.indexOf (tag, true);
    focusBox.setSelectedId (tagIndex >= 0 ? tagIndex + 2 : 1, juce::dontSendNotification);
    focusBox.onChange = [this] { replan(); };
    focusBox.setTooltip ("Only topics with this tag (on the topic or one of its resources)");
    addAndMakeVisible (focusBox);

    shuffleButton.onClick = [this] { replan(); };
    shuffleButton.setTooltip ("Plan on the spot again (neglected topics come up more often)");
    addAndMakeVisible (shuffleButton);

    addButton.onClick = [this] { showAddMenu(); };
    rowHolder.addAndMakeVisible (addButton);
    rowViewport.setViewedComponent (&rowHolder, false);
    rowViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (rowViewport);

    totalLabel.setColour (juce::Label::textColourId, primaryText());
    totalLabel.setFont (juce::FontOptions (15.0f));
    addAndMakeVisible (totalLabel);

    statusLabel.setColour (juce::Label::textColourId, axisText());
    statusLabel.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (statusLabel);

    emptyLabel.setColour (juce::Label::textColourId, axisText());
    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setText ("No rotation topics to pick from. Tick \"In practice rotation\" on topics in the Topics tab.",
                        juce::dontSendNotification);
    addChildComponent (emptyLabel);

    startButton.setColour (juce::TextButton::buttonColourId, theme::accent());
    startButton.onClick = [this]
    {
        auto playable = playableSlots (slots);

        if (onStart && ! playable.empty())
            onStart (std::move (playable), timeBox.getSelectedId(), currentTag());
    };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (startButton);
    addAndMakeVisible (cancelButton);

    if (plans != nullptr)
        plans->reloadIfChanged();

    if (plans != nullptr && planId.isNotEmpty() && plans->find (planId) != nullptr)
        loadPlan (planId);
    else
        replan();
}

SessionPlanner::~SessionPlanner() = default;

juce::String SessionPlanner::currentTag() const
{
    return focusBox.getSelectedId() > 1 ? focusBox.getText().substring (1) : juce::String();
}

const SessionPlan* SessionPlanner::loadedPlan() const
{
    return plans != nullptr && loadedPlanId.isNotEmpty() ? plans->find (loadedPlanId) : nullptr;
}

void SessionPlanner::replan()
{
    loadedPlanId = {};
    edited = false;
    candidates = candidatesFor ? candidatesFor (currentTag()) : std::vector<PlanCandidate> {};
    slots = planSession (candidates, timeBox.getSelectedId(), random);
    refreshPlanBox();
    rebuildRows();
}

void SessionPlanner::loadPlan (const juce::String& planId)
{
    if (plans != nullptr)
        plans->reloadIfChanged();

    const auto* plan = plans != nullptr ? plans->find (planId) : nullptr;

    if (plan == nullptr)
    {
        replan();
        return;
    }

    loadedPlanId = planId;
    edited = false;
    candidates = candidatesFor ? candidatesFor (currentTag()) : std::vector<PlanCandidate> {};
    slots = slotsFromPlan (*plan, titleOf, candidatesFor, random);
    refreshPlanBox();
    rebuildRows();
}

void SessionPlanner::refreshPlanBox()
{
    planBox.clear (juce::dontSendNotification);
    planBox.addItem ("Plan on the spot", 1);
    int selected = 1;

    if (plans != nullptr && ! plans->getPlans().empty())
    {
        planBox.addSeparator();
        planBox.addSectionHeading ("Saved plans");

        for (size_t i = 0; i < plans->getPlans().size(); ++i)
        {
            const auto& p = plans->getPlans()[i];
            planBox.addItem (p.name, 100 + (int) i);

            if (p.id == loadedPlanId)
                selected = 100 + (int) i;
        }
    }

    if (selected == 1)
        loadedPlanId = {};

    planBox.setSelectedId (selected, juce::dontSendNotification);
    planBox.setEnabled (plans != nullptr && ! plans->getPlans().empty());
    saveButton.setEnabled (plans != nullptr);

    if (plans == nullptr)
        saveButton.setTooltip ("Choose a library folder first: plans are kept in it");

    planMenuButton.setEnabled (loadedPlan() != nullptr);
}

void SessionPlanner::slotsEdited()
{
    edited = true;
    updateTotal();
}

void SessionPlanner::rebuildRows()
{
    rows.clear();

    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto row = std::make_unique<SlotRow> ((int) i + 1, slots[i]);
        row->onMinutes = [this, i] (int m) { if (i < slots.size()) { slots[i].minutes = m; slotsEdited(); } };
        row->onRandom = [this, i] (bool on)
        {
            if (i >= slots.size())
                return;

            slots[i].random = on;

            if (on && ! slots[i].missing)
                slots[i].tag = currentTag();

            edited = true;
            rebuildRows();
        };
        row->onReroll = [this, i]
        {
            if (i >= slots.size())
                return;

            const auto pool = slots[i].random && candidatesFor ? candidatesFor (slots[i].tag) : candidates;

            if (rerollSlot (slots, i, pool, random))
            {
                edited = true;
                rebuildRows();
            }
        };
        row->onRemove = [this, i]
        {
            if (i < slots.size())
            {
                slots.erase (slots.begin() + (std::ptrdiff_t) i);
                edited = true;
                rebuildRows();
            }
        };
        rowHolder.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    emptyLabel.setVisible (candidates.empty() && slots.empty());
    updateTotal();
    resized();
}

void SessionPlanner::updateTotal()
{
    const auto playable = playableSlots (slots);
    totalLabel.setText (playable.empty() ? juce::String() : "Total: " + juce::String (totalMinutes (playable)) + " min, "
                                                              + juce::String ((int) playable.size()) + (playable.size() == 1 ? " topic" : " topics"),
                        juce::dontSendNotification);
    startButton.setEnabled (! playable.empty());

    juce::StringArray notes;

    if (playable.size() < slots.size())
        notes.add ("Missing topics are skipped (Re-roll or X to fix them)");

    if (const auto* plan = loadedPlan())
        notes.add (edited ? "Changed for today - \"" + plan->name + "\" stays as saved unless you save it"
                          : "Saved plan \"" + plan->name + "\"");

    statusLabel.setText (notes.joinIntoString (". "), juce::dontSendNotification);

    juce::StringArray used;

    for (auto& s : slots)
        used.add (s.topicId);

    addButton.setEnabled (pickWeighted (candidates, random, used) >= 0 && slots.size() < 8);
    planMenuButton.setEnabled (loadedPlan() != nullptr);
}

void SessionPlanner::showAddMenu()
{
    juce::StringArray used;

    for (auto& s : slots)
        used.add (s.topicId);

    auto addPick = [this, used] (int pick)
    {
        if (! juce::isPositiveAndBelow (pick, (int) candidates.size()) || used.contains (candidates[(size_t) pick].topicId))
            return;

        slots.push_back ({ candidates[(size_t) pick].topicId, candidates[(size_t) pick].title, 10 });
        edited = true;
        rebuildRows();
    };

    juce::PopupMenu menu;
    menu.addItem ("Roll the dice", [this, used, addPick] { addPick (pickWeighted (candidates, random, used)); });
    menu.addSeparator();

    for (int i = 0; i < (int) candidates.size(); ++i)
        if (! used.contains (candidates[(size_t) i].topicId))
            menu.addItem (candidates[(size_t) i].title, [addPick, i] { addPick (i); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton));
}

void SessionPlanner::askName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done)
{
    auto* window = new juce::AlertWindow (title, "Name:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", initial);
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe = SafePointer<SessionPlanner> (this), window, done] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();

        if (safe != nullptr && result == 1 && name.isNotEmpty())
            done (name);
    }), true);
}

void SessionPlanner::saveAsNew (const juce::String& name)
{
    if (plans == nullptr)
        return;

    plans->reloadIfChanged();

    auto store = [safe = SafePointer<SessionPlanner> (this), name]
    {
        if (safe == nullptr || safe->plans == nullptr)
            return;

        auto& list = *safe->plans;
        const auto slotsToSave = planSlotsFrom (safe->slots);

        if (const auto* existing = list.findByName (name))
        {
            const auto id = existing->id;
            list.setSlots (id, slotsToSave);
            safe->loadedPlanId = id;
        }
        else
        {
            safe->loadedPlanId = list.add (name, slotsToSave);
        }

        safe->edited = false;
        safe->refreshPlanBox();
        safe->updateTotal();
    };

    if (plans->findByName (name) != nullptr && (loadedPlan() == nullptr || ! loadedPlan()->name.equalsIgnoreCase (name)))
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions{}.withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Save plan").withMessage ("A plan called \"" + name + "\" exists. Replace it?")
                                          .withButton ("Replace", 1).withButton ("Cancel", 0).withAssociatedComponent (this),
                                      [store] (int r) { if (r == 1) store(); });
        return;
    }

    store();
}

void SessionPlanner::showSaveMenu()
{
    if (plans == nullptr || slots.empty())
        return;

    auto askNew = [this]
    {
        askName ("Save as plan", plans->uniqueName (juce::String (totalMinutes (slots)) + " min plan"),
                 [this] (juce::String name) { saveAsNew (name); });
    };

    const auto* plan = loadedPlan();

    if (plan == nullptr)
    {
        askNew();
        return;
    }

    juce::PopupMenu menu;
    menu.addItem ("Save changes to \"" + plan->name + "\"", edited, false, [this]
    {
        plans->reloadIfChanged();

        if (plans->setSlots (loadedPlanId, planSlotsFrom (slots)))
        {
            edited = false;
            refreshPlanBox();
            updateTotal();
        }
    });
    menu.addItem ("Save as a new plan...", askNew);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&saveButton));
}

void SessionPlanner::showPlanMenu()
{
    const auto* plan = loadedPlan();

    if (plan == nullptr)
        return;

    const auto id = plan->id, name = plan->name;
    juce::PopupMenu menu;

    menu.addItem ("Rename...", [this, id, name]
    {
        askName ("Rename plan", name, [this, id] (juce::String newName)
        {
            plans->reloadIfChanged();

            if (const auto* other = plans->findByName (newName); other != nullptr && other->id != id)
            {
                statusLabel.setText ("There's already a plan called \"" + newName + "\"", juce::dontSendNotification);
                return;
            }

            plans->rename (id, newName);
            refreshPlanBox();
            updateTotal();
        });
    });

    menu.addItem ("Duplicate", [this, id]
    {
        plans->reloadIfChanged();
        const auto copy = plans->duplicate (id);

        if (copy.isNotEmpty())
            loadPlan (copy);
    });

    menu.addItem ("Delete...", [this, id, name]
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions{}.withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Delete plan").withMessage ("Delete the plan \"" + name + "\"? Your practice log is not affected.")
                                          .withButton ("Delete", 1).withButton ("Cancel", 0).withAssociatedComponent (this),
                                      [safe = SafePointer<SessionPlanner> (this), id] (int result)
        {
            if (safe == nullptr || result != 1 || safe->plans == nullptr)
                return;

            safe->plans->reloadIfChanged();
            safe->plans->remove (id);
            safe->loadedPlanId = {};   // today's slots stay, now as a plan on the spot
            safe->edited = false;
            safe->refreshPlanBox();
            safe->updateTotal();
        });
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&planMenuButton));
}

void SessionPlanner::paint (juce::Graphics& g)
{
    g.setColour (theme::background());
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void SessionPlanner::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto planRow = area.removeFromTop (36);
    planLabel.setBounds (planRow.removeFromLeft (70));
    planMenuButton.setBounds (planRow.removeFromRight (80).reduced (0, 2));
    planRow.removeFromRight (6);
    saveButton.setBounds (planRow.removeFromRight (130).reduced (0, 2));
    planRow.removeFromRight (10);
    planBox.setBounds (planRow.reduced (0, 2));
    area.removeFromTop (6);

    auto top = area.removeFromTop (36);
    timeLabel.setBounds (top.removeFromLeft (70));
    timeBox.setBounds (top.removeFromLeft (150).reduced (0, 2));
    top.removeFromLeft (20);
    focusLabel.setBounds (top.removeFromLeft (60));
    shuffleButton.setBounds (top.removeFromRight (100).reduced (0, 2));
    top.removeFromRight (10);
    focusBox.setBounds (top.reduced (0, 2));

    auto bottom = area.removeFromBottom (44);
    startButton.setBounds (bottom.removeFromRight (150).reduced (3));
    cancelButton.setBounds (bottom.removeFromRight (110).reduced (3));
    totalLabel.setBounds (bottom);
    statusLabel.setBounds (area.removeFromBottom (22));

    area.removeFromTop (14);
    emptyLabel.setBounds (area);
    rowViewport.setBounds (area);

    constexpr int rowHeight = 48, gap = 6;
    const auto width = area.getWidth() - ((int) rows.size() * (rowHeight + gap) + 36 > area.getHeight() ? rowViewport.getScrollBarThickness() : 0);
    int y = 0;

    for (auto& row : rows)
    {
        row->setBounds (0, y, width, rowHeight);
        y += rowHeight + gap;
    }

    addButton.setBounds (0, y, 160, 36);
    rowHolder.setSize (width, y + 36);
}

//==============================================================================
/** One measure over time: thin line, 8 px dots, latest (and best) value labelled, hover shows a session. */
class TopicHistory::LineChart final : public juce::Component
{
public:
    struct Point { juce::Time when; double value; double minutes; };

    LineChart (const juce::String& titleText, const juce::String& unitText, std::vector<Point> pts)
        : title (titleText), unit (unitText), points (std::move (pts))
    {
        setRepaintsOnMouseActivity (true);
    }

    bool hasData() const noexcept    { return ! points.empty(); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        g.setColour (primaryText());
        g.setFont (juce::FontOptions (15.0f).withStyle ("Bold"));
        g.drawText (title, area.removeFromTop (24), juce::Justification::centredLeft);

        if (points.empty())
            return;

        layout();
        const juce::Font small (juce::FontOptions (12.0f));
        g.setFont (small);

        // Recessive grid with y labels.
        for (auto v = yMin; v <= yMax + yStep * 0.5; v += yStep)
        {
            const auto y = yFor (v);
            g.setColour (gridColour());
            g.fillRect (plot.getX(), juce::roundToInt (y), plot.getWidth(), 1);
            g.setColour (axisText());
            g.drawText (juce::String (juce::roundToInt (v)), 0, juce::roundToInt (y) - 8, plot.getX() - 8, 16, juce::Justification::centredRight);
        }

        // X labels: first, last (and the middle when there's room).
        g.setColour (axisText());
        const auto labelY = plot.getBottom() + 4;
        g.drawText (points.front().when.formatted ("%d %b"), plot.getX(), labelY, 80, 16, juce::Justification::centredLeft);

        if (points.size() > 1)
            g.drawText (points.back().when.formatted ("%d %b"), plot.getRight() - 80, labelY, 80, 16, juce::Justification::centredRight);

        // Line, then markers with a surface ring so overlapping dots stay distinct.
        if (points.size() > 1)
        {
            juce::Path line;

            for (size_t i = 0; i < points.size(); ++i)
            {
                const auto p = posFor (points[i]);

                if (i == 0) line.startNewSubPath (p);
                else        line.lineTo (p);
            }

            g.setColour (seriesColour());
            g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto p = posFor (points[i]);
            const auto hovered = (int) i == hoverIndex;
            const auto r = hovered ? 6.0f : 4.0f;
            g.setColour (theme::background());
            g.fillEllipse (p.x - r - 2.0f, p.y - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f);
            g.setColour (seriesColour());
            g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        }

        // Direct labels: the latest value, and the best one if it isn't the latest.
        auto best = std::max_element (points.begin(), points.end(), [] (const Point& a, const Point& b) { return a.value < b.value; });
        g.setFont (juce::FontOptions (12.5f).withStyle ("Bold"));
        g.setColour (primaryText());
        labelPoint (g, points.back(), "now " + format (points.back().value));

        if (best != points.end() - 1 && best->value > points.back().value)
            labelPoint (g, *best, "best " + format (best->value));

        if (juce::isPositiveAndBelow (hoverIndex, (int) points.size()))
        {
            const auto& p = points[(size_t) hoverIndex];
            drawTooltip (g, posFor (p), p.when.formatted ("%a %d %b") + ": " + format (p.value) + ", "
                                            + formatPracticeDuration (p.minutes * 60.0), getLocalBounds());
        }
    }

    void mouseMove (const juce::MouseEvent& e) override    { setHover (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override    { setHover (e.position); }
    void mouseExit (const juce::MouseEvent&) override       { hoverIndex = -1; repaint(); }

private:
    void layout()
    {
        plot = getLocalBounds().withTrimmedTop (34).withTrimmedBottom (22).withTrimmedLeft (44).withTrimmedRight (40);

        auto lo = points.front().value, hi = lo;

        for (auto& p : points)
        {
            lo = juce::jmin (lo, p.value);
            hi = juce::jmax (hi, p.value);
        }

        if (hi - lo < 1.0e-9)
        {
            lo -= 5.0;
            hi += 5.0;
        }

        yStep = niceStep (hi - lo, 3);
        yMin = std::floor (lo / yStep) * yStep;
        yMax = std::ceil (hi / yStep) * yStep;

        t0 = (double) points.front().when.toMilliseconds();
        t1 = (double) points.back().when.toMilliseconds();
    }

    float yFor (double v) const
    {
        return (float) juce::jmap (v, yMin, yMax, (double) plot.getBottom(), (double) plot.getY());
    }

    juce::Point<float> posFor (const Point& p) const
    {
        const auto x = t1 > t0 ? juce::jmap ((double) p.when.toMilliseconds(), t0, t1, (double) plot.getX(), (double) plot.getRight())
                               : (double) plot.getCentreX();
        return { (float) x, yFor (p.value) };
    }

    juce::String format (double v) const     { return juce::String (juce::roundToInt (v)) + unit; }

    void labelPoint (juce::Graphics& g, const Point& p, const juce::String& text)
    {
        const auto pos = posFor (p);
        const auto w = textWidth (g.getCurrentFont(), text) + 4.0f;
        auto x = juce::jlimit (0.0f, (float) getWidth() - w, pos.x - w * 0.5f);
        g.drawText (text, juce::Rectangle<float> (x, pos.y - 26.0f, w, 16.0f), juce::Justification::centred, false);
    }

    void setHover (juce::Point<float> pos)
    {
        int best = -1;
        float bestDistance = 18.0f;

        if (! points.empty())
        {
            layout();

            for (size_t i = 0; i < points.size(); ++i)
            {
                const auto d = std::abs (posFor (points[i]).x - pos.x);

                if (d < bestDistance)
                {
                    bestDistance = d;
                    best = (int) i;
                }
            }
        }

        if (best != hoverIndex)
        {
            hoverIndex = best;
            repaint();
        }
    }

    juce::String title, unit;
    std::vector<Point> points;
    juce::Rectangle<int> plot;
    double yMin = 0.0, yMax = 1.0, yStep = 1.0, t0 = 0.0, t1 = 0.0;
    int hoverIndex = -1;
};

//==============================================================================
/** Minutes per week: bars from the baseline with rounded tops, a 2 px gap, hover shows the week. */
class TopicHistory::WeekBars final : public juce::Component
{
public:
    WeekBars (std::vector<double> secondsPerWeek, juce::Time thisWeek)
        : weeks (std::move (secondsPerWeek)), lastWeekStart (thisWeek)
    {
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        g.setColour (primaryText());
        g.setFont (juce::FontOptions (15.0f).withStyle ("Bold"));
        g.drawText ("Minutes per week", area.removeFromTop (24), juce::Justification::centredLeft);

        if (weeks.empty())
            return;

        const auto plot = plotArea();
        auto maxMinutes = 0.0;

        for (auto s : weeks)
            maxMinutes = juce::jmax (maxMinutes, s / 60.0);

        const auto step = niceStep (juce::jmax (10.0, maxMinutes), 2);
        const auto top = juce::jmax (step, std::ceil (maxMinutes / step) * step);
        const juce::Font small (juce::FontOptions (12.0f));
        g.setFont (small);

        for (auto v = 0.0; v <= top + step * 0.5; v += step)
        {
            const auto y = juce::roundToInt (juce::jmap (v, 0.0, top, (double) plot.getBottom(), (double) plot.getY()));
            g.setColour (gridColour());
            g.fillRect (plot.getX(), y, plot.getWidth(), 1);
            g.setColour (axisText());
            g.drawText (juce::String (juce::roundToInt (v)), 0, y - 8, plot.getX() - 8, 16, juce::Justification::centredRight);
        }

        const auto slot = (float) plot.getWidth() / (float) weeks.size();

        for (size_t i = 0; i < weeks.size(); ++i)
        {
            const auto minutes = weeks[i] / 60.0;
            const auto x = (float) plot.getX() + slot * (float) i + 1.0f;   // 2 px gap between bars
            const auto w = juce::jmax (1.0f, slot - 2.0f);

            if (minutes > 0.0)
            {
                const auto h = (float) juce::jmap (minutes, 0.0, top, 0.0, (double) plot.getHeight());
                auto bar = juce::Rectangle<float> (x, (float) plot.getBottom() - h, w, h);
                g.setColour ((int) i == hoverIndex ? seriesColour().brighter (0.25f) : seriesColour());

                // Rounded only at the data end; square on the baseline.
                juce::Path p;
                p.addRoundedRectangle (bar.getX(), bar.getY(), bar.getWidth(), bar.getHeight(),
                                       juce::jmin (4.0f, h, w * 0.5f), juce::jmin (4.0f, h, w * 0.5f), true, true, false, false);
                g.fillPath (p);
            }

            if (i == 0 || i == weeks.size() - 1 || (weeks.size() > 6 && i == weeks.size() / 2))
            {
                g.setColour (axisText());
                g.drawText (weekStart (i).formatted ("%d %b"), juce::roundToInt (x + w * 0.5f) - 40, plot.getBottom() + 4, 80, 16,
                            juce::Justification::centred);
            }
        }

        if (juce::isPositiveAndBelow (hoverIndex, (int) weeks.size()))
        {
            const auto i = (size_t) hoverIndex;
            const auto x = (float) plot.getX() + slot * ((float) i + 0.5f);
            const auto h = (float) juce::jmap (weeks[i] / 60.0, 0.0, top, 0.0, (double) plot.getHeight());
            drawTooltip (g, { x, (float) plot.getBottom() - h }, "Week of " + weekStart (i).formatted ("%d %b") + ": "
                                                                    + formatPracticeDuration (weeks[i]), getLocalBounds());
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto plot = plotArea();
        const auto i = (int) std::floor ((e.position.x - (float) plot.getX()) / ((float) plot.getWidth() / (float) juce::jmax ((size_t) 1, weeks.size())));
        const auto newIndex = juce::isPositiveAndBelow (i, (int) weeks.size()) && plot.toFloat().expanded (0, 20).contains (e.position) ? i : -1;

        if (newIndex != hoverIndex)
        {
            hoverIndex = newIndex;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override     { hoverIndex = -1; repaint(); }

private:
    juce::Rectangle<int> plotArea() const
    {
        return getLocalBounds().withTrimmedTop (34).withTrimmedBottom (22).withTrimmedLeft (44).withTrimmedRight (12);
    }

    juce::Time weekStart (size_t i) const
    {
        const auto back = (int) (weeks.size() - 1 - i) * 7;
        return juce::Time (lastWeekStart.getYear(), lastWeekStart.getMonth(), lastWeekStart.getDayOfMonth() - back, 0, 0, 0, 0, true);
    }

    std::vector<double> weeks;
    juce::Time lastWeekStart;
    int hoverIndex = -1;
};

//==============================================================================
/** All-topics view: played time per topic over the last weeks, longest first, as horizontal bars.
    Tapping a topic's bar opens its own history. */
class TopicHistory::TopicBars final : public juce::Component
{
public:
    struct Row { juce::String topicId, title; double seconds = 0.0; bool clickable = false; };

    TopicBars (const juce::String& titleText, std::vector<Row> allRows) : title (titleText)
    {
        std::stable_sort (allRows.begin(), allRows.end(), [] (const Row& a, const Row& b) { return a.seconds > b.seconds; });

        constexpr size_t maxRows = 8;

        if (allRows.size() > maxRows + 1)
        {
            Row other;
            const auto rest = allRows.size() - maxRows;

            for (auto i = maxRows; i < allRows.size(); ++i)
                other.seconds += allRows[i].seconds;

            other.title = "Other (" + juce::String ((int) rest) + " topics)";
            allRows.resize (maxRows);
            allRows.push_back (other);
        }

        rows = std::move (allRows);
        setRepaintsOnMouseActivity (true);
    }

    bool hasData() const noexcept    { return ! rows.empty(); }

    std::function<void (const juce::String&)> onClick;

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        g.setColour (primaryText());
        g.setFont (juce::FontOptions (15.0f).withStyle ("Bold"));
        g.drawText (title, area.removeFromTop (24), juce::Justification::centredLeft);

        if (rows.empty())
            return;

        auto maxSeconds = 0.0;

        for (auto& r : rows)
            maxSeconds = juce::jmax (maxSeconds, r.seconds);

        const auto rowH = rowHeight();
        const auto labelW = labelWidth();
        const juce::Font font (juce::FontOptions (juce::jmin (14.0f, (float) rowH - 4.0f)));
        g.setFont (font);

        for (size_t i = 0; i < rows.size(); ++i)
        {
            const auto& r = rows[i];
            auto line = juce::Rectangle<int> (0, top + (int) i * rowH, getWidth(), rowH);
            const auto hovered = (int) i == hoverIndex && r.clickable;

            if (hovered)
            {
                g.setColour (theme::text().withAlpha (0.07f));
                g.fillRoundedRectangle (line.toFloat(), 4.0f);
            }

            g.setColour (r.clickable ? primaryText() : axisText());
            g.drawText (r.title, line.removeFromLeft (labelW).reduced (4, 0), juce::Justification::centredLeft, true);

            const auto valueText = formatPracticeDuration (r.seconds);
            const auto valueW = juce::roundToInt (textWidth (font, valueText)) + 12;
            const auto barSpace = juce::jmax (1, line.getWidth() - valueW);
            const auto w = maxSeconds > 0.0 ? (float) (r.seconds / maxSeconds) * (float) barSpace : 0.0f;
            const auto bar = juce::Rectangle<float> ((float) line.getX(), (float) line.getY() + 3.0f,
                                                     juce::jmax (2.0f, w), (float) rowH - 6.0f);

            juce::Path p;   // rounded only at the data end; square on the baseline
            const auto radius = juce::jmin (4.0f, bar.getWidth() * 0.5f, bar.getHeight() * 0.5f);
            p.addRoundedRectangle (bar.getX(), bar.getY(), bar.getWidth(), bar.getHeight(), radius, radius, false, true, false, true);
            g.setColour (hovered ? seriesColour().brighter (0.25f) : seriesColour());
            g.fillPath (p);

            g.setColour (axisText());
            g.drawText (valueText, juce::roundToInt (bar.getRight()) + 6, line.getY(), valueW, rowH, juce::Justification::centredLeft, false);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto i = indexAt (e.y);
        setMouseCursor (i >= 0 && rows[(size_t) i].clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);

        if (i != hoverIndex)
        {
            hoverIndex = i;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override     { hoverIndex = -1; repaint(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto i = indexAt (e.y);

        if (i >= 0 && e.mouseWasClicked() && rows[(size_t) i].clickable && onClick)
            onClick (rows[(size_t) i].topicId);
    }

private:
    static constexpr int top = 32;

    int rowHeight() const
    {
        return juce::jlimit (16, 28, (getHeight() - top) / juce::jmax (1, (int) rows.size()));
    }

    int labelWidth() const     { return juce::jlimit (110, 260, getWidth() / 3); }

    int indexAt (int y) const
    {
        const auto i = (y - top) / rowHeight();
        return y >= top && juce::isPositiveAndBelow (i, (int) rows.size()) ? i : -1;
    }

    juce::String title;
    std::vector<Row> rows;
    int hoverIndex = -1;
};

//==============================================================================
/** Every session and take of the topic, newest first; sessions show their note, takes can be tapped. */
class TopicHistory::SessionList final : public juce::Component
{
public:
    SessionList (std::vector<PracticeEntry> entries, std::vector<TakeRow> takes, std::function<void (const juce::File&)> open,
                 bool showTopicTitles = false)
        : onOpen (std::move (open)), showTopics (showTopicTitles)
    {
        for (auto& e : entries)
            items.push_back ({ e.start, false, e, {} });

        for (auto& t : takes)
            items.push_back ({ t.when, true, {}, t });

        std::stable_sort (items.begin(), items.end(), [] (const Item& x, const Item& y) { return x.when > y.when; });
    }

    std::function<void (const juce::String& entryId)> onEdit;

    int contentHeight() const
    {
        int h = 0;

        for (auto& item : items)
            h += heightOf (item);

        return juce::jmax (30, h);
    }

    void paint (juce::Graphics& g) override
    {
        int y = 0;

        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];
            auto r = juce::Rectangle<int> (4, y, getWidth() - 8, 26);

            if ((int) i == hoverIndex)
            {
                g.setColour (theme::text().withAlpha (0.07f));
                g.fillRoundedRectangle (r.withHeight (heightOf (item)).toFloat(), 4.0f);
            }

            y += heightOf (item);

            if (item.isTake)
            {
                g.setFont (juce::FontOptions (14.0f));
                g.setColour (axisText());
                g.drawText (formatTakeLength (item.take.seconds) + "  -  tap to replay", r, juce::Justification::centredRight, true);
                g.setColour (seriesColour());
                g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6  Take  ")) + item.when.formatted ("%a %d %b %Y, %H:%M")
                                + "  -  " + item.take.title,
                            r, juce::Justification::centredLeft, true);
                continue;
            }

            const auto& e = item.entry;
            juce::StringArray detail { formatPracticeDuration (e.playedSeconds) };

            if (e.maxBpm > 0.0)    detail.add (juce::String (juce::roundToInt (e.maxBpm)) + " bpm");
            if (e.maxSpeed > 0.0)  detail.add (juce::String (juce::roundToInt (e.maxSpeed * 100.0)) + " %");
            if (e.looper)          detail.add ("looper");
            if (e.noGuitar)        detail.add ("no guitar");
            if (e.manual)          detail.add ("added by hand");

            const juce::Font rowFont (juce::FontOptions (14.0f));
            const auto detailText = detail.joinIntoString ("  -  ");
            g.setFont (rowFont);
            g.setColour (axisText());
            g.drawText (detailText, r, juce::Justification::centredRight, true);

            auto left = r.withTrimmedRight (juce::roundToInt (textWidth (rowFont, detailText)) + 16);
            const auto when = e.start.formatted ("%a %d %b %Y, %H:%M");
            g.setColour (primaryText());

            if (showTopics)
            {
                const auto whenW = juce::roundToInt (textWidth (rowFont, when)) + 18;
                g.drawText (when, left.removeFromLeft (whenW), juce::Justification::centredLeft, true);
                g.setColour (e.isFree() ? axisText() : seriesColour());
                g.drawText (e.topicTitle, left, juce::Justification::centredLeft, true);
            }
            else
            {
                g.drawText (when, left, juce::Justification::centredLeft, true);
            }

            if (e.note.isNotEmpty())
            {
                g.setColour (theme::highlight().withAlpha (0.85f));
                g.setFont (juce::FontOptions (13.0f).withStyle ("Italic"));
                g.drawText (e.note.replaceCharacters ("\r\n", "  "), r.translated (16, 20).withHeight (18), juce::Justification::centredLeft, true);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto index = indexAt (e.y);
        setMouseCursor (index >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);

        if (index != hoverIndex)
        {
            hoverIndex = index;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override       { hoverIndex = -1; repaint(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto index = indexAt (e.y);

        if (index < 0 || ! e.mouseWasClicked())
            return;

        const auto& item = items[(size_t) index];

        if (item.isTake)
        {
            if (onOpen)
                onOpen (item.take.file);
        }
        else if (onEdit)
        {
            onEdit (item.entry.id);
        }
    }

private:
    struct Item
    {
        juce::Time when;
        bool isTake = false;
        PracticeEntry entry;
        TakeRow take;
    };

    static int heightOf (const Item& item)     { return ! item.isTake && item.entry.note.isNotEmpty() ? 46 : 26; }

    static juce::String formatTakeLength (double seconds)
    {
        const auto total = juce::roundToInt (seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    int indexAt (int y) const
    {
        int top = 0;

        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto h = heightOf (items[i]);

            if (y >= top && y < top + h)
                return (int) i;

            top += h;
        }

        return -1;
    }

    std::vector<Item> items;
    std::function<void (const juce::File&)> onOpen;
    bool showTopics = false;
    int hoverIndex = -1;
};

//==============================================================================
TopicHistory::TopicHistory (const PracticeLog& log, const juce::String& topicId, std::vector<TakeRow> takes,
                            const std::map<juce::String, juce::String>& topicTitles)
{
    getProperties().set ("fillOverlay", true);

    const auto all = topicId == allTopicsId;
    const auto now = juce::Time::getCurrentTime();
    constexpr int numWeeks = 12;

    std::vector<PracticeEntry> entries;
    std::vector<LineChart::Point> tempo, speed;

    if (all)
    {
        for (auto& e : log.getEntries())
        {
            entries.push_back (e);
            auto& copy = entries.back();

            if (copy.isFree())
                copy.topicTitle = "Free practice";
            else if (auto it = topicTitles.find (copy.topicId); it != topicTitles.end() && it->second.isNotEmpty())
                copy.topicTitle = it->second;
            else if (copy.topicTitle.isEmpty())
                copy.topicTitle = "(untitled)";
        }
    }
    else
    {
        for (auto* e : log.historyFor (topicId))
        {
            entries.push_back (*e);

            if (e->maxBpm > 0.0)
                tempo.push_back ({ e->end, e->maxBpm, e->playedSeconds / 60.0 });

            if (e->maxSpeed > 0.0)
                speed.push_back ({ e->end, e->maxSpeed * 100.0, e->playedSeconds / 60.0 });
        }
    }

    const auto numTakes = (int) takes.size();

    if (all && ! entries.empty())
    {
        // Totals, and time per topic over the same weeks as the bar chart.
        const auto from = PracticeLog::startOfWeek (now) - juce::RelativeTime::weeks (numWeeks - 1);
        std::map<juce::String, TopicBars::Row> perTopic;
        juce::StringArray topicsEver;
        auto total = 0.0;

        for (auto& e : entries)
        {
            total += e.playedSeconds;

            if (! e.isFree())
                topicsEver.addIfNotAlreadyThere (e.topicId);

            if (e.start < from)
                continue;

            auto& row = perTopic[e.topicId];
            row.topicId = e.topicId;
            row.title = e.topicTitle;
            row.clickable = ! e.isFree() && topicTitles.count (e.topicId) > 0;
            row.seconds += e.playedSeconds;
        }

        juce::StringArray stats;
        stats.add (juce::String ((int) entries.size()) + (entries.size() == 1 ? " session" : " sessions"));
        stats.add (formatPracticeDuration (total) + " in total");
        stats.add (juce::String (topicsEver.size()) + (topicsEver.size() == 1 ? " topic" : " topics"));
        stats.add ("since " + entries.front().start.formatted ("%d %b %Y"));
        stats.add ("this week " + formatPracticeDuration (log.playedBetween (PracticeLog::startOfWeek (now),
                                                                             now + juce::RelativeTime::days (1))));
        statsLabel.setText (stats.joinIntoString ("     "), juce::dontSendNotification);

        std::vector<TopicBars::Row> rows;

        for (auto& [id, row] : perTopic)
            if (row.seconds > 0.0)
                rows.push_back (row);

        topicBars = std::make_unique<TopicBars> ("Time per topic (last " + juce::String (numWeeks) + " weeks)", std::move (rows));
        topicBars->onClick = [this] (const juce::String& id) { if (onShowTopic) onShowTopic (id); };
        addChildComponent (*topicBars);
        topicBars->setVisible (topicBars->hasData());
    }
    else if (! entries.empty())
    {
        const auto summary = log.summarise (topicId);
        juce::StringArray stats;
        stats.add (juce::String (summary.sessions) + (summary.sessions == 1 ? " session" : " sessions"));
        stats.add (formatPracticeDuration (summary.totalSeconds) + " in total");
        stats.add ("since " + entries.front().start.formatted ("%d %b %Y"));
        stats.add ("last practised " + formatPracticeAgo (summary.last, now));

        if (numTakes > 0)
            stats.add (juce::String (numTakes) + (numTakes == 1 ? " take" : " takes"));
        statsLabel.setText (stats.joinIntoString ("     "), juce::dontSendNotification);
    }

    statsLabel.setFont (juce::FontOptions (16.0f));
    statsLabel.setColour (juce::Label::textColourId, primaryText());
    addAndMakeVisible (statsLabel);

    emptyLabel.setText (all ? "Nothing logged yet. Practise a topic, or just play: your sessions show up here by themselves."
                            : "Not practised yet. Press Practise and play: your sessions, tempo and song speed show up here by themselves.",
                        juce::dontSendNotification);
    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setColour (juce::Label::textColourId, axisText());
    addChildComponent (emptyLabel);
    emptyLabel.setVisible (entries.empty() && takes.empty());

    tempoChart = std::make_unique<LineChart> ("Metronome tempo you played along to (bpm)", " bpm", std::move (tempo));
    speedChart = std::make_unique<LineChart> ("Song speed you played along to (%)", " %", std::move (speed));
    weekBars = std::make_unique<WeekBars> (all ? log.weeklySecondsAll (now, numWeeks) : log.weeklySeconds (topicId, now, numWeeks),
                                           PracticeLog::startOfWeek (now));
    addChildComponent (*tempoChart);
    addChildComponent (*speedChart);
    addChildComponent (*weekBars);
    tempoChart->setVisible (tempoChart->hasData());
    speedChart->setVisible (speedChart->hasData());
    weekBars->setVisible (! entries.empty());

    addAndMakeVisible (styleCaption (sessionsHeader, juce::String (takes.empty() ? "Sessions" : "Sessions and takes")
                                                         + (entries.empty() ? "" : "  (tap a session to correct or delete it)")));
    sessionsHeader.setVisible (! entries.empty() || ! takes.empty());
    sessionList = std::make_unique<SessionList> (std::move (entries), std::move (takes),
                                                 [this] (const juce::File& f) { if (onOpenTake) onOpenTake (f); }, all);
    sessionList->onEdit = [this] (const juce::String& id) { if (onEditEntry) onEditEntry (id); };

    addTimeButton.setTooltip ("Add practice time by hand (e.g. theory done away from the app)");
    addTimeButton.onClick = [this] { if (onAddTime) onAddTime(); };
    addAndMakeVisible (addTimeButton);
    sessionViewport.setViewedComponent (sessionList.get(), false);
    sessionViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (sessionViewport);
}

TopicHistory::~TopicHistory() = default;

void TopicHistory::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
}

void TopicHistory::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto statsRow = area.removeFromTop (32);
    addTimeButton.setBounds (statsRow.removeFromRight (130).reduced (0, 1));
    statsLabel.setBounds (statsRow);
    area.removeFromTop (8);
    emptyLabel.setBounds (area.withHeight (juce::jmin (area.getHeight(), 120)));

    juce::Rectangle<int> charts, list;

    if (area.getWidth() >= 1000)
    {
        charts = area.removeFromLeft ((int) (area.getWidth() * 0.62f));
        area.removeFromLeft (24);
        list = area;
    }
    else
    {
        list = area.removeFromBottom (juce::jmax (160, area.getHeight() / 3));
        area.removeFromBottom (12);
        charts = area;
    }

    std::vector<juce::Component*> visibleCharts;

    for (juce::Component* c : { (juce::Component*) tempoChart.get(), (juce::Component*) speedChart.get(),
                                (juce::Component*) weekBars.get(), (juce::Component*) topicBars.get() })
        if (c != nullptr && c->isVisible())
            visibleCharts.push_back (c);

    if (! visibleCharts.empty())
    {
        const auto h = juce::jmin (300, (charts.getHeight() - 16 * ((int) visibleCharts.size() - 1)) / (int) visibleCharts.size());

        for (auto* c : visibleCharts)
        {
            c->setBounds (charts.removeFromTop (h));
            charts.removeFromTop (16);
        }
    }

    sessionsHeader.setBounds (list.removeFromTop (30));
    sessionViewport.setBounds (list);
    sessionList->setSize (juce::jmax (1, sessionViewport.getMaximumVisibleWidth()), sessionList->contentHeight());
}

//==============================================================================
EntryEditor::EntryEditor (const PracticeEntry& entry, bool stillRunning, std::vector<std::pair<juce::String, juce::String>> topics)
    : topicChoices (std::move (topics)), ownTopicId (entry.topicId), ownTopicTitle (entry.topicTitle)
{
    auto what = entry.isFree() ? juce::String ("Free practice") : entry.topicTitle;

    if (entry.songName.isNotEmpty())
        what << "  -  " << entry.songName;

    summaryLabel.setText (what + "\n" + entry.start.formatted ("%a %d %b %Y, %H:%M") + " - " + entry.end.formatted ("%H:%M")
                              + ",  " + formatPracticeDuration (entry.playedSeconds) + " played",
                          juce::dontSendNotification);
    setUpFields (entry);

    if (! topicChoices.empty())
    {
        // A topic deleted since (or not in the library) stays selectable, so saving doesn't move the entry by accident.
        if (std::none_of (topicChoices.begin(), topicChoices.end(), [&] (const auto& t) { return t.first == entry.topicId; }))
            topicChoices.push_back ({ entry.topicId, entry.topicTitle.isNotEmpty() ? entry.topicTitle : juce::String ("(untitled)") });

        setUpTopicChoice (entry.topicId);
    }

    if (stillRunning)
    {
        errorLabel.setText ("This session is still being recorded: stop it first (Stop on the Today tab), then correct it.",
                            juce::dontSendNotification);

        for (juce::Component* c : { (juce::Component*) &startEditor, (juce::Component*) &minutesEditor,
                                    (juce::Component*) &noteEditor, (juce::Component*) &saveButton, (juce::Component*) &deleteButton,
                                    (juce::Component*) &topicBox })
            c->setEnabled (false);
    }
}

EntryEditor::EntryEditor (NewEntry, const PracticeEntry& defaults, std::vector<std::pair<juce::String, juce::String>> topics)
    : topicChoices (std::move (topics)), ownTopicId (defaults.topicId), ownTopicTitle (defaults.topicTitle)
{
    summaryLabel.setText (topicChoices.empty() ? "Add time to \"" + defaults.topicTitle + "\"" : juce::String ("Add practice time"),
                          juce::dontSendNotification);
    setUpFields (defaults);
    minutesEditor.clear();
    minutesEditor.setTextToShowWhenEmpty ("e.g. 20", theme::text().withAlpha (0.35f));
    deleteButton.setVisible (false);
    saveButton.setButtonText ("Add");

    if (! topicChoices.empty())
        setUpTopicChoice (defaults.topicId);
}

void EntryEditor::setUpTopicChoice (const juce::String& selectedId)
{
    addAndMakeVisible (styleCaption (topicLabel, "Topic"));

    for (size_t i = 0; i < topicChoices.size(); ++i)
        topicBox.addItem (topicChoices[i].first.isEmpty() ? juce::String ("Free practice") : topicChoices[i].second, (int) i + 1);

    topicBox.setSelectedItemIndex (0, juce::dontSendNotification);

    for (size_t i = 0; i < topicChoices.size(); ++i)
        if (topicChoices[i].first == selectedId)
            topicBox.setSelectedItemIndex ((int) i, juce::dontSendNotification);

    addAndMakeVisible (topicBox);
}

juce::String EntryEditor::getTopicId() const
{
    const auto i = topicBox.getSelectedItemIndex();
    return juce::isPositiveAndBelow (i, (int) topicChoices.size()) ? topicChoices[(size_t) i].first : ownTopicId;
}

juce::String EntryEditor::getTopicTitle() const
{
    const auto i = topicBox.getSelectedItemIndex();
    return juce::isPositiveAndBelow (i, (int) topicChoices.size()) ? topicChoices[(size_t) i].second : ownTopicTitle;
}

void EntryEditor::setUpFields (const PracticeEntry& entry)
{
    summaryLabel.setFont (juce::FontOptions (16.0f));
    summaryLabel.setColour (juce::Label::textColourId, primaryText());
    summaryLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (summaryLabel);

    auto setUp = [this] (juce::Label& label, const juce::String& text, juce::TextEditor& editor)
    {
        addAndMakeVisible (styleCaption (label, text));
        editor.setColour (juce::TextEditor::backgroundColourId, theme::panel());
        editor.setColour (juce::TextEditor::outlineColourId, theme::text().withAlpha (0.15f));
        editor.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent());
        editor.setFont (juce::FontOptions (15.0f));
        addAndMakeVisible (editor);
    };

    setUp (startLabel, "Started (day/month/year hours:minutes)", startEditor);
    startEditor.setText (formatEntryTime (entry.start), false);

    setUp (minutesLabel, "Minutes played", minutesEditor);
    const auto minutes = entry.playedSeconds / 60.0;
    minutesEditor.setText (std::abs (minutes - std::round (minutes)) < 0.05 ? juce::String (juce::roundToInt (minutes))
                                                                             : juce::String (minutes, 1), false);
    minutesEditor.setInputRestrictions (8, "0123456789.,:");

    setUp (noteLabel, "Note", noteEditor);
    noteEditor.setMultiLine (true, true);
    noteEditor.setReturnKeyStartsNewLine (true);
    noteEditor.setText (entry.note, false);

    errorLabel.setColour (juce::Label::textColourId, theme::highlight());
    addAndMakeVisible (errorLabel);

    saveButton.onClick = [this] { save(); };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    deleteButton.onClick = [this]
    {
        auto options = juce::MessageBoxOptions{}.withIconType (juce::MessageBoxIconType::WarningIcon)
                                                .withTitle ("Delete entry")
                                                .withMessage ("Delete this practice entry? Totals and charts are updated on every device.")
                                                .withButton ("Delete", 1).withButton ("Cancel", 0)
                                                .withAssociatedComponent (this);
        juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<EntryEditor> (this)] (int result)
        {
            if (result == 1 && safe != nullptr && safe->onDelete)
                safe->onDelete();
        });
    };
    deleteButton.setColour (juce::TextButton::buttonColourId, theme::record());
    addAndMakeVisible (saveButton);
    addAndMakeVisible (cancelButton);
    addAndMakeVisible (deleteButton);
}

void EntryEditor::save()
{
    const auto start = parseEntryTime (startEditor.getText());
    const auto minutes = parseMinutes (minutesEditor.getText());

    if (! start.has_value())
    {
        errorLabel.setText ("Type the start like 15/11/2026 14:30.", juce::dontSendNotification);
        startEditor.grabKeyboardFocus();
        return;
    }

    if (! minutes.has_value() || *minutes <= 0.0 || *minutes > 24.0 * 60.0)
    {
        errorLabel.setText ("Type the minutes played as a number, e.g. 25 (or 1:10 for an hour and ten minutes).",
                            juce::dontSendNotification);
        minutesEditor.grabKeyboardFocus();
        return;
    }

    if (onSave)
        onSave (*start, *minutes * 60.0, noteEditor.getText().trim());
}

void EntryEditor::paint (juce::Graphics& g)
{
    g.setColour (theme::background());
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void EntryEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto buttons = area.removeFromBottom (44);
    saveButton.setBounds (buttons.removeFromRight (120).reduced (3));
    cancelButton.setBounds (buttons.removeFromRight (120).reduced (3));
    deleteButton.setBounds (buttons.removeFromLeft (140).reduced (3));
    errorLabel.setBounds (area.removeFromBottom (40));

    summaryLabel.setBounds (area.removeFromTop (summaryLabel.getText().containsChar ('\n') ? 48 : 26));
    area.removeFromTop (8);

    if (topicBox.isVisible())
    {
        topicLabel.setBounds (area.removeFromTop (28));
        topicBox.setBounds (area.removeFromTop (34));
        area.removeFromTop (10);
    }

    auto row = area.removeFromTop (62);
    auto left = row.removeFromLeft (row.getWidth() * 2 / 3).withTrimmedRight (12);
    startLabel.setBounds (left.removeFromTop (28));
    startEditor.setBounds (left.removeFromTop (34));
    minutesLabel.setBounds (row.removeFromTop (28));
    minutesEditor.setBounds (row.removeFromTop (34));

    area.removeFromTop (10);
    noteLabel.setBounds (area.removeFromTop (28));
    noteEditor.setBounds (area.withTrimmedBottom (6));
}

} // namespace pv
