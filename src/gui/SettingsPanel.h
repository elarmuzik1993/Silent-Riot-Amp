#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SegmentedSelector.h"
#include "ToggleSwitch.h"

namespace sr {

class SettingsPanel : public juce::Component {
public:
    SettingsPanel();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

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

    static constexpr float panelW = 340.0f;
    static constexpr float maxH = 600.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};

} // namespace sr
