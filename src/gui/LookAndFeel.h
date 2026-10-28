// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

// Rotary sweep shared by drawRotarySlider() and every slider painted with it:
// 270° centred on vertical, JUCE convention (radians, clockwise, 0 = 12
// o'clock). setRotaryParameters() requires end > start, hence 135° + 2π rather
// than wrapping to -135°.
inline constexpr float kRotaryStartAngle = juce::degreesToRadians(225.0f);
inline constexpr float kRotaryEndAngle   = juce::degreesToRadians(135.0f)
                                         + juce::MathConstants<float>::twoPi;

struct SilentRiotLookAndFeel : public juce::LookAndFeel_V4 {
    SilentRiotLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool highlighted, bool down) override;
};

} // namespace sr
