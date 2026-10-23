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
#include <cmath>
#include "dsp/tubes/GridConduction.h"

using namespace sr;
using namespace sr::test;
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
    REQUIRE(model.gridResistance == GRID_R_V1A_N);
    REQUIRE(model.sourceResistance == Approx(VS::r(100.0f)));  // guitar pickup
}

TEST_CASE("Grid factories: V1b has real stage impedances", "[grid][factory]") {
    auto model = makeV1bGrid();
    REQUIRE(model.gridResistance == GRID_R_V1B_N);
    REQUIRE(model.sourceResistance == GRID_SOURCE_R_V1B_N);
}

TEST_CASE("Grid factories: PI has real stage impedances", "[grid][factory]") {
    auto model = makePIGrid();
    REQUIRE(model.gridResistance == GRID_R_PI_N);
    REQUIRE(model.sourceResistance == GRID_SOURCE_R_PI_N);
}

TEST_CASE("Grid factories: EL84 has different threshold", "[grid][factory]") {
    auto triode = makeV1aGrid();
    auto pentode = makeEL84Grid();
    
    REQUIRE(pentode.conductionThreshold == GRID_CONDUCT_THRESH_EL84_N);
    REQUIRE(pentode.conductionThreshold != triode.conductionThreshold);
}

TEST_CASE("Grid factories: EL84 has real stage impedances", "[grid][factory]") {
    auto model = makeEL84Grid();
    REQUIRE(model.gridResistance == GRID_R_EL84_N);
    REQUIRE(model.sourceResistance == GRID_SOURCE_R_EL84_N);
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

// ── Sample-rate independence of the settled bias shift (review B7) ──────────
//
// The charge increment used to be a fixed per-sample GRID_CHARGE_RATE while the
// decay was rate-aware, so the settled bias shift grew with the effective rate
// (96 kHz / 8× was ~5.7 dB quieter than the calibrated 44.1 kHz / 4×).

namespace {

// Drive a stage with a steady 110 Hz sine for 1 s of real time at effectiveSR
// and return the mean coupling-cap charge over the last 100 ms (whole cycles
// of ripple, long after the 22-47 ms charge time constants have settled).
float settledBiasShift(GridConductionModel model, float effectiveSR, float amplitudeN)
{
    const int total   = static_cast<int>(effectiveSR);          // 1 s
    const int tailLen = static_cast<int>(effectiveSR * 0.1f);   // 100 ms = 11 cycles
    double sum = 0.0;
    for (int i = 0; i < total; ++i) {
        const float t = static_cast<float>(i) / effectiveSR;
        applyGridConduction(amplitudeN * std::sin(2.0f * 3.14159265f * 110.0f * t), model);
        if (i >= total - tailLen) sum += model.couplingCapCharge;
    }
    return static_cast<float>(sum / tailLen);
}

} // namespace

TEST_CASE("Grid charge: default rate is the 176.4 kHz calibration", "[grid][cap][rate]") {
    // A model that never sees recalcGridChargeDecay must behave as it always has.
    REQUIRE(makeV1bGrid().chargeRate == GRID_CHARGE_RATE);
    GridConductionModel m{};
    REQUIRE(m.chargeRate == GRID_CHARGE_RATE);

    // And recalc at the reference rate leaves the increment bit-identical.
    auto r = makeV1bGrid();
    recalcGridChargeDecay(r, GRID_TAU_PREAMP_S, GRID_CHARGE_REF_SR);
    REQUIRE(r.chargeRate == GRID_CHARGE_RATE);
}

TEST_CASE("Grid charge: settled bias shift is independent of the sample rate", "[grid][cap][rate]") {
    struct Stage { const char* name; GridConductionModel model; float tau; float amp; };
    const Stage stages[] = {
        { "V1b",  makeV1bGrid(),  GRID_TAU_PREAMP_S, VS::v(1.2f) },
        { "PI",   makePIGrid(),   GRID_TAU_PI_S,     VS::v(1.0f) },
        { "EL84", makeEL84Grid(), GRID_TAU_EL84_S,   VS::v(9.5f) },
    };
    // 88.2k = 44.1k×2, 176.4k = 44.1k×4 (reference), 352.8k = 44.1k×8,
    // 384k = 96k×4, 768k = 96k×8.
    const float rates[] = { 88200.0f, 176400.0f, 352800.0f, 384000.0f, 768000.0f };

    for (const auto& s : stages) {
        INFO(s.name);

        // Today's calibrated behaviour at 176.4 kHz: decay set by hand, charge
        // rate left at its default — exactly what the code did before the fix.
        auto legacy = s.model;
        legacy.chargeDecay = std::exp(-1.0f / (s.tau * GRID_CHARGE_REF_SR));
        const float reference = settledBiasShift(legacy, GRID_CHARGE_REF_SR, s.amp);

        // Meaningful operating point: charged, but below the MAX_BIAS_SHIFT_N cap
        // (a railed charge would read the same at every rate and prove nothing).
        REQUIRE(reference > 0.05f * std::abs(MAX_BIAS_SHIFT_N));
        REQUIRE(reference < 0.8f * std::abs(MAX_BIAS_SHIFT_N));

        for (float sr : rates) {
            INFO("effective SR " << sr);
            auto m = s.model;
            recalcGridChargeDecay(m, s.tau, sr);
            const float shift = settledBiasShift(m, sr, s.amp);
            if (sr == GRID_CHARGE_REF_SR)
                REQUIRE(shift == reference);                     // bit-identical
            else
                REQUIRE(shift == Approx(reference).epsilon(0.01)); // within 1 %
        }
    }
}
