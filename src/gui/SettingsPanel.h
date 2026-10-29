// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SegmentedSelector.h"
#include "ToggleSwitch.h"
#include "InputSelector.h"

#include <memory>

namespace sr {

class SettingsPanel : public juce::Component {
public:
    SettingsPanel();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    // Authoritative overlay dimensions (single source of truth — the editor
    // positions the panel using these instead of hardcoded literals).
    static constexpr int panelW = 340;
    static constexpr int panelH = 420;
    // Extra height of the AUDIO INPUT section (standalone only).
    static constexpr int kInputSectionH = 64;

    // Standalone only: adds the AUDIO INPUT section (an InputSelector on the
    // app's device manager) above UI PREFERENCES. A second call is a no-op.
    void attachInputSelector(juce::AudioDeviceManager& deviceManager);
    bool hasInputSelector() const { return inputSel != nullptr; }
    InputSelector* inputSelector() const { return inputSel.get(); }

    // Height the editor gives the overlay: panelH, plus the AUDIO INPUT section
    // when it is attached.
    int panelHeight() const { return panelH + (hasInputSelector() ? kInputSectionH : 0); }

    // Settings state
    int  themeIndex = 0;        // 0=Dark, 1=Light
    int  meterBallistics = 1;   // 0=Fast, 1=Medium, 2=Slow
    int  knobMode = 0;          // 0=Linear, 1=Rotary
    int  uiScale = 1;           // 0=75%, 1=100%, 2=125%, 3=150%
    bool peakHold = true;
    bool tooltipsEnabled = true;

    // Push current state into the controls (after restoring from saved state)
    void syncControls();

    std::function<void()> onClose;
    std::function<void(int)> onScaleChanged;
    std::function<void()> onSettingsChanged;  // theme/ballistics/knob/peak/tooltips

private:
    SegmentedSelector themeSelector     { {"DARK", "LIGHT"}, 0 };
    SegmentedSelector ballisticsSelector{ {"FAST", "MED", "SLOW"}, 1 };
    SegmentedSelector knobModeSelector  { {"LINEAR", "ROTARY"}, 0 };
    SegmentedSelector scaleSelector     { {"75%", "100%", "125%", "150%"}, 1 };
    ToggleSwitch peakHoldToggle         { "PEAK HOLD" };
    ToggleSwitch tooltipsToggle         { "TOOLTIPS" };
    std::unique_ptr<InputSelector> inputSel;

    // Top of the UI PREFERENCES section: below AUDIO INPUT when it is shown.
    float prefsTop() const { return hasInputSelector() ? static_cast<float>(kInputSectionH) : 0.0f; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};

} // namespace sr
