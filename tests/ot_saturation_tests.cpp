// ═══════════════════════════════════════════════════════════════
// OT Core Saturation Tests — flux-based model
//
// Tests for:
//   - Zero input → zero output (no DC offset or state leakage)
//   - Low-level signal passes near unity
//   - High-level signal is compressed (saturation works)
//   - Bass saturates more than treble (frequency-dependent)
//   - Reset clears state
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test_helpers.h"
#include "dsp/cabinet/OTSaturation.h"

using namespace sr;
using namespace sr::test;
using Catch::Approx;

TEST_CASE("OT saturation: zero input produces zero output", "[ot_sat]") {
    OTSaturation sat;
    sat.recalcCoeffs(TEST_SR);

    for (int i = 0; i < 1000; ++i) {
        float out = sat.process(0.0f);
        REQUIRE(out == Approx(0.0f).margin(1e-10f));
    }
}

TEST_CASE("OT saturation: low-level signal passes near unity", "[ot_sat]") {
    OTSaturation sat;
    sat.recalcCoeffs(TEST_SR);

    // Use a small-amplitude 1kHz sine — should pass with gain ≈ 1.0
    float freq = 1000.0f;
    float amp = 0.001f;  // well below saturation threshold
    int warmup = static_cast<int>(TEST_SR * 0.05f);  // 50ms warmup
    int measure = static_cast<int>(TEST_SR * 0.05f);  // 50ms measure

    // Warm up
    for (int i = 0; i < warmup; ++i)
        sat.process(amp * std::sin(2.0f * PI_F * freq * i / TEST_SR));

    // Measure
    float inPeak = 0.0f, outPeak = 0.0f;
    for (int i = warmup; i < warmup + measure; ++i) {
        float in = amp * std::sin(2.0f * PI_F * freq * i / TEST_SR);
        float out = sat.process(in);
        inPeak = std::max(inPeak, std::abs(in));
        outPeak = std::max(outPeak, std::abs(out));
    }

    float gain = outPeak / inPeak;
    REQUIRE(gain > 0.9f);
    REQUIRE(gain < 1.1f);
}

TEST_CASE("OT saturation: high-level signal is compressed", "[ot_sat]") {
    OTSaturation sat;
    sat.recalcCoeffs(TEST_SR);

    // Full-power speaker output is ~1.5 (normalized domain).
    // At 100Hz: flux ≈ 1.5/(2π*100) ≈ 0.0024, drive*flux ≈ 16.7 → real compression
    float freq = 100.0f;
    float amp = 1.5f;
    int warmup = static_cast<int>(TEST_SR * 0.1f);
    int measure = static_cast<int>(TEST_SR * 0.1f);

    for (int i = 0; i < warmup; ++i)
        sat.process(amp * std::sin(2.0f * PI_F * freq * i / TEST_SR));

    float inPeak = 0.0f, outPeak = 0.0f;
    for (int i = warmup; i < warmup + measure; ++i) {
        float in = amp * std::sin(2.0f * PI_F * freq * i / TEST_SR);
        float out = sat.process(in);
        inPeak = std::max(inPeak, std::abs(in));
        outPeak = std::max(outPeak, std::abs(out));
    }

    // Output peak should be less than input peak (compression)
    REQUIRE(outPeak < inPeak);
}

TEST_CASE("OT saturation: bass saturates more than treble", "[ot_sat]") {
    // Same amplitude at 80Hz vs 2kHz — bass should have lower output ratio
    auto measureGain = [](float freq, float amp) {
        OTSaturation sat;
        sat.recalcCoeffs(TEST_SR);

        int warmup = static_cast<int>(TEST_SR * 0.1f);
        int measure = static_cast<int>(TEST_SR * 0.1f);

        for (int i = 0; i < warmup; ++i)
            sat.process(amp * std::sin(2.0f * PI_F * freq * i / TEST_SR));

        float inPeak = 0.0f, outPeak = 0.0f;
        for (int i = warmup; i < warmup + measure; ++i) {
            float in = amp * std::sin(2.0f * PI_F * freq * i / TEST_SR);
            float out = sat.process(in);
            inPeak = std::max(inPeak, std::abs(in));
            outPeak = std::max(outPeak, std::abs(out));
        }
        return outPeak / inPeak;
    };

    float amp = 1.5f;  // realistic speaker coupling output level
    float bassGain = measureGain(80.0f, amp);
    float trebleGain = measureGain(2000.0f, amp);

    // Bass should be more compressed (lower gain) than treble
    REQUIRE(bassGain < trebleGain);
}

TEST_CASE("OT saturation: reset clears state", "[ot_sat]") {
    OTSaturation sat;
    sat.recalcCoeffs(TEST_SR);

    // Process some signal to build up state
    for (int i = 0; i < 2000; ++i)
        sat.process(2.0f * std::sin(2.0f * PI_F * 200.0f * i / TEST_SR));

    sat.reset();

    // After reset, zero input should give zero output
    float out = sat.process(0.0f);
    REQUIRE(out == Approx(0.0f).margin(1e-10f));
}

TEST_CASE("OT saturation: sustained DC input never blows up the integrator", "[ot_sat]") {
    OTSaturation sat;
    sat.recalcCoeffs(TEST_SR);

    // Pathological constant DC offset — the asymmetric-tube-DC case the DC block
    // is meant to bleed out. Output must stay finite and flux must stay clamped.
    for (int i = 0; i < 200000; ++i) {
        float out = sat.process(5.0f);
        REQUIRE(std::isfinite(out));
    }
    REQUIRE(std::isfinite(sat.flux));
    REQUIRE(std::abs(sat.flux) <= OT_SAT_FLUX_MAX);
}
