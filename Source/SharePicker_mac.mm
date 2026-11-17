#include "SharePicker.h"

#import <AppKit/AppKit.h>

namespace pv
{

void showSystemSharePicker (const juce::Array<juce::File>& files, juce::Component& anchor)
{
    auto* peer = anchor.getPeer();
    auto* view = peer != nullptr ? (NSView*) peer->getNativeHandle() : nil;

    if (view == nil || files.isEmpty())
    {
        if (! files.isEmpty())
            files.getFirst().revealToUser();

        return;
    }

    @autoreleasepool
    {
        static NSSharingServicePicker* picker = nil;   // kept alive while the menu is open (manual retain/release)

        if (picker != nil)
            [picker release];

        NSMutableArray* items = [NSMutableArray array];

        for (auto& file : files)
            [items addObject: [NSURL fileURLWithPath: [NSString stringWithUTF8String: file.getFullPathName().toRawUTF8()]]];

        picker = [[NSSharingServicePicker alloc] initWithItems: items];

        // JUCE's views are flipped (y down), like component coordinates.
        const auto area = peer->getComponent().getLocalArea (&anchor, anchor.getLocalBounds().withSizeKeepingCentre (2, 2));
        [picker showRelativeToRect: NSMakeRect (area.getX(), area.getY(), area.getWidth(), area.getHeight())
                            ofView: view
                     preferredEdge: NSRectEdgeMinY];
    }
}

} // namespace pv
