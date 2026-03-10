#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace tt {

// Data struct for processor readout
struct DSPReadout {
    float vBPlus = 300.0f;   // volts (real)
    float vScreen = 282.0f;
    float vPi = 260.0f;
    float vPreamp = 198.0f;
    float sagPercent = 0.0f;
    float biasV = -13.2f;
    float gridCurrentMA = 0.0f;
    int osFactor = 4;
    bool is7W = false;
    float tempC = 420.0f;
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
    float dispBPlus = 315.0f, dispScreen = 282.0f, dispPi = 260.0f, dispPreamp = 198.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdvancedPanel)
};

} // namespace tt
