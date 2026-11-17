#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace pv
{
    /** iPad: the system's Bluetooth MIDI device list in a themed card, for the app's overlay
        (instead of JUCE's pairing dialogue, a bare light-grey popup with no title or Close button).
        Returns nullptr where there's no such view: use juce::BluetoothMidiDevicePairingDialogue there. */
   #if JUCE_IOS
    std::unique_ptr<juce::Component> createBluetoothMidiView();
   #else
    inline std::unique_ptr<juce::Component> createBluetoothMidiView()   { return nullptr; }
   #endif
}
