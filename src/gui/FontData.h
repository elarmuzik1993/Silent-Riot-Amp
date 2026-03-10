#pragma once
#include <juce_graphics/juce_graphics.h>

namespace tt::Fonts {

// Call once at startup (e.g. in editor constructor) to load typefaces from BinaryData
void init();

// Space Mono — monospace, used for labels, values, readouts
juce::Font spaceMono(float size, bool bold = false);

// DM Sans — used for body text, descriptions
// weight: 400=Regular, 500=Medium, 600=SemiBold, 700=Bold
juce::Font dmSans(float size, int weight = 400);

} // namespace tt::Fonts
