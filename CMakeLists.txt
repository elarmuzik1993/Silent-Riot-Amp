cmake_minimum_required(VERSION 3.22)

project(SilentRiot
    VERSION 0.3.3
    LANGUAGES CXX
    DESCRIPTION "Monolit Beatz — Silent Riot Amp Sim"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ─────────────────────────────────────
option(SR_BUILD_TESTS   "Build unit tests"   OFF)

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
juce_add_plugin(SilentRiot
    COMPANY_NAME            "Monolit Beatz"
    BUNDLE_ID               "com.monolitbeatz.SilentRiot"
    PLUGIN_MANUFACTURER_CODE Mnlt
    PLUGIN_CODE              SiRi
    FORMATS                  VST3 Standalone    # add AU LV2 as needed
    PRODUCT_NAME             "Silent Riot"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    VST3_COPY_DIR            "${CMAKE_SOURCE_DIR}/build/install/VST3"
)

# ── Source files (mirrors spec Section 6 class hierarchy) ──
target_sources(SilentRiot
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
        src/dsp/cabinet/IRLibrary.h

        # Oversampling state machine
        src/dsp/OversampleSwitcher.h
        src/dsp/OversampleSwitcher.cpp

        # GUI
        src/gui/ColourPalette.h
        src/gui/FontData.h
        src/gui/FontData.cpp
        src/gui/LookAndFeel.h
        src/gui/LookAndFeel.cpp
        src/gui/NotchKnob.h
        src/gui/NotchKnob.cpp
        src/gui/VUMeter.h
        src/gui/VUMeter.cpp
        src/gui/ToggleSwitch.h
        src/gui/ToggleSwitch.cpp
        src/gui/HeaderBar.h
        src/gui/HeaderBar.cpp
        src/gui/FooterBar.h
        src/gui/FooterBar.cpp
        src/gui/SectionBar.h
        src/gui/SectionBar.cpp
        src/gui/AdvancedPanel.h
        src/gui/AdvancedPanel.cpp
        src/gui/PresetSelector.h
        src/gui/PresetSelector.cpp
        src/gui/SegmentedSelector.h
        src/gui/SegmentedSelector.cpp
        src/gui/SettingsPanel.h
        src/gui/SettingsPanel.cpp
        src/gui/IRWaveformDisplay.h
        src/gui/IRWaveformDisplay.cpp
        src/gui/IRLoaderPanel.h
        src/gui/IRLoaderPanel.cpp
)

# ── Binary resources ────────────────────────────
juce_add_binary_data(SilentRiotData
    SOURCES
        resources/fonts/SpaceMono-Regular.ttf
        resources/fonts/SpaceMono-Bold.ttf
        resources/fonts/DMSans-Regular.ttf
        resources/fonts/DMSans-Medium.ttf
        resources/fonts/DMSans-SemiBold.ttf
        resources/fonts/DMSans-Bold.ttf
)

# ── Compile definitions ─────────────────────────
target_compile_definitions(SilentRiot
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

# ── X11 (compositor bypass on Linux) ────────────
if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
endif()

# ── Link ────────────────────────────────────────
target_link_libraries(SilentRiot
    PRIVATE
        SilentRiotData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
        $<$<PLATFORM_ID:Linux>:X11::X11>
        $<$<PLATFORM_ID:Linux>:X11::Xext>
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(SilentRiot PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ── Tests ───────────────────────────────────────
if(SR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
