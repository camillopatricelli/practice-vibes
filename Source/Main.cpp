#include <juce_gui_basics/juce_gui_basics.h>
#include "AppEngine.h"
#include "AppLog.h"
#include "AppPaths.h"
#include "BuildInfo.h"
#include "Localisation.h"
#include "MainComponent.h"
#include "SharePicker.h"
#include "TopicShare.h"
#include "StoragePanel.h"
#include "Theme.h"
#include "WarmLookAndFeel.h"

namespace pv
{

/** Windows 10/11: the native title bar in the theme's colours (Windows 11 caption colour; dark mode on Windows 10). */
inline void colourTitleBar (juce::Component& window)
{
   #if JUCE_WINDOWS
    using SetAttribute = long (__stdcall*) (void*, unsigned long, const void*, unsigned long);
    static juce::DynamicLibrary dwm ("dwmapi.dll");
    static auto setAttribute = (SetAttribute) dwm.getFunction ("DwmSetWindowAttribute");

    auto* hwnd = window.getWindowHandle();

    if (setAttribute == nullptr || hwnd == nullptr)
        return;

    auto colorRef = [] (juce::Colour c) -> unsigned long { return (unsigned long) c.getRed() | ((unsigned long) c.getGreen() << 8) | ((unsigned long) c.getBlue() << 16); };

    const int dark = theme::isDark() ? 1 : 0;
    const auto caption = colorRef (theme::background()), text = colorRef (theme::text());
    setAttribute (hwnd, 20, &dark, sizeof (dark));          // DWMWA_USE_IMMERSIVE_DARK_MODE
    setAttribute (hwnd, 35, &caption, sizeof (caption));    // DWMWA_CAPTION_COLOR (Windows 11)
    setAttribute (hwnd, 36, &text, sizeof (text));          // DWMWA_TEXT_COLOR (Windows 11)
   #else
    juce::ignoreUnused (window);
   #endif
}

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, AppEngine& engine, AppLog& log)
        : DocumentWindow (name, theme::background(), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (engine, log), true);

       #if JUCE_IOS || JUCE_ANDROID
        setFullScreen (true);
        setVisible (true);
       #else
        setResizable (true, true);
        setResizeLimits (900, 600, 4000, 3000);
        centreWithSize (1280, 800);     // the size "Restore" goes back to
        // Desktop: stays hidden until showMaximised(), so the half-drawn window isn't seen while
        // the engine opens the audio device and reloads the plugins.
       #endif
    }

    ~MainWindow() override
    {
        theme::removeListener (this);
    }

    void showMaximised()
    {
        colourTitleBar (*this);
        theme::addListener (this, [this] (const theme::Palette&) { colourTitleBar (*this); });

       #if ! (JUCE_IOS || JUCE_ANDROID)
        setVisible (true);
        setFullScreen (true);           // start maximised (title bar always on screen)
       #endif
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

//==============================================================================
class PracticeVibesApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        log = std::make_unique<AppLog>();

        juce::Logger::writeToLog (getApplicationName() + " " + pv::build::versionLine() + " starting on "
                                  + pv::build::platform()
                                  + " (" + juce::SystemStats::getDeviceDescription() + ")");
        juce::Logger::writeToLog ("Data folder: " + getAppDataFolder().getFullPathName());

        pv::StorageScan::clearWebCacheIfAsked (getAppDataFolder());    // before any web view uses it

        // The look, before any component exists: theme (Dark / Light / Follow system) from ui.xml, then the fonts and colours.
        // ... and the language (empty = the device language; a change applies at the next start).
        if (auto ui = juce::parseXML (getUiSettingsFile()))
        {
            theme::setMode (theme::modeFromName (ui->getStringAttribute ("theme", "dark")));
            i18n::initialise (ui->getStringAttribute ("language"), getAppDataFolder());
        }
        else
        {
            i18n::initialise ({}, getAppDataFolder());
        }

        juce::Logger::writeToLog ("Language: " + i18n::currentLanguage() + " (device: " + i18n::deviceLanguage() + ")");

        lookAndFeel = std::make_unique<WarmLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        engine = std::make_unique<AppEngine>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine, *log);
        engine->start();
        mainWindow->showMaximised();    // desktop: shown only once the blocking start-up work is done

       #if JUCE_WINDOWS
        registerTopicFileType();
       #endif
       #if JUCE_IOS
        installOpenUrlHandler ([this] (const juce::File& f) { openSharedTopic (f); });
       #endif

        openSongFrom (commandLine);
    }

    /** Desktop: a song opened while the app is running (Windows: a second launch with the file on its command line;
        macOS: Finder's "Open With" / a file dropped on the Dock icon, which also arrives here at start-up). */
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        openSongFrom (commandLine);

        if (mainWindow != nullptr)
            mainWindow->toFront (true);
    }

    void shutdown() override
    {
        if (engine != nullptr)
            engine->saveState();

        mainWindow.reset();
        engine.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
        log.reset();
    }

    void suspended() override
    {
        // iOS: the app is going to the background and may be killed without warning.
        if (engine != nullptr)
            engine->saveState();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    void openSharedTopic (const juce::File& file)
    {
        if (mainWindow != nullptr)
            if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                main->openSharedTopic (file);
    }

   #if JUCE_WINDOWS
    /** Double-clicking a .pvtopic opens this app (per user, no admin rights): HKCU\Software\Classes. Rewritten only
        when it points elsewhere (e.g. the app moved), then Explorer is told the associations changed. */
    static void registerTopicFileType()
    {
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
        const juce::String classes = "HKEY_CURRENT_USER\\Software\\Classes\\", progId = "PracticeVibes.Topic";
        const auto command = "\"" + exe + "\" \"%1\"";

        // A path ending in a backslash means the key's default value.
        if (juce::WindowsRegistry::getValue (classes + progId + "\\shell\\open\\command\\") == command
            && juce::WindowsRegistry::getValue (classes + ".pvtopic\\") == progId)
            return;

        bool ok = juce::WindowsRegistry::setValue (classes + ".pvtopic\\", progId);
        ok = juce::WindowsRegistry::setValue (classes + ".pvtopic\\Content Type", juce::String ("application/x-practicevibes-topic")) && ok;
        ok = juce::WindowsRegistry::setValue (classes + progId + "\\", juce::String ("Practice Vibes topic")) && ok;
        ok = juce::WindowsRegistry::setValue (classes + progId + "\\DefaultIcon\\", "\"" + exe + "\",0") && ok;
        ok = juce::WindowsRegistry::setValue (classes + progId + "\\shell\\open\\command\\", command) && ok;
        juce::Logger::writeToLog (juce::String (ok ? "Registered" : "Could not register") + " .pvtopic files for " + exe);

        using ChangeNotify = void (__stdcall*) (long, unsigned int, const void*, const void*);
        static juce::DynamicLibrary shell ("shell32.dll");

        if (auto notify = (ChangeNotify) shell.getFunction ("SHChangeNotify"))
            notify (0x08000000 /* SHCNE_ASSOCCHANGED */, 0 /* SHCNF_IDLIST */, nullptr, nullptr);
    }
   #endif

    /** Opens the first existing absolute file path in a command line in the player (desktop only). The whole line is
        tried first (an unquoted path with spaces), then each token (JUCE on macOS quotes paths that contain spaces).
        A shared topic (.pvtopic) opens the import preview instead. */
    void openSongFrom (const juce::String& commandLine)
    {
       #if ! (JUCE_IOS || JUCE_ANDROID)
        if (engine == nullptr)
            return;

        juce::StringArray candidates { commandLine.trim().unquoted() };
        candidates.addTokens (commandLine, " ", "\"");

        for (auto candidate : candidates)
        {
            candidate = candidate.trim().unquoted();

            if (candidate.isNotEmpty() && juce::File::isAbsolutePath (candidate) && juce::File (candidate).existsAsFile())
            {
                juce::Logger::writeToLog ("Opening " + candidate);

                if (TopicShare::isSharedTopicFile (juce::File (candidate)))
                {
                    // After start-up work queued on the message thread (the library is opened by then).
                    juce::MessageManager::callAsync ([this, file = juce::File (candidate)] { openSharedTopic (file); });
                    return;
                }

                engine->player->loadAsync (juce::URL (juce::File (candidate)), {});
                return;
            }
        }
       #else
        juce::ignoreUnused (commandLine);
       #endif
    }

    std::unique_ptr<AppLog> log;
    std::unique_ptr<WarmLookAndFeel> lookAndFeel;      // outlives every component
    std::unique_ptr<AppEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace pv

START_JUCE_APPLICATION (pv::PracticeVibesApplication)
