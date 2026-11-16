cmake_minimum_required(VERSION 3.22)

project(PracticeVibes VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.5
    GIT_SHALLOW    TRUE)
FetchContent_Declare(signalsmith-stretch    # time-stretch / pitch-shift (MIT)
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(JUCE signalsmith-stretch)

# Windows: the in-app viewer (documents, YouTube) uses Microsoft's WebView2 (runtime ships with Windows 11).
# The SDK package (headers + static loader) is downloaded from NuGet into the build folder, where JUCE's
# FindWebView2 looks for a "*Microsoft.Web.WebView2*" directory.
if(WIN32)
    set(PV_WEBVIEW2_VERSION 1.0.3485.44)
    FetchContent_Declare(webview2
        URL           https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${PV_WEBVIEW2_VERSION}
        DOWNLOAD_NAME webview2.zip
        SOURCE_DIR    ${CMAKE_BINARY_DIR}/nuget/Microsoft.Web.WebView2.${PV_WEBVIEW2_VERSION})
    FetchContent_MakeAvailable(webview2)
    set(JUCE_WEBVIEW2_PACKAGE_LOCATION ${CMAKE_BINARY_DIR}/nuget)
    set(PV_WEBVIEW2 TRUE)

    # Stem separation runs the Demucs network with ONNX Runtime (MIT). Only its headers are used at build time;
    # onnxruntime.dll is copied next to the app and loaded on first use.
    set(PV_ONNXRUNTIME_VERSION 1.20.1)
    FetchContent_Declare(onnxruntime
        URL https://github.com/microsoft/onnxruntime/releases/download/v${PV_ONNXRUNTIME_VERSION}/onnxruntime-win-x64-${PV_ONNXRUNTIME_VERSION}.zip)
    FetchContent_MakeAvailable(onnxruntime)
    set(PV_ONNXRUNTIME_DIR ${onnxruntime_SOURCE_DIR})
    set(PV_ONNXRUNTIME_LIB ${PV_ONNXRUNTIME_DIR}/lib/onnxruntime.dll)
else()
    set(PV_WEBVIEW2 FALSE)
endif()

# macOS desktop (not iOS): the same ONNX Runtime as a universal (arm64 + x86_64) dylib, bundled in
# Contents/Frameworks and loaded with dlopen on first use.
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PV_MACOS TRUE)
    set(PV_ONNXRUNTIME_VERSION 1.20.1)
    FetchContent_Declare(onnxruntime
        URL https://github.com/microsoft/onnxruntime/releases/download/v${PV_ONNXRUNTIME_VERSION}/onnxruntime-osx-universal2-${PV_ONNXRUNTIME_VERSION}.tgz)
    FetchContent_MakeAvailable(onnxruntime)
    set(PV_ONNXRUNTIME_DIR ${onnxruntime_SOURCE_DIR})
    set(PV_ONNXRUNTIME_LIB ${PV_ONNXRUNTIME_DIR}/lib/libonnxruntime.${PV_ONNXRUNTIME_VERSION}.dylib)
    if(NOT EXISTS ${PV_ONNXRUNTIME_LIB})
        file(GLOB found ${PV_ONNXRUNTIME_DIR}/lib/*)
        message(FATAL_ERROR "ONNX Runtime dylib not found at ${PV_ONNXRUNTIME_LIB}; lib/ holds: ${found}")
    endif()
    # Finder's "Open With" lists the app for these song files (they arrive in anotherInstanceStarted).
    set(PV_DOCUMENT_EXTENSIONS wav aif aiff flac mp3 m4a aac ogg)
else()
    set(PV_MACOS FALSE)
    set(PV_DOCUMENT_EXTENSIONS "")
endif()

# Treat third-party headers as system headers so our strict warnings don't apply to them.
set_target_properties(signalsmith-stretch signalsmith-linear PROPERTIES SYSTEM TRUE)

# AudioUnit hosting exists only on Apple platforms (AUv3 on iPad, AUv2 + AUv3 on macOS), VST3 hosting only on desktop.
if(APPLE)
    set(PV_HOST_AU TRUE)
else()
    set(PV_HOST_AU FALSE)
endif()

# App icon: iOS wants a full square without alpha (it rounds the corners itself), desktop a rounded transparent one.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PV_APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon/AppIcon-ios-1024.png)
else()
    set(PV_APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon/AppIcon-desktop-1024.png)
endif()

juce_add_gui_app(PracticeVibes
    PRODUCT_NAME                   "Practice Vibes"
    ICON_BIG                       ${PV_APP_ICON}
    ICON_SMALL                     ${PV_APP_ICON}
    COMPANY_NAME                   "Practice Vibes"
    BUNDLE_ID                      io.github.joefrezza.practicevibes
    VERSION                        ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED  TRUE
    MICROPHONE_PERMISSION_TEXT     "Practice Vibes processes the signal from your guitar."
    BLUETOOTH_PERMISSION_ENABLED   TRUE
    BLUETOOTH_PERMISSION_TEXT      "Practice Vibes connects to Bluetooth MIDI footswitches."
    BACKGROUND_AUDIO_ENABLED       TRUE
    FILE_SHARING_ENABLED           TRUE
    DOCUMENT_BROWSER_ENABLED       TRUE
    STATUS_BAR_HIDDEN              TRUE
    REQUIRES_FULL_SCREEN           TRUE
    TARGETED_DEVICE_FAMILY         "1,2"
    PLUGINHOST_AU                  ${PV_HOST_AU}
    NEEDS_WEB_BROWSER              TRUE
    NEEDS_WEBVIEW2                 ${PV_WEBVIEW2}
    NEEDS_CURL                     FALSE
    DOCUMENT_EXTENSIONS            ${PV_DOCUMENT_EXTENSIONS})

target_sources(PracticeVibes PRIVATE
    Source/Main.cpp
    Source/AacFile.cpp
    Source/AppEngine.cpp
    Source/AppLog.cpp
    Source/ChordAnalyzer.cpp
    Source/ChordView.cpp
    Source/DrumEditor.cpp
    Source/DrumKit.cpp
    Source/DrumPattern.cpp
    Source/DrumSequencer.cpp
    Source/DrumSynth.cpp
    Source/FolderAccess.cpp
    Source/Footswitch.cpp
    Source/FootswitchPanel.cpp
    Source/Library.cpp
    Source/LibraryPanel.cpp
    Source/Links.cpp
    Source/LinksPanel.cpp
    Source/LoopFiles.cpp
    Source/Looper.cpp
    Source/LooperPanel.cpp
    Source/MainComponent.cpp
    Source/OnnxDemucs.cpp
    Source/MediaFoundationAudio.cpp
    Source/MetronomeProcessor.cpp
    Source/MidiMonitor.cpp
    Source/PlayerProcessor.cpp
    Source/PluginCatalogue.cpp
    Source/PluginChain.cpp
    Source/PracticeLog.cpp
    Source/PracticePanel.cpp
    Source/PracticePlanner.cpp
    Source/SessionPlans.cpp
    Source/PracticeViews.cpp
    Source/Recorder.cpp
    Source/RigPanel.cpp
    Source/StemMixer.cpp
    Source/StoragePanel.cpp
    Source/Stems.cpp
    Source/Take.cpp
    Source/TodayPanel.cpp
    Source/Theme.cpp
    Source/Tuner.cpp
    Source/TunerPanel.cpp
    Source/WarmLookAndFeel.cpp
    Source/WaveformView.cpp
    Source/YouTubePlayer.cpp)

target_compile_definitions(PracticeVibes PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PracticeVibes,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PracticeVibes,JUCE_VERSION>"
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_SILENCE_XCODE_15_LINKER_WARNING=1)

# iPad: exported mixes go out through the share sheet; library folders in cloud drives need folder access (bookmarks).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_sources(PracticeVibes PRIVATE Source/FolderAccess_ios.mm)
    target_compile_definitions(PracticeVibes PRIVATE JUCE_CONTENT_SHARING=1)
endif()

if(WIN32)
    target_compile_definitions(PracticeVibes PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_USE_WINRT_MIDI=1     # Bluetooth LE MIDI on Windows 10/11
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0)
endif()

# macOS: VST3 plugins too (AudioUnits are hosted on all Apple platforms via PLUGINHOST_AU).
if(PV_MACOS)
    target_compile_definitions(PracticeVibes PRIVATE JUCE_PLUGINHOST_VST3=1)
endif()

# Adds ONNX Runtime's headers to a target and ships the runtime library with it: next to the executable
# (Windows, console tools) or in the app bundle's Contents/Frameworks (macOS app; licences in Contents/Resources).
function(pv_use_onnxruntime target)
    if(PV_ONNXRUNTIME_DIR)
        target_include_directories(${target} SYSTEM PRIVATE ${PV_ONNXRUNTIME_DIR}/include)
        target_compile_definitions(${target} PRIVATE PV_HAS_ONNX=1)

        get_filename_component(library_name ${PV_ONNXRUNTIME_LIB} NAME)
        set(library_dir $<TARGET_FILE_DIR:${target}>)
        set(notices_dir $<TARGET_FILE_DIR:${target}>)

        if(PV_MACOS)
            set(library_name libonnxruntime.dylib)
            get_target_property(is_bundle ${target} MACOSX_BUNDLE)

            if(is_bundle)
                set(library_dir $<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks)
                set(notices_dir $<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources)
            endif()
        endif()

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${library_dir} ${notices_dir}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PV_ONNXRUNTIME_LIB} ${library_dir}/${library_name}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PV_ONNXRUNTIME_DIR}/LICENSE ${notices_dir}/onnxruntime-LICENSE.txt
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PV_ONNXRUNTIME_DIR}/ThirdPartyNotices.txt ${notices_dir}/onnxruntime-ThirdPartyNotices.txt)
    endif()
endfunction()

pv_use_onnxruntime(PracticeVibes)

# The icon drawn in the app's own top bar.
juce_add_binary_data(PracticeVibesIcons
    HEADER_NAME AppIconData.h
    NAMESPACE   AppIconData
    SOURCES     Resources/AppIcon/AppIcon-desktop-1024.png)

# The sampled drum kit (made by Tools/DrumKitTool from Big Rusty Drums, CC0).
file(GLOB PV_DRUM_SAMPLES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/DrumKit/*.flac)
juce_add_binary_data(PracticeVibesDrumKit
    HEADER_NAME DrumKitData.h
    NAMESPACE   DrumKitData
    SOURCES     ${PV_DRUM_SAMPLES})

# The UI fonts: Outfit (headings) and DM Sans (text), both SIL Open Font License (Resources/Fonts/*-OFL.txt).
file(GLOB PV_FONTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/*.ttf)
juce_add_binary_data(PracticeVibesFonts
    HEADER_NAME FontData.h
    NAMESPACE   FontData
    SOURCES     ${PV_FONTS})

target_link_libraries(PracticeVibes
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        signalsmith-stretch
        PracticeVibesIcons
        PracticeVibesDrumKit
        PracticeVibesFonts
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline DSP tests (desktop only)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    juce_add_console_app(PracticeVibesTests PRODUCT_NAME "PracticeVibesTests")

    target_sources(PracticeVibesTests PRIVATE
        Tests/AudioTests.cpp
        Source/AacFile.cpp
        Source/ChordAnalyzer.cpp
        Source/DrumKit.cpp
        Source/DrumPattern.cpp
        Source/DrumSequencer.cpp
        Source/DrumSynth.cpp
        Source/FolderAccess.cpp
    Source/Footswitch.cpp
        Source/Library.cpp
        Source/Links.cpp
        Source/LoopFiles.cpp
        Source/Looper.cpp
        Source/MediaFoundationAudio.cpp
        Source/MetronomeProcessor.cpp
        Source/PlayerProcessor.cpp
        Source/PracticeLog.cpp
        Source/PracticePlanner.cpp
        Source/SessionPlans.cpp
        Source/Recorder.cpp
        Source/OnnxDemucs.cpp
        Source/StemMixer.cpp
        Source/StoragePanel.cpp
        Source/Stems.cpp
        Source/Take.cpp
        Source/Theme.cpp
        Source/Tuner.cpp)

    pv_use_onnxruntime(PracticeVibesTests)

    target_compile_definitions(PracticeVibesTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)

    target_link_libraries(PracticeVibesTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            signalsmith-stretch
            PracticeVibesDrumKit
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Developer tool: builds Resources/DrumKit from the Big Rusty Drums samples (CC0).
    juce_add_console_app(DrumKitTool PRODUCT_NAME "DrumKitTool")
    target_sources(DrumKitTool PRIVATE Tools/DrumKitTool.cpp Source/DrumSynth.cpp)
    target_compile_definitions(DrumKitTool PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(DrumKitTool
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Developer tool: prints the chord analysis of audio files.
    juce_add_console_app(ChordTool PRODUCT_NAME "ChordTool")
    target_sources(ChordTool PRIVATE Tools/ChordTool.cpp Source/ChordAnalyzer.cpp)
    target_compile_definitions(ChordTool PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ChordTool
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
