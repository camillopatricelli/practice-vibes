#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "AppEngine.h"
#include "Localisation.h"

namespace pv
{

/** The "Rig" tab: plugin catalogue, plugin chain (an instrument on top, played from a MIDI keyboard, then the
    effects), levels, scenes (saved sounds of the chain, switched while playing) and MIDI footswitch. */
class RigPanel final : public juce::Component,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    explicit RigPanel (AppEngine&);
    ~RigPanel() override;

    /** Supplied by the owner: shows a component full-window (plugin UIs). */
    std::function<void (const juce::String& title, std::unique_ptr<juce::Component>)> showOverlay;
    std::function<void()> hideOverlay;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class ListModel;
    class LevelMeter;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void refreshCatalogue();
    void refreshChain();
    void refreshInputChannels();
    void refreshMidiDevices();
    void handleMidi (const juce::MidiMessage&, const juce::String& source);

    void refreshScenes();
    void sceneClicked (int scene);
    void showSceneMenu();
    void renameScene (int scene);

    void addSelectedPlugin();
    void openEditor (int chainIndex);
    int selectedChainIndex() const;

    AppEngine& engine;

    std::vector<PluginCatalogue::Entry> catalogueEntries;

    juce::Label catalogueHeader, chainHeader, levelsHeader, scenesHeader, midiHeader;

    std::unique_ptr<ListModel> catalogueModel, chainModel;
    juce::ListBox catalogueList, chainList;
    juce::TextButton addButton { tr ("Add to chain") }, scanButton { tr ("Scan VST3") };
    juce::Label catalogueStatus, chainHint;

    juce::TextButton editButton { tr ("Open UI") }, bypassButton { tr ("Bypass") },
                     upButton { tr ("Up") }, downButton { tr ("Down") }, removeButton { tr ("Remove") };

    juce::ComboBox inputChannelBox;
    std::unique_ptr<LevelMeter> inputMeter, outputMeter;
    juce::Slider inputGainSlider, outputGainSlider;
    juce::Label inputGainLabel, outputGainLabel;
    juce::ToggleButton monitorButton { tr ("Monitor input (use headphones!)") };

    std::array<juce::TextButton, PluginChain::numScenes> sceneButtons;
    juce::TextButton sceneMenuButton { "..." };
    juce::Label sceneHint;
    int shownSceneVersion = -1, shownFootswitchMode = -1;

    juce::Label midiDevicesLabel, lastMidiLabel;
    juce::TextButton footswitchButton { tr ("Footswitch setup...") };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigPanel)
};

} // namespace pv
