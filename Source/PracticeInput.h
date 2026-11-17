#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace pv
{

/** What kind of input the guitar would come in from. A microphone also hears the room (talking, TV, the street),
    so practice can't be timed from its level: it uses the Start button instead.
*/
enum class InputKind
{
    none,           // no input open
    microphone,     // built-in, headset or Bluetooth mic
    audioInterface  // audio interface, USB device, line input
};

struct InputInfo
{
    InputKind kind = InputKind::none;
    juce::String name;      // what the user knows it as ("iPad Microphone", "iRig HD X"); the key for the setting
    juce::String reason;    // how it was decided, for the log
};

/** A cheap string that changes when the open input changes (polled to know when to call classifyInput again). */
juce::String getInputKey (juce::AudioDeviceManager&);

/** Looks at the open input device. Message thread; may take a few milliseconds on Windows. */
InputInfo classifyInput (juce::AudioDeviceManager&);

/** How practice is timed from an input (chosen per input in the settings). */
enum class PracticeTiming
{
    byInputType,    // default: microphones use the Start button, everything else counts playing automatically
    automatic,      // counts time from the input level
    startButton     // counts time between Start and Pause
};

bool usesStartButton (const InputInfo&, PracticeTiming);

/** The per-input choices, in practice-inputs.xml. */
class PracticeInputSettings
{
public:
    PracticeInputSettings();

    PracticeTiming get (const juce::String& inputName) const;
    void set (const juce::String& inputName, PracticeTiming);

private:
    std::map<juce::String, PracticeTiming> timings;
};

} // namespace pv
