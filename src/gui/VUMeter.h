#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace tt {

class VUMeter : public juce::Component, private juce::Timer {
public:
    explicit VUMeter(const juce::String& label);
    void paint(juce::Graphics&) override;
    void setLevel(float newLevel); // 0.0–1.0

private:
    void timerCallback() override;

    juce::String label;
    float targetLevel = 0.0f;
    float displayLevel = 0.0f;
    juce::Random rng;

    static constexpr int numSegments = 24;
    static constexpr float lerpFactor = 0.12f;
    static constexpr float jitterAmount = 0.02f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VUMeter)
};

} // namespace tt
