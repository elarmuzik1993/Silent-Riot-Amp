// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

// ── Reactive glow ──
// The faceplate's piping bead lights with playing (docs/plans/
// 2026-10-29-reactive-panel-glow.md). These are GUI paint constants, not DSP.

// Ceiling on the bead's alpha over a dark panel (the halo branch). The three
// halo strokes stack on the piping line, so the brightest pixel there carries
// 1 - (1 - a)(1 - 0.4a)(1 - 0.15a) = 0.48 at this ceiling. That keeps
// Colours::text above 4.5:1 against the lit bead at full drive and full orange
// (tests/glow_colour_tests.cpp does the arithmetic), so a hardware-realism
// glow never outshines the value readouts. Raising it fails that test.
inline constexpr float kGlowAlphaMaxHalo = 0.36f;

// Ceiling over a light panel (the tinted-bead branch). There is no halo to
// stack, and a bloom on white reads as a smudge, so the bead itself colours up
// and runs more opaque than the halo; capped where Colours::text still clears
// 4.5:1 against the reddest bead composited on white.
inline constexpr float kGlowAlphaMaxBead = 0.6f;

// The dark-theme halo: strokes along the piping, widest and faintest last.
struct GlowStroke { float width, alphaScale; };
inline constexpr GlowStroke kGlowHaloStrokes[] = { { 1.0f, 1.0f }, { 2.5f, 0.4f }, { 5.0f, 0.15f } };

// The light-theme bead: one stroke, a little wider than the 1px piping it covers.
inline constexpr float kGlowBeadWidth = 1.5f;

// True when the palette is dark enough for an additive halo to read as light.
// Decided from the palette's own luminance, not a stored theme index, so a
// future theme inherits the right treatment.
bool glowUsesHalo();

// The bead's colour for an output `level` and a compression `heat`, both 0..1
// (clamped; non-finite reads as 0). Pure: reads only the Colours palette.
// Hue runs Colours::accent (clean) to Colours::red (hard-driven) with heat,
// interpolated in HSB; alpha rises linearly with level from 0 (fully
// transparent at every heat) to kGlowAlphaMaxHalo or kGlowAlphaMaxBead,
// depending on glowUsesHalo().
juce::Colour glowBeadColour(float level, float heat);

// The amp head's front panel, painted behind the GAIN/SHAPE/VOLUME row:
// raised panel, grille-cloth weave, piping, corner screws and a jewel pilot
// lamp that lights with PWR. No wordmark: the header already carries the name.
// Drawn in code so it follows the theme and the UI scale; it never takes mouse
// clicks. It carries live signal state: setDrive() lights the piping bead
// (glowBeadColour()) and widens the lamp's bloom with playing. The static
// plate is cached in an Image keyed on size, theme and display scale, so a
// 30 Hz drive update redraws only the glow, the lamp and its label.
class AmpFaceplate : public juce::Component {
public:
    AmpFaceplate();
    void paint(juce::Graphics&) override;

    void setPowered(bool on);

    // Output level and compression heat, 0..1 each (sanitised). Repaints only
    // when either moves by more than a small step, or to/from exactly 0, so a
    // silent amp stays idle. Ignored for painting while unpowered.
    void setDrive(float level, float heat);
    float getDriveLevel() const noexcept { return driveLevel; }
    float getDriveHeat() const noexcept { return driveHeat; }

    // A domed deep-ruby glass pilot lamp of lens radius r centred on c. brightness
    // runs 0 (off: dark tinted glass) to 1 (lit, with bloom onto the panel).
    // bloom is the glow's radius in lens radii; the bezel alone reaches r + 4,
    // so a small lamp in a tight component passes less to keep its glow inside
    // the bounds JUCE clips paint() to.
    static void paintLamp(juce::Graphics&, juce::Point<float> c, float r,
                          juce::Colour colour, float brightness, float bloom = 3.2f);

    // The panel itself: raised gradient, grille-cloth weave and piping, without
    // the screws or lamp. The EFFECTS cards are painted with it too, so the two
    // sections read as the same hardware.
    static void paintPlate(juce::Graphics&, juce::Rectangle<float> bounds, float radius);

    // Where the editor's layout puts the lamp, in this component's coordinates.
    void setLampCentre(juce::Point<float> centre);

private:
    void renderPlateCache(float scale);

    bool powered = true;
    juce::Point<float> lampCentre;
    float driveLevel = 0.0f, driveHeat = 0.0f;

    juce::Image plateCache;
    juce::uint32 plateCacheTheme = 0;
    float plateCacheScale = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpFaceplate)
};

} // namespace sr
