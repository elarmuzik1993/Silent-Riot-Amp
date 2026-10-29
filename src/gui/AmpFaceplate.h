// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

// The amp head's front panel, painted behind the GAIN/SHAPE/VOLUME row:
// raised panel, grille-cloth weave, piping, corner screws and a jewel pilot
// lamp that lights with PWR. No wordmark: the header already carries the name.
// Pure decoration, drawn in code so it follows the theme and the UI scale; it
// never takes mouse clicks.
class AmpFaceplate : public juce::Component {
public:
    AmpFaceplate();
    void paint(juce::Graphics&) override;

    void setPowered(bool on);

    // A domed deep-ruby glass pilot lamp of lens radius r centred on c. brightness
    // runs 0 (off: dark tinted glass) to 1 (lit, with bloom onto the panel).
    static void paintLamp(juce::Graphics&, juce::Point<float> c, float r,
                          juce::Colour colour, float brightness);

    // Width the right end reserves for the lamp.
    static constexpr int sideW = 104;

private:
    bool powered = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpFaceplate)
};

} // namespace sr
