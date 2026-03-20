cmake_minimum_required(VERSION 3.22)
project(Flopster VERSION 1.25)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Deployment target ─────────────────────────────────────────────────────────
# Explicitly set to avoid inheriting a beta SDK's minos (e.g. 26.0 from Xcode beta)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

# ── JUCE ──────────────────────────────────────────────────────────────────────
add_subdirectory(JUCE JUCE_build)

# ── Platform-specific plugin formats ─────────────────────────────────────────
if(APPLE)
    set(FLOPSTER_FORMATS VST3 AU Standalone)
elseif(WIN32)
    set(FLOPSTER_FORMATS VST3 Standalone)
else()
    set(FLOPSTER_FORMATS VST3 Standalone)

    # Linux: find required system packages
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WEBKIT2GTK webkit2gtk-4.1 QUIET)
    if(NOT WEBKIT2GTK_FOUND)
        pkg_check_modules(WEBKIT2GTK webkit2gtk-4.0 REQUIRED)
    endif()
    pkg_check_modules(ALSA REQUIRED alsa)
    pkg_check_modules(FREETYPE REQUIRED freetype2)
    pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
endif()

# ── Icons ─────────────────────────────────────────────────────────────────────
# juceaide accepts only PNG as input and converts to .icns / .ico itself.
# Passing a pre-built .icns produces a corrupt 8-byte stub.
set(FLOPSTER_ICON_PNG "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.png")

# ── Plugin target ─────────────────────────────────────────────────────────────
juce_add_plugin(Flopster
    VERSION                     "1.25"
    COMPANY_NAME                "Shiru & Resonaura"
    COMPANY_WEBSITE             "http://shiru.untergrund.net"
    PLUGIN_MANUFACTURER_CODE    Shru
    PLUGIN_CODE                 Flps
    FORMATS                     ${FLOPSTER_FORMATS}
    PRODUCT_NAME                "Flopster"
    DESCRIPTION                 "Floppy drive instrument by Shiru & Resonaura"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE

    # Icons — PNG source; juceaide converts to .icns (macOS) or .ico (Windows)
    ICON_BIG                    "${FLOPSTER_ICON_PNG}"
    ICON_SMALL                  "${FLOPSTER_ICON_PNG}"

    # AU-specific
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    AU_EXPORT_PREFIX            FlopsterAU

    # Bundle / app identifiers
    BUNDLE_ID                   com.shiru.flopster
    PLUGIN_AU_EXPORT_PREFIX     FlopsterAU

    # Suppress the resourceUsage block in AU Info.plist — it causes Logic Pro
    # to silently reject the plugin when the AU is not sandboxed/signed for MAS
    SUPPRESS_AU_PLIST_RESOURCE_USAGE TRUE
)

juce_generate_juce_header(Flopster)

juce_add_binary_data(FlopsterFonts
    NAMESPACE BinaryData
    HEADER_NAME BinaryData.h
    SOURCES
        fonts/PixgamerRegular.ttf
        fonts/PixelEmulator.ttf
)

# ── Sources ───────────────────────────────────────────────────────────────────
target_sources(Flopster
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
)

target_include_directories(Flopster
    PRIVATE
        src
)

# ── Compile definitions ───────────────────────────────────────────────────────
target_compile_definitions(Flopster
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0

        JUCE_USE_OPENGL=1
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(Flopster
    PRIVATE
        FlopsterFonts
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_plugin_client
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_events
        juce::juce_core
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ── Windows: suppress CRT warnings ───────────────────────────────────────────
if(WIN32)
    target_compile_definitions(Flopster
        PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
    )
endif()


# ── Linux: link system libraries ─────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    target_link_libraries(Flopster
        PRIVATE
            ${WEBKIT2GTK_LIBRARIES}
            ${ALSA_LIBRARIES}
            ${FREETYPE_LIBRARIES}
            ${FONTCONFIG_LIBRARIES}
            dl pthread
    )
    target_include_directories(Flopster
        PRIVATE
            ${WEBKIT2GTK_INCLUDE_DIRS}
            ${ALSA_INCLUDE_DIRS}
            ${FREETYPE_INCLUDE_DIRS}
            ${FONTCONFIG_INCLUDE_DIRS}
    )
endif()

# NOTE: Resource files (back.bmp, char.bmp, samples/) are copied into plugin
# bundles by install.sh (macOS) / install.bat (Windows) / install_linux.sh
# after the build, rather than as CMake post-build steps, to avoid conflicting
# with JUCE's own bundle code-signing step.
