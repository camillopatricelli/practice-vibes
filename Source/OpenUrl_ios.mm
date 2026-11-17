#include "SharePicker.h"

#import <UIKit/UIKit.h>
#import <objc/runtime.h>

namespace pv
{

namespace
{
    std::function<void (const juce::File&)>& handler()
    {
        static std::function<void (const juce::File&)> h;
        return h;
    }

    /** Copies the opened file (security-scoped when it comes from Files) to the temporary folder and hands it on. */
    void received (NSURL* url)
    {
        if (url == nil || ! url.isFileURL)
            return;

        const bool scoped = [url startAccessingSecurityScopedResource];
        const juce::File source (juce::String::fromUTF8 (url.path.UTF8String));
        const auto copy = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("Opened").getChildFile (source.getFileName());
        copy.getParentDirectory().createDirectory();
        copy.deleteFile();
        const bool ok = source.copyFileTo (copy);

        if (scoped)
            [url stopAccessingSecurityScopedResource];

        juce::Logger::writeToLog ("Opened from another app: " + source.getFileName() + (ok ? "" : " (could not be read)"));

        if (ok)
            juce::MessageManager::callAsync ([copy] { if (handler()) handler() (copy); });
    }

    void receivedContexts (NSSet<UIOpenURLContext*>* contexts) API_AVAILABLE (ios (13.0))
    {
        for (UIOpenURLContext* context in contexts)
            received (context.URL);
    }
}

void installOpenUrlHandler (std::function<void (const juce::File&)> fn)
{
    handler() = std::move (fn);

    if (@available (iOS 13.0, *))
    {
        Class delegateClass = NSClassFromString (@"JuceAppSceneDelegate");

        if (delegateClass == nil)
        {
            juce::Logger::writeToLog ("Open-in handler: JUCE scene delegate not found");
            return;
        }

        // The app is running: scene:openURLContexts:
        IMP openImp = imp_implementationWithBlock (^(id, UIScene*, NSSet<UIOpenURLContext*>* contexts) { receivedContexts (contexts); });
        class_replaceMethod (delegateClass, @selector (scene:openURLContexts:), openImp, "v@:@@");

        // Cold start with a file: the URLs come in the connection options. Wrap JUCE's own method.
        SEL connectSel = @selector (scene:willConnectToSession:options:);
        Method connect = class_getInstanceMethod (delegateClass, connectSel);

        if (connect == nullptr)
            return;

        using ConnectFn = void (*) (id, SEL, UIScene*, UISceneSession*, UISceneConnectionOptions*);
        auto original = (ConnectFn) method_getImplementation (connect);

        IMP wrapped = imp_implementationWithBlock (^(id self, UIScene* scene, UISceneSession* session, UISceneConnectionOptions* options)
        {
            original (self, connectSel, scene, session, options);
            receivedContexts (options.URLContexts);
        });
        method_setImplementation (connect, wrapped);
    }
}

} // namespace pv
