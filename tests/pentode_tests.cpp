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

using namespace tt;
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

TEST_CASE("Pentode: cutoff — zero current when grid far below bias", "[pentode]") {
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
    // Even for small signals, screen affects the transfer curve
    float vGrid = 0.05f;  // moderate drive
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

TEST_CASE("Pentode: screen current ratio near 0.25 at nominal plate", "[pentode]") {
    float vGrid = 0.1f;
    PentodeOutput out = pentodeWaveshaper(vGrid, PENTODE_BIAS_15W_N,
                                           V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N);
    
    if (out.plateCurrent > 0.001f) {
        float ratio = out.screenCurrent / out.plateCurrent;
        // Spec: nominal ratio ~0.25, with plateLimit near 1.0 at full voltage
        REQUIRE(ratio > 0.20f);
        REQUIRE(ratio < 0.35f);
    }
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
