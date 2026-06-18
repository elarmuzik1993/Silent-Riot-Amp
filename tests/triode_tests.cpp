#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "utils/VoltageScale.h"
#include "utils/Constants.h"
#include "dsp/tubes/TriodeWaveshaper.h"
#include "test_helpers.h"
#include <cmath>
using namespace sr;
using namespace sr::test;
using Catch::Approx;

// ═══════════════════════════════════════════════════════════════
// Spec §4.1 — Triode waveshaper (Koren model, iterative load-line solve)
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
    // Koren model produces plate current via iterative solve
    // Quiescent plate voltage is well above zero (tube is partially conducting)
    float out = triodeWaveshaper(0.0f, VS::V_BIAS_12AX7, VS::V_PREAMP, PREAMP_RPLATE_N);
    // Expect plate voltage around 0.5–0.8 of supply (typical idle point for Koren ECC83)
    REQUIRE(out > 0.1f);
    REQUIRE(out < VS::V_PREAMP);
}

TEST_CASE("Triode: asymmetric clipping - positive grid clips harder (plate to floor)", "[triode]") {
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
    // With Koren model, positive grid clips harder (more current into load)
    REQUIRE(dropPos > 0.0f);
    REQUIRE(riseNeg > 0.0f);
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

TEST_CASE("Triode: monotonic - increasing grid voltage decreases plate voltage (inverting)", "[triode]") {
    float prev = triodeWaveshaper(0.0f, VS::V_BIAS_12AX7, VS::V_PREAMP, PREAMP_RPLATE_N);
    for (float vg = 0.001f; vg <= 0.01f; vg += 0.001f) {
        float out = triodeWaveshaper(vg, VS::V_BIAS_12AX7, VS::V_PREAMP, PREAMP_RPLATE_N);
        // Plate voltage should decrease as grid goes more positive (inverting amp)
        REQUIRE(out <= prev + 1e-6f);
        prev = out;
    }
}

// ═══════════════════════════════════════════════════════════════
// Spec §4.1 — Koren triode current model
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Koren: deep cutoff produces near-zero current", "[koren]") {
    auto p = makeECC83Params();
    float ip = korenTriode(VS::v(-5.0f), VS::V_PREAMP, p);
    REQUIRE(ip < 0.01f);
}

TEST_CASE("Koren: positive Vgk produces positive current", "[koren]") {
    auto p = makeECC83Params();
    float ip = korenTriode(VS::v(0.5f), VS::V_PREAMP, p);
    REQUIRE(ip > 0.0f);
}

TEST_CASE("Koren: monotonically increasing with Vgk", "[koren]") {
    auto p = makeECC83Params();
    float prev = 0.0f;
    for (float vgk = -0.02f; vgk <= 0.02f; vgk += 0.002f) {
        float ip = korenTriode(vgk, VS::V_PREAMP, p);
        REQUIRE(ip >= prev - 1e-8f);
        prev = ip;
    }
}

TEST_CASE("Koren: never returns negative", "[koren]") {
    auto p = makeECC83Params();
    for (float vgk = -0.1f; vgk <= 0.1f; vgk += 0.005f) {
        float ip = korenTriode(vgk, VS::V_PREAMP, p);
        REQUIRE(ip >= 0.0f);
    }
}

TEST_CASE("Koren: plate voltage dependence - higher plate gives more current", "[koren]") {
    auto p = makeECC83Params();
    float vgk = VS::v(-1.5f);  // typical operating point
    float ipLow  = korenTriode(vgk, VS::v(100.0f), p);
    float ipHigh = korenTriode(vgk, VS::v(250.0f), p);
    // Koren model: higher plate voltage → more current (unlike simple Child-Langmuir)
    REQUIRE(ipHigh > ipLow);
}

// ═══════════════════════════════════════════════════════════════
// Spec §4.1 VERIFY: normalized Koren matches real-domain Koren
// "Feed known Vg=-1.5V, Vp=200V through both real Koren and
//  normalized Koren. Confirm Ip matches to 3+ sig figs."
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Koren: normalized matches real domain (Vg=-1.5V, Vp=200V)", "[koren]") {
    // Real-domain Koren equation for ECC83/12AX7
    float Vg = -1.5f, Vp = 200.0f;
    float mu = 100.0f, ex = 1.4f, Kg = 1060.0f, Kp = 600.0f, Kvb = 300.0f;

    float E1_real = (Vp / Kp) * std::log(1.0f + std::exp(
        Kp * (1.0f / mu + Vg / std::sqrt(Kvb + Vp * Vp))));
    float Ip_real = std::pow(std::max(E1_real, 0.0f), ex) / Kg;  // amps

    // Normalized-domain Koren
    auto p = makeECC83Params();
    float ip_n = korenTriode(VS::v(Vg), VS::v(Vp), p);
    float Ip_from_norm = ip_n * VS::N_TO_I;  // convert back to amps

    // Must match to 3+ significant figures (0.1% tolerance)
    REQUIRE(Ip_from_norm == Approx(Ip_real).epsilon(0.001f));
}

TEST_CASE("Koren: normalized matches real domain (Vg=-1.0V, Vp=250V)", "[koren]") {
    float Vg = -1.0f, Vp = 250.0f;
    float mu = 100.0f, ex = 1.4f, Kg = 1060.0f, Kp = 600.0f, Kvb = 300.0f;

    float E1_real = (Vp / Kp) * std::log(1.0f + std::exp(
        Kp * (1.0f / mu + Vg / std::sqrt(Kvb + Vp * Vp))));
    float Ip_real = std::pow(std::max(E1_real, 0.0f), ex) / Kg;

    auto p = makeECC83Params();
    float ip_n = korenTriode(VS::v(Vg), VS::v(Vp), p);
    float Ip_from_norm = ip_n * VS::N_TO_I;

    REQUIRE(Ip_from_norm == Approx(Ip_real).epsilon(0.001f));
}

// ═══════════════════════════════════════════════════════════════
// Audio taper (used by gain/volume)
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Audio taper: 0 maps to 0", "[taper]") {
    REQUIRE(sr::audioTaper(0.0f) == 0.0f);
}

TEST_CASE("Audio taper: 1 maps to 1", "[taper]") {
    REQUIRE(sr::audioTaper(1.0f) == 1.0f);
}

TEST_CASE("Audio taper: 0.5 maps to 0.25", "[taper]") {
    REQUIRE(sr::audioTaper(0.5f) == Approx(0.25f));
}

TEST_CASE("Audio taper: monotonically increasing", "[taper]") {
    float prev = 0.0f;
    for (float x = 0.0f; x <= 1.0f; x += 0.1f) {
        float y = sr::audioTaper(x);
        REQUIRE(y >= prev);
        prev = y;
    }
}
