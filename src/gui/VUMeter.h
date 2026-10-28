// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

class VUMeter : public juce::Component, private juce::Timer {
public:
    explicit VUMeter(const juce::String& label);
    void paint(juce::Graphics&) override;
    void setLevel(float newLevel); // 0.0–1.0

    void setBallistics(int mode);  // 0=Fast, 1=Medium, 2=Slow
    void setPeakHold(bool shouldHold);

private:
    void timerCallback() override;

    juce::String label;
    float targetLevel = 0.0f;
    float displayLevel = 0.0f;
    float peakLevel = 0.0f;
    bool  peakHoldEnabled = true;
    juce::Random rng;

    static constexpr int numSegments = 24;
    float lerpFactor = 0.12f;          // ballistics (Medium default)
    static constexpr float jitterAmount = 0.02f;
    static constexpr float peakDecay = 0.94f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VUMeter)
};

} // namespace sr
