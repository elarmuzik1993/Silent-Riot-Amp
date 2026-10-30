// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_graphics/juce_graphics.h>

namespace sr::Fonts {

// Call once at startup (e.g. in editor constructor) to load typefaces from BinaryData.
// Later calls are no-ops, so a font set by previewUIFont() survives a new editor.
void init();

// Tools only (sr_editor_snapshot font=): draw every ui() call in another
// typeface, to compare candidate UI fonts in the real editor. A null bold falls
// back to the regular face. Calls init() first.
void previewUIFont(juce::Typeface::Ptr regular, juce::Typeface::Ptr bold);

// Oxanium — the one UI typeface (squared techno, proportional), for every
// label, value and readout. Regular and Bold are embedded.
juce::Font ui(float size, bool bold = false);

// Type scale. These are JUCE font *heights* (ascent + descent), not CSS px — a
// height of 9 renders with a cap height around 6.3px. The UI spec's size table
// is written in CSS px, so these run larger than the numbers quoted there.
namespace Sizes {
    inline constexpr float micro = 9.0f;    // knob ring numbers, panel section heads
    inline constexpr float small = 10.0f;   // secondary labels, toggles, meters, footer
    inline constexpr float body  = 11.0f;   // knob names, section titles, list rows
    inline constexpr float value = 12.5f;   // knob readouts, preset name, popup menus
    inline constexpr float title = 14.0f;   // logo, settings header
    inline constexpr float hero  = 22.0f;   // tuner note name
}

} // namespace sr::Fonts
