#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

struct SilentRiotLookAndFeel : public juce::LookAndFeel_V4 {
    SilentRiotLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool highlighted, bool down) override;
};

// Keep alias for backward compat during transition
using OrangeLookAndFeel = SilentRiotLookAndFeel;

} // namespace sr
