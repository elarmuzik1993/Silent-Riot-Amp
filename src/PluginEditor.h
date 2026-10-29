// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include "PluginProcessor.h"
#include "gui/LookAndFeel.h"
#include "gui/NotchKnob.h"
#include "gui/VUMeter.h"
#include "gui/AmpFaceplate.h"
#include "gui/ToggleSwitch.h"
#include "gui/HeaderBar.h"
#include "gui/TunerBar.h"
#include "gui/FooterBar.h"
#include "gui/SectionBar.h"
#include "gui/AdvancedPanel.h"
#include "gui/SettingsPanel.h"
#include "gui/IRLoaderPanel.h"
#include "gui/EffectsPanel.h"

// Wires an index-based SegmentedSelector to an AudioParameterChoice — there's
// no built-in APVTS attachment type for this custom widget (mirrors how
// juce::ComboBoxParameterAttachment wires a juce::ComboBox).
class ChoiceSelectorAttachment {
public:
    ChoiceSelectorAttachment(juce::AudioProcessorValueTreeState& state,
                              const juce::String& paramID, sr::SegmentedSelector& selectorIn);

private:
    sr::SegmentedSelector& selector;
    juce::ParameterAttachment attachment;
};

class SilentRiotEditor : public juce::AudioProcessorEditor,
                         private juce::Timer {
public:
    explicit SilentRiotEditor(SilentRiotProcessor&);
    ~SilentRiotEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void layoutContent();
    void saveUIState();
    void loadUIState();
    void applySettings();   // apply theme/ballistics/knob-mode/peak-hold/tooltips
    int getContentHeight() const;
    void applyShapeMask();
    void setWindowBackground();
    void disableCompositorEffects();
    void* getX11Display();   // cached XOpenDisplay connection (Linux); void* keeps Xlib out of the header

    SilentRiotProcessor& proc;
    sr::SilentRiotLookAndFeel lnf;

    // Content container
    juce::Component contentArea;

    // Layout
    sr::HeaderBar header;
    sr::TunerBar  tuner;
    sr::FooterBar footer;
    sr::AmpFaceplate faceplate;   // behind the amp row; added first so it paints under it
    sr::VUMeter vuIn{"IN"}, vuOut{"OUT"};

    // Knobs
    sr::NotchKnob gainKnob{"GAIN", 0.6f};
    sr::NotchKnob toneKnob{"SHAPE", 0.5f};
    sr::NotchKnob volumeKnob{"VOLUME", 0.5f};

    // Toggles
    sr::ToggleSwitch halfPowerToggle{"7W"};
    sr::ToggleSwitch cabSimToggle{"CAB"};
    sr::ToggleSwitch powerToggle{"PWR"};  // same orange as CAB and 7W: one accent colour on the faceplate

    // Collapsible sections
    sr::SectionBar irSectionBar{"IR LOADER"};
    sr::IRLoaderPanel irLoaderPanel;
    bool irPanelOpen = false;
    float irPanelAnimH = 0.0f;

    sr::SectionBar fxSectionBar{"EFFECTS"};
    sr::EffectsPanel effectsPanel;
    bool fxPanelOpen = false;
    float fxPanelAnimH = 0.0f;

    sr::SectionBar advSectionBar{"ADVANCED"};
    sr::AdvancedPanel advancedPanel;
    bool advPanelOpen = false;
    float advPanelAnimH = 0.0f;

    // Settings overlay
    sr::SettingsPanel settingsPanel;

    // Tooltips (created when enabled in settings; null = tooltips off)
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

    // APVTS attachments
    using SliderA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderA> gainA, toneA, volA;
    std::unique_ptr<ButtonA> halfPowerA, cabSimA, powerA, tunerMuteA;

    // Effects module attachments
    std::unique_ptr<ButtonA> boostOnA, delayOnA, reverbOnA, delayPingPongA;
    std::unique_ptr<SliderA> gateThreshA, boostLevelA, boostTightA, boostBrightA,
                             delayTimeA, delayFbA, delayMixA, delayToneA,
                             reverbMixA, reverbSizeA, reverbToneA, widthA;
    std::unique_ptr<SliderA> inputLevelA, outputLevelA;
    std::unique_ptr<ChoiceSelectorAttachment> delaySyncA;

    static constexpr int kWindowW = 700;
    static constexpr int kHeaderH   = 38;
    static constexpr int kTunerH    = sr::TunerBar::barHeight;  // always-visible live tuner
    static constexpr int kBodyH     = 198;
    static constexpr int kSectionH  = 26;
    static constexpr int kFooterH   = 30;
    // 3 section bars: EFFECTS, IR LOADER, ADVANCED
    static constexpr int kBaseH     = kHeaderH + kTunerH + kBodyH + kSectionH * 3 + kFooterH;

    float currentScale = 1.0f;
    bool compositorBypassed = false;
    // Sample rate the tuner's detector is currently prepared for. 0 = not yet
    // known (editor built before prepareToPlay); timerCallback re-prepares when
    // this stops matching the processor's rate.
    double preparedTunerSR = 0.0;
    void* x11Display = nullptr;   // cached Linux X11 connection (closed in dtor)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SilentRiotEditor)
};
