#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <devicetopology.h>
 #include <wrl/client.h>
#endif

#include "PracticeInput.h"
#include "AppPaths.h"

#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>
#endif

namespace pv
{

#if JUCE_IOS
// PracticeInput_ios.mm: the current route's first input (AVAudioSession port type and name).
void getIosInputRoute (juce::String& portType, juce::String& portName, bool& isMicrophone);
#endif

namespace
{
    bool hasActiveInput (juce::AudioDeviceManager& dm)
    {
        auto* device = dm.getCurrentAudioDevice();
        return device != nullptr && device->getActiveInputChannels().countNumberOfSetBits() > 0;
    }

    /** Last resort, from the name alone. */
    InputKind guessFromName (const juce::String& name)
    {
        for (auto* word : { "microphone array", "mic array", "built-in", "internal mic", "webcam", "camera", "headset" })
            if (name.containsIgnoreCase (word))
                return InputKind::microphone;

        return InputKind::audioInterface;
    }

   #if defined (_WIN32)
    using Microsoft::WRL::ComPtr;

    // Defined here rather than taken from functiondiscoverykeys_devpkey.h (which needs INITGUID games).
    const PROPERTYKEY keyFriendlyName { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };
    const PROPERTYKEY keyFormFactor   { { 0x1da5d803, 0xd492, 0x4edd, { 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e } }, 0 };

    enum { formLineLevel = 2, formHeadphones = 3, formMicrophone = 4, formHeadset = 5, formHandset = 6 };

    juce::String getStringProperty (IPropertyStore& store, const PROPERTYKEY& key)
    {
        PROPVARIANT value;
        PropVariantInit (&value);
        juce::String result;

        if (SUCCEEDED (store.GetValue (key, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr)
            result = juce::String (value.pwszVal);

        PropVariantClear (&value);
        return result;
    }

    int getIntProperty (IPropertyStore& store, const PROPERTYKEY& key, int fallback)
    {
        PROPVARIANT value;
        PropVariantInit (&value);
        auto result = fallback;

        if (SUCCEEDED (store.GetValue (key, &value)) && (value.vt == VT_UI4 || value.vt == VT_I4))
            result = (int) value.ulVal;

        PropVariantClear (&value);
        return result;
    }

    /** The driver device the endpoint belongs to ("{2}.\\?\usb#vid_...", "...\hdaudio#func_01..."), lower case. */
    juce::String getAdapterId (IMMDevice& endpoint)
    {
        ComPtr<IDeviceTopology> topology;
        ComPtr<IConnector> connector;
        LPWSTR id = nullptr;
        juce::String result;

        if (SUCCEEDED (endpoint.Activate (__uuidof (IDeviceTopology), CLSCTX_ALL, nullptr, (void**) topology.GetAddressOf()))
            && SUCCEEDED (topology->GetConnector (0, connector.GetAddressOf()))
            && SUCCEEDED (connector->GetDeviceIdConnectedTo (&id)) && id != nullptr)
        {
            result = juce::String (id).toLowerCase();
        }

        if (id != nullptr)
            CoTaskMemFree (id);

        return result;
    }

    InputInfo classifyWindowsEndpoint (const juce::String& name)
    {
        InputInfo info { InputKind::audioInterface, name, {} };

        const auto hr = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
        const bool mustUninit = SUCCEEDED (hr);

        {
            ComPtr<IMMDeviceEnumerator> enumerator;
            ComPtr<IMMDeviceCollection> endpoints;
            UINT count = 0;

            if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator),
                                          (void**) enumerator.GetAddressOf()))
                || FAILED (enumerator->EnumAudioEndpoints (eCapture, DEVICE_STATE_ACTIVE, endpoints.GetAddressOf()))
                || FAILED (endpoints->GetCount (&count)))
            {
                count = 0;
            }

            ComPtr<IMMDevice> found;

            for (UINT i = 0; i < count && found == nullptr; ++i)
            {
                ComPtr<IMMDevice> endpoint;
                ComPtr<IPropertyStore> store;

                if (SUCCEEDED (endpoints->Item (i, endpoint.GetAddressOf()))
                    && SUCCEEDED (endpoint->OpenPropertyStore (STGM_READ, store.GetAddressOf()))
                    && getStringProperty (*store.Get(), keyFriendlyName) == name)
                {
                    found = endpoint;
                }
            }

            if (found == nullptr)
            {
                info.kind = guessFromName (name);
                info.reason = "not found among the Windows inputs: guessed from the name";
            }
            else
            {
                ComPtr<IPropertyStore> store;
                const auto form = SUCCEEDED (found->OpenPropertyStore (STGM_READ, store.GetAddressOf()))
                                      ? getIntProperty (*store.Get(), keyFormFactor, -1) : -1;
                const auto adapter = getAdapterId (*found.Get());
                const bool micForm = form == formMicrophone || form == formHeadset || form == formHeadphones || form == formHandset;

                if (adapter.contains ("usb"))
                {
                    // USB: an audio interface, unless it's a webcam's mic.
                    info.kind = guessFromName (name) == InputKind::microphone ? InputKind::microphone : InputKind::audioInterface;
                    info.reason = "USB device";
                }
                else if (adapter.contains ("bth"))
                {
                    info.kind = InputKind::microphone;
                    info.reason = "Bluetooth device";
                }
                else if (form == formLineLevel)
                {
                    info.kind = InputKind::audioInterface;
                    info.reason = "line input";
                }
                else if (micForm || adapter.contains ("hdaudio") || adapter.contains ("intelaudio") || adapter.contains ("acpi"))
                {
                    info.kind = InputKind::microphone;
                    info.reason = "built-in audio, microphone input";
                }
                else
                {
                    info.kind = guessFromName (name);
                    info.reason = "unknown kind of input: guessed from the name";
                }

                info.reason << " (form factor " << form << ", " << (adapter.isNotEmpty() ? adapter.upToFirstOccurrenceOf ("&", false, false) : juce::String ("no adapter id")) << ")";
            }
        }

        if (mustUninit)
            CoUninitialize();

        return info;
    }
   #endif

   #if JUCE_MAC
    template <typename T>
    bool getProperty (AudioObjectID object, AudioObjectPropertySelector selector, T& value)
    {
        AudioObjectPropertyAddress address { selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        UInt32 size = sizeof (T);
        return AudioObjectGetPropertyData (object, &address, 0, nullptr, &size, &value) == noErr;
    }

    InputInfo classifyMacDevice (const juce::String& name)
    {
        InputInfo info { guessFromName (name), name, "not found among the CoreAudio devices: guessed from the name" };

        AudioObjectPropertyAddress address { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        UInt32 size = 0;

        if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr)
            return info;

        std::vector<AudioObjectID> devices (size / sizeof (AudioObjectID));

        if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr)
            return info;

        for (auto device : devices)
        {
            CFStringRef cfName = nullptr;

            if (! getProperty (device, kAudioObjectPropertyName, cfName) || cfName == nullptr)
                continue;

            const auto deviceName = juce::String::fromCFString (cfName);
            CFRelease (cfName);

            if (deviceName != name)
                continue;

            UInt32 transport = 0;
            getProperty (device, kAudioDevicePropertyTransportType, transport);

            const bool mic = transport == kAudioDeviceTransportTypeBuiltIn
                          || transport == kAudioDeviceTransportTypeBluetooth
                          || transport == kAudioDeviceTransportTypeBluetoothLE
                          || transport == 'ccwd' || transport == 'ccwl';   // Continuity Camera (an iPhone's mic)

            info.kind = mic ? InputKind::microphone : InputKind::audioInterface;
            info.reason = "CoreAudio transport type '" + juce::String::toHexString ((int) transport) + "'";
            break;
        }

        return info;
    }
   #endif
}

//==============================================================================
juce::String getInputKey (juce::AudioDeviceManager& dm)
{
    if (! hasActiveInput (dm))
        return "none";

    const auto setup = dm.getAudioDeviceSetup();
    juce::String key = dm.getCurrentAudioDeviceType() + "|" + setup.inputDeviceName;

   #if JUCE_IOS
    juce::String portType, portName;
    bool mic = true;
    getIosInputRoute (portType, portName, mic);
    key << "|" << portType << "|" << portName;
   #endif

    return key;
}

InputInfo classifyInput (juce::AudioDeviceManager& dm)
{
    if (! hasActiveInput (dm))
        return { InputKind::none, {}, "no input channel open" };

    const auto setup = dm.getAudioDeviceSetup();
    const auto type = dm.getCurrentAudioDeviceType();
    const auto name = setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName : dm.getCurrentAudioDevice()->getName();

   #if JUCE_IOS
    juce::String portType, portName;
    bool mic = true;
    getIosInputRoute (portType, portName, mic);
    juce::ignoreUnused (type);

    return { mic ? InputKind::microphone : InputKind::audioInterface,
             portName.isNotEmpty() ? portName : name,
             "iOS input port " + (portType.isNotEmpty() ? portType : juce::String ("unknown")) };
   #elif defined (_WIN32)
    if (type == "ASIO")
        return { InputKind::audioInterface, name, "ASIO driver" };

    return classifyWindowsEndpoint (name);
   #elif JUCE_MAC
    juce::ignoreUnused (type);
    return classifyMacDevice (name);
   #else
    juce::ignoreUnused (type);
    return { guessFromName (name), name, "guessed from the name" };
   #endif
}

bool usesStartButton (const InputInfo& info, PracticeTiming timing)
{
    switch (timing)
    {
        case PracticeTiming::automatic:   return false;
        case PracticeTiming::midiNotes:   return false;
        case PracticeTiming::startButton: return true;
        case PracticeTiming::byInputType: break;
    }

    return info.kind == InputKind::microphone;
}

//==============================================================================
namespace
{
    juce::File getPracticeInputsFile()   { return getAppDataFolder().getChildFile ("practice-inputs.xml"); }
}

PracticeInputSettings::PracticeInputSettings()
{
    if (auto xml = juce::parseXML (getPracticeInputsFile()); xml != nullptr && xml->hasTagName ("PRACTICE_INPUTS"))
    {
        midiDevice = xml->getStringAttribute ("midiDevice");

        for (auto* e : xml->getChildWithTagNameIterator ("INPUT"))
        {
            const auto timing = e->getStringAttribute ("timing");
            const auto name = e->getStringAttribute ("name");

            if (name.isNotEmpty() && (timing == "automatic" || timing == "startButton" || timing == "midiNotes"))
                timings[name] = timing == "automatic" ? PracticeTiming::automatic
                              : timing == "midiNotes" ? PracticeTiming::midiNotes
                                                      : PracticeTiming::startButton;
        }
    }
}

void PracticeInputSettings::setMidiDevice (const juce::String& name)
{
    if (name == midiDevice)
        return;

    midiDevice = name;
    save();
}

bool PracticeInputSettings::midiNoteCounts (const juce::String& sourceName, PracticeTiming timing) const
{
    if (midiDevice == anyMidi)
        return true;

    if (midiDevice == noMidi)
        return timing == PracticeTiming::midiNotes;

    return sourceName == midiDevice;
}

PracticeTiming PracticeInputSettings::get (const juce::String& inputName) const
{
    const auto it = timings.find (inputName);
    return it != timings.end() ? it->second : PracticeTiming::byInputType;
}

void PracticeInputSettings::set (const juce::String& inputName, PracticeTiming timing)
{
    if (inputName.isEmpty())
        return;

    if (timing == PracticeTiming::byInputType)
        timings.erase (inputName);
    else
        timings[inputName] = timing;

    save();
}

void PracticeInputSettings::save() const
{
    juce::XmlElement xml ("PRACTICE_INPUTS");

    if (midiDevice.isNotEmpty())
        xml.setAttribute ("midiDevice", midiDevice);

    for (auto& [name, t] : timings)
    {
        auto* e = xml.createNewChildElement ("INPUT");
        e->setAttribute ("name", name);
        e->setAttribute ("timing", t == PracticeTiming::automatic ? "automatic"
                                 : t == PracticeTiming::midiNotes ? "midiNotes" : "startButton");
    }

    xml.writeTo (getPracticeInputsFile());
}

} // namespace pv
