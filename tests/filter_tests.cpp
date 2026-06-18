// ═══════════════════════════════════════════════════════════════
// Filter Tests — Spec Section 2.6.2
//
// Tests for:
//   - OnePole LP/HP frequency response at DC, passband, and Nyquist
//   - BLT pre-warping (setFreq vs setFreqFast)
//   - Biquad RBJ designs (LP, HP, peak, notch)
//   - Anti-denormal injection prevents denormals in filter state
//   - FilterBank initialization gives valid coefficients
//   - Circuit-specific filter frequencies from spec
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test_helpers.h"
#include "dsp/filters/OnePole.h"
#include "dsp/filters/Biquad.h"
#include "dsp/filters/DynamicMillerLP.h"

using namespace sr;
using namespace sr::test;
using Catch::Approx;

// ── OnePole LP ───────────────────────────────────────────────

TEST_CASE("OnePoleLP: passes DC (magnitude ~ 1.0)", "[filter][onepole]") {
    OnePoleLP lp;
    lp.setFreq(1000.0f, TEST_SR);
    
    // Process DC=1.0 for enough samples to settle
    float out = 0.0f;
    for (int i = 0; i < 1000; ++i) out = lp.process(1.0f);
    REQUIRE(out == Approx(1.0f).margin(0.01f));
}

TEST_CASE("OnePoleLP: attenuates well above cutoff", "[filter][onepole]") {
    OnePoleLP lp;
    lp.setFreq(100.0f, TEST_SR);  // 100Hz cutoff
    
    float mag = measureMagnitude(lp, 5000.0f, TEST_SR);
    REQUIRE(mag < 0.1f);  // should be well attenuated at 50x cutoff
}

TEST_CASE("OnePoleLP: -3dB near cutoff frequency", "[filter][onepole]") {
    OnePoleLP lp;
    float fc = 1000.0f;
    lp.setFreq(fc, TEST_SR);
    
    float mag = measureMagnitude(lp, fc, TEST_SR);
    // -3dB = 0.707
    REQUIRE(mag > 0.6f);
    REQUIRE(mag < 0.8f);
}

// ── OnePole HP ───────────────────────────────────────────────

TEST_CASE("OnePoleHP: blocks DC", "[filter][onepole]") {
    OnePoleHP hp;
    hp.setFreq(100.0f, TEST_SR);
    
    float out = 0.0f;
    for (int i = 0; i < 5000; ++i) out = hp.process(1.0f);
    REQUIRE(std::abs(out) < 0.01f);
}

TEST_CASE("OnePoleHP: passes signals well above cutoff", "[filter][onepole]") {
    OnePoleHP hp;
    hp.setFreq(100.0f, TEST_SR);
    
    float mag = measureMagnitude(hp, 5000.0f, TEST_SR);
    REQUIRE(mag > 0.9f);
}

TEST_CASE("OnePoleHP: -3dB near cutoff frequency", "[filter][onepole]") {
    OnePoleHP hp;
    float fc = 1000.0f;
    hp.setFreq(fc, TEST_SR);
    
    float mag = measureMagnitude(hp, fc, TEST_SR);
    REQUIRE(mag > 0.6f);
    REQUIRE(mag < 0.8f);
}

// ── Circuit-specific filter checks ───────────────────────────

TEST_CASE("Input HP (7Hz): passes 1kHz guitar signal", "[filter][circuit]") {
    OnePoleHP hp;
    hp.setFreq(INPUT_HP_FREQ, TEST_SR);  // 7Hz — coupling cap DC block

    float mag = measureMagnitude(hp, 1000.0f, TEST_SR);
    REQUIRE(mag > 0.99f);  // essentially unity at 1kHz
}

TEST_CASE("Input HP (7Hz): passes 20Hz bass", "[filter][circuit]") {
    OnePoleHP hp;
    hp.setFreq(INPUT_HP_FREQ, TEST_SR);

    float mag = measureMagnitude(hp, 20.0f, TEST_SR, 30);
    REQUIRE(mag > 0.9f);  // 20Hz is well above 7Hz cutoff
}

TEST_CASE("Input HP (7Hz): attenuates sub-bass at 2Hz", "[filter][circuit]") {
    OnePoleHP hp;
    hp.setFreq(INPUT_HP_FREQ, TEST_SR);

    float mag = measureMagnitude(hp, 2.0f, TEST_SR, 60);
    REQUIRE(mag < 0.35f);
}

TEST_CASE("Cathode bypass shelf: reduces LF gain, passes audio band", "[filter][circuit]") {
    OnePoleLP lp;
    lp.setFreq(CATHODE_BYPASS_FREQ, TEST_SR);

    auto shelfProcess = [&](float x) {
        return x - lp.process(x) * CATHODE_BYPASS_DEPTH;
    };

    // Audio freq (100Hz) — should pass near unity
    lp.reset();
    float audioMag = measureMagnitudeCustom(shelfProcess, 100.0f, TEST_SR);
    REQUIRE(audioMag > 0.95f);

    // Sub-bass (0.5Hz) — should be attenuated to ~0.29
    lp.reset();
    float lfMag = measureMagnitudeCustom(shelfProcess, 0.5f, TEST_SR, 5);
    REQUIRE(lfMag < 0.35f);
    REQUIRE(lfMag > 0.25f);
}

TEST_CASE("Stage 1 coupling HP (72Hz): attenuates 20Hz bass", "[filter][circuit]") {
    OnePoleHP hp;
    hp.setFreq(STAGE1_COUPLING_HP, TEST_SR);  // 72Hz
    
    float mag20 = measureMagnitude(hp, 20.0f, TEST_SR, 30);
    REQUIRE(mag20 < 0.4f);
    
    float mag1k = measureMagnitude(hp, 1000.0f, TEST_SR);
    REQUIRE(mag1k > 0.95f);
}

// ── BLT pre-warping vs fast approximation ────────────────────

TEST_CASE("OnePole: setFreq and setFreqFast give similar results at low f", "[filter][onepole]") {
    OnePoleLP exact, fast;
    float fc = 100.0f;
    exact.setFreq(fc, TEST_SR);
    fast.setFreqFast(fc, TEST_SR);
    
    // At low frequencies relative to SR, the approximation is accurate
    REQUIRE(exact.b0 == Approx(fast.b0).margin(0.001f));
}

TEST_CASE("OnePole: setFreq diverges from setFreqFast near Nyquist", "[filter][onepole]") {
    OnePoleLP exact, fast;
    float fc = 15000.0f;  // high relative to 44.1kHz
    exact.setFreq(fc, TEST_SR);
    fast.setFreqFast(fc, TEST_SR);
    
    // At high frequencies, tan() pre-warping matters
    // The coefficients should differ measurably
    float diff = std::abs(exact.b0 - fast.b0);
    REQUIRE(diff > 0.001f);
}

// ── Biquad LP ────────────────────────────────────────────────

TEST_CASE("Biquad LP: passes DC", "[filter][biquad]") {
    Biquad lp;
    lp.makeLowPass(1000.0f, 0.707f, TEST_SR);
    
    float out = 0.0f;
    for (int i = 0; i < 2000; ++i) out = lp.process(1.0f);
    REQUIRE(out == Approx(1.0f).margin(0.02f));
}

TEST_CASE("Biquad LP: attenuates above cutoff", "[filter][biquad]") {
    Biquad lp;
    lp.makeLowPass(1000.0f, 0.707f, TEST_SR);
    
    float mag = measureMagnitude(lp, 5000.0f, TEST_SR);
    REQUIRE(mag < 0.1f);  // 2nd order rolls off faster than 1st
}

// ── Biquad HP ────────────────────────────────────────────────

TEST_CASE("Biquad HP: blocks DC", "[filter][biquad]") {
    Biquad hp;
    hp.makeHighPass(100.0f, 0.707f, TEST_SR);
    
    float out = 0.0f;
    for (int i = 0; i < 5000; ++i) out = hp.process(1.0f);
    REQUIRE(std::abs(out) < 0.01f);
}

// ── Biquad notch ─────────────────────────────────────────────

TEST_CASE("Biquad notch: attenuates at center frequency", "[filter][biquad]") {
    Biquad notch;
    notch.makeNotch(400.0f, 2.0f, TEST_SR);
    
    float magCenter = measureMagnitude(notch, 400.0f, TEST_SR);
    float magAway   = measureMagnitude(notch, 2000.0f, TEST_SR);
    
    REQUIRE(magCenter < 0.2f);
    REQUIRE(magAway > 0.8f);
}

// ── Biquad peak EQ ──────────────────────────────────────────

TEST_CASE("Biquad peak: boosts at center frequency", "[filter][biquad]") {
    Biquad peak;
    peak.makePeakEQ(1000.0f, 1.0f, 6.0f, TEST_SR);  // +6dB
    
    float magCenter = measureMagnitude(peak, 1000.0f, TEST_SR);
    float magAway   = measureMagnitude(peak, 100.0f, TEST_SR, 30);
    
    REQUIRE(magCenter > 1.5f);  // ~+6dB
    REQUIRE(magAway < 1.2f);    // minimal boost away from center
}

// ── Anti-denormal ────────────────────────────────────────────

TEST_CASE("Filter state doesn't produce denormals after silence", "[filter][denormal]") {
    OnePoleLP lp;
    lp.setFreq(1000.0f, TEST_SR);
    
    // Process a burst then silence
    for (int i = 0; i < 100; ++i) lp.process(0.5f);
    for (int i = 0; i < 100000; ++i) lp.process(0.0f);
    
    // State should not be denormal
    REQUIRE(std::fpclassify(lp.z1) != FP_SUBNORMAL);
}

// ── DynamicMillerLP ──────────────────────────────────────────

TEST_CASE("Miller LP: higher gain estimate lowers cutoff", "[filter][miller]") {
    DynamicMillerLP miller;
    miller.init(CGP_12AX7, CGK_12AX7, V1A_RSOURCE_REAL, StageGains::V1A_GAIN);
    miller.recalc(TEST_SR);
    
    // Process a signal with low gain estimate
    float outLow = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float in = std::sin(2.0f * PI_F * 10000.0f * i / TEST_SR);
        outLow = miller.process(in, 20.0f, TEST_SR);  // low gain → high fc → more HF
    }
    
    miller.reset();
    
    // Process same signal with high gain estimate
    float outHigh = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float in = std::sin(2.0f * PI_F * 10000.0f * i / TEST_SR);
        outHigh = miller.process(in, 70.0f, TEST_SR);  // high gain → low fc → less HF
    }
    
    // Higher gain estimate should give more HF attenuation → lower output at 10kHz
    REQUIRE(std::abs(outHigh) < std::abs(outLow));
}

// ── OT leakage filter (7kHz LP) ─────────────────────────────

TEST_CASE("OT leakage LP (7kHz): passes guitar fundamentals", "[filter][circuit]") {
    OnePoleLP lp;
    lp.setFreq(OT_LP_FREQ, TEST_SR);  // 7000Hz
    
    float mag1k = measureMagnitude(lp, 1000.0f, TEST_SR);
    REQUIRE(mag1k > 0.95f);
}

TEST_CASE("OT leakage LP (7kHz): attenuates ultrasonic", "[filter][circuit]") {
    OnePoleLP lp;
    lp.setFreq(OT_LP_FREQ, TEST_SR);
    
    float mag15k = measureMagnitude(lp, 15000.0f, TEST_SR);
    REQUIRE(mag15k < 0.6f);
}

// ── Filter reset ─────────────────────────────────────────────

TEST_CASE("OnePole: reset clears state", "[filter]") {
    OnePoleLP lp;
    lp.setFreq(1000.0f, TEST_SR);
    for (int i = 0; i < 100; ++i) lp.process(1.0f);
    REQUIRE(lp.z1 != 0.0f);
    
    lp.reset();
    REQUIRE(lp.z1 == 0.0f);
}

TEST_CASE("Biquad: reset clears state", "[filter]") {
    Biquad b;
    b.makeLowPass(1000.0f, 0.707f, TEST_SR);
    for (int i = 0; i < 100; ++i) b.process(1.0f);
    
    b.reset();
    REQUIRE(b.z1 == 0.0f);
    REQUIRE(b.z2 == 0.0f);
}
