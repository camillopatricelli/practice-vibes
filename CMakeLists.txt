cmake_minimum_required(VERSION 3.22)

project(PracticeVibes VERSION 0.1.0 LANGUAGES C CXX)

# Build number shown in About and the log: CI passes its run number (-DPV_BUILD_NUMBER=...); empty = local build.
set(PV_BUILD_NUMBER "" CACHE STRING "Build number (CI run number); empty for local builds")

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

# JUCE patch (Windows start-up time): WASAPI probes every channel count from 36 down to the device's mix format
# with IsFormatSupported(). Some drivers (seen: Cirrus Logic laptop speakers) take ~25 ms per rejected format,
# so opening a stereo output took ~3 s, twice at start-up. Probing from 8 channels down keeps 7.1 outputs and
# multichannel interfaces whose mix format already has 8+ channels. Re-applied on every configure (idempotent).
set(_pv_wasapi_file "${juce_SOURCE_DIR}/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp")
file(READ "${_pv_wasapi_file}" _pv_wasapi_src)
set(_pv_wasapi_old "static constexpr auto maxNumChannelsToQuery = static_cast<int> (AudioChannelSet::maxChannelsOfNamedLayout);")
set(_pv_wasapi_new "static constexpr auto maxNumChannelsToQuery = 8; // Practice Vibes patch: start-up time")
string(FIND "${_pv_wasapi_src}" "${_pv_wasapi_new}" _pv_wasapi_done)
if(_pv_wasapi_done EQUAL -1)
    string(FIND "${_pv_wasapi_src}" "${_pv_wasapi_old}" _pv_wasapi_found)
    if(_pv_wasapi_found EQUAL -1)
        message(FATAL_ERROR "JUCE WASAPI patch no longer applies: check queryMaxNumChannels in ${_pv_wasapi_file}")
    endif()
    string(REPLACE "${_pv_wasapi_old}" "${_pv_wasapi_new}" _pv_wasapi_src "${_pv_wasapi_src}")
    file(WRITE "${_pv_wasapi_file}" "${_pv_wasapi_src}")
endif()

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
else()
    set(PV_WEBVIEW2 FALSE)
endif()

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PV_MACOS TRUE)
else()
    set(PV_MACOS FALSE)
endif()

# Stem separation runs the Demucs network with ONNX Runtime (MIT), Windows and macOS desktop only. Only its headers
# are used at build time; the runtime library ships with the app and is loaded on first use (Windows: onnxruntime.dll
# next to the exe; macOS: a universal arm64 + x86_64 libonnxruntime.dylib in Contents/Frameworks).
# Source: Microsoft's official NuGet package (same binaries as the GitHub release archives, which stopped being
# downloadable). The hash pins the exact file.
if(WIN32 OR PV_MACOS)
    set(PV_ONNXRUNTIME_VERSION 1.20.1)
    FetchContent_Declare(onnxruntime
        URL           https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${PV_ONNXRUNTIME_VERSION}/microsoft.ml.onnxruntime.${PV_ONNXRUNTIME_VERSION}.nupkg
        URL_HASH      SHA256=9c32108e9a1c78b090a95c2a219f244dd5f067d4caf68fb0f967103f3f20839c
        DOWNLOAD_NAME onnxruntime-${PV_ONNXRUNTIME_VERSION}.zip)
    FetchContent_MakeAvailable(onnxruntime)
    set(PV_ONNXRUNTIME_DIR ${onnxruntime_SOURCE_DIR})
    set(PV_ONNXRUNTIME_INCLUDE_DIR ${PV_ONNXRUNTIME_DIR}/build/native/include)

    if(WIN32)
        set(PV_ONNXRUNTIME_LIB ${PV_ONNXRUNTIME_DIR}/runtimes/win-x64/native/onnxruntime.dll)
    else()
        # The package has one dylib per architecture: join them into a universal one.
        set(PV_ONNXRUNTIME_LIB ${CMAKE_BINARY_DIR}/onnxruntime-universal/libonnxruntime.dylib)
        if(NOT EXISTS ${PV_ONNXRUNTIME_LIB})
            file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/onnxruntime-universal)
            execute_process(
                COMMAND lipo -create
                    ${PV_ONNXRUNTIME_DIR}/runtimes/osx-arm64/native/libonnxruntime.dylib
                    ${PV_ONNXRUNTIME_DIR}/runtimes/osx-x64/native/libonnxruntime.dylib
                    -output ${PV_ONNXRUNTIME_LIB}
                RESULT_VARIABLE lipo_result)
            if(NOT lipo_result EQUAL 0)
                message(FATAL_ERROR "lipo could not build the universal ONNX Runtime dylib (${lipo_result})")
            endif()
        endif()
    endif()

    if(NOT EXISTS ${PV_ONNXRUNTIME_LIB} OR NOT EXISTS ${PV_ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h)
        message(FATAL_ERROR "ONNX Runtime files missing under ${PV_ONNXRUNTIME_DIR}")
    endif()
endif()

# Document types (Info.plist, Apple platforms). Shared topics (.pvtopic) open the app with the import preview on
# macOS and iOS (Mail / WhatsApp / Files "Open in"); macOS also offers "Open With" for song files. Written here
# instead of DOCUMENT_EXTENSIONS, which can't declare our own file type (UTExportedTypeDeclarations).
set(PV_TOPIC_UTI io.github.joefrezza.practicevibes.topic)
set(PV_TOPIC_DOCTYPE "<dict><key>CFBundleTypeName</key><string>Practice Vibes topic</string><key>CFBundleTypeRole</key><string>Editor</string><key>LSHandlerRank</key><string>Owner</string><key>LSItemContentTypes</key><array><string>${PV_TOPIC_UTI}</string></array></dict>")
set(PV_TOPIC_UTTYPE "<key>UTExportedTypeDeclarations</key><array><dict><key>UTTypeIdentifier</key><string>${PV_TOPIC_UTI}</string><key>UTTypeDescription</key><string>Practice Vibes topic</string><key>UTTypeConformsTo</key><array><string>public.data</string><string>public.content</string></array><key>UTTypeTagSpecification</key><dict><key>public.filename-extension</key><array><string>pvtopic</string></array><key>public.mime-type</key><array><string>application/x-practicevibes-topic</string></array></dict></dict></array>")
if(PV_MACOS)
    set(PV_SONG_DOCTYPE "<dict><key>CFBundleTypeName</key><string>Audio file</string><key>CFBundleTypeRole</key><string>Viewer</string><key>LSHandlerRank</key><string>Alternate</string><key>CFBundleTypeExtensions</key><array><string>wav</string><string>aif</string><string>aiff</string><string>flac</string><string>mp3</string><string>m4a</string><string>aac</string><string>ogg</string></array></dict>")
    set(PV_PLIST_TO_MERGE "<plist><dict><key>CFBundleDocumentTypes</key><array>${PV_TOPIC_DOCTYPE}${PV_SONG_DOCTYPE}</array>${PV_TOPIC_UTTYPE}</dict></plist>")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PV_PLIST_TO_MERGE "<plist><dict><key>CFBundleDocumentTypes</key><array>${PV_TOPIC_DOCTYPE}</array>${PV_TOPIC_UTTYPE}<key>LSSupportsOpeningDocumentsInPlace</key><false/></dict></plist>")
else()
    set(PV_PLIST_TO_MERGE "")
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
    PLIST_TO_MERGE                 "${PV_PLIST_TO_MERGE}")

target_sources(PracticeVibes PRIVATE
    Source/Main.cpp
    Source/AacFile.cpp
    Source/AboutPanel.cpp
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
    Source/FretboardPanel.cpp
    Source/Icons.cpp
    Source/Library.cpp
    Source/LibraryPanel.cpp
    Source/Links.cpp
    Source/LinksPanel.cpp
    Source/Localisation.cpp
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
    Source/PracticeInput.cpp
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
    Source/TopicShare.cpp
    Source/StudentGroups.cpp
    Source/SharePanel.cpp
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
    JUCE_SILENCE_XCODE_15_LINKER_WARNING=1
    PV_BUILD_NUMBER="${PV_BUILD_NUMBER}")

# iPad: exported mixes go out through the share sheet; library folders in cloud drives need folder access (bookmarks).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_sources(PracticeVibes PRIVATE Source/FolderAccess_ios.mm Source/BluetoothMidiView_ios.mm Source/PracticeInput_ios.mm
                                         Source/OpenUrl_ios.mm)
    target_compile_definitions(PracticeVibes PRIVATE JUCE_CONTENT_SHARING=1)
endif()

if(WIN32)
    target_compile_definitions(PracticeVibes PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_USE_WINRT_MIDI=1     # Bluetooth LE MIDI on Windows 10/11
        JUCE_WASAPI=1
        JUCE_ASIO=1               # ASIO SDK headers are bundled with JUCE (Steinberg dual licence, used under GPLv3)
        JUCE_DIRECTSOUND=0)
endif()

# macOS: VST3 plugins too (AudioUnits are hosted on all Apple platforms via PLUGINHOST_AU).
if(PV_MACOS)
    target_compile_definitions(PracticeVibes PRIVATE JUCE_PLUGINHOST_VST3=1)
    target_sources(PracticeVibes PRIVATE Source/SharePicker_mac.mm)
endif()

# Adds ONNX Runtime's headers to a target and ships the runtime library with it: next to the executable
# (Windows, console tools) or in the app bundle's Contents/Frameworks (macOS app; licences in Contents/Resources).
function(pv_use_onnxruntime target)
    if(PV_ONNXRUNTIME_DIR)
        target_include_directories(${target} SYSTEM PRIVATE ${PV_ONNXRUNTIME_INCLUDE_DIR})
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

# Built-in translations: Resources/Languages/<code>.txt (template.txt is the translators' starting point, not a language).
file(GLOB PV_LANGUAGES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Languages/*.txt)
list(FILTER PV_LANGUAGES EXCLUDE REGEX "/template\\.txt$")
juce_add_binary_data(PracticeVibesLanguages
    HEADER_NAME LanguageData.h
    NAMESPACE   LanguageData
    SOURCES     ${PV_LANGUAGES})

# The third-party notices shown in About > Open source licences.
juce_add_binary_data(PracticeVibesNotices
    HEADER_NAME NoticesData.h
    NAMESPACE   NoticesData
    SOURCES     THIRD_PARTY_NOTICES.md)

# The Fretboard tab's page and its font (Resources/Web; Archivo: SIL Open Font License, Resources/Web/fonts/Archivo-OFL.txt).
juce_add_binary_data(PracticeVibesWeb
    HEADER_NAME WebData.h
    NAMESPACE   WebData
    SOURCES     Resources/Web/fretboard.html Resources/Web/fonts/Archivo.ttf)

target_link_libraries(PracticeVibes
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        signalsmith-stretch
        PracticeVibesIcons
        PracticeVibesDrumKit
        PracticeVibesFonts
        PracticeVibesLanguages
        PracticeVibesNotices
        PracticeVibesWeb
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
        Source/Localisation.cpp
        Source/LoopFiles.cpp
        Source/Looper.cpp
        Source/MediaFoundationAudio.cpp
        Source/MetronomeProcessor.cpp
        Source/PlayerProcessor.cpp
        Source/PluginChain.cpp
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
        Source/TopicShare.cpp
        Source/StudentGroups.cpp
        Source/Tuner.cpp)

    pv_use_onnxruntime(PracticeVibesTests)

    target_compile_definitions(PracticeVibesTests PRIVATE
        $<$<PLATFORM_ID:Windows>:JUCE_PLUGINHOST_VST3=1>    # optional real-instrument check (PV_REAL_INSTRUMENT)
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)

    target_link_libraries(PracticeVibesTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            signalsmith-stretch
            PracticeVibesDrumKit
            PracticeVibesLanguages
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
