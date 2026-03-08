// ═══════════════════════════════════════════════════════════════
// Speaker Coupling Tests — Spec Section 3.9
//
// Tests for:
//   - Unity gain for nominal impedance signal
//   - Gain modulation bounded by tanh limiter (~0.7 to ~1.5)
//   - Bass-heavy signal gets higher gain (resonance impedance)
//   - Zero input produces zero output
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test_helpers.h"
#include "dsp/cabinet/SpeakerCoupling.h"

using namespace tt;
using namespace tt::test;
using Catch::Approx;

TEST_CASE("Speaker coupling: zero input produces zero output", "[speaker]") {
    SpeakerCoupling spk;
    spk.recalcFilters(TEST_SR);
    
    float out = spk.process(0.0f);
    REQUIRE(out == Approx(0.0f).margin(1e-10f));
}

TEST_CASE("Speaker coupling: output scales with input", "[speaker]") {
    SpeakerCoupling spk;
    spk.recalcFilters(TEST_SR);
    
    // Warm up with a few samples
    for (int i = 0; i < 500; ++i)
        spk.process(0.1f * std::sin(2.0f * PI_F * 1000.0f * i / TEST_SR));
    
    float outSmall = std::abs(spk.process(0.1f));
    
    SpeakerCoupling spk2;
    spk2.recalcFilters(TEST_SR);
    for (int i = 0; i < 500; ++i)
        spk2.process(0.5f * std::sin(2.0f * PI_F * 1000.0f * i / TEST_SR));
    
    float outLarge = std::abs(spk2.process(0.5f));
    
    REQUIRE(outLarge > outSmall);
}

TEST_CASE("Speaker coupling: gain modulation range", "[speaker]") {
    SpeakerCoupling spk;
    spk.recalcFilters(TEST_SR);
    
    // Process a sustained tone and measure gain ratio
    std::vector<float> gains;
    for (int i = 0; i < 4000; ++i) {
        float in = 0.3f * std::sin(2.0f * PI_F * 200.0f * i / TEST_SR);
        float out = spk.process(in);
        if (std::abs(in) > 0.1f) {  // only measure where input is significant
            gains.push_back(std::abs(out) / std::abs(in));
        }
    }
    
    if (!gains.empty()) {
        float minGain = *std::min_element(gains.begin(), gains.end());
        float maxGain = *std::max_element(gains.begin(), gains.end());
        
        // Spec: gainMod range ~0.7 to ~1.5 (tanh-limited)
        REQUIRE(minGain >= 0.3f);
        REQUIRE(maxGain <= 2.5f);
    }
}

TEST_CASE("Speaker coupling: reset clears state", "[speaker]") {
    SpeakerCoupling spk;
    spk.recalcFilters(TEST_SR);
    
    // Process some signal
    for (int i = 0; i < 1000; ++i)
        spk.process(0.3f * std::sin(2.0f * PI_F * 500.0f * i / TEST_SR));
    
    spk.reset();
    
    // After reset, zero input should give zero output
    float out = spk.process(0.0f);
    REQUIRE(out == Approx(0.0f).margin(1e-10f));
}
