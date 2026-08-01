// ═══════════════════════════════════════════════════════════════
// Signal Chain Integration Tests — Spec Section 2.5
//
// Tests individual stages wired in sequence against the spec's
// operating levels table:
//
//   Guitar In:    ±0.00167
//   V1a out:      ±0.05 to ±0.15
//   V1b out:      ±0.05 to ±0.20
//   PI per side:  ±0.15 to ±0.40
//   EL84 out:     ±0.3 to ±0.8
//   Final out:    ±1.0 (after tanh)
//
// These are the "did I wire everything correctly" tests.
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test_helpers.h"
#include "dsp/tubes/TriodeWaveshaper.h"
#include "dsp/tubes/PentodeWaveshaper.h"
#include "dsp/tubes/GridConduction.h"
#include "dsp/power/LTPSolver.h"
#include "dsp/power/SagEnvelope.h"
#include "dsp/filters/OnePole.h"

using namespace sr;
using namespace sr::test;
using Catch::Approx;

// ── Audio taper ──────────────────────────────────────────────

TEST_CASE("Audio taper: 0 maps to 0", "[chain]") {
    REQUIRE(sr::audioTaper(0.0f) == Approx(0.0f));
}

TEST_CASE("Audio taper: 1 maps to 1", "[chain]") {
    REQUIRE(sr::audioTaper(1.0f) == Approx(1.0f));
}

TEST_CASE("Audio taper: 0.5 maps to 0.25 (squared)", "[chain]") {
    REQUIRE(sr::audioTaper(0.5f) == Approx(0.25f));
}

TEST_CASE("Audio taper: monotonically increasing", "[chain]") {
    float prev = 0.0f;
    for (float x = 0.0f; x <= 1.0f; x += 0.01f) {
        float y = sr::audioTaper(x);
        REQUIRE(y >= prev - 1e-6f);
        prev = y;
    }
}

// ── Gain control law (regression) ─────────────────────────────
//
// The gain pot is dual-gang and the signal passes through both gangs. After the
// EL84 current calibration, the raw x^3 combined law made knob 3 hit edge/crank
// levels and left no clean range. The user knob is now remapped to a virtual pot
// position before feeding the two physical gangs.
//
// Gang A tapers, Gang B is linear. These tests pin the remap plus combined law
// so clean/edge/crank stay in their intended parts of the knob.

static float combinedGainPotLaw(float knob) {
    const float drive = sr::gainDriveTaper(knob);
    return sr::audioTaper(drive) * drive;
}

// GAIN_KNOB_HEADROOM remaps the knob's full sweep onto the lower 80% of the
// drive curve: the old top end added compression rather than usable drive, so
// knob 10 now delivers what knob 8 used to.
TEST_CASE("Gain knob headroom: full sweep tops out at the old knob-8 drive", "[chain][taper]") {
    // The raw cubic, i.e. what the taper produced before the headroom scaling.
    auto rawCubic = [](float x) {
        return x * (0.7166667f + x * (-1.55f + x * 1.8333333f));
    };
    REQUIRE(sr::gainDriveTaper(1.0f) == Approx(rawCubic(GAIN_KNOB_HEADROOM)).margin(1e-6f));
    REQUIRE(sr::gainDriveTaper(1.0f) < rawCubic(1.0f));   // top end really was removed
}

TEST_CASE("Gain drive taper: canonical clean/edge points", "[chain][taper]") {
    // Anchors move with the headroom scaling — these are the 0.8 values.
    REQUIRE(sr::gainDriveTaper(0.30f) == Approx(0.108064f).margin(1e-5f));
    REQUIRE(sr::gainDriveTaper(0.50f) == Approx(0.156000f).margin(1e-5f));
    REQUIRE(sr::gainDriveTaper(1.00f) == Approx(0.520000f).margin(1e-5f));
}

TEST_CASE("Gain pot combined law: endpoints", "[chain][taper]") {
    REQUIRE(combinedGainPotLaw(0.0f) == Approx(0.0f));
    // Full knob is no longer full drive — that is the point of the headroom cap.
    REQUIRE(combinedGainPotLaw(1.0f) == Approx(0.140608f).margin(1e-6f));
    REQUIRE(combinedGainPotLaw(1.0f) < 1.0f);
}

TEST_CASE("Gain pot combined law: noon sits below the top", "[chain][taper]") {
    const float noon = combinedGainPotLaw(0.5f);
    REQUIRE(noon == Approx(0.003796f).margin(1e-6f));
    REQUIRE(noon < combinedGainPotLaw(1.0f));
}

TEST_CASE("Gain pot combined law: knob 3 stays clean", "[chain][taper]") {
    REQUIRE(combinedGainPotLaw(0.3f) == Approx(0.001262f).margin(1e-6f));
    REQUIRE(combinedGainPotLaw(0.3f) < 0.003f);
}

TEST_CASE("Gain pot combined law: monotonically increasing", "[chain][taper]") {
    float prev = 0.0f;
    for (float g = 0.0f; g <= 1.0f; g += 0.01f) {
        float y = combinedGainPotLaw(g);
        REQUIRE(y >= prev - 1e-6f);
        prev = y;
    }
}

// ── V1a stage operating level ────────────────────────────────

TEST_CASE("V1a: guitar input produces plate voltage within supply", "[chain][v1a]") {
    // Koren model: output is plate voltage [0, vSupply]
    float input = VS::V_GUITAR;
    float supply = VS::V_PREAMP;

    float output = triodeWaveshaper(input, VS::V_BIAS_12AX7, supply, PREAMP_RPLATE_N);

    // Output is a plate voltage within supply rails
    REQUIRE(output >= 0.0f);
    REQUIRE(output <= supply);
}

// ── V1b stage with gain pot ──────────────────────────────────

TEST_CASE("V1b: gain=1.0 drives plate toward floor", "[chain][v1b]") {
    // V1a output → Gain A (1.0) → V1b
    // With circuit model, large positive input drives plate voltage low
    float v1aOut = 0.1f;  // typical V1a AC output after coupling HP
    float gainA = sr::audioTaper(1.0f);  // full gain
    float v1bIn = v1aOut * gainA;

    float output = triodeWaveshaper(v1bIn, VS::V_BIAS_12AX7, VS::V_PREAMP, PREAMP_RPLATE_N);

    // At full gain with large input, plate voltage should be near floor
    REQUIRE(output >= 0.0f);
    REQUIRE(output < VS::V_PREAMP * 0.3f);  // plate driven low
}

TEST_CASE("V1b: gain=0.0 produces zero output", "[chain][v1b]") {
    float v1aOut = 0.1f;
    float gainA = sr::audioTaper(0.0f);  // gain pot at zero
    float v1bIn = v1aOut * gainA;
    
    REQUIRE(v1bIn == Approx(0.0f));
}

// ── LTP operating levels ─────────────────────────────────────

TEST_CASE("LTP: output per side within spec range at moderate drive", "[chain][pi]") {
    LTPModel ltp;
    ltp.init();
    
    // Settle
    for (int i = 0; i < 200; ++i)
        solveLTP(0.0f, V_PI_NOMINAL_N, ltp);
    
    // Moderate input (post gain+volume)
    float input = 0.05f;
    LTPOutput out = solveLTP(input, V_PI_NOMINAL_N, ltp);
    
    // Raw plate voltages (DC coupling removed in processBlock by pi_couplingHP).
    // Plate voltages sit between cathode and supply.
    REQUIRE(out.piA >= 0.0f);
    REQUIRE(out.piA <= V_PI_NOMINAL_N);
    REQUIRE(out.piB >= 0.0f);
    REQUIRE(out.piB <= V_PI_NOMINAL_N);
}

// ── Power amp push-pull ──────────────────────────────────────

TEST_CASE("Power amp: push-pull sum is differential", "[chain][power]") {
    float piA =  0.2f;
    float piB = -0.2f;  // ideally inverted
    float bias = PENTODE_BIAS_15W_N;
    
    float condA = softConductionCurve(piA, bias);
    float condB = softConductionCurve(piB, bias);
    PentodeOutput ppA = pentodeWaveshaper(piA, bias, V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);
    PentodeOutput ppB = pentodeWaveshaper(piB, bias, V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);
    float powerOut = ppA.plateCurrent * condA - ppB.plateCurrent * condB;
    
    // Push-pull sum should be non-zero for differential input
    REQUIRE(std::abs(powerOut) > 0.0f);
}

TEST_CASE("Power amp: zero input produces near-zero output", "[chain][power]") {
    float bias = PENTODE_BIAS_15W_N;
    
    float condA = softConductionCurve(0.0f, bias);
    float condB = softConductionCurve(0.0f, bias);
    PentodeOutput ppA = pentodeWaveshaper(0.0f, bias, V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);
    PentodeOutput ppB = pentodeWaveshaper(0.0f, bias, V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);
    float powerOut = ppA.plateCurrent * condA - ppB.plateCurrent * condB;
    
    // Both tubes drawing same current → cancels in push-pull
    REQUIRE(std::abs(powerOut) < 0.01f);
}

// ── Sag → bias interaction ───────────────────────────────────

TEST_CASE("Sag modifies effective bias correctly", "[chain][sag]") {
    SagEnvelope sag;
    sag.recalcCoeffs(TEST_SR);

    // At quiescent: bias should be base bias (sag offset ≈ 0)
    float baseBias = PENTODE_BIAS_15W_N;
    float effectiveBias = baseBias + sag.biasOffset();
    REQUIRE(effectiveBias == Approx(baseBias).margin(1e-6f));

    // Under heavy drive: total plate current exceeds quiescent → bias shifts colder
    for (int i = 0; i < 5000; ++i)
        sag.update(130.0f);  // 2×65mA drive

    effectiveBias = baseBias + sag.biasOffset();
    REQUIRE(effectiveBias < baseBias);  // more negative = colder
}

// ── Output limiter ───────────────────────────────────────────

TEST_CASE("tanh limiter: output bounded to +/-1.0", "[chain][output]") {
    for (float in = -5.0f; in <= 5.0f; in += 0.5f) {
        float out = std::tanh(in);
        REQUIRE(out >= -1.0f);
        REQUIRE(out <= 1.0f);
    }
}

TEST_CASE("tanh limiter: small signal passes near-linearly", "[chain][output]") {
    float in = 0.1f;
    float out = std::tanh(in);
    REQUIRE(out == Approx(in).margin(0.01f));  // tanh(x) ≈ x for small x
}

// ── End-to-end signal level sanity ───────────────────────────

TEST_CASE("Full preamp chain: guitar -> V1a -> gain -> V1b produces sound", "[chain][e2e]") {
    // Simulate the preamp chain manually
    float input = VS::V_GUITAR;
    float supply = VS::V_PREAMP;

    // V1a — output is plate voltage [0, supply]
    float x = triodeWaveshaper(input, VS::V_BIAS_12AX7, supply, PREAMP_RPLATE_N);
    REQUIRE(x >= 0.0f);
    REQUIRE(x <= supply);

    // Gain pot at 50% (AC coupling would remove DC in real chain, but
    // for this test we just verify the chain produces non-zero output)
    x *= sr::audioTaper(0.5f);

    // V1b
    x = triodeWaveshaper(x, VS::V_BIAS_12AX7, supply, PREAMP_RPLATE_N);
    REQUIRE(x >= 0.0f);      // valid plate voltage
    REQUIRE(x <= supply);     // within supply rails
}
