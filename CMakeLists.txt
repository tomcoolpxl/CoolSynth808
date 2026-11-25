cmake_minimum_required(VERSION 3.22)

project(CoolSynth808 VERSION 0.1.0 LANGUAGES C CXX) # C as well: JUCE 9 builds zlib, libpng, libjpeg and libflac as C

# The settings every CoolSynth program shares (the C++ runtime, a Debug build compiled optimised), from the engine.
include(external/coolsynth-engine/cmake/CoolSynthFlags.cmake)

# A Release build optimised across all its code at link time (whole-program optimisation: /GL with /LTCG on MSVC), the
# engine's and JUCE's too: the same sound, a little faster (D327). Not AVX2: CoolSynth's own test found it no faster and
# bit for bit the same, and it would shut out every PC from before 2013. The everyday Debug build is not touched.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

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
        resources/lid/bassdrum_currents.json # the bass drum's currents, recorded from ngspice by the engine's tools/kick808/mockup_currents.py
        resources/lid/snare_currents.json # the snare's, by its tools/tr808/lid_currents.py (D278)
        resources/lid/tom_currents.json # the low tom's, the same way
        resources/lid/midtom_currents.json # the mid and high tom's, and the three congas' (D339)
        resources/lid/hightom_currents.json
        resources/lid/lowconga_currents.json
        resources/lid/midconga_currents.json
        resources/lid/highconga_currents.json
        resources/lid/rimshot_currents.json # the rim shot's
        resources/lid/clap_currents.json # the hand clap's, its maracas' side left out
        resources/lid/cowbell_currents.json # the cowbell's parts of the metal board
        resources/lid/metal_currents.json # its six oscillators and two band-passes
        resources/lid/cymbal_currents.json # the cymbal's parts
        resources/lid/hats_currents.json # the two hi-hats'
        resources/lid/board_mt.json # the whole board's: the mid tom,
        resources/lid/board_ht.json # the high tom,
        resources/lid/board_oh.json # the open hat alone
        resources/lid/board_ch.json # and the closed hat alone (D280)
        resources/lid/board_envelopes.json # each drum's output after a strike, for the panel's etched circuit (D298)
        # The panel's typefaces (D292): Archivo for every label, IBM Plex Mono for numbers; both SIL OFL, their
        # licences beside them in resources/fonts.
        resources/fonts/ArchivoCondensed-SemiBold.ttf
        resources/fonts/ArchivoSemiCondensed-SemiBold.ttf
        resources/fonts/ArchivoSemiCondensed-Bold.ttf
        resources/fonts/Archivo-Medium.ttf
        resources/fonts/Archivo-ExtraBold.ttf
        resources/fonts/IBMPlexMono-Medium.ttf
        resources/fonts/IBMPlexMono-SemiBold.ttf)

# A JUCE audio plugin: the standalone app (D267) and a VST3 for a DAW (D311, D325), as CoolSynth 303's.
juce_add_plugin(CoolSynth808
    COMPANY_NAME "tomcoolpxl"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/coolsynth808-icon-512.png" # the user's pick (D363)
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/coolsynth808-icon-32.png" # the knob alone, for 32 px and under
    PRODUCT_NAME "CoolSynth 808"
    PLUGIN_NAME "CoolSynth 808"
    PLUGIN_MANUFACTURER_CODE Tcpx
    PLUGIN_CODE Cs88
    FORMATS Standalone VST3
    VST3_CATEGORIES Instrument Drum
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE)

set(COOLSYNTH808_SOURCES
    src/Kit.cpp
    src/Project.cpp
    src/Library.cpp
    src/MidiExport.cpp
    src/AudioExport.cpp
    src/Processor.cpp
    src/Editor.cpp
    src/Controls.cpp
    src/StandaloneMidiIn.cpp
    src/StandaloneMidiOut.cpp
    src/StandaloneDevices.cpp
    src/midi/ControllerMap.cpp
    src/midi/ControllerSetup.cpp
    src/ui/ControllerPanel.cpp
    src/ui/LibraryShelf.cpp
    src/ui/Widgets.cpp
    src/ui/Hardware.cpp
    src/ui/EtchedBoard.cpp
    src/ui/EtchedCircuit.cpp
    src/ui/Transport.cpp
    src/ui/Sequencer.cpp
    src/ui/LidView.cpp
    src/ui/WindowLook.cpp
    src/ui/OptionsWindow.cpp
    src/ui/ExportPanel.cpp
    src/mcp/McpServer.cpp
    src/mcp/Tools808.cpp)

target_sources(CoolSynth808 PRIVATE ${COOLSYNTH808_SOURCES} src/StandaloneApp.cpp)
target_include_directories(CoolSynth808 PRIVATE src)
target_compile_features(CoolSynth808 PUBLIC cxx_std_20)
target_compile_definitions(CoolSynth808
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    PRIVATE
        # Our own app around the standalone (D311): JUCE's window without its Options button, in the theme.
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
# ASIO for audio interfaces (D311), when Steinberg's ASIO SDK is on this computer: ASIO_SDK_DIR, or E:/SDKs beside
# the repositories. Without it the app offers the Windows drivers only.
set(COOLSYNTH808_ASIO_SDK "$ENV{ASIO_SDK_DIR}" CACHE PATH "Steinberg's ASIO SDK (the folder holding common/iasiodrv.h)")
if(NOT COOLSYNTH808_ASIO_SDK)
    get_filename_component(COOLSYNTH808_ASIO_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../SDKs/asio/sdk/ASIOSDK" ABSOLUTE)
endif()
if(WIN32 AND EXISTS "${COOLSYNTH808_ASIO_SDK}/common/iasiodrv.h")
    message(STATUS "CoolSynth 808: ASIO from ${COOLSYNTH808_ASIO_SDK}")
    target_compile_definitions(CoolSynth808 PUBLIC JUCE_ASIO=1)
    target_include_directories(CoolSynth808 PUBLIC "${COOLSYNTH808_ASIO_SDK}/common")
endif()

target_link_libraries(CoolSynth808
    PRIVATE
        coolsynth_engine
        juce::juce_audio_utils
        juce::juce_gui_extra
        CoolSynth808Library
        coolsynth_config_flags
        juce::juce_recommended_warning_flags)

# The built-in beats as WAV files, for listening beside their records.
juce_add_console_app(CoolSynth808Render)
target_sources(CoolSynth808Render PRIVATE tools/RenderLibrary.cpp ${COOLSYNTH808_SOURCES})
target_include_directories(CoolSynth808Render PRIVATE src)
target_compile_features(CoolSynth808Render PUBLIC cxx_std_20)
target_compile_definitions(CoolSynth808Render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(CoolSynth808Render
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
    tests/ClockFollowerTests.cpp
            tests/ProjectTests.cpp
            tests/ProcessorTests.cpp
            tests/LibraryTests.cpp
            tests/EditorSnapshotTests.cpp
            tests/ControllerTests.cpp
            tests/HostingTests.cpp
            tests/McpTests.cpp
            tests/McpSwitchTests.cpp
            tests/ExportTests.cpp
            tests/CpuTests.cpp
            tests/BeatSyncTests.cpp
            ${COOLSYNTH808_SOURCES})
    target_include_directories(CoolSynth808Tests PRIVATE src tests)
    target_compile_features(CoolSynth808Tests PUBLIC cxx_std_20)
    target_compile_definitions(CoolSynth808Tests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_STANDALONE_APPLICATION=1
            COOLSYNTH808_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            COOLSYNTH808_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
            JUCE_PLUGINHOST_VST3=1)
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
