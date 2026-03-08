#pragma once
// Orange-inspired dark GUI theme
#include <juce_gui_basics/juce_gui_basics.h>
namespace tt {
struct OrangeLookAndFeel : public juce::LookAndFeel_V4 {
    OrangeLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float pos, float start, float end, juce::Slider&) override;
};
} // namespace tt
