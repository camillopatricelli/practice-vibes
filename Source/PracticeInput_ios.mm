#include <juce_core/juce_core.h>
#import <AVFoundation/AVFoundation.h>

namespace pv
{

void getIosInputRoute (juce::String& portType, juce::String& portName, bool& isMicrophone)
{
    portType = {};
    portName = {};
    isMicrophone = true;   // no input in the route: the built-in mic is what iOS falls back to

    @autoreleasepool
    {
        AVAudioSessionPortDescription* input = [[[AVAudioSession sharedInstance] currentRoute] inputs].firstObject;

        if (input != nil)
        {
            portType = juce::String::fromUTF8 ([input.portType UTF8String]);
            portName = juce::String::fromUTF8 ([input.portName UTF8String]);

            NSString* type = input.portType;
            isMicrophone = [type isEqualToString: AVAudioSessionPortBuiltInMic]
                        || [type isEqualToString: AVAudioSessionPortHeadsetMic]
                        || [type isEqualToString: AVAudioSessionPortBluetoothHFP]
                        || [type isEqualToString: AVAudioSessionPortCarAudio]
                        || [type rangeOfString: @"Continuity"].location != NSNotFound;
        }
    }
}

} // namespace pv
