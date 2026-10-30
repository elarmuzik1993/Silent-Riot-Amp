// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Reactive panel glow, Task 3 (docs/plans/2026-10-29-reactive-panel-glow.md):
// the editor timer feeding the faceplate.
//
//   - GlowDriveFollower, the per-frame easing: output peak -> level, and
//     compression (out/in RMS against a learnt clean ratio) -> heat.
//   - Review Focus 2: the EFFECTS cards share paintPlate and must not light.
//   - The real editor on a ProcessorHarness-prepared processor, its timer
//     frame run by hand (runTimerFrameForTest): loud playing lights the
//     faceplate, silence lets it fall, power off zeroes it on the first frame.
// ═══════════════════════════════════════════════════════════════
#include "processor_harness.h"
#include "PluginEditor.h"
#include "gui/AmpFaceplate.h"
#include "gui/EffectsPanel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <functional>
#include <limits>
#include <memory>
#include <type_traits>

using sr::GlowDriveFollower;
using sr::test::dB;

namespace {

constexpr double kFrame = 1.0 / 30.0;
constexpr uint32_t kSig = 1234;

// Runs `seconds` of frames at a steady meter reading; returns the last drive.
GlowDriveFollower::Drive hold(GlowDriveFollower& f, double seconds, float outPeak, float inRms,
                              float outRms, double dt = kFrame, uint32_t sig = kSig)
{
    GlowDriveFollower::Drive d { 0.0f, 0.0f };
    const int frames = static_cast<int>(std::lround(seconds / dt));
    for (int i = 0; i < frames; ++i)
        d = f.update(outPeak, inRms, outRms, dt, sig);
    return d;
}

} // namespace

// ── GlowDriveFollower ──

TEST_CASE("Glow follower: silence is dark and cool", "[gui][glow]")
{
    GlowDriveFollower f;
    const auto d = hold(f, 1.0, 0.0f, 0.0f, 0.0f);
    CHECK(d.level == 0.0f);
    CHECK(d.heat == 0.0f);
}

TEST_CASE("Glow follower: level maps the output peak in dB and rises at once", "[gui][glow]")
{
    GlowDriveFollower f;
    CHECK(f.update(dB(sr::kGlowLevelFloorDb - 6.0), 0.0f, 0.0f, kFrame, kSig).level == 0.0f);
    // One frame, straight to the value: no attack smoothing on a rise
    CHECK_THAT(f.update(dB(-27.0), 0.0f, 0.0f, kFrame, kSig).level,
               Catch::Matchers::WithinAbs(0.5, 1e-3));
    CHECK(f.update(dB(sr::kGlowLevelCeilDb), 0.0f, 0.0f, kFrame, kSig).level == 1.0f);
    CHECK(f.update(1.0f, 0.0f, 0.0f, kFrame, kSig).level == 1.0f);   // clamps above the ceiling
}

TEST_CASE("Glow follower: level falls as the processor's peak releases", "[gui][glow]")
{
    GlowDriveFollower f;
    const float loud = f.update(dB(-10.0), dB(-18.0), dB(-24.0), kFrame, kSig).level;
    REQUIRE(loud > 0.8f);
    // The audio side decays the held peak; the follower tracks it down
    const float lower = f.update(dB(-30.0), 0.0f, 0.0f, kFrame, kSig).level;
    CHECK(lower < loud);
    CHECK(f.update(0.0f, 0.0f, 0.0f, kFrame, kSig).level == 0.0f);
}

TEST_CASE("Glow follower: clean playing at a steady ratio stays cool", "[gui][glow]")
{
    GlowDriveFollower f;
    // Clean ratio 0.57 (default knobs, Task 0's table) at three input levels
    for (double in : { -36.0, -30.0, -24.0 })
    {
        const auto d = hold(f, 1.0, dB(in + 1.0), dB(in), dB(in) * 0.57f);
        CHECK(d.heat == 0.0f);
    }
    CHECK_THAT(f.getReference(), Catch::Matchers::WithinRel(0.57, 1e-3));
}

TEST_CASE("Glow follower: compression warms the hue, gradually", "[gui][glow]")
{
    GlowDriveFollower f;
    hold(f, 1.0, dB(-30.0), dB(-36.0), dB(-36.0) * 0.57f);   // learn the clean ratio

    // A hard phrase: ratio 12 dB under the reference, i.e. full heat as a target
    const float squashed = 0.57f * dB(-12.0);
    const float first = f.update(dB(-8.0), dB(-6.0), dB(-6.0) * squashed, kFrame, kSig).heat;
    CHECK(first > 0.0f);
    CHECK(first < 0.3f);   // not in one frame: the envelope eases it in

    // Near full heat. Not quite: the reference releases 0.5 dB/s meanwhile.
    const auto settled = hold(f, 1.5, dB(-8.0), dB(-6.0), dB(-6.0) * squashed);
    CHECK(settled.heat > 0.9f);

    // 6.5 dB of compression sits halfway up the 1..12 dB ramp; after 1 s the
    // reference has released 0.5 dB of it: (6.5 - 0.5 - 1) / 11 = 0.45
    GlowDriveFollower g;
    hold(g, 1.0, dB(-30.0), dB(-36.0), dB(-36.0) * 0.57f);
    const auto half = hold(g, 1.0, dB(-10.0), dB(-12.0), dB(-12.0) * 0.57f * dB(-6.5));
    CHECK_THAT(half.heat, Catch::Matchers::WithinAbs(0.45, 0.04));

    // Back to clean: cools over the slower release, not at once
    const float cooling = f.update(dB(-30.0), dB(-36.0), dB(-36.0) * 0.57f, kFrame, kSig).heat;
    CHECK(cooling > 0.85f);
    CHECK(hold(f, 4.0, dB(-30.0), dB(-36.0), dB(-36.0) * 0.57f).heat < 0.01f);
}

TEST_CASE("Glow follower: a settings change re-learns the clean ratio", "[gui][glow]")
{
    GlowDriveFollower f;
    hold(f, 1.0, dB(-20.0), dB(-24.0), dB(-24.0) * 3.5f);   // gain 8: clean ratio 3.5

    // VOLUME down: the ratio drops 15 dB. Same signature, that reads as squash...
    const float lower = 3.5f * dB(-15.0);
    CHECK(hold(f, 1.0, dB(-30.0), dB(-24.0), dB(-24.0) * lower).heat > 0.9f);

    // ...but with the new signature it is the new clean
    GlowDriveFollower g;
    hold(g, 1.0, dB(-20.0), dB(-24.0), dB(-24.0) * 3.5f);
    const auto d = hold(g, 1.0, dB(-30.0), dB(-24.0), dB(-24.0) * lower, kFrame, kSig + 1);
    CHECK(d.heat == 0.0f);
    CHECK_THAT(g.getReference(), Catch::Matchers::WithinRel(static_cast<double>(lower), 1e-3));
}

TEST_CASE("Glow follower: the reference releases at 0.5 dB/s, on playing frames only", "[gui][glow]")
{
    GlowDriveFollower f;
    f.update(dB(-20.0), dB(-24.0), dB(-24.0), kFrame, kSig);
    REQUIRE_THAT(f.getReference(), Catch::Matchers::WithinRel(1.0, 1e-4));

    hold(f, 4.0, 0.0f, 0.0f, 0.0f);   // silence holds it
    CHECK_THAT(f.getReference(), Catch::Matchers::WithinRel(1.0, 1e-4));

    hold(f, 4.0, dB(-30.0), dB(-24.0), dB(-24.0) * dB(-20.0));   // 4 s under it: -2 dB
    CHECK_THAT(f.getReference(), Catch::Matchers::WithinRel(static_cast<double>(dB(-2.0)), 1e-3));
}

TEST_CASE("Glow follower: the same signal at 30 and 60 frames/s lands at the same heat",
          "[gui][glow]")
{
    const auto run = [](double dt) {
        GlowDriveFollower f;
        hold(f, 1.0, dB(-30.0), dB(-36.0), dB(-36.0) * 0.57f, dt);
        // 0.3 s into a hard phrase: mid-attack, where a rate dependence would show
        return hold(f, 0.3, dB(-8.0), dB(-6.0), dB(-6.0) * 0.57f * dB(-12.0), dt).heat;
    };
    const float at30 = run(1.0 / 30.0), at60 = run(1.0 / 60.0);
    CHECK(at30 > 0.5f);
    CHECK(at30 < 0.95f);
    CHECK_THAT(at60, Catch::Matchers::WithinAbs(at30, 0.01));
}

TEST_CASE("Glow follower: a stalled frame is clamped", "[gui][glow]")
{
    GlowDriveFollower f;
    hold(f, 1.0, dB(-30.0), dB(-36.0), dB(-36.0) * 0.57f);
    const float squashed = 0.57f * dB(-12.0);
    // A 5 s stall counts as kGlowMaxFrameSeconds: the hue does not snap to red
    const float h = f.update(dB(-8.0), dB(-6.0), dB(-6.0) * squashed, 5.0, kSig).heat;
    CHECK(h > 0.0f);
    CHECK(h < 0.6f);
}

TEST_CASE("Glow follower: an input under the gate targets no heat", "[gui][glow]")
{
    GlowDriveFollower f;
    hold(f, 1.0, dB(-30.0), dB(-36.0), dB(-36.0) * 0.57f);
    const float ref = f.getReference();
    // Under -50 dBFS RMS the ratio is not read: a squashed-looking ratio is ignored
    const float quiet = dB(sr::kGlowInputGateDb - 1.0);
    CHECK(hold(f, 2.0, dB(-60.0), quiet, quiet * 0.001f).heat == 0.0f);
    CHECK(f.getReference() == ref);

    // and a warm hue cools there
    hold(f, 1.0, dB(-8.0), dB(-6.0), dB(-6.0) * 0.57f * dB(-12.0));
    CHECK(hold(f, 4.0, 0.0f, quiet, quiet * 0.001f).heat < 0.01f);
}

TEST_CASE("Glow follower: a silent output under a live input is not compression", "[gui][glow]")
{
    GlowDriveFollower f;   // tuner mute, OUTPUT trim at 0
    hold(f, 1.0, dB(-30.0), dB(-36.0), dB(-36.0) * 0.57f);
    const auto d = hold(f, 1.0, 0.0f, dB(-12.0), 0.0f);
    CHECK(d.level == 0.0f);
    CHECK(d.heat == 0.0f);
}

TEST_CASE("Glow follower: non-finite input and dt are safe", "[gui][glow]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    GlowDriveFollower f;
    hold(f, 1.0, dB(-30.0), dB(-36.0), dB(-36.0) * 0.57f);
    for (const auto& d : { f.update(nan, nan, nan, kFrame, kSig),
                           f.update(inf, inf, inf, kFrame, kSig),
                           f.update(-1.0f, -1.0f, -1.0f, kFrame, kSig),
                           f.update(dB(-8.0), dB(-6.0), dB(-7.0),
                                    std::numeric_limits<double>::quiet_NaN(), kSig),
                           f.update(dB(-8.0), dB(-6.0), dB(-7.0), -1.0, kSig) })
    {
        CHECK(std::isfinite(d.level));
        CHECK(std::isfinite(d.heat));
        CHECK(d.level >= 0.0f);
        CHECK(d.level <= 1.0f);
        CHECK(d.heat >= 0.0f);
        CHECK(d.heat <= 1.0f);
    }
    CHECK(std::isfinite(f.getReference()));
    // and it still works afterwards
    CHECK(hold(f, 1.0, dB(-30.0), dB(-36.0), dB(-36.0) * 0.57f).level > 0.0f);
}

TEST_CASE("Glow follower: reset() darkens, cools and forgets the reference", "[gui][glow]")
{
    GlowDriveFollower f;
    hold(f, 1.0, dB(-30.0), dB(-36.0), dB(-36.0) * 0.57f);
    hold(f, 1.0, dB(-8.0), dB(-6.0), dB(-6.0) * 0.57f * dB(-12.0));
    f.reset();
    CHECK(f.getReference() == 0.0f);
    // The next playing reads clean against a fresh reference
    CHECK(f.update(dB(-8.0), dB(-6.0), dB(-6.0) * 0.1f, kFrame, kSig).heat == 0.0f);
}

// ── Review Focus 2: the EFFECTS cards must not light ──

// Structural: paintPlate has no glow parameter, so a card painted with it
// cannot carry drive state.
static_assert(std::is_same_v<decltype(&sr::AmpFaceplate::paintPlate),
                             void (*)(juce::Graphics&, juce::Rectangle<float>, float)>,
              "paintPlate is shared with the EFFECTS cards; it must not take glow state");

TEST_CASE("Glow: driving the faceplate leaves the EFFECTS cards' pixels unchanged", "[gui][glow]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sr::EffectsPanel effects;
    effects.setSize(700, sr::EffectsPanel::panelHeight);
    sr::AmpFaceplate plate;
    plate.setSize(300, 120);
    plate.setLampCentre({ 250.0f, 60.0f });

    const auto snap = [](juce::Component& c) {
        return c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
    };
    const auto same = [](const juce::Image& a, const juce::Image& b) {
        if (a.getBounds() != b.getBounds()) return false;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
        return true;
    };

    plate.setDrive(0.0f, 0.0f);
    const auto plateIdle = snap(plate);
    const auto cardsIdle = snap(effects);

    plate.setDrive(1.0f, 1.0f);
    const auto plateLit = snap(plate);
    const auto cardsAfter = snap(effects);

    CHECK_FALSE(same(plateIdle, plateLit));   // the faceplate lit...
    CHECK(same(cardsIdle, cardsAfter));       // ...and only the faceplate
}

// ── The editor timer ──

namespace {

sr::AmpFaceplate* findFaceplate(juce::Component& c)
{
    if (auto* p = dynamic_cast<sr::AmpFaceplate*>(&c))
        return p;
    for (auto* child : c.getChildren())
        if (auto* p = findFaceplate(*child))
            return p;
    return nullptr;
}

struct EditorRig
{
    sr::test::ProcessorHarness h;   // first: the processor outlives the editor
    std::unique_ptr<SilentRiotEditor> editor;
    sr::AmpFaceplate* plate = nullptr;

    EditorRig()
    {
        editor.reset(dynamic_cast<SilentRiotEditor*>(h.proc.createEditor()));
        REQUIRE(editor != nullptr);
        plate = findFaceplate(*editor);
        REQUIRE(plate != nullptr);
    }

    ~EditorRig() { editor.reset(); }

    // Renders one timer frame's worth of audio, then runs that frame.
    void frame(const sr::test::Fill& in)
    {
        h.run(kFrame, in);
        editor->runTimerFrameForTest(kFrame);
    }
};

} // namespace

TEST_CASE("Glow editor: loud playing lights the faceplate, silence lets it fall", "[gui][glow]")
{
    EditorRig r;
    r.editor->runTimerFrameForTest(kFrame);
    CHECK(r.plate->getDriveLevel() == 0.0f);   // nothing played yet

    for (int i = 0; i < 6; ++i)
        r.frame(sr::test::sine(110.0f, 0.5f));
    const float lit = r.plate->getDriveLevel();
    CHECK(lit > 0.5f);

    for (int i = 0; i < 45; ++i)   // 1.5 s: the peak releases at ~35 dB/s
        r.frame(sr::test::zeros());
    CHECK(r.plate->getDriveLevel() < lit);
    CHECK(r.plate->getDriveLevel() < 0.1f);
}

TEST_CASE("Glow editor: hard playing after soft warms the hue at the default knobs",
          "[gui][glow]")
{
    EditorRig r;
    // Task 0: at the default knobs -36 dBFS RMS is clean, -6 dBFS ~12 dB squashed.
    // A sine's RMS is its peak / sqrt 2.
    const float soft = dB(-36.0) * 1.41421356f, hard = dB(-6.0) * 1.41421356f;
    for (int i = 0; i < 30; ++i)
        r.frame(sr::test::sine(110.0f, soft));
    CHECK(r.plate->getDriveHeat() < 0.05f);

    for (int i = 0; i < 30; ++i)
        r.frame(sr::test::sine(110.0f, hard));
    CHECK(r.plate->getDriveHeat() > 0.5f);
}

TEST_CASE("Glow editor: power off zeroes the drive on the first frame", "[gui][glow]")
{
    EditorRig r;
    for (int i = 0; i < 30; ++i)
        r.frame(sr::test::sine(110.0f, dB(-36.0) * 1.41421356f));
    for (int i = 0; i < 15; ++i)
        r.frame(sr::test::sine(110.0f, dB(-6.0) * 1.41421356f));
    REQUIRE(r.plate->getDriveLevel() > 0.0f);
    REQUIRE(r.plate->getDriveHeat() > 0.0f);

    r.h.set(sr::Params::IDs::POWER, 0.0f);
    r.editor->runTimerFrameForTest(kFrame);   // no block processed in between
    CHECK(r.plate->getDriveLevel() == 0.0f);
    CHECK(r.plate->getDriveHeat() == 0.0f);
}
