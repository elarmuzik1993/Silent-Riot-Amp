include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# ── Test executable — all test files ──
add_executable(TinyTerrorTests
    # Test files
    voltage_scale_tests.cpp
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
    level_trace_test.cpp

    # DSP source files (tests call functions defined in these)
    ${CMAKE_SOURCE_DIR}/src/dsp/tubes/TriodeWaveshaper.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/tubes/PentodeWaveshaper.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/tubes/GridConduction.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/filters/FilterBank.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/power/PowerSupplyModel.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/power/LTPSolver.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/cabinet/SpeakerCoupling.cpp
    # CabinetSim.cpp and OversampleSwitcher.cpp excluded — they depend on
    # JUCE headers and no test file exercises them directly.
)

target_link_libraries(TinyTerrorTests PRIVATE Catch2::Catch2WithMain)
target_include_directories(TinyTerrorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

# C++17 to match main project
target_compile_features(TinyTerrorTests PRIVATE cxx_std_17)

include(CTest)
include(Catch)
catch_discover_tests(TinyTerrorTests)

# ── JUCE-dependent tests (cabinet IR, convolution) ──
add_executable(TinyTerrorJuceTests
    cabinet_ir_tests.cpp

    ${CMAKE_SOURCE_DIR}/src/dsp/cabinet/CabinetSim.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/cabinet/SpeakerCoupling.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/filters/FilterBank.cpp
)

target_link_libraries(TinyTerrorJuceTests PRIVATE
    Catch2::Catch2WithMain
    juce::juce_dsp
    juce::juce_audio_formats
    juce::juce_audio_basics
)

target_compile_definitions(TinyTerrorJuceTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_include_directories(TinyTerrorJuceTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

target_compile_features(TinyTerrorJuceTests PRIVATE cxx_std_17)

catch_discover_tests(TinyTerrorJuceTests)
