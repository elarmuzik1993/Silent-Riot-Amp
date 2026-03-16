// ═══════════════════════════════════════════════════════════════
// Grid Conduction Tests — Spec Section 4.1.1
//
// Tests for:
//   - Below threshold: output ≈ input (no conduction)
//   - Above threshold: positive peaks attenuated (impedance loading)
//   - Coupling cap charge accumulates under sustained drive
//   - Coupling cap decays toward zero in absence of drive
//   - Per-stage factories produce correct impedances
//   - Bias shift is negative (shifts operating point colder)
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test_helpers.h"
#include "dsp/tubes/GridConduction.h"

using namespace tt;
using namespace tt::test;
using Catch::Approx;

// ── Below threshold: transparent ─────────────────────────────

TEST_CASE("Grid conduction: transparent below threshold", "[grid]") {
    auto model = makeV1aGrid();
    recalcGridChargeDecay(model, 0.022f, TEST_SR);
    model.couplingCapCharge = 0.0f;
    
    // Feed signal well below threshold
    float input = GRID_CONDUCT_THRESH_N * 0.5f;  // half the threshold
    float output = applyGridConduction(input, model);
    
    // With no accumulated charge, output should equal input
    REQUIRE(output == Approx(input).margin(1e-8f));
}

TEST_CASE("Grid conduction: negative signals pass through unchanged", "[grid]") {
    auto model = makeV1aGrid();
    recalcGridChargeDecay(model, 0.022f, TEST_SR);
    model.couplingCapCharge = 0.0f;
    
    float input = -0.01f;  // negative, well below threshold
    float output = applyGridConduction(input, model);
    REQUIRE(output == Approx(input).margin(1e-8f));
}

// ── Above threshold: impedance loading ───────────────────────

TEST_CASE("Grid conduction: attenuates positive peaks above threshold", "[grid]") {
    auto model = makeV1aGrid();
    recalcGridChargeDecay(model, 0.022f, TEST_SR);
    model.couplingCapCharge = 0.0f;
    
    float input = GRID_CONDUCT_THRESH_N + 0.01f;  // above threshold
    float output = applyGridConduction(input, model);
    
    // Output should be less than input (grid "eats" excess voltage)
    REQUIRE(output < input);
}

TEST_CASE("Grid conduction: loading increases with overdrive", "[grid]") {
    auto model1 = makeV1aGrid();
    auto model2 = makeV1aGrid();
    recalcGridChargeDecay(model1, 0.022f, TEST_SR);
    recalcGridChargeDecay(model2, 0.022f, TEST_SR);
    model1.couplingCapCharge = 0.0f;
    model2.couplingCapCharge = 0.0f;
    
    float inputSmall = GRID_CONDUCT_THRESH_N + 0.005f;
    float inputLarge = GRID_CONDUCT_THRESH_N + 0.05f;
    
    float outSmall = applyGridConduction(inputSmall, model1);
    float outLarge = applyGridConduction(inputLarge, model2);
    
    float attenuationSmall = 1.0f - (outSmall / inputSmall);
    float attenuationLarge = 1.0f - (outLarge / inputLarge);
    
    // Harder drive = more attenuation
    REQUIRE(attenuationLarge > attenuationSmall);
}

// ── Coupling cap charge ──────────────────────────────────────

TEST_CASE("Grid conduction: cap charge accumulates under sustained drive", "[grid][cap]") {
    auto model = makeV1aGrid();
    recalcGridChargeDecay(model, 0.022f, TEST_SR);
    model.couplingCapCharge = 0.0f;
    
    // Drive above threshold for many samples
    float hotInput = GRID_CONDUCT_THRESH_N + 0.02f;
    for (int i = 0; i < 1000; ++i) {
        applyGridConduction(hotInput, model);
    }
    
    // Charge should have accumulated
    REQUIRE(model.couplingCapCharge > 0.0f);
}

TEST_CASE("Grid conduction: cap charge decays in silence", "[grid][cap]") {
    auto model = makeV1aGrid();
    recalcGridChargeDecay(model, 0.022f, TEST_SR);
    
    // Build up charge
    float hotInput = GRID_CONDUCT_THRESH_N + 0.02f;
    for (int i = 0; i < 1000; ++i) {
        applyGridConduction(hotInput, model);
    }
    float chargeAfterDrive = model.couplingCapCharge;
    REQUIRE(chargeAfterDrive > 0.0f);
    
    // Now feed signal below threshold (silence)
    for (int i = 0; i < 50000; ++i) {
        applyGridConduction(0.0f, model);
    }
    
    // Charge should have decayed significantly
    REQUIRE(model.couplingCapCharge < chargeAfterDrive * 0.1f);
}

TEST_CASE("Grid conduction: cap charge causes negative bias shift", "[grid][cap]") {
    auto model = makeV1aGrid();
    recalcGridChargeDecay(model, 0.022f, TEST_SR);
    model.couplingCapCharge = 0.0f;
    
    // Build up charge with sustained drive
    float hotInput = GRID_CONDUCT_THRESH_N + 0.02f;
    for (int i = 0; i < 2000; ++i) {
        applyGridConduction(hotInput, model);
    }
    
    // Now pass a zero-level signal — the cap charge should shift output negative
    float output = applyGridConduction(0.0f, model);
    REQUIRE(output < 0.0f);  // negative bias shift
}

// ── Snap-to-zero ─────────────────────────────────────────────

TEST_CASE("Grid conduction: charge snaps to zero when tiny", "[grid]") {
    auto model = makeV1aGrid();
    recalcGridChargeDecay(model, 0.022f, TEST_SR);
    model.couplingCapCharge = 1e-11f;  // just above snap threshold
    
    applyGridConduction(0.0f, model);  // one below-threshold process
    
    // After decay, should snap to exactly zero (< 1e-10)
    REQUIRE(model.couplingCapCharge == 0.0f);
}

// ── Per-stage factory functions ──────────────────────────────

TEST_CASE("Grid factories: V1a has input impedance source R", "[grid][factory]") {
    auto model = makeV1aGrid();
    REQUIRE(model.conductionThreshold == GRID_CONDUCT_THRESH_N);
    REQUIRE(model.gridResistance == GRID_R_TRIODE_N);
    REQUIRE(model.sourceResistance == Approx(VS::r(100.0f)));  // guitar pickup
}

TEST_CASE("Grid factories: V1b source R = 1kΩ (reduced for musical loading)", "[grid][factory]") {
    auto model = makeV1bGrid();
    REQUIRE(model.sourceResistance == GRID_SOURCE_R_MUSICAL_N);
}

TEST_CASE("Grid factories: PI source R = 1kΩ (reduced for musical loading)", "[grid][factory]") {
    auto model = makePIGrid();
    REQUIRE(model.sourceResistance == GRID_SOURCE_R_MUSICAL_N);
}

TEST_CASE("Grid factories: EL84 has different threshold", "[grid][factory]") {
    auto triode = makeV1aGrid();
    auto pentode = makeEL84Grid();
    
    REQUIRE(pentode.conductionThreshold == GRID_CONDUCT_THRESH_EL84_N);
    REQUIRE(pentode.conductionThreshold != triode.conductionThreshold);
}

TEST_CASE("Grid factories: EL84 source R = 1kΩ (reduced for musical loading)", "[grid][factory]") {
    auto model = makeEL84Grid();
    REQUIRE(model.sourceResistance == GRID_SOURCE_R_MUSICAL_N);
}

// ── recalcGridChargeDecay ────────────────────────────────────

TEST_CASE("Grid charge decay: higher SR gives slower per-sample decay", "[grid]") {
    auto model1 = makeV1aGrid();
    auto model2 = makeV1aGrid();
    
    recalcGridChargeDecay(model1, 0.022f, 44100.0f);
    recalcGridChargeDecay(model2, 0.022f, 176400.0f);  // 4× SR
    
    // At higher SR, each sample represents less real time,
    // so decay per sample should be closer to 1.0 (slower)
    REQUIRE(model2.chargeDecay > model1.chargeDecay);
}
