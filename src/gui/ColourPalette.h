#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace tt::Colours {

// Background
inline const juce::Colour bg         { 0xFF08080C };
inline const juce::Colour bg2        { 0xFF0B0B10 };
inline const juce::Colour surface    { 0xFF0F0F15 };
inline const juce::Colour panel      { 0xFF111118 };
inline const juce::Colour panelLight { 0xFF14141C };

// Borders & tracks
inline const juce::Colour border     { 0xFF1A1A24 };
inline const juce::Colour borderHi   { 0xFF252533 };
inline const juce::Colour track      { 0xFF1C1C28 };

// Text hierarchy
inline const juce::Colour muted      { 0xFF2E2E3C };
inline const juce::Colour dim        { 0xFF3C3C50 };
inline const juce::Colour label      { 0xFF4A4A5E };
inline const juce::Colour text       { 0xFF9898AE };
inline const juce::Colour bright     { 0xFFD0D0DC };
inline const juce::Colour white      { 0xFFEAEAF0 };

// Accent
inline const juce::Colour accent     { 0xFFFF6600 };
inline const juce::Colour accentDim  { 0xFFCC5200 };
inline const juce::Colour accentLo   { 0xFF993D00 };

// Status
inline const juce::Colour green      { 0xFF2DD4A0 };
inline const juce::Colour greenDim   { 0xFF1A8060 };
inline const juce::Colour yellow     { 0xFFE8C840 };
inline const juce::Colour red        { 0xFFE84040 };
inline const juce::Colour teal       { 0xFF00CCB0 };
inline const juce::Colour tealDim    { 0xFF009980 };

} // namespace tt::Colours
