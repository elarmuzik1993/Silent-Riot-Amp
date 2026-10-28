// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// NoiseGate Tests — front-of-amp downward gate
//
//   - Gate opens for signal above threshold (gain → 1)
//   - Gate closes for signal below threshold (gain → 0)
//   - Hysteresis: a signal between close/open thresholds holds state
//   - Coefficients are valid (0..1) after recalc
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/effects/NoiseGate.h"
#include <cmath>
#include <vector>
#include <algorithm>

using namespace sr;

namespace {
constexpr float kSR = 48000.0f;

// Run a steady sine of the given amplitude through the gate long enough for the
// gain envelope to settle, then return the per-sample gain it converged to.
float settledGain(NoiseGate& g, float amplitude, float thresholdDb, int samples = 24000)
{
    float gain = 1.0f;
    for (int i = 0; i < samples; ++i)
    {
        float x = amplitude * std::sin(2.0f * PI_F * 200.0f * i / kSR);
        float y = g.process(x, thresholdDb);
        gain = (std::abs(x) > 1e-9f) ? std::abs(y / x) : gain;
    }
    return gain;
}
} // namespace

TEST_CASE("NoiseGate recalc gives valid coefficients", "[gate]")
{
    NoiseGate g;
    g.recalcCoeffs(kSR);
    REQUIRE(g.attackCoeff  > 0.0f);  REQUIRE(g.attackCoeff  < 1.0f);
    REQUIRE(g.releaseCoeff > 0.0f);  REQUIRE(g.releaseCoeff < 1.0f);
    REQUIRE(g.detectCoeff  > 0.0f);  REQUIRE(g.detectCoeff  < 1.0f);
    // Release is slower than attack → larger (closer to 1) decay coefficient.
    REQUIRE(g.releaseCoeff > g.attackCoeff);
}

TEST_CASE("NoiseGate opens for loud signal", "[gate]")
{
    NoiseGate g;
    g.recalcCoeffs(kSR);
    // -40 dB threshold, signal well above it → gate fully open.
    float gain = settledGain(g, 0.5f, -40.0f);
    REQUIRE(gain > 0.95f);
}

TEST_CASE("NoiseGate closes for quiet signal", "[gate]")
{
    NoiseGate g;
    g.recalcCoeffs(kSR);
    // -20 dB threshold, signal far below it → gate closed.
    float gain = settledGain(g, 0.005f, -20.0f);
    REQUIRE(gain < 0.05f);
}

TEST_CASE("NoiseGate hysteresis holds open between thresholds", "[gate]")
{
    NoiseGate g;
    g.recalcCoeffs(kSR);

    // Open with a loud burst at -30 dB threshold.
    settledGain(g, 0.5f, -30.0f);
    REQUIRE(g.open == true);

    // A signal between the close threshold (-34 dB ≈ 0.0200) and the open
    // threshold (-30 dB ≈ 0.0316) must NOT close an already-open gate.
    float between = std::pow(10.0f, -32.0f / 20.0f);  // ≈ 0.0251
    settledGain(g, between, -30.0f);
    REQUIRE(g.open == true);
}

TEST_CASE("NoiseGate is a true bypass at threshold 0 (GATE_THRESH_MIN_DB)", "[gate]")
{
    NoiseGate g;
    g.recalcCoeffs(kSR);

    // A signal far BELOW -80 dB would normally be gated shut, but with the knob
    // parked at 0 the gate is OFF: output must equal input, sample for sample.
    const float quiet = std::pow(10.0f, -100.0f / 20.0f);  // -100 dB
    for (int i = 0; i < 24000; ++i)
    {
        float x = quiet * std::sin(2.0f * PI_F * 200.0f * i / kSR);
        float y = g.process(x, GATE_THRESH_MIN_DB);
        REQUIRE(y == x);
    }
    REQUIRE(g.gain == Catch::Approx(1.0f));
    REQUIRE(g.open == true);
}

TEST_CASE("NoiseGate reset restores open state", "[gate]")
{
    NoiseGate g;
    g.recalcCoeffs(kSR);
    g.reset();
    REQUIRE(g.gain == Catch::Approx(1.0f));
    REQUIRE(g.open == false);
    REQUIRE(g.envelope == Catch::Approx(0.0f));
}

// ── Regression: THRESHOLD knob sweeping to/from 0 must not step the gain ──
// The gate has no ON/OFF toggle, so the THRESHOLD knob is swept through 0 in
// normal use. Keying the processor's gate block off the RAW knob alone stops
// calling process() the instant the knob hits 0, abandoning the gate
// mid-attenuation: the applied gain jumps 0 -> 1 in ONE sample (a click), and
// the stale smoother then over-gates for a full ramp when the knob leaves 0
// again. gateShouldRun() keeps the block alive until the smoother reaches the
// floor, so the threshold sweeps out continuously instead.
namespace {

// Stand-in for juce::SmoothedValue<float> with reset(sr, 0.05): linear ramp.
struct LinearRamp {
    float cur = 0.0f, target = 0.0f;
    int   stepsLeft = 0, rampSteps = 1;
    void  reset(float sr, float sec) { rampSteps = std::max(1, static_cast<int>(sr * sec)); }
    void  setCurrentAndTarget(float v) { cur = target = v; stepsLeft = 0; }
    void  setTargetValue(float v) { if (v != target) { target = v; stepsLeft = rampSteps; } }
    float getNextValue() {
        if (stepsLeft <= 0) { cur = target; return cur; }
        cur += (target - cur) / static_cast<float>(stepsLeft); --stepsLeft; return cur;
    }
    float getCurrentValue() const { return cur; }
};

// Mirrors the processor's pre-amp gate block over a knob automation curve.
// Returns the applied gain per sample (1.0 where the block is skipped).
std::vector<float> runGateBlock(const std::vector<float>& knob, float signalDb)
{
    NoiseGate gate; gate.recalcCoeffs(kSR); gate.reset();
    LinearRamp sm;   sm.reset(kSR, 0.05f); sm.setCurrentAndTarget(0.0f);

    const float amp = std::pow(10.0f, signalDb / 20.0f);
    std::vector<float> gains(knob.size(), 1.0f);

    for (size_t i = 0; i < knob.size(); ++i)
    {
        const float x = amp * std::sin(2.0f * PI_F * 220.0f * static_cast<float>(i) / kSR);
        if (gateShouldRun(knob[i], sm.getCurrentValue()))
        {
            sm.setTargetValue(knob[i]);
            const float thrDb = GATE_THRESH_MIN_DB
                              + sm.getNextValue() * (GATE_THRESH_MAX_DB - GATE_THRESH_MIN_DB);
            gate.process(x, thrDb);
            gains[i] = gate.gain;
        }
    }
    return gains;
}

} // namespace

TEST_CASE("NoiseGate threshold sweep to zero is click-free", "[gate][regression]")
{
    const int sec = static_cast<int>(kSR);
    const float signalDb = -55.0f;   // below knob 0.5 (-50 dB), above knob 0.02 (-78.8 dB)

    std::vector<float> knob;
    knob.insert(knob.end(), sec,     0.5f);    // gate shuts on the quiet signal
    knob.insert(knob.end(), sec / 2, 0.0f);    // knob to OFF — must ramp, not cut
    knob.insert(knob.end(), sec / 2, 0.02f);   // small nudge back up off zero

    const auto gains = runGateBlock(knob, signalDb);

    SECTION("no single-sample discontinuity in the applied gain")
    {
        float maxJump = 0.0f;
        for (size_t i = 1; i < gains.size(); ++i)
            maxJump = std::max(maxJump, std::abs(gains[i] - gains[i - 1]));
        // Keyed off the raw knob alone this is exactly 1.0 (full-scale step).
        REQUIRE(maxJump < 0.1f);
    }

    SECTION("gate actually shut while the threshold was above the signal")
    {
        REQUIRE(gains[static_cast<size_t>(sec) - 1] < 0.01f);
    }

    SECTION("nudging the knob off zero does not over-gate from a stale smoother")
    {
        // 10 ms after the knob moves to 0.02 the threshold is far below the
        // signal, so the gate must already be open — not still ramping down
        // from the previous 0.5 setting.
        const size_t at = static_cast<size_t>(sec * 1.5f + kSR * 0.010f);
        REQUIRE(gains[at] > 0.9f);
    }
}
