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
#include "dsp/effects/TempoSync.h"

// Wires the DELAY TIME knob and SYNC jewel to two parameters. SYNC off: the
// knob is DELAY_TIME (20–1000 ms). SYNC on: the same knob steps through the
// tempo divisions of DELAY_SYNC_DIV, one detent each (TempoSync.h); index 0,
// MANUAL, *is* SYNC off. A SliderAttachment binds one parameter for life, so
// this swaps which one the knob drives — the parameters themselves are
// unchanged, and saved sessions and automation keep working.
class DelayTimeAttachment {
public:
    DelayTimeAttachment(juce::AudioProcessorValueTreeState& state,
                        sr::NotchKnob& timeKnob, sr::ToggleSwitch& syncToggle);
    ~DelayTimeAttachment();

    // apvts.state property holding the division SYNC returns to when switched
    // back on, so a round trip through manual keeps the player's division.
    static constexpr const char* kLastDivisionProperty = "delayLastSyncDiv";

private:
    bool synced() const { return division > 0; }
    void refresh();          // parameters → knob + toggle
    void sliderChanged();    // knob → the active parameter
    juce::ParameterAttachment& active() { return synced() ? divisionAttachment : timeAttachment; }

    juce::AudioProcessorValueTreeState& state;
    sr::NotchKnob& knob;
    juce::Slider& slider;
    juce::Button& syncButton;
    const double manualDefault;   // the knob's own double-click value (ms mode)

    float time = 0.0f;
    int division = 0;
    int lastDivision = sr::kDefaultSyncDivision;
    bool updating = false;                          // re-entrancy guard for refresh()
    juce::ParameterAttachment* gesture = nullptr;   // the attachment a drag began on

    juce::ParameterAttachment timeAttachment;
    juce::ParameterAttachment divisionAttachment;
};

class SilentRiotEditor : public juce::AudioProcessorEditor,
                         private juce::Timer {
public:
    explicit SilentRiotEditor(SilentRiotProcessor&);
    ~SilentRiotEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // Runs one editor timer frame now, as if dtSeconds had passed since the
    // last one. For tests: the 30 Hz timer itself needs a running message loop.
    void runTimerFrameForTest(double dtSeconds);

private:
    void timerCallback() override;
    // The faceplate glow's frame: meters -> GlowDriveFollower -> setDrive.
    void updateFaceplateGlow(bool powerOn, double dtSeconds);
    // Hash of the settings the glow's clean ratio depends on (GlowDriveFollower).
    uint32_t glowParamsSignature() const;
    void layoutContent();
    void saveUIState();
    void loadUIState();
    void applySettings();   // apply theme/ballistics/knob-mode/peak-hold/tooltips
    int getContentHeight() const;
    // Window height (unscaled): the content, or more while Settings is open and
    // would not fit under the header — the window grows rather than clip it.
    int getWindowHeight() const;
    void setSettingsOpen(bool open);
    // Contents' clicks (a nested mouse listener): one outside Settings closes it.
    void mouseDown(const juce::MouseEvent&) override;
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

    // Settings overlay, centred over a dimmed backdrop (a click on it closes Settings)
    struct Scrim : juce::Component {
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black.withAlpha(0.45f)); }
    };
    Scrim settingsScrim;
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
                             delayFbA, delayMixA, delayToneA,
                             reverbMixA, reverbSizeA, reverbToneA, widthA;
    std::unique_ptr<SliderA> inputLevelA, outputLevelA;
    std::unique_ptr<DelayTimeAttachment> delayTimeA;

    static constexpr int kWindowW = 700;
    static constexpr int kHeaderH   = 38;
    static constexpr int kTunerH    = sr::TunerBar::barHeight;  // always-visible live tuner
    static constexpr int kBodyH     = 198;
    static constexpr int kSectionH  = 26;
    static constexpr int kFooterH   = 30;
    // 3 section bars: EFFECTS, IR LOADER, ADVANCED
    static constexpr int kBaseH     = kHeaderH + kTunerH + kBodyH + kSectionH * 3 + kFooterH;
    // Settings overlay: centred below the header, in content coordinates.
    static constexpr int kSettingsInset  = 4;   // gap under the header
    static constexpr int kSettingsMargin = 8;   // gap to the window's right and bottom edges
    // The plugin's panel fits the smallest window (every section closed) at any
    // UI scale, since the panel scales with the content; only the standalone's
    // extra AUDIO INPUT section makes the window grow while it is open.
    static_assert(kHeaderH + kSettingsInset + sr::SettingsPanel::panelH + kSettingsMargin <= kBaseH,
                  "Settings no longer fits the collapsed window");
    static_assert(sr::SettingsPanel::panelW + 2 * kSettingsMargin <= kWindowW,
                  "Settings is wider than the window");

    float currentScale = 1.0f;
    bool compositorBypassed = false;
    // Sample rate the tuner's detector is currently prepared for. 0 = not yet
    // known (editor built before prepareToPlay); timerCallback re-prepares when
    // this stops matching the processor's rate.
    double preparedTunerSR = 0.0;
    // Faceplate glow: the follower, the last frame's time (0 = none yet) and
    // a frame length forced by runTimerFrameForTest (< 0 = measure the clock).
    sr::GlowDriveFollower glowFollower;
    double lastGlowFrameMs = 0.0;
    double glowDtOverride = -1.0;
    void* x11Display = nullptr;   // cached Linux X11 connection (closed in dtor)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SilentRiotEditor)
};
