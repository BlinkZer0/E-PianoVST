cmake_minimum_required(VERSION 3.22)

project(EPianoVST VERSION 1.0.0 LANGUAGES C CXX)

# Link the MSVC runtime statically so E-Piano.exe and E-Piano.vst3 run on PCs
# without the Visual C++ Redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if EPIANO_JUCE_DIR is given, otherwise fetch it.
# ---------------------------------------------------------------------------
set(EPIANO_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout")
set(EPIANO_JUCE_TAG "8.0.17-security" CACHE STRING "JUCE git tag to fetch")

if(EPIANO_JUCE_DIR AND EXISTS "${EPIANO_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${EPIANO_JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${EPIANO_JUCE_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(EPianoVST
    COMPANY_NAME            "EPiano"
    PRODUCT_NAME            "E-Piano"
    ICON_BIG                "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    PLUGIN_MANUFACTURER_CODE Epno
    PLUGIN_CODE             Ep3o
    FORMATS                 VST3 Standalone
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES         Instrument Synth
    NEEDS_WEBVIEW2          TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# The UI (HTML/CSS/JS) is compiled into the plugin binary.
file(GLOB_RECURSE EPIANO_UI_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.js")
# ...and so are the 80 factory presets.
juce_add_binary_data(EPianoUI SOURCES ${EPIANO_UI_FILES} presets/factory_presets.json)

target_sources(EPianoVST PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetManager.cpp
    Source/SampleData.cpp)

target_include_directories(EPianoVST PRIVATE Source)

target_compile_definitions(EPianoVST PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(EPianoVST
    PRIVATE
        EPianoUI
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Console tests for the preset system:  --target EPianoTests
# ---------------------------------------------------------------------------
juce_add_console_app(EPianoTests PRODUCT_NAME "EPianoTests")
target_sources(EPianoTests PRIVATE tests/PresetTests.cpp tests/EngineTools.cpp Source/PresetManager.cpp Source/SampleData.cpp)
target_include_directories(EPianoTests PRIVATE Source)
target_compile_definitions(EPianoTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(EPianoTests PRIVATE EPianoUI juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
