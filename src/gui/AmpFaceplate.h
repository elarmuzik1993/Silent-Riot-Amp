// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>

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

// ── Glow drive follower ──
// Turns the processor's meter atomics into the faceplate's (level, heat), once
// per editor timer frame. Kept out of the editor so the easing is testable
// without a window (tests/gui_faceplate_glow_tests.cpp). Every time constant is
// applied through the frame's real dt, so the result does not depend on the
// timer's rate, and a stalled frame is clamped to kGlowMaxFrameSeconds.

// Level: outputPeakForGUI in dBFS, mapped linearly onto 0..1 between these.
// Silence and the idle noise floor sit under the floor; a nominal DI at the
// default knobs peaks around -18 dBFS (~0.7) and a cranked amp (-14 LUFS)
// near the ceiling.
inline constexpr float kGlowLevelFloorDb = -48.0f;
inline constexpr float kGlowLevelCeilDb  = -6.0f;
// Level's display easing. The raw peak jumps on every pick attack and moves in
// 30 Hz steps, which reads as flicker on a soft glow; a short attack keeps the
// response quick and the release irons the steps on top of the audio side's
// own peak release (METER_PEAK_RELEASE_MS). Under kGlowLevelOffBelow with
// nothing to show, level snaps to 0 so a silent amp goes fully dark.
inline constexpr float kGlowLevelAttackSec  = 0.06f;
inline constexpr float kGlowLevelReleaseSec = 0.12f;
inline constexpr float kGlowLevelOffBelow   = 0.004f;

// Heat is compression, not sag (plan Task 0's result: sag barely travels).
// ratio = outRms / inRms, read only while the input is above this gate.
inline constexpr float kGlowInputGateDb = -50.0f;
// The clean reference: a peak-hold of ratio, released at this rate on gated
// frames only (silence holds it), and re-learnt when the signature changes.
inline constexpr float kGlowRefReleaseDbPerSec = 0.5f;
// heat = (compDb - knee) / span, clamped: the knee absorbs clean play's
// +-1 dB wobble; full heat at 12 dB is a hard phrase at the default knobs.
inline constexpr float kGlowHeatKneeDb = 1.0f;
inline constexpr float kGlowHeatSpanDb = 11.0f;
// Heat's own envelope: compression jumps (10 dB between two input steps at
// the default knobs), so the hue warms in ~150 ms and cools over ~600 ms.
inline constexpr float kGlowHeatAttackSec  = 0.15f;
inline constexpr float kGlowHeatReleaseSec = 0.6f;
// A host that stalls the timer delivers one long frame; treat it as this.
inline constexpr double kGlowMaxFrameSeconds = 0.1;

class GlowDriveFollower {
public:
    struct Drive { float level, heat; };

    // One frame. outPeak, inRms, outRms are the processor's linear meter
    // values (non-finite or negative reads as 0). paramsSignature names the
    // settings the clean ratio depends on (the editor hashes GAIN, VOLUME,
    // 7W, OUTPUT trim, CAB, IR_INDEX and every effect's on/off); any change
    // forgets the reference, so the next playing reads clean.
    Drive update(float outPeak, float inRms, float outRms, double dtSeconds,
                 uint32_t paramsSignature);

    // Power off: dark, cool, and the reference forgotten.
    void reset();

    float getReference() const noexcept { return ref; }   // 0 = not learnt yet

private:
    float ref = 0.0f;
    float level = 0.0f;
    float heat = 0.0f;
    uint32_t signature = 0;
    bool haveSignature = false;
};

// The amp head's front panel, painted behind the GAIN/SHAPE/VOLUME row:
// raised panel, grille-cloth weave, piping, corner screws and a jewel pilot
// lamp that lights with PWR. No wordmark: the header already carries the name.
// Drawn in code so it follows the theme and the UI scale; it never takes mouse
// clicks. It carries live signal state: setDrive() widens the lamp's bloom
// with playing (the piping glow moved to CabinetView). The static plate is
// cached in an Image keyed on size, theme and display scale, so a 30 Hz drive
// update redraws only the lamp and its label.
class AmpFaceplate : public juce::Component {
public:
    AmpFaceplate();
    void paint(juce::Graphics&) override;

    void setPowered(bool on);

    // Output level and compression heat, 0..1 each (sanitised). Repaints only
    // when either moves by more than a small step, or to/from exactly 0, so a
    // silent amp stays idle, and then only the lamp's bloom, so the knobs over
    // the plate are not repainted with it. Ignored
    // for painting while unpowered.
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
    void repaintGlow();   // the region setDrive() changes: the lamp bloom

    bool powered = true;
    juce::Point<float> lampCentre;
    float driveLevel = 0.0f, driveHeat = 0.0f;

    juce::Image plateCache;
    juce::uint32 plateCacheTheme = 0;
    float plateCacheScale = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpFaceplate)
};

} // namespace sr
