// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../utils/Constants.h"

namespace sr {

// Data struct for processor readout
struct DSPReadout {
    float vBPlus  = VS::toVolts(V_POWER_NOMINAL_N);   // volts (real)
    float vScreen = VS::toVolts(V_SCREEN_NOMINAL_N);
    float vPi     = VS::toVolts(V_PI_NOMINAL_N);
    float vPreamp = VS::toVolts(V_PREAMP_NOMINAL_N);
    float sagPercent = 0.0f;
    float biasV = -13.2f;
    float gridCurrentMA = 0.0f;
    int osFactor = 4;
    bool is7W = false;
};

class AdvancedPanel : public juce::Component, private juce::Timer {
public:
    AdvancedPanel();
    void paint(juce::Graphics&) override;

    void setReadout(const DSPReadout& r) { readout = r; }

    static constexpr int panelHeight = 180;

private:
    void timerCallback() override;

    DSPReadout readout;
    juce::Random rng;

    // Jittered display voltages
    float dispBPlus  = VS::toVolts(V_POWER_NOMINAL_N),  dispScreen = VS::toVolts(V_SCREEN_NOMINAL_N);
    float dispPi     = VS::toVolts(V_PI_NOMINAL_N),     dispPreamp = VS::toVolts(V_PREAMP_NOMINAL_N);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdvancedPanel)
};

} // namespace sr
