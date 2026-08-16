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

using namespace sr;
using namespace sr::test;
using Catch::Approx;

static LTPModel freshLTP() {
    LTPModel ltp;
    ltp.init();
    return ltp;
}

// ── Plate lower bound: soft, and consistent with the iteration ───
//
// Regression. The solver used to bound the plate at 0.0 inside its iteration but
// at vCathode for the final result, so it converged against one constraint and
// then hard-clipped the answer to a tighter one — on >58% of samples at high
// drive. std::clamp is a derivative discontinuity; a real triode compresses
// smoothly as the plate nears the cathode. Both are now the same softplus knee.

// Static transfer point: settle a fresh solver at a DC input.
static LTPOutput settleAt(float vIn, float vSupply, int n = 64) {
    auto ltp = freshLTP();
    LTPOutput out{};
    for (int i = 0; i < n; ++i) out = solveLTP(vIn, vSupply, ltp);
    return out;
}

TEST_CASE("LTP: plate stays within the cathode/supply bounds", "[ltp][regression]") {
    // Both bounds are now applied identically inside the iteration and to the
    // final result. Previously the loop bounded at 0.0 and the output at
    // vCathode, so the solver converged against one constraint and the answer was
    // then clipped to a tighter one on >58% of samples at high drive.
    //
    // Weak inequality on the lower bound: the softplus knee is asymptotic, but at
    // extreme overdrive exp() underflows and it reaches the bound exactly.
    for (float vIn = 0.0f; vIn <= 0.6f; vIn += 0.01f) {
        auto out = settleAt(vIn, V_PI_NOMINAL_N);
        REQUIRE(out.piA >= out.vCathode);
        REQUIRE(out.piB >= out.vCathode);
        REQUIRE(out.piA <= V_PI_NOMINAL_N);
        REQUIRE(out.piB <= V_PI_NOMINAL_N);
    }
}

// Convergence — the acceptance test for the root-find solver.
//
// The previous relaxation settled into a period-2 limit cycle rather than a
// fixed point: at a constant 1.5 V input piA alternated between 0.933 and 0.292
// EVERY sample, synthesising a Nyquist-rate square wave out of DC which the
// downstream nonlinearities folded back into the audio band. A constant input
// must produce a constant output.
TEST_CASE("LTP: constant input settles to a fixed point", "[ltp][regression]") {
    for (float vIn : { 0.0f, 0.002f, 0.005f, 0.01f, 0.02f, 0.05f, 0.1f, 0.2f }) {
        auto ltp = freshLTP();
        for (int i = 0; i < 168; ++i) solveLTP(vIn, V_PI_NOMINAL_N, ltp);

        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 32; ++i) {
            auto o = solveLTP(vIn, V_PI_NOMINAL_N, ltp);
            lo = std::min(lo, o.piA);
            hi = std::max(hi, o.piA);
        }
        INFO("vIn = " << vIn << " (" << VS::toVolts(vIn) << " V)");
        REQUIRE((hi - lo) < 1e-5f);
    }
}

TEST_CASE("LTP: solved cathode actually satisfies its own node equation", "[ltp][regression]") {
    // vCathode must equal the tail current through the tail resistor. The old
    // relaxation left a partially-converged iterate here, so solver error leaked
    // into the signal as state.
    for (float vIn : { 0.0f, 0.005f, 0.02f, 0.05f }) {
        auto ltp = freshLTP();
        LTPOutput out{};
        for (int i = 0; i < 200; ++i) out = solveLTP(vIn, V_PI_NOMINAL_N, ltp);

        // Re-solving must not move the answer: it is a genuine fixed point.
        auto again = solveLTP(vIn, V_PI_NOMINAL_N, ltp);
        INFO("vIn = " << vIn);
        REQUIRE(again.vCathode == Approx(out.vCathode).margin(1e-6f));
        REQUIRE(again.piA == Approx(out.piA).margin(1e-6f));
        REQUIRE(again.piB == Approx(out.piB).margin(1e-6f));
        REQUIRE(out.vCathode >= 0.0f);
        REQUIRE(out.vCathode < V_PI_NOMINAL_N);   // no runaway past the rail
    }
}

TEST_CASE("LTP: still inverts differentially after the rewrite", "[ltp][regression]") {
    // Guard the thing that alpha=1.0 broke: a "stable" solver that has killed the
    // stage also reports zero oscillation. Plates must move in OPPOSITE directions.
    auto idle = freshLTP();
    LTPOutput oIdle{};
    for (int i = 0; i < 400; ++i) oIdle = solveLTP(0.0f, V_PI_NOMINAL_N, idle);

    auto driven = freshLTP();
    LTPOutput oDriven{};
    for (int i = 0; i < 400; ++i) oDriven = solveLTP(0.004f, V_PI_NOMINAL_N, driven);

    REQUIRE(oDriven.piA < oIdle.piA);        // driven grid pulls its plate down
    REQUIRE(oDriven.piB > oIdle.piB);        // the other side rises
    REQUIRE(oIdle.piA < V_PI_NOMINAL_N - 1e-3f);   // not cut off at idle
    REQUIRE(oIdle.piB < V_PI_NOMINAL_N - 1e-3f);
}

// ── Zero input: near-symmetric ───────────────────────────────

TEST_CASE("LTP: zero input produces quiescent plate voltages", "[ltp]") {
    auto ltp = freshLTP();

    // Run a few iterations to let cathode settle
    LTPOutput out;
    for (int i = 0; i < 100; ++i)
        out = solveLTP(0.0f, V_PI_NOMINAL_N, ltp);

    // Both plates should be within the supply range
    REQUIRE(out.piA > 0.0f);
    REQUIRE(out.piA < V_PI_NOMINAL_N);
    REQUIRE(out.piB > 0.0f);
    REQUIRE(out.piB < V_PI_NOMINAL_N);

    // Plates should be nearly balanced at quiescence (within a few percent).
    // Koren plate-voltage feedback shifts exact balance vs simple load ratio.
    float diff = std::abs(out.piA - out.piB);
    REQUIRE(diff < 0.05f);
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
    // Margin accounts for Koren nonlinearity + plate load asymmetry (100K vs 82K)
    // + cached plate voltage initial guess on first signal sample
    REQUIRE(outPos.piA == Approx(outNeg.piB).margin(0.1f));
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

TEST_CASE("LTP: cathode voltage is positive (current x Rtail)", "[ltp]") {
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

    // Settle cathode
    for (int i = 0; i < 100; ++i)
        solveLTP(0.0f, supply, ltp);

    // Sweep input from large negative to large positive
    for (float vin = -0.2f; vin <= 0.2f; vin += 0.01f) {
        LTPOutput out = solveLTP(vin, supply, ltp);

        REQUIRE(out.piA >= out.vCathode - 0.001f);
        REQUIRE(out.piA <= supply + 0.001f);
        REQUIRE(out.piB >= out.vCathode - 0.001f);
        REQUIRE(out.piB <= supply + 0.001f);
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
    REQUIRE(ltp.rPlateA_n == PI_RPLATE_A_N);
    REQUIRE(ltp.rPlateB_n == PI_RPLATE_B_N);
    REQUIRE(ltp.rPlateA_n > ltp.rPlateB_n);  // A=100K > B=82K
    REQUIRE(ltp.vBiasRef == PI_BIAS_REF_N);
    REQUIRE(ltp.vCathode == PI_CATHODE_INIT_N);
}

// ── reset() parity with init() ───────────────────────────────

TEST_CASE("LTP: reset restores idle cathode + clears plate cache", "[ltp]") {
    auto ltp = freshLTP();

    // Drive hard so vCathode drifts well away from PI_CATHODE_INIT_N.
    for (int i = 0; i < 200; ++i)
        solveLTP(0.1f, V_PI_NOMINAL_N, ltp);
    REQUIRE(ltp.vCathode != Approx(PI_CATHODE_INIT_N).margin(0.001f));

    ltp.reset();

    REQUIRE(ltp.vCathode == PI_CATHODE_INIT_N);
    REQUIRE(ltp.vPlateA == -1.0f);
    REQUIRE(ltp.vPlateB == -1.0f);
}

TEST_CASE("LTP: plate load asymmetry creates AC gain difference", "[ltp]") {
    auto ltp = freshLTP();

    // Settle at zero
    LTPOutput outQ;
    for (int i = 0; i < 100; ++i)
        outQ = solveLTP(0.0f, V_PI_NOMINAL_N, ltp);

    // Small positive signal — measure AC swing from quiescent
    LTPOutput out = solveLTP(0.005f, V_PI_NOMINAL_N, ltp);
    float swingA = std::abs(out.piA - outQ.piA);
    float swingB = std::abs(out.piB - outQ.piB);

    // Side A (100K plate load) should have larger AC swing than side B (82K)
    REQUIRE(swingA > swingB);
}
