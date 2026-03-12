#pragma once
#include "PluginProcessor.h"
#include "gui/LookAndFeel.h"
#include "gui/NotchKnob.h"
#include "gui/VUMeter.h"
#include "gui/ToggleSwitch.h"
#include "gui/HeaderBar.h"
#include "gui/FooterBar.h"
#include "gui/SectionBar.h"
#include "gui/AdvancedPanel.h"
#include "gui/SettingsPanel.h"
#include "gui/IRLoaderPanel.h"

class TinyTerrorEditor : public juce::AudioProcessorEditor,
                         private juce::Timer {
public:
    explicit TinyTerrorEditor(TinyTerrorProcessor&);
    ~TinyTerrorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void layoutContent();
    void saveUIState();
    void loadUIState();

    TinyTerrorProcessor& proc;
    tt::SilentRiotLookAndFeel lnf;

    // Content container — all GUI children go here, clipped to the window
    juce::Component contentArea;

    // Layout
    tt::HeaderBar header;
    tt::FooterBar footer;
    tt::VUMeter vuIn{"IN"}, vuOut{"OUT"};

    // Knobs
    tt::NotchKnob gainKnob{"GAIN", 0.6f};
    tt::NotchKnob toneKnob{"TONE", 0.5f};
    tt::NotchKnob volumeKnob{"VOLUME", 0.5f};

    // Toggles
    tt::ToggleSwitch halfPowerToggle{"7W"};
    tt::ToggleSwitch cabSimToggle{"CAB"};
    tt::ToggleSwitch powerToggle{"PWR", juce::Colour(0xFF2DD4A0)};

    // Collapsible sections
    tt::SectionBar irSectionBar{"IR LOADER"};
    tt::IRLoaderPanel irLoaderPanel;
    bool irPanelOpen = false;
    float irPanelAnimH = 0.0f;  // animated height (0 to panelHeight)

    tt::SectionBar advSectionBar{"ADVANCED"};
    tt::AdvancedPanel advancedPanel;
    bool advPanelOpen = false;
    float advPanelAnimH = 0.0f; // animated height (0 to panelHeight)

    // Settings overlay
    tt::SettingsPanel settingsPanel;

    // APVTS attachments
    using SliderA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderA> gainA, toneA, volA;
    std::unique_ptr<ButtonA> halfPowerA, cabSimA, powerA;

    // Fixed window height = max possible content
    static constexpr int kWindowW = 700;
    static constexpr int kWindowH = 900;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TinyTerrorEditor)
};
