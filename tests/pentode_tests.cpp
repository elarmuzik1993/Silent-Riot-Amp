// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Pentode Waveshaper Tests — Spec Section 4.2
//
// Tests for:
//   - Screen voltage affects gain (not just headroom)
//   - Plate saturation knee (1 - exp behavior)
//   - Cutoff below grid threshold
//   - Screen current redistribution at low plate voltage
//   - softConductionCurve behavior
//   - 15W vs 7W bias difference
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test_helpers.h"
#include "dsp/tubes/PentodeWaveshaper.h"

using namespace sr;
using Catch::Approx;

// ── Basic output properties ──────────────────────────────────

TEST_CASE("Pentode: plate current non-negative", "[pentode]") {
    for (float vg = -0.5f; vg <= 0.5f; vg += 0.05f) {
        PentodeOutput out = pentodeWaveshaper(vg, PENTODE_BIAS_15W_N,
                                               V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);
        REQUIRE(out.plateCurrent >= 0.0f);
        REQUIRE(out.screenCurrent >= 0.0f);
    }
}

TEST_CASE("Pentode: cutoff - zero current when grid far below bias", "[pentode]") {
    // Grid at -2.0 normalized with bias at -0.9 → vg1 = -2.0 + (-0.9) = -2.9
    // Way below cutoff, should produce zero current
    PentodeOutput out = pentodeWaveshaper(-2.0f, PENTODE_BIAS_15W_N,
                                           V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);
    REQUIRE(out.plateCurrent == Approx(0.0f).margin(1e-10f));
}

// ── Screen voltage sensitivity ───────────────────────────────

TEST_CASE("Pentode: lower screen voltage reduces plate current", "[pentode]") {
    float vGrid = -0.02f;  // moderate level, below EL84_IP_MAX_N clamp
    float vPlate = V_POWER_NOMINAL_N;
    
    PentodeOutput outHigh = pentodeWaveshaper(vGrid, PENTODE_BIAS_15W_N,
                                               vPlate, V_SCREEN_NOMINAL_N);
    PentodeOutput outLow  = pentodeWaveshaper(vGrid, PENTODE_BIAS_15W_N,
                                               vPlate, V_SCREEN_NOMINAL_N * 0.7f);
    
    // Spec: "Screen voltage controls GAIN: lower Vg2 → less current → less gain"
    REQUIRE(outLow.plateCurrent < outHigh.plateCurrent);
}

TEST_CASE("Pentode: screen voltage scales gain, not just clips", "[pentode]") {
    // Must be measured at a drive level the tube actually operates at. vGrid=0.05
    // puts vg1 at +5.4 V — a POSITIVE grid, far outside normal operation — where
    // both the nominal and half-screen cases sit at 99% of EL84_IP_MAX_N and the
    // ratio collapses toward 1 simply because both are clamped. That measures the
    // current ceiling, not the screen's effect on gain.
    // vGrid=0.01 puts vg1 at -6.6 V: real operating territory, 69% of the ceiling.
    float vGrid = 0.01f;
    float vPlate = V_POWER_NOMINAL_N;

    PentodeOutput outNom  = pentodeWaveshaper(vGrid, PENTODE_BIAS_15W_N, vPlate, V_SCREEN_NOMINAL_N);
    PentodeOutput outHalf = pentodeWaveshaper(vGrid, PENTODE_BIAS_15W_N, vPlate, V_SCREEN_NOMINAL_N * 0.5f);

    // Should be significantly different, not just clipped at a lower ceiling
    if (outNom.plateCurrent > 0.001f) {
        float ratio = outHalf.plateCurrent / outNom.plateCurrent;
        REQUIRE(ratio < 0.8f);  // at least 20% gain reduction
    }
}

// ── Plate saturation knee ────────────────────────────────────

TEST_CASE("Pentode: plate saturation limits current at low Vplate", "[pentode]") {
    float vGrid = 0.1f;
    
    PentodeOutput outHealthy = pentodeWaveshaper(vGrid, PENTODE_BIAS_15W_N,
                                                  V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);
    PentodeOutput outStarved = pentodeWaveshaper(vGrid, PENTODE_BIAS_15W_N,
                                                  VS::v(20.0f), V_SCREEN_NOMINAL_N);
    
    // Near the knee voltage, current should be substantially reduced
    REQUIRE(outStarved.plateCurrent < outHealthy.plateCurrent);
}

// ── Screen current redistribution ────────────────────────────

TEST_CASE("Pentode: screen current ratio increases at low plate voltage", "[pentode]") {
    float vGrid = 0.1f;
    
    PentodeOutput outHealthy = pentodeWaveshaper(vGrid, PENTODE_BIAS_15W_N,
                                                  V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);
    PentodeOutput outStarved = pentodeWaveshaper(vGrid, PENTODE_BIAS_15W_N,
                                                  VS::v(20.0f), V_SCREEN_NOMINAL_N);
    
    // Spec: screen ratio goes from 0.25 (healthy) to 0.40 (starved)
    if (outHealthy.plateCurrent > 0.001f && outStarved.plateCurrent > 0.001f) {
        float ratioHealthy = outHealthy.screenCurrent / outHealthy.plateCurrent;
        float ratioStarved = outStarved.screenCurrent / outStarved.plateCurrent;
        REQUIRE(ratioStarved > ratioHealthy);
    }
}

TEST_CASE("Pentode: screen current ratio matches the datasheet at nominal plate", "[pentode]") {
    float vGrid = 0.1f;
    PentodeOutput out = pentodeWaveshaper(vGrid, PENTODE_BIAS_15W_N,
                                           V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);

    if (out.plateCurrent > 0.001f) {
        float ratio = out.screenCurrent / out.plateCurrent;
        // EL84 datasheet: Ig2/Ia = 5.5 mA / 48 mA = 0.115 at the reference point.
        // Was asserted at ~0.25, which drew over twice the real screen current and
        // made the screen supply node droop far harder than it should.
        REQUIRE(ratio > 0.09f);
        REQUIRE(ratio < 0.15f);
    }
}

// ── Datasheet calibration (regression) ───────────────────────
//
// The model used to draw 3.9x too little current: 13.5 mA at the datasheet
// reference point instead of 48 mA, so the EL84 pair idled at 24 mA instead of
// ~90 mA. With a 100R shared cathode resistor that tiny idle meant an ordinary
// playing swing demanded a huge bias shift relative to it, and the sag offset
// slammed into its cold clamp from GAIN 3 upward — the power-tube bias froze at
// -12.6 V and stopped responding to picking dynamics.
//
// These pin the operating point so the calibration cannot drift again. If
// EL84_KG1 or EL84_IP_MAX_N is retuned, re-solve against the datasheet — the
// tanh knee compresses the result, so the two constants are coupled.

TEST_CASE("Pentode: matches the EL84 datasheet operating point", "[pentode][regression]") {
    // EL84 datasheet: Va = 250 V, Vg2 = 250 V, Vg1 = -7.3 V -> Ia = 48 mA, Ig2 = 5.5 mA
    PentodeOutput out = pentodeWaveshaper(0.0f, VS::v(-7.3f), VS::v(250.0f), VS::v(250.0f));

    const float iaMa  = VS::toAmps(out.plateCurrent)  * 1000.0f;
    const float ig2Ma = VS::toAmps(out.screenCurrent) * 1000.0f;

    REQUIRE(iaMa  == Approx(48.0f).margin(2.0f));
    REQUIRE(ig2Ma == Approx(5.5f).margin(1.0f));
}

TEST_CASE("Pentode: pair idles at a realistic cathode current", "[pentode][regression]") {
    // A real EL84 pair cathode-biased around -9.6 V idles ~90-105 mA total.
    // This is the reference the sag envelope subtracts, so getting it wrong
    // desynchronises sag from the tube model.
    const float pair15 = pentodeQuiescentTotal(PENTODE_BIAS_15W_N,
                                               V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);
    const float mA15 = VS::toAmps(pair15) * 1000.0f;
    REQUIRE(mA15 > 70.0f);
    REQUIRE(mA15 < 120.0f);

    // 7W runs a hotter (less negative) bias, so it must idle higher.
    const float pair7 = pentodeQuiescentTotal(PENTODE_BIAS_7W_N,
                                              V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);
    REQUIRE(pair7 > pair15);
}

TEST_CASE("Pentode: idle leaves headroom below the current ceiling", "[pentode][regression]") {
    // EL84_IP_MAX_N is a soft ceiling for PEAKS. If idle sits near it, the tanh
    // knee compresses normal operation and the screen-voltage/gain relationship
    // collapses because everything is clamped.
    const float perTubeIdle = pentodeQuiescentTotal(PENTODE_BIAS_15W_N,
                                                    V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N) * 0.5f;
    REQUIRE(perTubeIdle < 0.6f * EL84_IP_MAX_N);
}

// ── 15W vs 7W bias ───────────────────────────────────────────

TEST_CASE("Pentode: 15W bias is more negative than 7W (colder)", "[pentode]") {
    // 15W Class AB: Vgk = -9.6V (more negative, colder)
    // 7W Class A:   Vgk = -7.0V (less negative, hotter)
    REQUIRE(PENTODE_BIAS_15W_N < PENTODE_BIAS_7W_N);
}

TEST_CASE("Pentode: 7W mode produces more quiescent current (hotter bias)", "[pentode]") {
    float vGrid = -0.02f;  // moderate level, below clamp

    PentodeOutput out15W = pentodeWaveshaper(vGrid, PENTODE_BIAS_15W_N,
                                              V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);
    PentodeOutput out7W  = pentodeWaveshaper(vGrid, PENTODE_BIAS_7W_N,
                                              V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);

    // Less negative bias (hotter) → more current
    REQUIRE(out7W.plateCurrent >= out15W.plateCurrent);
}

// ── softConductionCurve ──────────────────────────────────────

TEST_CASE("Conduction curve: full conduction well above cutoff", "[pentode][conduction]") {
    float cond = softConductionCurve(0.5f, PENTODE_BIAS_15W_N);
    REQUIRE(cond > 0.95f);
}

TEST_CASE("Conduction curve: near zero at deep cutoff", "[pentode][conduction]") {
    float cond = softConductionCurve(-0.5f, PENTODE_BIAS_15W_N);
    REQUIRE(cond < 0.05f);
}

TEST_CASE("Conduction curve: range is 0 to 1", "[pentode][conduction]") {
    for (float vg = -1.0f; vg <= 1.0f; vg += 0.05f) {
        float cond = softConductionCurve(vg, PENTODE_BIAS_15W_N);
        REQUIRE(cond >= 0.0f);
        REQUIRE(cond <= 1.0f);
    }
}

TEST_CASE("Conduction curve: monotonically increases", "[pentode][conduction]") {
    float prev = 0.0f;
    for (float vg = -1.0f; vg <= 1.0f; vg += 0.01f) {
        float cond = softConductionCurve(vg, PENTODE_BIAS_15W_N);
        REQUIRE(cond >= prev - 1e-6f);
        prev = cond;
    }
}

TEST_CASE("Conduction curve: 7W has wider overlap than 15W", "[pentode][conduction]") {
    // Just below cutoff: 7W's wider overlap retains more residual conduction
    // (more Class A — tube doesn't cut off as sharply on negative grid swings)
    float vGridBelowCutoff = EL84_CUTOFF_N - 0.005f;
    float cond15W = softConductionCurve(vGridBelowCutoff, PENTODE_BIAS_15W_N);
    float cond7W  = softConductionCurve(vGridBelowCutoff, PENTODE_BIAS_7W_N);

    // 7W (wider overlap, more Class A) retains more conduction below cutoff
    REQUIRE(cond7W > cond15W);
}

// ── Koren softplus smoothness ───────────────────────────────

TEST_CASE("Pentode: Koren softplus provides smooth cutoff near bias point", "[pentode]") {
    // Sweep grid voltage through the cutoff region and verify the numerical
    // derivative (finite difference) changes gradually — no discontinuous jump.
    float vPlate = V_POWER_NOMINAL_N;
    float vScreen = V_SCREEN_NOMINAL_N;

    // Sweep around the bias point where cutoff occurs
    float prevIp = 0.0f;
    float prevSlope = 0.0f;
    bool first = true;
    bool secondSlope = true;
    constexpr float step = 0.001f;  // fine step for derivative check

    for (float vg = -0.06f; vg <= 0.02f; vg += step) {
        PentodeOutput out = pentodeWaveshaper(vg, PENTODE_BIAS_15W_N, vPlate, vScreen);
        float ip = out.plateCurrent;

        if (first) {
            prevIp = ip;
            first = false;
            continue;
        }

        float slope = (ip - prevIp) / step;

        if (secondSlope) {
            prevSlope = slope;
            secondSlope = false;
        } else {
            // Second derivative (slope change) should be bounded — no hard kink
            float slopeChange = std::abs(slope - prevSlope);
            // Allow generous margin but catch hard discontinuities
            REQUIRE(slopeChange < 500.0f);
            prevSlope = slope;
        }

        prevIp = ip;
    }
}
