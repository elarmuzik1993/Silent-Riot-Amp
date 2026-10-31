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

    // Slider property that swaps the flat dial for a satin pointer knob (the amp
    // row's GAIN / SHAPE / VOLUME). Ticks and value arc are shared.
    static constexpr const char* kPointerKnobProperty = "srPointerKnob";
    // Optional int property on a pointer knob: a non-zero seed paints handling
    // wear (aged, faded pointer line; polished bar edges; rim nicks; dust).
    // Each seed wears differently and the same seed always looks the same.
    static constexpr const char* kKnobWearProperty = "srKnobWear";
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool highlighted, bool down) override;

    // ComboBox and its popup (the standalone INPUT selector) take type from
    // Fonts::Sizes like everything else, not JUCE's height-derived default.
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

} // namespace sr
