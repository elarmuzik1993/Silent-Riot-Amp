#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

// Rotary knob + value readout + parameter label.
//
// The knob face itself is painted by SilentRiotLookAndFeel::drawRotarySlider
// (see LookAndFeel.cpp) — this component owns the slider, reserves the bottom
// strip for the readout and label, and sizes the slider to a square so the
// LookAndFeel's jmin(w,h) has no asymmetric padding to work with.
//
// The name is historical: the knob was a segmented arc with a notch pointer
// before the five-tick arc design landed.
class NotchKnob : public juce::Component {
public:
    explicit NotchKnob(const juce::String& label, float defaultValue = 0.5f);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Slider& getSlider() { return slider; }

    void setKnobMode(int mode);  // 0=Linear (vertical drag), 1=Rotary (circular drag)

private:
    // Custom slider with keyboard step overrides and a focus ring.
    // Arrow: ±0.01 internal (±0.1 on 0-10 display)
    // PageUp/Down: ±0.1 internal (±1.0 on display)
    class KnobSlider : public juce::Slider {
    public:
        using Slider::Slider;
        void paint(juce::Graphics&) override;
        void focusGained(FocusChangeType) override;
        void focusLost(FocusChangeType) override;
        bool keyPressed(const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::upKey || key == juce::KeyPress::rightKey)
            { setValue(getValue() + 0.01, juce::sendNotificationSync); return true; }
            if (key == juce::KeyPress::downKey || key == juce::KeyPress::leftKey)
            { setValue(getValue() - 0.01, juce::sendNotificationSync); return true; }
            if (key == juce::KeyPress::pageUpKey)
            { setValue(getValue() + 0.1, juce::sendNotificationSync); return true; }
            if (key == juce::KeyPress::pageDownKey)
            { setValue(getValue() - 0.1, juce::sendNotificationSync); return true; }
            return Slider::keyPressed(key);
        }
    };

    KnobSlider slider;
    juce::String label;
    float defaultVal;

    // Bottom strip reserved out of the component bounds; whatever square is
    // left over becomes the knob, so the knob scales with the component.
    static constexpr int kReadoutH   = 16;
    static constexpr int kReadoutGap = 4;
    static constexpr int kLabelH     = 14;
    static constexpr int kStripH     = kReadoutH + kReadoutGap + kLabelH;
    static constexpr float kReadoutMinW = 44.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NotchKnob)
};

} // namespace sr
