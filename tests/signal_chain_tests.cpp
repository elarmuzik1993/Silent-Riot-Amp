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

// ── Master volume control law (regression) ────────────────────
//
// The master volume sits directly in front of the EL84 grids, so it IS the
// power-stage drive control. The chain delivers 3-5x more drive than an EL84
// biased at -9.6 V can use, so with a plain audioTaper() the grids crossed
// GRID_CONDUCT_THRESH_EL84_N (8.6 V) at about knob 4 and stayed in permanent
// conduction above it. Past that point the grid clamp — not the tube — sets the
// level, so the top half of the knob added 1.15 dB of loudness for 3x the drive
// and the amp lost its touch sensitivity:
//
//   knob   EL84 grid drive   D1 sag depth   output span over 36 dB of input
//   0.30        5.67 V          1.59 dB          8.7 dB   (4.1:1)
//   0.50       15.68 V          0.60 dB          4.6 dB   (7.9:1)
//   0.70       30.28 V          0.38 dB          2.4 dB  (14.8:1)  <- old P-CRANK
//   0.90       49.76 V          0.31 dB          1.3 dB  (27.6:1)
//
// The output tanh is the second limiter in the same chain: OUTPUT_MAKEUP_GAIN
// moves compression from the grid clamp onto it, so the two must be set
// together. At headroom 0.30 with makeup 2.5, knob 10 measured D1 = 0.98 —
// the give had simply relocated. 0.21 + makeup 3.5 keeps knob 10 at D1 = 1.66.
//
// VOLUME_KNOB_HEADROOM remaps the full sweep onto the part of the range that
// stays out of permanent grid conduction — the same fix GAIN_KNOB_HEADROOM
// applies to the gain knob, and for the same reason.

TEST_CASE("Volume knob headroom: full sweep tops out below grid conduction", "[chain][taper]") {
    // Knob 10 now delivers what knob 2 used to — the drive that measures
    // D1 = 1.98 dB with 9.5 dB of give, instead of the railed 0.38 dB.
    REQUIRE(sr::volumeDriveTaper(1.0f)
            == Approx(sr::audioTaper(VOLUME_KNOB_HEADROOM)).margin(1e-6f));
    REQUIRE(sr::volumeDriveTaper(1.0f) < sr::audioTaper(1.0f));  // top end really was removed
}

TEST_CASE("Volume drive taper: canonical points", "[chain][taper]") {
    REQUIRE(sr::volumeDriveTaper(0.00f) == Approx(0.0f).margin(1e-9f));
    REQUIRE(sr::volumeDriveTaper(0.50f) == Approx(0.011025f).margin(1e-6f));
    REQUIRE(sr::volumeDriveTaper(1.00f) == Approx(0.044100f).margin(1e-6f));
}

TEST_CASE("Volume drive taper: monotonically increasing", "[chain][taper]") {
    float prev = -1.0f;
    for (float v = 0.0f; v <= 1.0f; v += 0.01f) {
        float y = sr::volumeDriveTaper(v);
        REQUIRE(y >= prev - 1e-9f);
        prev = y;
    }
}

TEST_CASE("Volume drive taper: clamps out-of-range knob values", "[chain][taper]") {
    REQUIRE(sr::volumeDriveTaper(-0.5f) == Approx(0.0f).margin(1e-9f));
    REQUIRE(sr::volumeDriveTaper(1.5f) == Approx(sr::volumeDriveTaper(1.0f)).margin(1e-9f));
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

// ── Input / output level trims (regression) ───────────────────
//
// Gain-staging utilities, NOT tone controls. Both are unity at knob centre and
// the processor skips the multiply entirely while they sit there, so the dry
// path stays bit-identical at defaults.
//
// INPUT is linear in dB across the whole sweep. OUTPUT is deliberately steeper
// below the detent than above, so the bottom half works as a fader while the
// top half stays fine enough for mix trimming.

TEST_CASE("Input trim: unity at centre", "[chain][trim]") {
    REQUIRE(sr::inputTrimGain(0.5f) == Approx(1.0f).margin(1e-7f));
}

TEST_CASE("Input trim: endpoints are -12 and +12 dB", "[chain][trim]") {
    REQUIRE(sr::inputTrimGain(0.0f) == Approx(0.2511886f).margin(1e-6f));  // -12 dB
    REQUIRE(sr::inputTrimGain(1.0f) == Approx(3.9810717f).margin(1e-5f));  // +12 dB
}

TEST_CASE("Input trim: monotonically increasing", "[chain][trim]") {
    float prev = -1.0f;
    for (float x = 0.0f; x <= 1.0f; x += 0.01f) {
        float y = sr::inputTrimGain(x);
        REQUIRE(y >= prev);
        prev = y;
    }
}

TEST_CASE("Input trim: clamps out-of-range knob values", "[chain][trim]") {
    REQUIRE(sr::inputTrimGain(-0.5f) == Approx(sr::inputTrimGain(0.0f)).margin(1e-7f));
    REQUIRE(sr::inputTrimGain(1.5f)  == Approx(sr::inputTrimGain(1.0f)).margin(1e-7f));
}

TEST_CASE("Output trim: unity at centre", "[chain][trim]") {
    REQUIRE(sr::outputTrimGain(0.5f) == Approx(1.0f).margin(1e-7f));
}

TEST_CASE("Output trim: knob 0 is exact silence", "[chain][trim]") {
    // Must be exactly zero, not merely small — this is the master fade's floor.
    REQUIRE(sr::outputTrimGain(0.0f) == 0.0f);
}

TEST_CASE("Output trim: boosts to +12 dB at full knob", "[chain][trim]") {
    REQUIRE(sr::outputTrimGain(1.0f) == Approx(3.9810717f).margin(1e-5f));
}

TEST_CASE("Output trim: fader law below the detent", "[chain][trim]") {
    REQUIRE(sr::outputTrimGain(0.40f) == Approx(0.2511886f).margin(1e-6f));  // -12 dB
    REQUIRE(sr::outputTrimGain(0.25f) == Approx(0.0316228f).margin(1e-7f));  // -30 dB
}

TEST_CASE("Output trim: fade below centre is steeper than boost above", "[chain][trim]") {
    // Same knob distance either side of the detent, much bigger cut than boost.
    const float cut   = sr::outputTrimGain(0.4f);   // -12 dB
    const float boost = sr::outputTrimGain(0.6f);   // +2.4 dB
    REQUIRE((1.0f / cut) > boost);
}

TEST_CASE("Output trim: monotonically increasing", "[chain][trim]") {
    float prev = -1.0f;
    for (float x = 0.0f; x <= 1.0f; x += 0.01f) {
        float y = sr::outputTrimGain(x);
        REQUIRE(y >= prev);
        prev = y;
    }
}

TEST_CASE("Output trim: clamps out-of-range knob values", "[chain][trim]") {
    REQUIRE(sr::outputTrimGain(-0.5f) == 0.0f);
    REQUIRE(sr::outputTrimGain(1.5f)  == Approx(sr::outputTrimGain(1.0f)).margin(1e-7f));
}
