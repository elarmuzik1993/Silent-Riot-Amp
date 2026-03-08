// ═══════════════════════════════════════════════════════════════
// PSU & Sag Tests — Spec Sections 3.8, 3.11
//
// Tests for:
//   - PSU init: all nodes at nominal voltage
//   - PSU droop: current draw reduces voltage (once implemented)
//   - PSU cascade: power node droop propagates downstream
//   - PSU recovery: voltages return to nominal without load
//   - SupplyNode tracking behavior
//   - Sag envelope: attack/release asymmetry
//   - Sag biasOffset: negative under load, zero at idle
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test_helpers.h"
#include "dsp/power/PowerSupplyModel.h"
#include "dsp/power/SagEnvelope.h"

using namespace tt;
using namespace tt::test;
using Catch::Approx;

// ── PSU initialization ───────────────────────────────────────

TEST_CASE("PSU: all nodes at nominal after init", "[psu]") {
    PowerSupplyModel psu;
    psu.init();
    
    REQUIRE(psu.vPower()  == Approx(V_POWER_NOMINAL_N));
    REQUIRE(psu.vScreen() == Approx(V_SCREEN_NOMINAL_N));
    REQUIRE(psu.vPi()     == Approx(V_PI_NOMINAL_N));
    REQUIRE(psu.vPreamp() == Approx(V_PREAMP_NOMINAL_N));
}

TEST_CASE("PSU: nominal voltages match spec reference table", "[psu]") {
    REQUIRE(V_POWER_NOMINAL_N  == Approx(1.0f));           // 300V
    REQUIRE(V_SCREEN_NOMINAL_N == Approx(0.9333f).margin(1e-3f));  // 280V
    REQUIRE(V_PI_NOMINAL_N     == Approx(0.9333f).margin(1e-3f));  // 280V
    REQUIRE(V_PREAMP_NOMINAL_N == Approx(0.8333f).margin(1e-3f));  // 250V
}

// ── SupplyNode tracking ──────────────────────────────────────

TEST_CASE("SupplyNode: tracks target voltage", "[psu][node]") {
    SupplyNode node;
    node.init(1.0f, 10.0f);  // nominal 1.0, tau 10ms
    node.recalcDecay(TEST_SR);
    
    // Track a lower target
    float target = 0.8f;
    for (int i = 0; i < 44100; ++i)  // 1 second
        node.track(target);
    
    // Should have converged
    REQUIRE(node.voltage == Approx(target).margin(0.001f));
}

TEST_CASE("SupplyNode: exponential approach (not instant)", "[psu][node]") {
    SupplyNode node;
    node.init(1.0f, 10.0f);
    node.recalcDecay(TEST_SR);
    
    // One sample of tracking shouldn't reach target
    node.track(0.5f);
    REQUIRE(node.voltage > 0.5f);  // not yet at target
    REQUIRE(node.voltage < 1.0f);  // moved toward target
}

TEST_CASE("SupplyNode: snap-to-target prevents denormals", "[psu][node]") {
    SupplyNode node;
    node.init(1.0f, 10.0f);
    node.recalcDecay(TEST_SR);
    
    // Track very close to target for a long time
    for (int i = 0; i < 500000; ++i)
        node.track(1.0f);
    
    // Should be exactly at target (snapped)
    REQUIRE(node.voltage == 1.0f);
}

TEST_CASE("SupplyNode: faster tau = faster tracking", "[psu][node]") {
    SupplyNode fast, slow;
    fast.init(1.0f, 6.0f);   // 6ms (screen)
    slow.init(1.0f, 80.0f);  // 80ms (preamp)
    fast.recalcDecay(TEST_SR);
    slow.recalcDecay(TEST_SR);
    
    float target = 0.8f;
    for (int i = 0; i < 441; ++i) {  // ~10ms
        fast.track(target);
        slow.track(target);
    }
    
    // Fast node should be closer to target
    float fastDist = std::abs(fast.voltage - target);
    float slowDist = std::abs(slow.voltage - target);
    REQUIRE(fastDist < slowDist);
}

// ── PSU update with no load ──────────────────────────────────

TEST_CASE("PSU: stable at nominal with no current draw", "[psu]") {
    PowerSupplyModel psu;
    psu.init();
    psu.recalcDecayCoeffs(TEST_SR);
    
    for (int i = 0; i < 44100; ++i) {
        psu.resetCurrentDraw();
        psu.update();
    }
    
    // All nodes should still be at nominal
    REQUIRE(psu.vPower()  == Approx(V_POWER_NOMINAL_N).margin(0.001f));
    REQUIRE(psu.vScreen() == Approx(V_SCREEN_NOMINAL_N).margin(0.001f));
    REQUIRE(psu.vPi()     == Approx(V_PI_NOMINAL_N).margin(0.001f));
    REQUIRE(psu.vPreamp() == Approx(V_PREAMP_NOMINAL_N).margin(0.001f));
}

// ── PSU current reporting ────────────────────────────────────

TEST_CASE("PSU: resetCurrentDraw clears accumulators", "[psu]") {
    PowerSupplyModel psu;
    psu.init();
    
    psu.reportPreampLevel(0.1f);
    psu.reportPreampLevel(0.1f);
    psu.reportPiLevel(0.2f);
    
    psu.resetCurrentDraw();
    
    REQUIRE(psu.preampCurrentAccum == 0.0f);
    REQUIRE(psu.piCurrentAccum == 0.0f);
}

TEST_CASE("PSU: preamp current accumulates across two stages", "[psu]") {
    PowerSupplyModel psu;
    psu.init();
    psu.resetCurrentDraw();
    
    psu.reportPreampLevel(0.1f);  // V1a
    float after1 = psu.preampCurrentAccum;
    
    psu.reportPreampLevel(0.1f);  // V1b
    float after2 = psu.preampCurrentAccum;
    
    REQUIRE(after2 > after1);
    REQUIRE(after2 == Approx(after1 * 2.0f).margin(1e-6f));
}

// ── PSU recalc ───────────────────────────────────────────────

TEST_CASE("PSU: recalc changes decay coefficients", "[psu]") {
    PowerSupplyModel psu;
    psu.init();
    
    float decayBefore = psu.powerNode.decay;
    psu.recalcDecayCoeffs(TEST_SR);
    
    // After recalc, decay should be different from the init default (0.999)
    REQUIRE(psu.powerNode.decay != Approx(decayBefore).margin(1e-5f));
}

// ═══════════════════════════════════════════════════════════════
// Sag Envelope — Spec Section 3.8
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Sag: zero at idle", "[sag]") {
    SagEnvelope sag;
    sag.recalcCoeffs(TEST_SR);
    
    REQUIRE(sag.envelope == 0.0f);
    REQUIRE(sag.biasOffset() == 0.0f);
}

TEST_CASE("Sag: envelope increases under load", "[sag]") {
    SagEnvelope sag;
    sag.recalcCoeffs(TEST_SR);
    
    for (int i = 0; i < 1000; ++i)
        sag.update(0.5f);
    
    REQUIRE(sag.envelope > 0.0f);
}

TEST_CASE("Sag: biasOffset is negative under load", "[sag]") {
    SagEnvelope sag;
    sag.recalcCoeffs(TEST_SR);
    
    for (int i = 0; i < 1000; ++i)
        sag.update(0.5f);
    
    // Spec: "returns NEGATIVE bias offset (shifts bias colder)"
    REQUIRE(sag.biasOffset() < 0.0f);
}

TEST_CASE("Sag: biasOffset magnitude bounded by depth * envelope", "[sag]") {
    SagEnvelope sag;
    sag.recalcCoeffs(TEST_SR);
    
    for (int i = 0; i < 10000; ++i)
        sag.update(1.0f);
    
    // biasOffset = -depth * envelope, envelope tracks |powerOut|
    // Max envelope ≈ input level = 1.0, so max offset ≈ -SAG_DEPTH
    REQUIRE(sag.biasOffset() >= -SAG_DEPTH - 0.01f);
}

TEST_CASE("Sag: attack is faster than release", "[sag]") {
    SagEnvelope sag;
    sag.recalcCoeffs(TEST_SR);
    
    // Attack: ramp up
    for (int i = 0; i < 500; ++i)
        sag.update(0.5f);
    float envAfterAttack = sag.envelope;
    
    // Release: let it decay from same starting point
    float envAfterRelease = envAfterAttack;
    SagEnvelope sagRelease;
    sagRelease.recalcCoeffs(TEST_SR);
    sagRelease.envelope = envAfterAttack;
    for (int i = 0; i < 500; ++i)
        sagRelease.update(0.0f);
    envAfterRelease = sagRelease.envelope;
    
    // Attack: went from 0 → envAfterAttack in 500 samples
    // Release: went from envAfterAttack → envAfterRelease in 500 samples
    float attackDelta = envAfterAttack;
    float releaseDelta = envAfterAttack - envAfterRelease;
    
    // Attack 10ms vs Release 100ms → attack should cover more ground
    REQUIRE(attackDelta > releaseDelta);
}

TEST_CASE("Sag: envelope decays to zero after signal stops", "[sag]") {
    SagEnvelope sag;
    sag.recalcCoeffs(TEST_SR);
    
    // Drive hard
    for (int i = 0; i < 10000; ++i)
        sag.update(0.5f);
    REQUIRE(sag.envelope > 0.0f);
    
    // Remove signal, wait for decay
    for (int i = 0; i < 500000; ++i)
        sag.update(0.0f);
    
    // Should have snapped to zero
    REQUIRE(sag.envelope == 0.0f);
}

TEST_CASE("Sag: responds to signal magnitude, not sign", "[sag]") {
    SagEnvelope sagPos, sagNeg;
    sagPos.recalcCoeffs(TEST_SR);
    sagNeg.recalcCoeffs(TEST_SR);
    
    for (int i = 0; i < 1000; ++i) {
        sagPos.update(0.5f);
        sagNeg.update(-0.5f);
    }
    
    // Envelope tracks abs(powerOut), so should be the same
    REQUIRE(sagPos.envelope == Approx(sagNeg.envelope).margin(1e-6f));
}
