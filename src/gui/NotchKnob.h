#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace sr {

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

    // Vertical room a knob needs to render in full: the knob graphic, the value
    // readout box, and the name below it. Lay one out shorter than this and the
    // name is drawn past the bottom edge and silently clipped — which is exactly
    // what used to happen to the DELAY card's knobs.
    static constexpr int preferredHeight = 112;

private:
    void drawSegmentedArc(juce::Graphics&, float cx, float cy, float pos) const;
    void drawNumberedLabels(juce::Graphics&, float cx, float cy) const;
    void drawKnobBody(juce::Graphics&, float cx, float cy, float angle) const;
    void drawNotchPointer(juce::Graphics&, float cx, float cy, float angle) const;

    // Custom slider with keyboard step overrides
    // Arrow: ±0.01 internal (±0.1 on 0-10 display)
    // PageUp/Down: ±0.1 internal (±1.0 on display)
    class KnobSlider : public juce::Slider {
    public:
        using Slider::Slider;
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
    std::function<juce::String(float)> valueFormatter;

    // Geometry constants (relative to 80px bounding box)
    static constexpr float kSize     = 80.0f;
    static constexpr float knobR     = 22.4f;   // body radius (28%)
    static constexpr float arcR      = 28.8f;   // segment ring (36%)
    static constexpr float labelR    = 36.8f;   // number positions (46%)
    static constexpr float startAngle = -135.0f; // degrees
    static constexpr float endAngle   =  135.0f;
    static constexpr float sweepDeg   = 270.0f;
    static constexpr int   numSegments = 20;
    static constexpr float segGapDeg   =  3.0f;
    static constexpr int   numLabels   = 11;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NotchKnob)
};

} // namespace sr
