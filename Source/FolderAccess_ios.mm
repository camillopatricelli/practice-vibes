#include "FolderAccess.h"

#import <Foundation/Foundation.h>

namespace juce
{
    void* getURLBookmark (URL&);    // juce_URL.cpp: the bookmark the iOS FileChooser attached to a picked URL
}

namespace pv::FolderAccess
{

namespace
{
    NSString* toNS (const juce::String& s)      { return [NSString stringWithUTF8String: s.toRawUTF8()]; }
    juce::String fromNS (NSString* s)           { return s != nil ? juce::String::fromUTF8 ([s UTF8String]) : juce::String(); }

    juce::String encode (NSData* data)
    {
        return juce::MemoryBlock ([data bytes], (size_t) [data length]).toBase64Encoding();
    }

    // The folder in use (access stays open while the app runs).
    NSURL* openUrl = nil;
    juce::String openBookmark;
}

juce::String makeBookmark (juce::URL chosenFolder)
{
    if (auto* data = (NSData*) juce::getURLBookmark (chosenFolder))
        return encode (data);

    juce::Logger::writeToLog ("Library folder: the picker gave no bookmark for " + chosenFolder.toString (false));
    return {};
}

juce::File resolve (const juce::String& bookmark, juce::String& refreshed)
{
    refreshed.clear();

    if (bookmark.isEmpty())
        return {};

    if (openUrl != nil && (bookmark == openBookmark))
        return juce::File (fromNS ([openUrl path]));

    @autoreleasepool
    {
        juce::MemoryBlock block;

        if (! block.fromBase64Encoding (bookmark))
            return {};

        NSData* data = [NSData dataWithBytes: block.getData() length: block.getSize()];
        BOOL stale = NO;
        NSError* error = nil;
        NSURL* url = [NSURL URLByResolvingBookmarkData: data
                                               options: 0
                                         relativeToURL: nil
                                   bookmarkDataIsStale: &stale
                                                 error: &error];

        if (url == nil)
        {
            juce::Logger::writeToLog ("Library folder: can't find the chosen folder any more ("
                                      + fromNS (error != nil ? [error localizedDescription] : @"no details") + ")");
            return {};
        }

        // Folders inside the app's own container need no permission: then this returns NO, and that's fine.
        const bool scoped = [url startAccessingSecurityScopedResource];

        if (openUrl != nil)
        {
            [openUrl stopAccessingSecurityScopedResource];
            [openUrl release];
        }

        openUrl = [url retain];
        openBookmark = bookmark;

        if (stale)
        {
            if (NSData* fresh = [url bookmarkDataWithOptions: 0 includingResourceValuesForKeys: nil relativeToURL: nil error: nil])
            {
                refreshed = encode (fresh);
                openBookmark = refreshed;
            }
        }

        const auto path = fromNS ([url path]);
        juce::Logger::writeToLog ("Library folder opened: " + path + (scoped ? " (access granted)" : "")
                                  + (stale ? " (bookmark renewed)" : ""));
        return juce::File (path);
    }
}

int requestDownloads (const juce::File& root)
{
    int requested = 0;

    @autoreleasepool
    {
        NSFileManager* fm = [NSFileManager defaultManager];
        NSURL* rootUrl = [NSURL fileURLWithPath: toNS (root.getFullPathName()) isDirectory: YES];
        NSArray* keys = @[ NSURLIsDirectoryKey, NSURLUbiquitousItemDownloadingStatusKey ];

        // Hidden folders too: stems (.stems), takes (.takes) and the practice log (.practice) live there.
        NSDirectoryEnumerator* items = [fm enumeratorAtURL: rootUrl includingPropertiesForKeys: keys options: 0 errorHandler: nil];

        for (NSURL* url in items)
        {
            NSNumber* isDirectory = nil;
            [url getResourceValue: &isDirectory forKey: NSURLIsDirectoryKey error: nil];

            if (isDirectory != nil && [isDirectory boolValue])
                continue;

            NSURL* target = url;
            NSString* name = [url lastPathComponent];

            // iCloud Drive can show a file that isn't downloaded as ".<name>.icloud".
            if ([name hasPrefix: @"."] && [name hasSuffix: @".icloud"] && [name length] > 8)
            {
                NSString* real = [name substringWithRange: NSMakeRange (1, [name length] - 8)];
                target = [[url URLByDeletingLastPathComponent] URLByAppendingPathComponent: real];
            }
            else
            {
                NSString* status = nil;
                [url getResourceValue: &status forKey: NSURLUbiquitousItemDownloadingStatusKey error: nil];

                if (status == nil || [status isEqualToString: NSURLUbiquitousItemDownloadingStatusCurrent])
                    continue;
            }

            if ([fm startDownloadingUbiquitousItemAtURL: target error: nil])
                ++requested;
        }
    }

    return requested;
}

} // namespace pv::FolderAccess
