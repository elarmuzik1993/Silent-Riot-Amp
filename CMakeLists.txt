cmake_minimum_required(VERSION 3.22)

project(SilentRiot
    VERSION 0.3.4
    LANGUAGES CXX
    DESCRIPTION "Monolit Beatz — Silent Riot Amp Sim"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ─────────────────────────────────────
option(SR_BUILD_TESTS   "Build unit tests"   OFF)
option(SR_BUILD_TOOLS   "Build offline tone-sign-off render tool (headless)" OFF)

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
        src/utils/ParamSanitize.h

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
        src/dsp/cabinet/OTSaturation.h
        src/dsp/cabinet/CabinetSim.h
        src/dsp/cabinet/CabinetSim.cpp
        src/dsp/cabinet/IRLibrary.h
        src/dsp/cabinet/UserIRState.h
        src/dsp/cabinet/UserIRState.cpp

        # Effects module (gate / boost / delay / reverb — outside the amp loop)
        src/dsp/effects/NoiseGate.h
        src/dsp/effects/Boost.h
        src/dsp/effects/SafetyLimiter.h
        src/dsp/effects/Delay.h
        src/dsp/effects/Delay.cpp
        src/dsp/effects/SpringReverb.h
        src/dsp/effects/Reverb.h
        src/dsp/effects/Reverb.cpp
        src/dsp/effects/StereoWidth.h
        src/dsp/effects/TempoSync.h

        # Analysis — live-tuner pitch detection
        src/dsp/analysis/PitchDetector.h
        src/dsp/analysis/PitchDetector.cpp

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
        src/gui/TunerBar.h
        src/gui/TunerBar.cpp
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
        src/gui/EffectsPanel.h
        src/gui/EffectsPanel.cpp
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

# ── ASIO (optional, Windows only) ───────────────
# Without ASIO the standalone falls back to WASAPI. In WASAPI *shared* mode
# Windows dictates the endpoint's "Default Format" and the app cannot negotiate
# a sample rate at all — so the rate selector is stuck on whatever the endpoint
# is configured for. ASIO lets the interface's own driver own the clock, which
# restores the full rate list and drops latency substantially.
#
# Steinberg's ASIO SDK is licence-restricted: it cannot be redistributed or
# downloaded automatically, so it is NOT vendored here. Download it, extract it,
# and point SR_ASIO_SDK_DIR at the folder containing common/iasiodrv.h:
#
#   cmake -B build -DSR_ASIO_SDK_DIR=C:/SDKs/asiosdk_2.3.3_2019-06-14
#
# or set an ASIOSDK_DIR environment variable. The build works fine without it.
set(SR_ASIO_SDK_DIR "" CACHE PATH
    "Steinberg ASIO SDK root (the folder containing common/iasiodrv.h). Enables ASIO in the standalone.")

if(WIN32 AND MSVC)
    # Glob external/asiosdk* so the SDK works whether it is renamed to plain
    # "asiosdk" or left as the versioned folder it ships as
    # (e.g. asiosdk_2.3.3_2019-06-14).
    file(GLOB _sr_asio_globbed
        "${CMAKE_CURRENT_SOURCE_DIR}/external/asiosdk*"
        "$ENV{USERPROFILE}/SDKs/asiosdk*")

    set(_sr_asio_candidates
        "${SR_ASIO_SDK_DIR}"
        "$ENV{ASIOSDK_DIR}"
        ${_sr_asio_globbed}
    )
    set(_sr_asio_found "")
    foreach(_cand IN LISTS _sr_asio_candidates)
        if(_cand AND EXISTS "${_cand}/common/iasiodrv.h")
            set(_sr_asio_found "${_cand}")
            break()
        endif()
    endforeach()

    if(_sr_asio_found)
        target_compile_definitions(SilentRiot PUBLIC JUCE_ASIO=1)
        target_include_directories(SilentRiot PUBLIC "${_sr_asio_found}/common")
        message(STATUS "Silent Riot: ASIO ENABLED  -> ${_sr_asio_found}")
    elseif(SR_ASIO_SDK_DIR)
        # Explicitly requested but wrong path — fail loudly rather than silently
        # producing a build with no ASIO after the user asked for it.
        message(FATAL_ERROR
            "SR_ASIO_SDK_DIR is set to '${SR_ASIO_SDK_DIR}' but "
            "'${SR_ASIO_SDK_DIR}/common/iasiodrv.h' does not exist. "
            "Point it at the SDK root (the folder containing 'common').")
    else()
        message(STATUS
            "Silent Riot: ASIO disabled (WASAPI only — sample rate will be locked "
            "to the Windows endpoint format). Set SR_ASIO_SDK_DIR to enable.")
    endif()
endif()

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

# ── Tools (offline render for the tone-sign-off harness) ──
if(SR_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
