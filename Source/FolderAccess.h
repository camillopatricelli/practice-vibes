#pragma once

#include <juce_core/juce_core.h>

namespace pv::FolderAccess
{

/** iPad: a folder picked in the Files app (iCloud Drive; providers such as Google Drive do not allow picking folders) may only be used while its
    "security-scoped" access is open, and the permission is kept across restarts as a bookmark. Elsewhere these
    do nothing (folders are plain paths). */

/** The bookmark (base64) of a folder chosen with juce::FileChooser; empty if there is none / not needed. */
juce::String makeBookmark (juce::URL chosenFolder);

/** Opens the bookmarked folder for the rest of the app's life (cached: calling again with the same bookmark is
    cheap). Empty File if it can't be found any more. `refreshed` gets a new bookmark to save when the old one
    went stale. Message thread. */
juce::File resolve (const juce::String& bookmark, juce::String& refreshed);

/** Asks the cloud provider to download every file below `root` that isn't on the device yet (cloud drives keep
    only placeholders until a file is opened). Returns how many were requested. Any thread. */
int requestDownloads (const juce::File& root);

} // namespace pv::FolderAccess
