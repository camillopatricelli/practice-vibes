#include "RigPanel.h"
#include "FootswitchPanel.h"
#include "Icons.h"
#include "Theme.h"
#include "Localisation.h"

namespace pv
{

//==============================================================================
class RigPanel::ListModel final : public juce::ListBoxModel
{
public:
    std::function<int()> rowCount;
    std::function<juce::String (int)> rowText;
    std::function<void (int)> onDoubleClick;

    int getNumRows() override { return rowCount ? rowCount() : 0; }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        const auto r = theme::paintListRow (g, width, height, selected);
        g.setColour (theme::text());
        g.setFont (juce::FontOptions (16.0f));
        g.drawText (rowText ? rowText (row) : juce::String(), r, juce::Justification::centredLeft, true);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (onDoubleClick)
            onDoubleClick (row);
    }
};

//==============================================================================
class RigPanel::LevelMeter final : public juce::Component
{
public:
    void push (float peak)
    {
        level = juce::jmax (peak, level * 0.85f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::shade (0.4f));
        g.fillRoundedRectangle (bounds, 3.0f);

        const auto db = juce::Decibels::gainToDecibels (level, -60.0f);
        const auto proportion = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f));

        g.setColour (db > -3.0f ? juce::Colours::red : db > -12.0f ? juce::Colours::orange : juce::Colours::limegreen);
        g.fillRoundedRectangle (bounds.withWidth (bounds.getWidth() * proportion), 3.0f);

        g.setColour (theme::text().withAlpha (0.85f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (level > 0.001f ? juce::String (db, 1) + " dB" : juce::String ("-inf"),
                    getLocalBounds().reduced (6, 0), juce::Justification::centredRight);
    }

private:
    float level = 0.0f;
};

//==============================================================================
//==============================================================================
RigPanel::RigPanel (AppEngine& e)
    : engine (e)
{
    auto setupHeader = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        theme::styleHeader (label);
        label.setColour (juce::Label::textColourId, theme::text().withAlpha (0.9f));
        addAndMakeVisible (label);
    };

    setupHeader (catalogueHeader, tr ("Available plugins"));
    setupHeader (chainHeader, tr ("Effects chain"));
    setupHeader (levelsHeader, tr ("Input / output"));
    setupHeader (scenesHeader, tr ("Scenes"));
    setupHeader (midiHeader, tr ("MIDI footswitch"));

    for (auto* b : { &addButton, &scanButton, &editButton, &bypassButton, &upButton, &downButton, &removeButton, &footswitchButton })
        addAndMakeVisible (*b);

    // Catalogue ------------------------------------------------------------
    catalogueModel = std::make_unique<ListModel>();
    catalogueModel->rowCount = [this] { return (int) catalogueEntries.size(); };
    catalogueModel->rowText = [this] (int row) -> juce::String
    {
        if (! juce::isPositiveAndBelow (row, (int) catalogueEntries.size()))
            return {};

        const auto& entry = catalogueEntries[(size_t) row];
        return "[" + tr (entry.kind) + "]  " + entry.description.name;
    };
    catalogueModel->onDoubleClick = [this] (int) { addSelectedPlugin(); };
    catalogueList.setModel (catalogueModel.get());
    catalogueList.setRowHeight (40);
    catalogueList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (catalogueList);

    catalogueStatus.setColour (juce::Label::textColourId, theme::text().withAlpha (0.6f));
    addAndMakeVisible (catalogueStatus);

    addButton.onClick = [this] { addSelectedPlugin(); };
    if (engine.catalogue->canScan())
    {
        scanButton.onClick = [this] { engine.catalogue->startScan(); };
    }
    else
    {
        // iPad: AudioUnits are listed from the system; this re-reads that list (e.g. after installing a plugin app).
        scanButton.setButtonText (tr ("Refresh list"));
        scanButton.onClick = [this] { engine.catalogue->refresh(); };
    }

    scanButton.setVisible (true);

    // Chain ----------------------------------------------------------------
    chainModel = std::make_unique<ListModel>();
    chainModel->rowCount = [this] { return engine.chain->getNumPlugins(); };
    chainModel->rowText = [this] (int row)
    {
        const auto number = engine.chain->hasInstrument() ? row : row + 1;     // effects count from 1
        return (engine.chain->isInstrument (row) ? tr ("Instrument:") + "  " : juce::String (number) + ".  ")
             + engine.chain->getPluginName (row) + (engine.chain->isBypassed (row) ? "   " + tr ("(bypassed)") : juce::String())
             + (engine.chain->isFullRecall (row) ? "   " + tr ("(full scene recall)") : juce::String());
    };
    chainModel->onDoubleClick = [this] (int row) { openEditor (row); };
    chainList.setModel (chainModel.get());
    chainList.setRowHeight (44);
    chainList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (chainList);

    chainHint.setColour (juce::Label::textColourId, theme::highlight());
    chainHint.setFont (juce::FontOptions (13.0f));
    chainHint.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (chainHint);

    addButton.setTooltip (tr ("Effects are added at the end of the chain. An instrument (a synth or piano plugin) goes on top "
                          "and is played from your MIDI keyboard; adding another instrument replaces it."));

    // Icon buttons: five of them share the chain column, too narrow for their words in portrait.
    editButton.setTooltip (tr ("Open the plugin's window"));
    bypassButton.setTooltip (tr ("Bypass the plugin (on/off)"));
    upButton.setTooltip (tr ("Move the plugin up the chain"));
    downButton.setTooltip (tr ("Move the plugin down the chain"));
    removeButton.setTooltip (tr ("Remove the plugin from the chain"));
    icons::setIcon (editButton, icons::Icon::openWindow, true);
    icons::setIcon (bypassButton, icons::Icon::power, true);
    icons::setIcon (upButton, icons::Icon::arrowUp, true);
    icons::setIcon (downButton, icons::Icon::arrowDown, true);
    icons::setIcon (removeButton, icons::Icon::trash, true);

    editButton.onClick = [this] { openEditor (selectedChainIndex()); };

    bypassButton.onClick = [this]
    {
        const auto i = selectedChainIndex();
        engine.chain->setBypassed (i, ! engine.chain->isBypassed (i));
    };

    upButton.onClick = [this]
    {
        const auto i = selectedChainIndex();
        engine.chain->movePlugin (i, -1);
        chainList.selectRow (juce::jmax (0, i - 1));
    };

    downButton.onClick = [this]
    {
        const auto i = selectedChainIndex();
        engine.chain->movePlugin (i, 1);
        chainList.selectRow (juce::jmin (engine.chain->getNumPlugins() - 1, i + 1));
    };

    removeButton.onClick = [this]
    {
        if (hideOverlay)
            hideOverlay();

        engine.chain->removePlugin (selectedChainIndex());
    };

    engine.chain->onChange = [this] { refreshChain(); };

    // Levels ---------------------------------------------------------------
    inputMeter = std::make_unique<LevelMeter>();
    outputMeter = std::make_unique<LevelMeter>();
    addAndMakeVisible (*inputMeter);
    addAndMakeVisible (*outputMeter);

    addAndMakeVisible (inputChannelBox);
    inputChannelBox.onChange = [this]
    {
        engine.chain->setInputChannel (juce::jmax (0, inputChannelBox.getSelectedItemIndex()));
    };

    auto setupGain = [this] (juce::Slider& slider, juce::Label& label, const juce::String& name,
                             double minDb, double maxDb, GainMeterProcessor& stage)
    {
        label.setText (name, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, theme::text().withAlpha (0.8f));
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 24);
        slider.setRange (minDb, maxDb, 0.1);
        slider.setTextValueSuffix (" dB");
        slider.setValue (juce::Decibels::gainToDecibels (stage.gain.load()), juce::dontSendNotification);
        slider.setDoubleClickReturnValue (true, 0.0);
        slider.onValueChange = [&slider, &stage] { stage.gain = juce::Decibels::decibelsToGain ((float) slider.getValue()); };
        addAndMakeVisible (slider);
    };

    setupGain (inputGainSlider, inputGainLabel, tr ("Input gain"), -24.0, 24.0, engine.chain->getInputStage());
    setupGain (outputGainSlider, outputGainLabel, tr ("Output level"), -60.0, 12.0, engine.chain->getOutputStage());

    monitorButton.setToggleState (! engine.chain->getOutputStage().muted.load(), juce::dontSendNotification);
    monitorButton.onClick = [this]
    {
        engine.chain->getOutputStage().muted = ! monitorButton.getToggleState();
        juce::Logger::writeToLog (monitorButton.getToggleState() ? "Monitoring ON" : "Monitoring OFF");
    };
    addAndMakeVisible (monitorButton);

    // Scenes ---------------------------------------------------------------
    for (int i = 0; i < PluginChain::numScenes; ++i)
    {
        auto& b = sceneButtons[(size_t) i];
        b.setClickingTogglesState (false);
        b.onClick = [this, i] { sceneClicked (i); };
        addAndMakeVisible (b);
    }

    sceneMenuButton.setTooltip (tr ("Save, rename or clear a scene"));
    sceneMenuButton.onClick = [this] { showSceneMenu(); };
    addAndMakeVisible (sceneMenuButton);

    sceneHint.setColour (juce::Label::textColourId, theme::text().withAlpha (0.6f));
    sceneHint.setFont (juce::FontOptions (13.0f));
    sceneHint.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (sceneHint);

    // MIDI -----------------------------------------------------------------
    midiDevicesLabel.setColour (juce::Label::textColourId, theme::text().withAlpha (0.8f));
    midiDevicesLabel.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (midiDevicesLabel);

    lastMidiLabel.setText (tr ("No MIDI received yet"), juce::dontSendNotification);
    lastMidiLabel.setFont (theme::displayFont (19.0f, "SemiBold"));
    lastMidiLabel.setColour (juce::Label::textColourId, theme::highlight());
    lastMidiLabel.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (lastMidiLabel);

    footswitchButton.setTooltip (tr ("Learn the pedal's switches and choose what they do (looper, metronome, bypass)."));
    footswitchButton.onClick = [this]
    {
        if (showOverlay)
            showOverlay (tr ("Footswitch"), std::make_unique<FootswitchPanel> (engine));
    };

    engine.onMidiMessage = [this] (const juce::MidiMessage& m, const juce::String& source) { handleMidi (m, source); };
    engine.getBypassTarget = [this] { return selectedChainIndex(); };
    engine.midi->onDevicesChanged = [this] { refreshMidiDevices(); };

    engine.catalogue->addChangeListener (this);
    engine.deviceManager.addChangeListener (this);

    refreshCatalogue();
    refreshChain();
    refreshInputChannels();
    refreshMidiDevices();

    startTimerHz (30);
}

RigPanel::~RigPanel()
{
    stopTimer();

    engine.catalogue->removeChangeListener (this);
    engine.deviceManager.removeChangeListener (this);

    engine.chain->onChange = nullptr;
    engine.onMidiMessage = nullptr;
    engine.getBypassTarget = nullptr;
    engine.midi->onDevicesChanged = nullptr;

    catalogueList.setModel (nullptr);
    chainList.setModel (nullptr);
}

//==============================================================================
void RigPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
    theme::paintListCard (g, catalogueList);
    theme::paintListCard (g, chainList);
}

void RigPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);

    constexpr int gap = 12;
    const auto columnWidth = (area.getWidth() - 2 * gap) / 3;
    auto left = area.removeFromLeft (columnWidth);
    area.removeFromLeft (gap);
    auto middle = area.removeFromLeft (columnWidth);
    area.removeFromLeft (gap);
    auto right = area;

    // Left: catalogue
    catalogueHeader.setBounds (left.removeFromTop (30));
    auto leftButtons = left.removeFromBottom (48);
    catalogueStatus.setBounds (left.removeFromBottom (26));

    if (scanButton.isVisible())
        scanButton.setBounds (leftButtons.removeFromRight (leftButtons.getWidth() / 2).reduced (3));

    addButton.setBounds (leftButtons.reduced (3));
    catalogueList.setBounds (left.reduced (theme::listCardPadX, theme::listCardPadY));

    // Middle: chain
    chainHeader.setBounds (middle.removeFromTop (30));
    auto chainButtons = middle.removeFromBottom (48);
    const auto buttonWidth = chainButtons.getWidth() / 5;

    for (auto* b : { &editButton, &bypassButton, &upButton, &downButton, &removeButton })
        b->setBounds (chainButtons.removeFromLeft (buttonWidth).reduced (3));

    chainHint.setBounds (middle.removeFromBottom (26));

    chainList.setBounds (middle.reduced (theme::listCardPadX, theme::listCardPadY));

    // Right: levels and MIDI
    auto take = [&right] (int height) { return right.removeFromTop (height); };

    levelsHeader.setBounds (take (30));
    inputChannelBox.setBounds (take (38).reduced (0, 3));
    inputMeter->setBounds (take (24).reduced (0, 3));

    {
        auto r = take (36);
        inputGainLabel.setBounds (r.removeFromLeft (95));
        inputGainSlider.setBounds (r);
    }

    outputMeter->setBounds (take (24).reduced (0, 3));

    {
        auto r = take (36);
        outputGainLabel.setBounds (r.removeFromLeft (95));
        outputGainSlider.setBounds (r);
    }

    monitorButton.setBounds (take (36));
    take (10);

    scenesHeader.setBounds (take (30));

    {
        auto r = take (46);
        sceneMenuButton.setBounds (r.removeFromRight (44).reduced (2, 3));
        const auto w = r.getWidth() / PluginChain::numScenes;

        for (auto& b : sceneButtons)
            b.setBounds (r.removeFromLeft (w).reduced (2, 3));
    }

    sceneHint.setBounds (take (24));
    take (10);

    midiHeader.setBounds (take (30));
    midiDevicesLabel.setBounds (take (30));
    lastMidiLabel.setBounds (take (36));
    footswitchButton.setBounds (take (48).reduced (0, 4));
}

//==============================================================================
void RigPanel::timerCallback()
{
    inputMeter->push (engine.chain->getInputStage().takePeak());
    outputMeter->push (engine.chain->getOutputStage().takePeak());

    // The saved gains are restored after this panel is built (the chain restores once the audio device is up),
    // so keep the sliders in step with the real gain whenever the user isn't using them.
    auto sync = [] (juce::Slider& slider, GainMeterProcessor& stage)
    {
        if (slider.isMouseButtonDown() || slider.hasKeyboardFocus (true))
            return;

        const auto db = juce::jlimit (slider.getMinimum(), slider.getMaximum(),
                                      (double) juce::Decibels::gainToDecibels (stage.gain.load(), -60.0f));

        if (std::abs (db - slider.getValue()) > 0.05)
            slider.setValue (db, juce::dontSendNotification);
    };

    sync (inputGainSlider, engine.chain->getInputStage());
    sync (outputGainSlider, engine.chain->getOutputStage());

    // An instrument is silent until monitoring is on (it goes through the same output stage as the input).
    const auto hint = ! engine.chain->hasInstrument() ? juce::String()
                    : engine.chain->getOutputStage().muted.load() ? tr ("Turn on Monitor input to hear the instrument")
                                                                   : tr ("Play the instrument from your MIDI keyboard");

    if (chainHint.getText() != hint)
        chainHint.setText (hint, juce::dontSendNotification);

    if (engine.chain->getSceneVersion() != shownSceneVersion || (int) engine.footswitch->getMode() != shownFootswitchMode)
        refreshScenes();

    const auto selected = selectedChainIndex();
    const auto movable = selected >= 0 && ! engine.chain->isInstrument (selected);
    upButton.setEnabled (movable && ! engine.chain->isInstrument (selected - 1) && selected > 0);
    downButton.setEnabled (movable && selected < engine.chain->getNumPlugins() - 1);
}

void RigPanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == engine.catalogue.get())
    {
        refreshCatalogue();
    }
    else if (source == &engine.deviceManager)
    {
        refreshInputChannels();
        engine.logAudioDeviceInfo();
    }
}

void RigPanel::refreshCatalogue()
{
    catalogueEntries = engine.catalogue->getEntries();
    catalogueList.updateContent();
    catalogueList.repaint();
    catalogueStatus.setText (engine.catalogue->getStatus(), juce::dontSendNotification);
    scanButton.setEnabled (! engine.catalogue->isScanning());
}

void RigPanel::refreshChain()
{
    chainList.updateContent();
    chainList.repaint();
}

void RigPanel::refreshInputChannels()
{
    inputChannelBox.clear (juce::dontSendNotification);

    if (auto* device = engine.deviceManager.getCurrentAudioDevice())
    {
        const auto names = device->getInputChannelNames();
        const auto active = device->getActiveInputChannels();
        int itemId = 1;

        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                inputChannelBox.addItem (names[i].isNotEmpty() ? names[i] : tr ("Input {n}", i + 1), itemId++);
    }

    const auto numItems = inputChannelBox.getNumItems();
    inputChannelBox.setEnabled (numItems > 0);

    if (numItems == 0)
        inputChannelBox.setTextWhenNothingSelected (tr ("No audio input"));
    else
        inputChannelBox.setSelectedItemIndex (juce::jmin (engine.chain->getInputChannel(), numItems - 1),
                                              juce::dontSendNotification);
}

void RigPanel::refreshMidiDevices()
{
    const auto names = engine.midi->getEnabledInputNames();
    midiDevicesLabel.setText (names.isEmpty() ? tr ("No MIDI inputs - tap 'Bluetooth MIDI' to pair the footswitch")
                                              : tr ("Inputs: {names}", names.joinIntoString (", ")),
                              juce::dontSendNotification);
}

void RigPanel::handleMidi (const juce::MidiMessage& message, const juce::String& source)
{
    lastMidiLabel.setText (message.getDescription(), juce::dontSendNotification);
    juce::Logger::writeToLog ("MIDI [" + source + "] " + message.getDescription());
}

//==============================================================================
void RigPanel::refreshScenes()
{
    auto& chain = *engine.chain;
    shownSceneVersion = chain.getSceneVersion();
    shownFootswitchMode = (int) engine.footswitch->getMode();

    for (int i = 0; i < PluginChain::numScenes; ++i)
    {
        auto& b = sceneButtons[(size_t) i];
        const auto saved = chain.isSceneSaved (i);
        const auto current = chain.getCurrentScene() == i;

        b.setButtonText (juce::String (i + 1) + "  " + (saved ? chain.getSceneName (i) : tr ("empty")));
        b.setToggleState (current, juce::dontSendNotification);
        b.setAlpha (saved ? 1.0f : 0.6f);
        b.setTooltip (saved ? tr ("Switch to this sound (a footswitch can do it too, in Scene mode)")
                            : tr ("Empty: tap to save the current sound here"));
    }

    const auto inSceneMode = engine.footswitch->getMode() == Footswitch::sceneMode;
    sceneHint.setText (! chain.hasSavedScenes() ? tr ("Set up a sound, then tap an empty scene to save it")
                       : inSceneMode ? tr ("Footswitch in Scene mode: A-D switch scenes")
                                     : tr ("Footswitch in Looper mode: hold D for scenes"),
                       juce::dontSendNotification);
    sceneHint.setColour (juce::Label::textColourId, inSceneMode ? theme::highlight() : theme::text().withAlpha (0.6f));
}

void RigPanel::sceneClicked (int scene)
{
    auto& chain = *engine.chain;

    if (chain.isSceneSaved (scene))
    {
        chain.recallScene (scene);
        return;
    }

    chain.saveScene (scene);
    engine.saveChain();
}

void RigPanel::showSceneMenu()
{
    auto& chain = *engine.chain;
    const auto current = chain.getCurrentScene();

    juce::PopupMenu save;

    for (int i = 0; i < PluginChain::numScenes; ++i)
        save.addItem (juce::String (i + 1) + "  " + chain.getSceneName (i), [this, i]
        {
            engine.chain->saveScene (i);
            engine.saveChain();
        });

    juce::PopupMenu menu;

    if (current >= 0)
        menu.addItem (tr ("Save the current sound to \"{scene}\"", chain.getSceneName (current)), [this, current]
        {
            engine.chain->saveScene (current);
            engine.saveChain();
        });

    menu.addSubMenu (tr ("Save the current sound to"), save);

    juce::PopupMenu rename, clear;

    for (int i = 0; i < PluginChain::numScenes; ++i)
    {
        rename.addItem (juce::String (i + 1) + "  " + chain.getSceneName (i), [this, i] { renameScene (i); });
        clear.addItem (juce::String (i + 1) + "  " + chain.getSceneName (i), chain.isSceneSaved (i), false, [this, i]
        {
            engine.chain->clearScene (i);
            engine.saveChain();
        });
    }

    menu.addSubMenu (tr ("Rename"), rename);
    menu.addSubMenu (tr ("Clear"), clear, chain.hasSavedScenes());
    menu.addSeparator();

    const auto selected = selectedChainIndex();

    if (selected >= 0)
        menu.addItem (tr ("{plugin}: scenes restore its full state (slower; for amp models, IR files...)", chain.getPluginName (selected)),
                      true, chain.isFullRecall (selected), [this, selected]
        {
            engine.chain->setFullRecall (selected, ! engine.chain->isFullRecall (selected));
            engine.saveChain();
        });
    else
        menu.addItem (tr ("Select a plugin in the chain to make scenes restore its full state"), false, false, [] {});

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sceneMenuButton));
}

void RigPanel::renameScene (int scene)
{
    auto* window = new juce::AlertWindow (tr ("Rename scene"), tr ("Name:"), juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", engine.chain->getSceneName (scene));
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis = SafePointer<RigPanel> (this), window, scene] (int result)
    {
        if (safeThis == nullptr || result != 1)
            return;

        safeThis->engine.chain->setSceneName (scene, window->getTextEditorContents ("name"));
        safeThis->engine.saveChain();
    }), true);
}

//==============================================================================
void RigPanel::addSelectedPlugin()
{
    const auto row = catalogueList.getSelectedRow();

    if (! juce::isPositiveAndBelow (row, (int) catalogueEntries.size()))
    {
        juce::Logger::writeToLog ("Select a plugin in the list first");
        return;
    }

    addButton.setEnabled (false);

    engine.chain->addPlugin (catalogueEntries[(size_t) row].description, [safeThis = SafePointer<RigPanel> (this)] (const juce::String&)
    {
        if (safeThis != nullptr)
        {
            safeThis->addButton.setEnabled (true);
            safeThis->chainList.selectRow (safeThis->engine.chain->getLastAddedIndex());
        }
    });
}

void RigPanel::openEditor (int chainIndex)
{
    auto* plugin = engine.chain->getPlugin (chainIndex);

    if (plugin == nullptr || ! showOverlay)
        return;

    // Destroy any open editor first: a plugin must never have two editors at once.
    if (hideOverlay)
        hideOverlay();

    std::unique_ptr<juce::Component> editor;

    if (plugin->hasEditor())
        editor.reset (plugin->createEditorAndMakeActive());

    if (editor == nullptr)
        editor = std::make_unique<juce::GenericAudioProcessorEditor> (*plugin);

    showOverlay (engine.chain->getPluginName (chainIndex), std::move (editor));
}

int RigPanel::selectedChainIndex() const
{
    return chainList.getSelectedRow();
}

} // namespace pv
