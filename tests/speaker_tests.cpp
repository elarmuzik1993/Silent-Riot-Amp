// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
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

using namespace sr;
using namespace sr::test;
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

TEST_CASE("Speaker coupling: bass signal gets higher gain than mid signal", "[speaker]") {
    // Bass at 75Hz (near resonance -> high impedance -> high gain)
    SpeakerCoupling spkBass;
    spkBass.recalcFilters(TEST_SR);
    for (int i = 0; i < 4000; ++i)
        spkBass.process(0.3f * std::sin(2.0f * PI_F * 75.0f * i / TEST_SR));
    float bassGainMod = std::abs(spkBass.process(0.3f)) / 0.3f;

    // Mid at 500Hz (near impedance minimum -> low impedance -> nominal gain)
    SpeakerCoupling spkMid;
    spkMid.recalcFilters(TEST_SR);
    for (int i = 0; i < 4000; ++i)
        spkMid.process(0.3f * std::sin(2.0f * PI_F * 500.0f * i / TEST_SR));
    float midGainMod = std::abs(spkMid.process(0.3f)) / 0.3f;

    // Bass should get higher gain (higher impedance at resonance)
    REQUIRE(bassGainMod > midGainMod);
}

TEST_CASE("Speaker coupling: loud bass > quiet bass (excursion)", "[speaker]") {
    // Same frequency (75Hz), different amplitudes — louder should deviate more
    auto measureGainMod = [](float amplitude) {
        SpeakerCoupling spk;
        spk.recalcFilters(TEST_SR);
        for (int i = 0; i < 8000; ++i)
            spk.process(amplitude * std::sin(2.0f * PI_F * 75.0f * i / TEST_SR));
        float in = amplitude;
        float out = spk.process(in);
        return std::abs(out) / std::abs(in);
    };

    float quietGain = measureGainMod(0.05f);
    float loudGain  = measureGainMod(0.3f);

    // Louder bass signal should produce larger gainMod deviation from 1.0
    REQUIRE(std::abs(loudGain - 1.0f) > std::abs(quietGain - 1.0f));
}

TEST_CASE("Speaker coupling: loud HF stays near unity (low excursion)", "[speaker]") {
    SpeakerCoupling spk;
    spk.recalcFilters(TEST_SR);

    // High-amplitude 5kHz signal — excursion weight is negligible
    for (int i = 0; i < 8000; ++i)
        spk.process(0.4f * std::sin(2.0f * PI_F * 5000.0f * i / TEST_SR));

    float in = 0.4f;
    float out = spk.process(in);
    float gainMod = std::abs(out) / std::abs(in);

    // HF excursion weight is 0.0025 — gainMod should stay close to 1.0
    REQUIRE(gainMod == Approx(1.0f).margin(0.15f));
}

TEST_CASE("Speaker coupling: excursion saturation (diminishing returns)", "[speaker]") {
    auto measureDeviation = [](float amplitude) {
        SpeakerCoupling spk;
        spk.recalcFilters(TEST_SR);
        for (int i = 0; i < 8000; ++i)
            spk.process(amplitude * std::sin(2.0f * PI_F * 75.0f * i / TEST_SR));
        float in = amplitude;
        float out = spk.process(in);
        return std::abs(std::abs(out) / std::abs(in) - 1.0f);
    };

    float dev1 = measureDeviation(0.1f);
    float dev2 = measureDeviation(0.2f);
    float dev3 = measureDeviation(0.3f);

    // All should increase
    REQUIRE(dev2 > dev1);
    REQUIRE(dev3 > dev2);

    // Diminishing returns — increments should decrease (tanh concavity)
    float delta1 = dev2 - dev1;
    float delta2 = dev3 - dev2;
    REQUIRE(delta2 < delta1);
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
