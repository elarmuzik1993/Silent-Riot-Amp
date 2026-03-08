cmake_minimum_required(VERSION 3.22)

project(TinyTerror
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Monolit Beatz — Silent Riot Amp Sim (Orange Tiny Terror DSP Model)"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ─────────────────────────────────────
option(TT_BUILD_TESTS   "Build unit tests"   OFF)

# ── JUCE via FetchContent ───────────────────────
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ── Plugin target ───────────────────────────────
juce_add_plugin(TinyTerror
    COMPANY_NAME            "Monolit Beatz"
    PLUGIN_MANUFACTURER_CODE Mnlt
    PLUGIN_CODE              Mtt1
    FORMATS                  VST3 Standalone    # add AU LV2 as needed
    PRODUCT_NAME             "Silent Riot"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    VST3_COPY_DIR            "${CMAKE_SOURCE_DIR}/build/install/VST3"
)

# ── Source files (mirrors spec Section 6 class hierarchy) ──
target_sources(TinyTerror
    PRIVATE
        # Core processor + editor
        src/PluginProcessor.cpp
        src/PluginProcessor.h
        src/PluginEditor.cpp
        src/PluginEditor.h

        # Utils — VoltageScale, constants, params
        src/utils/VoltageScale.h
        src/utils/Constants.h
        src/utils/Parameters.h
        src/utils/ParamSmoother.h

        # Filters — OnePole, Biquad, FilterBank, DynamicMiller
        src/dsp/filters/OnePole.h
        src/dsp/filters/Biquad.h
        src/dsp/filters/FilterBank.h
        src/dsp/filters/FilterBank.cpp
        src/dsp/filters/DynamicMillerLP.h

        # Tubes — triode, pentode, grid conduction
        src/dsp/tubes/TriodeWaveshaper.h
        src/dsp/tubes/TriodeWaveshaper.cpp
        src/dsp/tubes/PentodeWaveshaper.h
        src/dsp/tubes/PentodeWaveshaper.cpp
        src/dsp/tubes/GridConduction.h
        src/dsp/tubes/GridConduction.cpp

        # Power — PSU model, sag envelope, LTP solver
        src/dsp/power/PowerSupplyModel.h
        src/dsp/power/PowerSupplyModel.cpp
        src/dsp/power/SagEnvelope.h
        src/dsp/power/LTPSolver.h
        src/dsp/power/LTPSolver.cpp

        # Cabinet — speaker coupling, cab sim, IR loader
        src/dsp/cabinet/SpeakerCoupling.h
        src/dsp/cabinet/SpeakerCoupling.cpp
        src/dsp/cabinet/CabinetSim.h
        src/dsp/cabinet/CabinetSim.cpp

        # Oversampling state machine
        src/dsp/OversampleSwitcher.h
        src/dsp/OversampleSwitcher.cpp

        # GUI
        src/gui/LookAndFeel.h
        src/gui/LookAndFeel.cpp
        src/gui/AmpKnob.h
        src/gui/AmpKnob.cpp
)

# ── Binary resources ────────────────────────────
# Placeholder — replace with actual assets
file(WRITE "${CMAKE_SOURCE_DIR}/resources/images/placeholder.txt" "Replace with actual background/knob assets")
juce_add_binary_data(TinyTerrorData
    SOURCES
        resources/images/placeholder.txt
)

# ── Compile definitions ─────────────────────────
target_compile_definitions(TinyTerror
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

# ── Link ────────────────────────────────────────
target_link_libraries(TinyTerror
    PRIVATE
        TinyTerrorData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(TinyTerror PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ── Tests ───────────────────────────────────────
if(TT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
