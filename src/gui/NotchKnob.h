#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

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

    // Override the readout text. The argument is the raw 0–1 slider position;
    // return whatever the knob should display (e.g. "350ms", "35%"). Unset =
    // the default 0.0–10.0 scale.
    void setValueFormatter(std::function<juce::String(float)> fn)
    {
        valueFormatter = std::move(fn);
        repaint();
    }

    // Visual scale relative to the default 80px knob. 1.0 = full size (main amp
    // knobs); 0.5 = half size (compact effect knobs).
    //
    // The knob graphic itself is NOT scaled here — drawRotarySlider derives every
    // dimension from jmin(w,h), so shrinking the component shrinks the knob for
    // free. What this scales is the reserved text strip, which does not shrink
    // for free: readout and label stop being readable long before the knob does,
    // so each metric below carries a floor.
    void setScale(float s);

    // Vertical room a knob needs to render in full *at scale 1.0*: the square
    // knob plus the readout + label strip. Lay one out shorter and the name is
    // drawn past the bottom edge and silently clipped — which is exactly what
    // used to happen to the DELAY card's knobs. Scaled knobs need proportionally
    // less; the effects cards size against their own compact figure.
    static constexpr int preferredHeight = 112;

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

    float stripH() const { return readoutH + readoutGap + labelH; }

    KnobSlider slider;
    juce::String label;
    float defaultVal;
    std::function<juce::String(float)> valueFormatter;

    // Bottom strip reserved out of the component bounds; whatever square is left
    // over becomes the knob, so the knob scales with the component.
    static constexpr float kReadoutHBase    = 16.0f;
    static constexpr float kReadoutGapBase  =  4.0f;
    static constexpr float kLabelHBase      = 14.0f;
    static constexpr float kReadoutMinWBase = 44.0f;

    // Scaled strip metrics (recomputed in setScale()).
    float scale       = 1.0f;
    float readoutH    = kReadoutHBase;
    float readoutGap  = kReadoutGapBase;
    float labelH      = kLabelHBase;
    float readoutMinW = kReadoutMinWBase;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NotchKnob)
};

} // namespace sr
