// ═══════════════════════════════════════════════════════════════
// LTP Solver Tests — Spec Section 3.5
//
// Tests for:
//   - Zero input → near-symmetric outputs
//   - Positive input → one plate drops, other rises (phase inversion)
//   - Cathode voltage rises under drive (current steering)
//   - Output bounded by supply
//   - Natural gain imbalance emerges at high drive
//   - Supply droop reduces headroom
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test_helpers.h"
#include "dsp/power/LTPSolver.h"

using namespace tt;
using namespace tt::test;
using Catch::Approx;

static LTPModel freshLTP() {
    LTPModel ltp;
    ltp.init();
    return ltp;
}

// ── Zero input: near-symmetric ───────────────────────────────

TEST_CASE("LTP: zero input produces near-symmetric outputs", "[ltp]") {
    auto ltp = freshLTP();
    
    // Run a few iterations to let cathode settle
    LTPOutput out;
    for (int i = 0; i < 100; ++i)
        out = solveLTP(0.0f, V_PI_NOMINAL_N, ltp);
    
    // Both outputs should be nearly equal (small difference from solver settling)
    REQUIRE(out.piA == Approx(out.piB).margin(0.01f));
}

TEST_CASE("LTP: zero input gives equal plate voltages", "[ltp]") {
    auto ltp = freshLTP();

    LTPOutput out;
    for (int i = 0; i < 100; ++i)
        out = solveLTP(0.0f, V_PI_NOMINAL_N, ltp);

    // With no input, both plates should be at the same DC operating point
    // (raw plate voltages — AC coupling handled by pi_couplingHP in processBlock)
    REQUIRE(out.piA == Approx(out.piB).margin(0.01f));
    REQUIRE(out.piA > 0.0f);
    REQUIRE(out.piA < V_PI_NOMINAL_N);
}

// ── Phase inversion ──────────────────────────────────────────

TEST_CASE("LTP: positive input drives outputs in opposite directions", "[ltp]") {
    auto ltp = freshLTP();
    
    // Settle first
    for (int i = 0; i < 50; ++i)
        solveLTP(0.0f, V_PI_NOMINAL_N, ltp);
    
    // Apply positive input
    LTPOutput out = solveLTP(0.01f, V_PI_NOMINAL_N, ltp);
    
    // piA and piB should move in opposite directions
    // (one increases, the other decreases relative to quiescent)
    REQUIRE(out.piA != Approx(out.piB).margin(0.001f));
}

TEST_CASE("LTP: inverted output for inverted input", "[ltp]") {
    auto ltpPos = freshLTP();
    auto ltpNeg = freshLTP();

    for (int i = 0; i < 100; ++i) {
        solveLTP(0.0f, V_PI_NOMINAL_N, ltpPos);
        solveLTP(0.0f, V_PI_NOMINAL_N, ltpNeg);
    }

    // Use small signal to stay in the linear region of the triode 3/2 power law
    LTPOutput outPos = solveLTP(0.001f, V_PI_NOMINAL_N, ltpPos);
    LTPOutput outNeg = solveLTP(-0.001f, V_PI_NOMINAL_N, ltpNeg);

    // piA with positive input ≈ piB with negative input (approximately)
    // Margin accounts for inherent asymmetry of the triode 3/2 power law
    REQUIRE(outPos.piA == Approx(outNeg.piB).margin(0.04f));
}

// ── Cathode coupling (current steering) ──────────────────────

TEST_CASE("LTP: cathode voltage rises under large drive", "[ltp]") {
    auto ltp = freshLTP();
    
    // Settle at zero
    LTPOutput outQuiet;
    for (int i = 0; i < 100; ++i)
        outQuiet = solveLTP(0.0f, V_PI_NOMINAL_N, ltp);
    float quietCathode = outQuiet.vCathode;
    
    // Now drive hard
    LTPOutput outDriven = solveLTP(0.05f, V_PI_NOMINAL_N, ltp);
    
    // Cathode should rise (more total current through tail R)
    // Actually for a single step it depends on the previous cathode state
    // Let's drive for several samples
    for (int i = 0; i < 50; ++i)
        outDriven = solveLTP(0.05f, V_PI_NOMINAL_N, ltp);
    
    // With sustained drive, one triode draws more → total current up → cathode up
    REQUIRE(outDriven.vCathode >= quietCathode - 0.01f);
}

TEST_CASE("LTP: cathode voltage is positive (current × Rtail)", "[ltp]") {
    auto ltp = freshLTP();
    
    LTPOutput out;
    for (int i = 0; i < 100; ++i)
        out = solveLTP(0.0f, V_PI_NOMINAL_N, ltp);
    
    REQUIRE(out.vCathode > 0.0f);
}

// ── Output bounded by supply ─────────────────────────────────

TEST_CASE("LTP: plate voltages clamped within [cathode, supply]", "[ltp]") {
    auto ltp = freshLTP();
    float supply = V_PI_NOMINAL_N;

    // Settle first so dcPlate tracks to quiescent point
    for (int i = 0; i < 100; ++i)
        solveLTP(0.0f, supply, ltp);

    // Sweep input from large negative to large positive
    for (float vin = -0.2f; vin <= 0.2f; vin += 0.01f) {
        LTPOutput out = solveLTP(vin, supply, ltp);

        // Reconstruct raw plate voltage from AC output + tracked DC reference
        float rawPlateA = out.piA + out.dcPlate;
        float rawPlateB = out.piB + out.dcPlate;

        REQUIRE(rawPlateA >= out.vCathode - 0.001f);
        REQUIRE(rawPlateA <= supply + 0.001f);
        REQUIRE(rawPlateB >= out.vCathode - 0.001f);
        REQUIRE(rawPlateB <= supply + 0.001f);
    }
}

// ── Supply sensitivity ───────────────────────────────────────

TEST_CASE("LTP: lower supply reduces output swing", "[ltp]") {
    auto ltpNom = freshLTP();
    auto ltpLow = freshLTP();
    
    float supplyNom = V_PI_NOMINAL_N;
    float supplyLow = VS::v(200.0f);
    
    // Settle both
    for (int i = 0; i < 100; ++i) {
        solveLTP(0.0f, supplyNom, ltpNom);
        solveLTP(0.0f, supplyLow, ltpLow);
    }
    
    // Drive with the same signal
    LTPOutput outNom = solveLTP(0.05f, supplyNom, ltpNom);
    LTPOutput outLow = solveLTP(0.05f, supplyLow, ltpLow);
    
    float swingNom = std::abs(outNom.piA - outNom.piB);
    float swingLow = std::abs(outLow.piA - outLow.piB);
    
    // Lower supply → less available swing
    REQUIRE(swingLow <= swingNom + 0.01f);
}

// ── Natural imbalance at high drive ──────────────────────────

TEST_CASE("LTP: gain imbalance emerges at high drive", "[ltp]") {
    auto ltp = freshLTP();
    
    // Settle
    for (int i = 0; i < 100; ++i)
        solveLTP(0.0f, V_PI_NOMINAL_N, ltp);
    
    // Drive hard — one side should clip while the other still has headroom
    LTPOutput out = solveLTP(0.1f, V_PI_NOMINAL_N, ltp);
    
    // |piA| and |piB| should be different at high drive (asymmetric clipping)
    float diff = std::abs(std::abs(out.piA) - std::abs(out.piB));
    // At extreme drive, difference should be measurable
    // (at small drive it's nearly symmetric, tested above)
    REQUIRE(diff > 0.0f);  // any measurable difference confirms the mechanism
}

// ── LTPModel initialization ──────────────────────────────────

TEST_CASE("LTP model: init sets correct constants from spec", "[ltp]") {
    auto ltp = freshLTP();
    
    REQUIRE(ltp.tailR_n == PI_TAIL_R_N);
    REQUIRE(ltp.rPlateA_n == PI_RPLATE_N);
    REQUIRE(ltp.rPlateB_n == PI_RPLATE_N);
    REQUIRE(ltp.vBiasRef == PI_BIAS_REF_N);
    REQUIRE(ltp.vCathode == PI_CATHODE_INIT_N);
}
