#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "GainMeterProcessor.h"

namespace pv
{

/** The app's audio graph:

        device input (one selected channel)
          -> input stage (gain + meter, mono)
          -> [looper, before FX: records the dry input, adds "before FX" loops]  --+
        MIDI input -> instrument plugin (optional, played from a MIDI keyboard) ---+ (mixed)
          -> plugin 1 -> plugin 2 -> ...        (AUv3 on iPad, VST3 on Windows, AU + VST3 on macOS)
          -> guitar output stage (gain + mute + meter, stereo)  --+
          -> [looper, after FX: records the processed guitar,    |
              plays "after FX" loops]  ----------------------------+-> master (meter) -> device output
        other sources (song player, metronome, drums...)  --------+

    Sources sit outside the instrument path: monitoring off does not mute them and the looper
    does not record them. Every plugin gets the play head set with setPlayHead().

    The instrument, if any, is always slot 0 (one at a time: adding another replaces it). Only it gets
    MIDI, queued from the MIDI input thread with sendInstrumentMidi(). Its sound joins the audio input
    after the "before FX" looper node, so before-FX loops don't contain it; after-FX loops and takes do.

    All methods must be called on the message thread.
*/
class PluginChain final : private juce::ChangeListener
{
public:
    PluginChain (juce::AudioDeviceManager&, juce::AudioPluginFormatManager&);
    ~PluginChain() override;

    int getNumPlugins() const noexcept                  { return (int) slots.size(); }
    juce::String getPluginName (int index) const;
    juce::AudioPluginInstance* getPlugin (int index) const;
    bool isBypassed (int index) const;
    void setBypassed (int index, bool shouldBeBypassed);

    using LoadCallback = std::function<void (const juce::String& errorOrEmpty)>;

    /** Loads a plugin asynchronously (required for AUv3) and appends it to the chain; an instrument
        goes to the top instead, replacing the current one. */
    void addPlugin (const juce::PluginDescription&, LoadCallback onDone = {});

    /** Adds an already created plugin (synchronously); same placement rules as addPlugin(). */
    void addPluginInstance (std::unique_ptr<juce::AudioPluginInstance>);

    /** Tests only: run the graph offline, without an audio device. `buffer` holds the device inputs on the
        way in and the device outputs on the way out; `midi` is what the MIDI input node delivers. */
    void prepareForTests (int numDeviceIns, int numDeviceOuts, double sampleRate, int blockSize);
    void processForTests (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    /** Index of the slot the last successful addPlugin() filled (-1 = none yet). */
    int getLastAddedIndex() const noexcept              { return lastAddedIndex; }

    /** Slot 0 when an instrument is loaded. */
    bool isInstrument (int index) const;
    bool hasInstrument() const                          { return isInstrument (0); }

    /** Any thread (meant for the MIDI input thread): queues a message for the instrument plugin. Dropped
        when there is none, it takes no MIDI, or the audio device isn't running. */
    void sendInstrumentMidi (const juce::MidiMessage&);

    /** Instruments: told by the description, or a plugin that takes MIDI and has no audio input. */
    static bool isInstrumentPlugin (const juce::AudioPluginInstance&);

    void removePlugin (int index);
    void movePlugin (int index, int delta);
    void clear();

    void setInputChannel (int channel);
    int getInputChannel() const noexcept                { return inputChannel; }

    GainMeterProcessor& getInputStage() const;
    GainMeterProcessor& getOutputStage() const;     // guitar output (monitor on/off)
    GainMeterProcessor& getMasterStage() const;

    /** Adds a stereo source (e.g. the song player) that is mixed into the master output.
        The graph owns the processor; the returned pointer stays valid while this chain exists.
    */
    template <typename ProcessorType>
    ProcessorType* addSource (std::unique_ptr<ProcessorType> processor)
    {
        auto* raw = processor.get();
        sources.push_back (graph.addNode (std::move (processor), std::nullopt, juce::AudioProcessorGraph::UpdateKind::none));
        rebuildConnections();
        return raw;
    }

    /** The transport every plugin sees (see BarClockPlayHead). Must outlive the chain. */
    void setPlayHead (juce::AudioPlayHead* playHead)   { graph.setPlayHead (playHead); }

    /** Inserts the looper's two nodes (see Looper). Call once, before audio starts. */
    void setLooperNodes (std::unique_ptr<juce::AudioProcessor> beforeFx, std::unique_ptr<juce::AudioProcessor> afterFx);

    /** Called on the audio thread at the start of every device callback, before the graph runs,
        with the absolute sample time of the block: drives the shared bar clock. */
    struct BlockListener
    {
        virtual ~BlockListener() = default;
        virtual void beginBlock (juce::int64 blockStart, int numSamples) = 0;
    };

    void setBlockListener (BlockListener* listener)    { blockListener = listener; }

    /** Total latency the effect plugins report (the graph delays the other paths by as much). The instrument's
        isn't counted: the looper compensates the audio input's round trip. */
    int getPluginLatencySamples() const;

    //==========================================================================
    // Scenes: up to numScenes saved sounds of the same chain ("Clean", "Crunch"...), switched while playing.
    // A scene keeps, per plugin, every parameter value, the bypass state and the plugin's full state, plus the
    // output level. Recalling sets only the parameters that differ (quick, works with any plugin); plugins marked
    // "full recall" get their whole state back instead (for sounds that live outside the parameters, e.g. an
    // amp model menu or an IR file; may drop out briefly). A scene's data travels with its plugin when the
    // chain is reordered; a plugin added after a scene was saved is left as it is by that scene.
    static constexpr int numScenes = 4;

    bool isSceneSaved (int scene) const;
    bool hasSavedScenes() const;
    juce::String getSceneName (int scene) const;            // "Scene 2" until renamed
    void setSceneName (int scene, const juce::String&);     // empty = back to "Scene N"
    void saveScene (int scene);
    bool recallScene (int scene);                           // false: not saved
    void recallNextScene (int delta);                       // +1 / -1 through the saved scenes, wrapping
    void clearScene (int scene);
    int getCurrentScene() const noexcept                    { return currentScene; }    // -1 = none recalled/saved yet

    bool isFullRecall (int index) const;
    void setFullRecall (int index, bool);

    /** Goes up whenever a scene is recalled, saved, renamed or cleared (UI timers poll it). */
    int getSceneVersion() const noexcept                    { return sceneVersion; }

    /** Serialises the chain, including every plugin's own state. */
    std::unique_ptr<juce::XmlElement> createStateXml() const;

    /** Replaces the chain with one previously saved by createStateXml(). */
    void restoreFromXml (const juce::XmlElement&, std::function<void()> onDone = {});

    /** Called (on the message thread) whenever the chain's contents change. */
    std::function<void()> onChange;

private:
    using NodePtr = juce::AudioProcessorGraph::Node::Ptr;
    using NodeCallback = std::function<void (NodePtr, const juce::String& error)>;

    /** One plugin's part of a scene. */
    struct SceneSnapshot
    {
        bool saved = false;
        bool bypassed = false;
        std::vector<float> values;          // every parameter, normalised, in getParameters() order
        juce::MemoryBlock state;            // getStateInformation()
    };

    using SlotScenes = std::array<SceneSnapshot, numScenes>;

    struct Slot
    {
        NodePtr node;
        juce::PluginDescription description;
        bool instrument = false;
        bool fullRecall = false;
        SlotScenes scenes {};
    };

    struct PendingSlot
    {
        juce::PluginDescription description;
        juce::MemoryBlock state;
        bool bypassed = false;
        bool fullRecall = false;
        SlotScenes scenes {};
    };

    struct SceneInfo
    {
        juce::String name;                  // empty = "Scene N"
        bool saved = false;
        float outputGain = 1.0f;
    };

    void scenesChanged();
    static void sceneToXml (const SceneSnapshot&, juce::XmlElement&);
    static void sceneFromXml (const juce::XmlElement&, SceneSnapshot&);

    void loadPlugin (const juce::PluginDescription&, NodeCallback);     // adds the node, unconnected
    void addSlot (NodePtr);
    void restoreNext (std::shared_ptr<std::vector<PendingSlot>>, size_t index, std::function<void()> onDone);
    void rebuildConnections();
    void notifyChange();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::AudioDeviceManager& deviceManager;
    juce::AudioPluginFormatManager& formatManager;

    /** Forwards everything to the AudioProcessorPlayer, calling the block listener first. */
    struct DeviceCallback final : public juce::AudioIODeviceCallback
    {
        explicit DeviceCallback (PluginChain& o) : owner (o) {}

        void audioDeviceIOCallbackWithContext (const float* const* ins, int numIns, float* const* outs, int numOuts,
                                               int numSamples, const juce::AudioIODeviceCallbackContext& context) override
        {
            if (auto* listener = owner.blockListener.load())
                listener->beginBlock (time, numSamples);

            time += numSamples;
            owner.player.audioDeviceIOCallbackWithContext (ins, numIns, outs, numOuts, numSamples, context);
        }

        void audioDeviceAboutToStart (juce::AudioIODevice* device) override
        {
            owner.player.audioDeviceAboutToStart (device);      // also prepares the MIDI collector
            owner.audioRunning = true;
        }

        void audioDeviceStopped() override
        {
            owner.audioRunning = false;
            owner.player.audioDeviceStopped();
        }

        void audioDeviceError (const juce::String& message) override            { owner.player.audioDeviceError (message); }

        PluginChain& owner;
        juce::int64 time = 0;
    };

    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    DeviceCallback deviceCallback { *this };
    std::atomic<BlockListener*> blockListener { nullptr };
    std::atomic<bool> audioRunning { false }, instrumentTakesMidi { false };
    int lastAddedIndex = -1;

    NodePtr midiInNode, audioInNode, audioOutNode, inputStageNode, outputStageNode, masterNode;
    NodePtr looperBeforeNode, looperAfterNode;
    std::vector<Slot> slots;
    std::vector<NodePtr> sources;
    int inputChannel = 0;
    std::array<SceneInfo, numScenes> scenes {};
    int currentScene = -1, sceneVersion = 0;

    // Async plugin-creation callbacks check this so they never touch a deleted chain.
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginChain)
};

} // namespace pv
