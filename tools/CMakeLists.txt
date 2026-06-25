# ── Offline tone-sign-off render tool (tools/README.md) ──────────────────────
# Built only when -DSR_BUILD_TOOLS=ON. This subdirectory is not even parsed
# otherwise, so it cannot affect the plugin build or CI.
#
# sr_render links the FULL plugin (processor + editor + GUI) so it can call the
# real SilentRiotProcessor::processBlock headlessly. The editor/GUI are linked
# (createEditor references them) but never instantiated.
#
# NOTE: the source list below mirrors the plugin's in the root CMakeLists.txt.
# If you add/remove a plugin .cpp there, update it here too. Kept separate
# (rather than hoisted into a shared variable) so a mistake in this optional
# tool can never break the shipped plugin target.

juce_add_console_app(sr_render PRODUCT_NAME "sr_render")

target_sources(sr_render PRIVATE
    sr_render.cpp

    ${CMAKE_SOURCE_DIR}/src/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginEditor.cpp

    ${CMAKE_SOURCE_DIR}/src/dsp/filters/FilterBank.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/tubes/TriodeWaveshaper.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/tubes/PentodeWaveshaper.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/tubes/GridConduction.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/power/PowerSupplyModel.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/power/LTPSolver.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/cabinet/SpeakerCoupling.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/cabinet/CabinetSim.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/OversampleSwitcher.cpp

    ${CMAKE_SOURCE_DIR}/src/gui/FontData.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/LookAndFeel.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/NotchKnob.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/VUMeter.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/ToggleSwitch.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/HeaderBar.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/FooterBar.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/SectionBar.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/AdvancedPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/PresetSelector.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/SegmentedSelector.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/SettingsPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/IRWaveformDisplay.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/IRLoaderPanel.cpp
)

target_compile_definitions(sr_render PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)

target_include_directories(sr_render PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(sr_render PRIVATE
    SilentRiotData                       # embedded fonts (BinaryData) — pulled by the GUI
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    $<$<PLATFORM_ID:Linux>:X11::X11>
    $<$<PLATFORM_ID:Linux>:X11::Xext>
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_features(sr_render PRIVATE cxx_std_17)
