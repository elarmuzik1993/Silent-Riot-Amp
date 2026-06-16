#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

class ToggleSwitch : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer {
public:
    ToggleSwitch(const juce::String& label, juce::Colour activeColor = juce::Colour(0xFFFF6600));
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(juce::Component::FocusChangeType) override;
    void focusLost(juce::Component::FocusChangeType) override;
    juce::ToggleButton& getButton() { return button; }

    void toggle();  // flip state with notification

private:
    void timerCallback() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    juce::ToggleButton button;
    juce::String label;
    juce::Colour activeColor;
    float thumbPos = 0.0f; // 0=off, 1=on (animated)

    static constexpr float trackW = 34.0f;
    static constexpr float trackH = 16.0f;
    static constexpr float thumbD = 12.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToggleSwitch)
};

} // namespace sr
