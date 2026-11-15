#include "FolderAccess.h"

#if ! JUCE_IOS   // iOS: FolderAccess_ios.mm

namespace pv::FolderAccess
{

juce::String makeBookmark (juce::URL)                           { return {}; }
juce::File resolve (const juce::String&, juce::String& refreshed) { refreshed.clear(); return {}; }
int requestDownloads (const juce::File&)                        { return 0; }

} // namespace pv::FolderAccess

#endif
