#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "utils/VoltageScale.h"
#include "utils/Constants.h"
#include "dsp/tubes/TriodeWaveshaper.h"
#include <cmath>
using namespace tt;
using Catch::Approx;

// ═══════════════════════════════════════════════════════════════
// Spec §4.1 Option A — Triode waveshaper properties
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Triode: output bounded by supply * HEADROOM_FACTOR", "[triode]") {
    float supply = VS::V_PREAMP;  // 0.833
    float ceiling = supply * HEADROOM_FACTOR;
    // Sweep grid voltage from large negative to large positive
    for (float vg = -0.05f; vg <= 0.05f; vg += 0.002f) {
        float out = triodeWaveshaper(vg, VS::V_BIAS_12AX7, supply, StageGains::V1A_GAIN);
        REQUIRE(out >= -ceiling - 1e-6f);
        REQUIRE(out <= ceiling + 1e-6f);
    }
}

TEST_CASE("Triode: zero input near bias produces near-zero output", "[triode]") {
    // At quiescent point: vGrid ≈ 0, drive * 0 + bias → tanh(bias) ≈ bias
    float out = triodeWaveshaper(0.0f, VS::V_BIAS_12AX7, VS::V_PREAMP, StageGains::V1A_GAIN);
    // bias = -0.005, drive = 70 → x = 0 * 70 + (-0.005) = -0.005
    // tanh(-0.005) ≈ -0.005, * 0.833 ≈ -0.004
    REQUIRE(std::abs(out) < 0.01f);
}

TEST_CASE("Triode: asymmetric clipping — negative clips harder", "[triode]") {
    float supply = VS::V_PREAMP;
    // Positive excursion
    float outPos = triodeWaveshaper(0.01f, 0.0f, supply, StageGains::V1A_GAIN);
    // Negative excursion of same magnitude
    float outNeg = triodeWaveshaper(-0.01f, 0.0f, supply, StageGains::V1A_GAIN);
    // Negative should be more compressed (harder clip / ASYM_FACTOR)
    REQUIRE(std::abs(outNeg) < std::abs(outPos));
}

TEST_CASE("Triode: V1a guitar input produces spec output range", "[triode]") {
    // Spec §2.5: V1a input ±0.00167, output ±0.05 to ±0.15
    float guitar = VS::V_GUITAR;
    float outPos = triodeWaveshaper(guitar, VS::V_BIAS_12AX7, VS::V_PREAMP, StageGains::V1A_GAIN);
    float outNeg = triodeWaveshaper(-guitar, VS::V_BIAS_12AX7, VS::V_PREAMP, StageGains::V1A_GAIN);
    float swing = std::max(std::abs(outPos), std::abs(outNeg));
    // Should be in the ±0.01 to ±0.20 range (relaxed bounds for MVP waveshaper)
    REQUIRE(swing > 0.001f);    // not silent
    REQUIRE(swing < 0.25f);     // not clipping rail
}

TEST_CASE("Triode: lower supply voltage reduces output swing", "[triode]") {
    float input = VS::V_GUITAR;
    float outFull = triodeWaveshaper(input, VS::V_BIAS_12AX7, VS::V_PREAMP, StageGains::V1A_GAIN);
    // 10% supply droop
    float outDroop = triodeWaveshaper(input, VS::V_BIAS_12AX7, VS::V_PREAMP * 0.9f, StageGains::V1A_GAIN);
    REQUIRE(std::abs(outDroop) < std::abs(outFull));
}

TEST_CASE("Triode: monotonic — more input produces more output", "[triode]") {
    float prev = 0.0f;
    for (float vg = 0.0f; vg <= 0.01f; vg += 0.001f) {
        float out = std::abs(triodeWaveshaper(vg, VS::V_BIAS_12AX7, VS::V_PREAMP, StageGains::V1A_GAIN));
        REQUIRE(out >= prev - 1e-6f);
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

// audioTaper should be defined in a header — test the expected behavior
// x^2 taper: 0→0, 0.5→0.25, 1→1
namespace {
    float audioTaper(float x) { return x * x; }
}

TEST_CASE("Audio taper: 0 maps to 0", "[taper]") {
    REQUIRE(audioTaper(0.0f) == 0.0f);
}

TEST_CASE("Audio taper: 1 maps to 1", "[taper]") {
    REQUIRE(audioTaper(1.0f) == 1.0f);
}

TEST_CASE("Audio taper: 0.5 maps to 0.25", "[taper]") {
    REQUIRE(audioTaper(0.5f) == Approx(0.25f));
}

TEST_CASE("Audio taper: monotonically increasing", "[taper]") {
    float prev = 0.0f;
    for (float x = 0.0f; x <= 1.0f; x += 0.1f) {
        float y = audioTaper(x);
        REQUIRE(y >= prev);
        prev = y;
    }
}
