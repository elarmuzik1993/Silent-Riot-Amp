#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr::Colours {

// Runtime-swappable palette. Components read these at paint time, so changing
// them via setTheme() + a top-level repaint re-themes the whole UI. GUI code is
// message-thread only, so plain mutable globals are safe here.
inline juce::Colour bg         { 0xFF08080C };
inline juce::Colour bg2        { 0xFF0B0B10 };
inline juce::Colour surface    { 0xFF0F0F15 };
inline juce::Colour panel      { 0xFF111118 };
inline juce::Colour panelLight { 0xFF14141C };

inline juce::Colour border     { 0xFF1A1A24 };
inline juce::Colour borderHi   { 0xFF252533 };
inline juce::Colour track      { 0xFF262635 };

// Rotary knob ramp — body sits slightly darker than the inner face so the two
// circles read apart at small sizes. Ticks are the only scale marking (there is
// no inactive range track), so they carry more weight than a normal dim label.
inline juce::Colour knobBody     { 0xFF15171E };
inline juce::Colour knobBodyEdge { 0xFF272B36 };
inline juce::Colour knobFace     { 0xFF191C24 };
inline juce::Colour knobFaceEdge { 0xFF20242E };
inline juce::Colour knobTick     { 0xFF6C7284 };

// Text ramp, ordered muted < dim < label < text < bright < white. Every tier that
// carries text clears 4.5:1 against `panel`; `muted` (3.5:1) is decoration only —
// tick marks and meter grid, never a glyph.
inline juce::Colour muted      { 0xFF6A6A80 };
inline juce::Colour dim        { 0xFF7E7E96 };
inline juce::Colour label      { 0xFFA0A0B8 };
inline juce::Colour text       { 0xFFC4C4D4 };
inline juce::Colour bright     { 0xFFE0E0EA };
inline juce::Colour white      { 0xFFF5F5F9 };

inline juce::Colour accent     { 0xFFFF6600 };
inline juce::Colour accentDim  { 0xFFCC5200 };
inline juce::Colour accentLo   { 0xFF993D00 };

inline juce::Colour green      { 0xFF2DD4A0 };
inline juce::Colour greenDim   { 0xFF1A8060 };
inline juce::Colour yellow     { 0xFFE8C840 };
inline juce::Colour red        { 0xFFE84040 };
inline juce::Colour teal       { 0xFF00CCB0 };
inline juce::Colour tealDim    { 0xFF009980 };

enum Theme { Dark = 0, Light = 1 };

// Apply a theme by reassigning the palette globals. Accent + status colours are
// shared across themes (they read well on both); only the neutral background/
// text ramp flips. Caller must repaint the UI afterwards.
inline void setTheme(int themeIndex)
{
    if (themeIndex == Light)
    {
        bg         = juce::Colour(0xFFF4F4F7);
        bg2        = juce::Colour(0xFFFBFBFD);
        surface    = juce::Colour(0xFFFFFFFF);
        panel      = juce::Colour(0xFFFFFFFF);
        panelLight = juce::Colour(0xFFE9E9EF);

        border     = juce::Colour(0xFFD2D2DC);
        borderHi   = juce::Colour(0xFFBABAC8);
        track      = juce::Colour(0xFFD4D4DE);

        knobBody     = juce::Colour(0xFFE9E9EF);
        knobBodyEdge = juce::Colour(0xFFC8C8D4);
        knobFace     = juce::Colour(0xFFF7F7FA);
        knobFaceEdge = juce::Colour(0xFFDCDCE6);
        knobTick     = juce::Colour(0xFF8A8A9C);

        muted      = juce::Colour(0xFF9494A4);
        dim        = juce::Colour(0xFF6E6E80);
        label      = juce::Colour(0xFF4E4E60);
        text       = juce::Colour(0xFF333340);
        bright     = juce::Colour(0xFF212129);
        white      = juce::Colour(0xFF0E0E12);

        accent     = juce::Colour(0xFFFF6600);
        accentDim  = juce::Colour(0xFFCC5200);
        accentLo   = juce::Colour(0xFFB35200);

        green      = juce::Colour(0xFF1FA37C);
        greenDim   = juce::Colour(0xFF1A8060);
        yellow     = juce::Colour(0xFFC79A1E);
        red        = juce::Colour(0xFFD43535);
        teal       = juce::Colour(0xFF009E88);
        tealDim    = juce::Colour(0xFF007A68);
    }
    else // Dark (default)
    {
        bg         = juce::Colour(0xFF08080C);
        bg2        = juce::Colour(0xFF0B0B10);
        surface    = juce::Colour(0xFF0F0F15);
        panel      = juce::Colour(0xFF111118);
        panelLight = juce::Colour(0xFF14141C);

        border     = juce::Colour(0xFF1A1A24);
        borderHi   = juce::Colour(0xFF252533);
        track      = juce::Colour(0xFF262635);

        knobBody     = juce::Colour(0xFF15171E);
        knobBodyEdge = juce::Colour(0xFF272B36);
        knobFace     = juce::Colour(0xFF191C24);
        knobFaceEdge = juce::Colour(0xFF20242E);
        knobTick     = juce::Colour(0xFF6C7284);

        muted      = juce::Colour(0xFF6A6A80);
        dim        = juce::Colour(0xFF7E7E96);
        label      = juce::Colour(0xFFA0A0B8);
        text       = juce::Colour(0xFFC4C4D4);
        bright     = juce::Colour(0xFFE0E0EA);
        white      = juce::Colour(0xFFF5F5F9);

        accent     = juce::Colour(0xFFFF6600);
        accentDim  = juce::Colour(0xFFCC5200);
        accentLo   = juce::Colour(0xFF993D00);

        green      = juce::Colour(0xFF2DD4A0);
        greenDim   = juce::Colour(0xFF1A8060);
        yellow     = juce::Colour(0xFFE8C840);
        red        = juce::Colour(0xFFE84040);
        teal       = juce::Colour(0xFF00CCB0);
        tealDim    = juce::Colour(0xFF009980);
    }
}

} // namespace sr::Colours
