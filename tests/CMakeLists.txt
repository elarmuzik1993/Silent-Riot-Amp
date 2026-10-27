include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# ── Test executable — all test files ──
add_executable(SilentRiotTests
    # Test files
    voltage_scale_tests.cpp
    param_smoother_tests.cpp
    regression_tests.cpp
    triode_tests.cpp
    pentode_tests.cpp
    filter_tests.cpp
    grid_conduction_tests.cpp
    ltp_tests.cpp
    psu_sag_tests.cpp
    signal_chain_tests.cpp
    speaker_tests.cpp
    ot_saturation_tests.cpp
    dynamics_diagnostic_tests.cpp
    di_chain_trace_test.cpp
    level_trace_test.cpp
    noise_gate_tests.cpp
    boost_tests.cpp
    stereo_width_tests.cpp
    spring_reverb_tests.cpp
    safety_limiter_tests.cpp
    tempo_sync_tests.cpp
    param_sanitize_tests.cpp
    pitch_detector_tests.cpp
    integer_delay_tests.cpp
    idle_operating_point_tests.cpp
    ir_level_tests.cpp

    # DSP source files (tests call functions defined in these)
    ${CMAKE_SOURCE_DIR}/src/dsp/tubes/TriodeWaveshaper.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/tubes/PentodeWaveshaper.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/tubes/GridConduction.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/filters/FilterBank.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/power/PowerSupplyModel.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/power/LTPSolver.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/cabinet/SpeakerCoupling.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/analysis/PitchDetector.cpp
    # CabinetSim.cpp and OversampleSwitcher.cpp excluded — they depend on
    # JUCE headers and no test file exercises them directly.
)

target_link_libraries(SilentRiotTests PRIVATE Catch2::Catch2WithMain)
target_include_directories(SilentRiotTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

# C++17 to match main project
target_compile_features(SilentRiotTests PRIVATE cxx_std_17)
target_compile_definitions(SilentRiotTests PRIVATE
    SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

include(CTest)
include(Catch)
catch_discover_tests(SilentRiotTests)

# ── JUCE-dependent tests (cabinet IR, convolution) ──
add_executable(SilentRiotJuceTests
    cabinet_ir_tests.cpp
    user_ir_state_tests.cpp
    oversample_switcher_tests.cpp
    effects_delay_reverb_tests.cpp

    ${CMAKE_SOURCE_DIR}/src/dsp/cabinet/CabinetSim.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/cabinet/UserIRState.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/cabinet/SpeakerCoupling.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/filters/FilterBank.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/OversampleSwitcher.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/effects/Delay.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/effects/Reverb.cpp
)

target_link_libraries(SilentRiotJuceTests PRIVATE
    Catch2::Catch2WithMain
    juce::juce_dsp
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_data_structures
)

target_compile_definitions(SilentRiotJuceTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(SilentRiotJuceTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

target_compile_features(SilentRiotJuceTests PRIVATE cxx_std_17)

catch_discover_tests(SilentRiotJuceTests)

# ── Processor-level tests (the real SilentRiotProcessor) ──
# Links the plugin's SharedCode target (`SilentRiot`, made by juce_add_plugin),
# so these tests run exactly the code that ships — processor, editor and the
# JUCE modules compiled with the plugin's own settings — with no third
# hand-copied source list to drift. A new test file goes in this list; drive
# the processor through tests/processor_harness.h.
add_executable(SilentRiotProcessorTests
    processor_effects_tests.cpp
    processor_ir_tests.cpp
    processor_meter_tests.cpp
)

target_link_libraries(SilentRiotProcessorTests PRIVATE
    SilentRiot
    Catch2::Catch2WithMain
)

# The SharedCode target links its JUCE modules PRIVATE, so their headers and
# config defines do not propagate; compile against the plugin's own.
target_include_directories(SilentRiotProcessorTests PRIVATE
    $<TARGET_PROPERTY:SilentRiot,INCLUDE_DIRECTORIES>
    ${CMAKE_SOURCE_DIR}/tests
)
target_compile_definitions(SilentRiotProcessorTests PRIVATE
    $<TARGET_PROPERTY:SilentRiot,COMPILE_DEFINITIONS>
    SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_compile_features(SilentRiotProcessorTests PRIVATE cxx_std_17)

catch_discover_tests(SilentRiotProcessorTests)
