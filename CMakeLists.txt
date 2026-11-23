cmake_minimum_required(VERSION 3.22)

project(CoolSynth808 VERSION 0.1.0 LANGUAGES C CXX) # C as well: JUCE 9 builds zlib, libpng, libjpeg and libflac as C

# The settings every CoolSynth program shares (the C++ runtime, a Debug build compiled optimised), from the engine.
include(external/coolsynth-engine/cmake/CoolSynthFlags.cmake)

add_subdirectory(external/JUCE)
include(CTest)
# The engine's own tests run in its repository's CI; here only this app's.
set(COOLSYNTH_ENGINE_TESTS OFF CACHE BOOL "Build the engine's own test program" FORCE)
add_subdirectory(external/coolsynth-engine)

# The pattern library's built-in beats.
juce_add_binary_data(CoolSynth808Library
    HEADER_NAME LibraryData.h
    NAMESPACE LibraryData
    SOURCES
        resources/library/patterns.json
        resources/lid/bassdrum_currents.json) # the bass drum's currents, recorded from ngspice by the engine's tools/kick808/mockup_currents.py

# A JUCE audio plugin built as a standalone app for now (D267): the plugin formats are one line away.
juce_add_plugin(CoolSynth808
    COMPANY_NAME "tomcoolpxl"
    PRODUCT_NAME "CoolSynth 808"
    PLUGIN_NAME "CoolSynth 808"
    PLUGIN_MANUFACTURER_CODE Tcpx
    PLUGIN_CODE Cs88
    FORMATS Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE)

set(COOLSYNTH808_SOURCES
    src/Kit.cpp
    src/Project.cpp
    src/Library.cpp
    src/MidiExport.cpp
    src/Processor.cpp
    src/Editor.cpp
    src/Controls.cpp
    src/StandaloneMidiOut.cpp
    src/midi/ControllerMap.cpp
    src/midi/ControllerSetup.cpp
    src/ui/ControllerPanel.cpp
    src/ui/Widgets.cpp
    src/ui/LidView.cpp)

target_sources(CoolSynth808 PRIVATE ${COOLSYNTH808_SOURCES})
target_include_directories(CoolSynth808 PRIVATE src)
target_compile_features(CoolSynth808 PUBLIC cxx_std_20)
target_compile_definitions(CoolSynth808
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(CoolSynth808
    PRIVATE
        coolsynth_engine
        juce::juce_audio_utils
        juce::juce_gui_extra
        CoolSynth808Library
        coolsynth_config_flags
        juce::juce_recommended_warning_flags)

if(BUILD_TESTING)
    juce_add_console_app(CoolSynth808Tests)
    target_sources(CoolSynth808Tests
        PRIVATE
            tests/TestMain.cpp
            tests/ProjectTests.cpp
            tests/ProcessorTests.cpp
            tests/LibraryTests.cpp
            tests/EditorSnapshotTests.cpp
            tests/ControllerTests.cpp
            ${COOLSYNTH808_SOURCES})
    target_include_directories(CoolSynth808Tests PRIVATE src tests)
    target_compile_features(CoolSynth808Tests PUBLIC cxx_std_20)
    target_compile_definitions(CoolSynth808Tests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_STANDALONE_APPLICATION=1
            COOLSYNTH808_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(CoolSynth808Tests
        PRIVATE
            coolsynth_engine
            juce::juce_audio_utils
            juce::juce_gui_extra
            CoolSynth808Library
            coolsynth_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME CoolSynth808Tests COMMAND CoolSynth808Tests)
endif()
