#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

class NotchKnob : public juce::Component {
public:
    explicit NotchKnob(const juce::String& label, float defaultValue = 0.5f);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Slider& getSlider() { return slider; }

    void setKnobMode(int mode);  // 0=Linear (vertical drag), 1=Rotary (circular drag)

    // Visual scale relative to the default 80px knob. 1.0 = full size (main amp
    // knobs); 0.5 = half size (compact effect knobs). Scales the body, arc and
    // pointer; numbered 0–10 labels are hidden below 0.75 where they'd be
    // unreadable. Text readouts keep a legible floor.
    void setScale(float s);

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

    // Base geometry (relative to the default 80px bounding box). The instance
    // members below are these values multiplied by `scale`.
    static constexpr float kSizeBase  = 80.0f;
    static constexpr float knobRBase  = 22.4f;  // body radius (28%)
    static constexpr float arcRBase   = 28.8f;  // segment ring (36%)
    static constexpr float labelRBase = 36.8f;  // number positions (46%)

    // Scaled geometry (recomputed in setScale()).
    float scale  = 1.0f;
    float kSize  = kSizeBase;
    float knobR  = knobRBase;
    float arcR   = arcRBase;
    float labelR = labelRBase;

    static constexpr float startAngle = -135.0f; // degrees
    static constexpr float endAngle   =  135.0f;
    static constexpr float sweepDeg   = 270.0f;
    static constexpr int   numSegments = 20;
    static constexpr float segGapDeg   =  3.0f;
    static constexpr int   numLabels   = 11;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NotchKnob)
};

} // namespace sr
