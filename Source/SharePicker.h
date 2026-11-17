#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pv
{

#if JUCE_MAC
/** macOS: the system share menu (AirDrop, Mail, Messages, WhatsApp if installed...) for a file, shown over the
    component. Falls back to showing the file in Finder. */
void showSystemSharePicker (const juce::Array<juce::File>&, juce::Component& anchor);
#endif

#if JUCE_IOS
/** iPad: files opened with the app ("Open in Practice Vibes" from Mail, WhatsApp, Files, AirDrop) arrive here as a
    readable copy in the temporary folder, on the message thread. JUCE's scene delegate ignores them, so this adds
    the UIScene URL callbacks to it. Call once from initialise(). */
void installOpenUrlHandler (std::function<void (const juce::File&)>);
#endif

} // namespace pv
