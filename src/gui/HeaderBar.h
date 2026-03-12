#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PresetSelector.h"

namespace tt {

class HeaderBar : public juce::Component, private juce::Timer {
public:
    HeaderBar();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    PresetSelector& getPresetSelector() { return presetSelector; }

    std::function<void()> onGearClick;

private:
    void timerCallback() override;
    void paintGearIcon(juce::Graphics& g, juce::Rectangle<float> area) const;

    PresetSelector presetSelector;
    bool gearActive = false;
    float gearRotation = 0.0f; // 0=inactive, 60=active (animated degrees)

    static constexpr float barHeight = 38.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeaderBar)
};

} // namespace tt
