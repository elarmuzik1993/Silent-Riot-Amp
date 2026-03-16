#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "utils/VoltageScale.h"
#include "utils/Constants.h"
#include "dsp/tubes/TriodeWaveshaper.h"
#include "test_helpers.h"
#include <cmath>
using namespace tt;
using namespace tt::test;
using Catch::Approx;

// ═══════════════════════════════════════════════════════════════
// Spec §4.1 — Triode waveshaper (Child-Langmuir circuit model)
// Output is plate voltage: 0 to vSupply
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Triode: output bounded within [0, vSupply]", "[triode]") {
    float supply = VS::V_PREAMP;  // 0.833
    // Sweep grid voltage from large negative to large positive
    for (float vg = -0.05f; vg <= 0.05f; vg += 0.002f) {
        float out = triodeWaveshaper(vg, VS::V_BIAS_12AX7, supply, PREAMP_RPLATE_N);
        REQUIRE(out >= -1e-6f);
        REQUIRE(out <= supply + 1e-6f);
    }
}

TEST_CASE("Triode: zero input at bias produces quiescent plate voltage", "[triode]") {
    // At quiescent point: vGrid=0, vgk = 0 + bias = -0.005
    // triodeCurrentModel produces plate current, vPlate = vSupply - ip * rPlate
    // Quiescent plate voltage is well above zero (tube is partially conducting)
    float out = triodeWaveshaper(0.0f, VS::V_BIAS_12AX7, VS::V_PREAMP, PREAMP_RPLATE_N);
    // Expect plate voltage around 0.3–0.6 of supply (typical idle point)
    REQUIRE(out > 0.1f);
    REQUIRE(out < VS::V_PREAMP);
}

TEST_CASE("Triode: asymmetric clipping — positive grid clips harder (plate to floor)", "[triode]") {
    float supply = VS::V_PREAMP;
    float bias = VS::V_BIAS_12AX7;  // -0.005 normalized
    float quiescent = triodeWaveshaper(0.0f, bias, supply, PREAMP_RPLATE_N);
    // Positive grid swing → more plate current → plate voltage drops
    float outPos = triodeWaveshaper(0.005f, bias, supply, PREAMP_RPLATE_N);
    // Negative grid swing → less plate current → plate voltage rises toward supply
    float outNeg = triodeWaveshaper(-0.005f, bias, supply, PREAMP_RPLATE_N);
    // Distance from quiescent
    float dropPos = quiescent - outPos;   // plate drops (positive value)
    float riseNeg = outNeg - quiescent;   // plate rises (positive value)
    // With 3/2 power law, positive grid clips harder (hits plate floor)
    REQUIRE(dropPos > 0.0f);
    REQUIRE(riseNeg > 0.0f);
    // Asymmetry: the drop should be larger than the rise for same swing magnitude
    // (3/2 power law compresses more on the cutoff side)
    REQUIRE(dropPos > riseNeg);
}

TEST_CASE("Triode: V1a guitar input produces plate voltage within supply", "[triode]") {
    float guitar = VS::V_GUITAR;
    float outPos = triodeWaveshaper(guitar, VS::V_BIAS_12AX7, VS::V_PREAMP, PREAMP_RPLATE_N);
    float outNeg = triodeWaveshaper(-guitar, VS::V_BIAS_12AX7, VS::V_PREAMP, PREAMP_RPLATE_N);
    // Both outputs should be valid plate voltages within [0, vSupply]
    REQUIRE(outPos >= 0.0f);
    REQUIRE(outPos <= VS::V_PREAMP);
    REQUIRE(outNeg >= 0.0f);
    REQUIRE(outNeg <= VS::V_PREAMP);
    // They should differ (tube is amplifying)
    REQUIRE(std::abs(outPos - outNeg) > 0.001f);
}

TEST_CASE("Triode: lower supply reduces max plate voltage", "[triode]") {
    float input = VS::V_GUITAR;
    float outFull = triodeWaveshaper(input, VS::V_BIAS_12AX7, VS::V_PREAMP, PREAMP_RPLATE_N);
    // 10% supply droop
    float outDroop = triodeWaveshaper(input, VS::V_BIAS_12AX7, VS::V_PREAMP * 0.9f, PREAMP_RPLATE_N);
    // Lower supply means lower plate voltage ceiling
    REQUIRE(outDroop < outFull);
}

TEST_CASE("Triode: monotonic — increasing grid voltage decreases plate voltage (inverting)", "[triode]") {
    float prev = triodeWaveshaper(0.0f, VS::V_BIAS_12AX7, VS::V_PREAMP, PREAMP_RPLATE_N);
    for (float vg = 0.001f; vg <= 0.01f; vg += 0.001f) {
        float out = triodeWaveshaper(vg, VS::V_BIAS_12AX7, VS::V_PREAMP, PREAMP_RPLATE_N);
        // Plate voltage should decrease as grid goes more positive (inverting amp)
        REQUIRE(out <= prev + 1e-6f);
        prev = out;
    }
}

// ═══════════════════════════════════════════════════════════════
// Spec §3.5 — triodeCurrentModel calibration
// "At Vgk = -1.5V, Vp = 250V → Ip ≈ 1.2mA"
// ═══════════════════════════════════════════════════════════════

TEST_CASE("TriodeCurrent: deep cutoff produces near-zero", "[triode_current]") {
    float ip = triodeCurrentModel(VS::v(-5.0f), PI_MU, PI_RP_N);
    REQUIRE(ip < 0.01f);
}

TEST_CASE("TriodeCurrent: positive Vgk produces positive current", "[triode_current]") {
    float ip = triodeCurrentModel(VS::v(0.5f), PI_MU, PI_RP_N);
    REQUIRE(ip > 0.0f);
}

TEST_CASE("TriodeCurrent: monotonically increasing with Vgk", "[triode_current]") {
    float prev = 0.0f;
    for (float vgk = -0.02f; vgk <= 0.02f; vgk += 0.002f) {
        float ip = triodeCurrentModel(vgk, PI_MU, PI_RP_N);
        REQUIRE(ip >= prev - 1e-8f);
        prev = ip;
    }
}

TEST_CASE("TriodeCurrent: never returns negative", "[triode_current]") {
    for (float vgk = -0.1f; vgk <= 0.1f; vgk += 0.005f) {
        float ip = triodeCurrentModel(vgk, PI_MU, PI_RP_N);
        REQUIRE(ip >= 0.0f);
    }
}

// ═══════════════════════════════════════════════════════════════
// Audio taper (used by gain/volume)
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Audio taper: 0 maps to 0", "[taper]") {
    REQUIRE(tt::audioTaper(0.0f) == 0.0f);
}

TEST_CASE("Audio taper: 1 maps to 1", "[taper]") {
    REQUIRE(tt::audioTaper(1.0f) == 1.0f);
}

TEST_CASE("Audio taper: 0.5 maps to 0.25", "[taper]") {
    REQUIRE(tt::audioTaper(0.5f) == Approx(0.25f));
}

TEST_CASE("Audio taper: monotonically increasing", "[taper]") {
    float prev = 0.0f;
    for (float x = 0.0f; x <= 1.0f; x += 0.1f) {
        float y = tt::audioTaper(x);
        REQUIRE(y >= prev);
        prev = y;
    }
}
