#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace tt {

class NotchKnob : public juce::Component {
public:
    explicit NotchKnob(const juce::String& label, float defaultValue = 0.5f);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Slider& getSlider() { return slider; }

private:
    void drawSegmentedArc(juce::Graphics&, float cx, float cy, float pos) const;
    void drawNumberedLabels(juce::Graphics&, float cx, float cy) const;
    void drawKnobBody(juce::Graphics&, float cx, float cy, float angle) const;
    void drawNotchPointer(juce::Graphics&, float cx, float cy, float angle) const;

    juce::Slider slider;
    juce::String label;
    float defaultVal;

    // Geometry constants (relative to 80px bounding box)
    static constexpr float kSize     = 80.0f;
    static constexpr float knobR     = 22.4f;   // body radius (28%)
    static constexpr float arcR      = 28.8f;   // segment ring (36%)
    static constexpr float labelR    = 36.8f;   // number positions (46%)
    static constexpr float startAngle = -135.0f; // degrees
    static constexpr float endAngle   =  135.0f;
    static constexpr float sweepDeg   = 270.0f;
    static constexpr int   numSegments = 20;
    static constexpr float segSpanDeg  = 13.5f;
    static constexpr float segGapDeg   =  3.0f;
    static constexpr int   numLabels   = 11;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NotchKnob)
};

} // namespace tt
