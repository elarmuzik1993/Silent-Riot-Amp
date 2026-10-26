// ═══════════════════════════════════════════════════════════════
// Idle operating point (review B1) — dsp/IdleOperatingPoint.h
//
// prepare used to start the amp from zeroed filters and a nominal supply, so
// it replayed ~0.5 s of settling as a burst on silence. These tests pin that
// the seeded state IS the state the amp settles to, so silence after a
// prepare stays silent from the first sample.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test_helpers.h"
#include "dsp/IdleOperatingPoint.h"

using namespace sr;
using namespace sr::test;
using Catch::Approx;

namespace {

// One oversampled sample of the processor's power stage on silence: PSU update
// with last sample's draw, both EL84 grids at 0 V, raw currents to PSU + sag.
void powerStageIdleTick(float bias, PowerSupplyModel& psu, SagEnvelope& sag) {
    psu.update();
    psu.resetCurrentDraw();
    const PentodeOutput t = pentodeWaveshaper(0.0f, bias + sag.biasOffset(), psu.vPower(), psu.vScreen());
    const float plate = 2.0f * std::abs(t.plateCurrent);
    psu.reportPowerDirect(plate, 2.0f * t.screenCurrent);
    sag.update(plate);
}

struct PowerStage {
    PowerSupplyModel psu;
    SagEnvelope sag;
    explicit PowerStage(float bias) {
        psu.init();
        psu.recalcDecayCoeffs(TEST_SR_4X);
        sag.recalcCoeffs(TEST_SR_4X);
        // The processor's reference (taken at nominal rails; review B2 will move it).
        sag.setQuiescent(pentodeQuiescentTotal(bias, V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N));
    }
};

} // namespace

TEST_CASE("Idle: power-stage solve converges to where silence settles", "[idle][seed]") {
    for (float bias : { PENTODE_BIAS_15W_N, PENTODE_BIAS_7W_N }) {
        PowerStage seeded(bias);
        REQUIRE(settlePowerStageAtIdle(bias, seeded.psu, seeded.sag) < 60);

        // The old start: nominal supply, envelope at the reference. 3 s of silence.
        PowerStage settled(bias);
        settled.psu.reset();
        settled.sag.reset();
        for (int i = 0; i < static_cast<int>(TEST_SR_4X * 3.0f); ++i)
            powerStageIdleTick(bias, settled.psu, settled.sag);

        REQUIRE(seeded.psu.vPower()  == Approx(settled.psu.vPower()).epsilon(1e-3));
        REQUIRE(seeded.psu.vScreen() == Approx(settled.psu.vScreen()).epsilon(1e-3));
        REQUIRE(seeded.psu.vPi()     == Approx(settled.psu.vPi()).epsilon(1e-3));
        REQUIRE(seeded.psu.vPreamp() == Approx(settled.psu.vPreamp()).epsilon(1e-3));
        // The float envelope creeps toward its target and stalls short of it,
        // which leaves the simulated rails up to ~0.1 V off the exact root.
        REQUIRE(VS::toVolts(seeded.sag.biasOffset())
                == Approx(VS::toVolts(settled.sag.biasOffset())).margin(0.01));
    }
}

TEST_CASE("Idle: the seeded power stage does not move on silence", "[idle][seed]") {
    for (float bias : { PENTODE_BIAS_15W_N, PENTODE_BIAS_7W_N }) {
        PowerStage ps(bias);
        settlePowerStageAtIdle(bias, ps.psu, ps.sag);
        const float p = ps.psu.vPower(), pre = ps.psu.vPreamp(), off = ps.sag.biasOffset();
        for (int i = 0; i < static_cast<int>(TEST_SR_4X * 0.5f); ++i)
            powerStageIdleTick(bias, ps.psu, ps.sag);
        REQUIRE(VS::toVolts(ps.psu.vPower() - p)    == Approx(0.0).margin(0.01));
        REQUIRE(VS::toVolts(ps.psu.vPreamp() - pre) == Approx(0.0).margin(0.01));
        REQUIRE(VS::toVolts(ps.sag.biasOffset() - off) == Approx(0.0).margin(0.01));
    }
}

TEST_CASE("Idle: seeding keeps today's settled operating point", "[idle][seed]") {
    // Review B2's table: these are where the shipped amp settles on silence.
    // B2 moves the sag reference on purpose; update these with it.
    PowerStage w15(PENTODE_BIAS_15W_N);
    settlePowerStageAtIdle(PENTODE_BIAS_15W_N, w15.psu, w15.sag);
    REQUIRE(VS::toVolts(w15.psu.vPower())     == Approx(282.2).margin(0.2));
    REQUIRE(VS::toVolts(w15.psu.vScreen())    == Approx(257.9).margin(0.2));
    REQUIRE(VS::toVolts(w15.sag.biasOffset()) == Approx(0.774).margin(0.01));

    PowerStage w7(PENTODE_BIAS_7W_N);
    settlePowerStageAtIdle(PENTODE_BIAS_7W_N, w7.psu, w7.sag);
    REQUIRE(VS::toVolts(w7.psu.vPower())      == Approx(274.4).margin(0.2));
    REQUIRE(VS::toVolts(w7.psu.vScreen())     == Approx(248.2).margin(0.2));
    REQUIRE(VS::toVolts(w7.sag.biasOffset())  == Approx(1.030).margin(0.01));
}

TEST_CASE("Idle: seeded preamp and PI pass no step on silence", "[idle][seed]") {
    // The processor's order: triode -> cathode-bypass shelf -> coupling HP, and
    // LTP -> PI coupling HPs. Unseeded, each plate's DC comes through as a step.
    auto run = [](bool seed) {
        FilterBank fb;
        fb.recalcAll(TEST_SR, 4);
        fb.resetAll();
        PowerSupplyModel psu;
        psu.init();
        SagEnvelope sag;
        sag.setQuiescent(pentodeQuiescentTotal(PENTODE_BIAS_15W_N, V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N));
        settlePowerStageAtIdle(PENTODE_BIAS_15W_N, psu, sag);
        TriodeState v1a, v1b;
        LTPModel ltp;
        ltp.init();
        if (seed) seedPreampAtIdle(fb, v1a, v1b, ltp, psu);

        float peak = 0.0f;
        for (int i = 0; i < static_cast<int>(TEST_SR_4X * 0.5f); ++i) {
            float x = triodeWaveshaper(0.0f, VS::V_BIAS_12AX7, psu.vPreamp(), PREAMP_RPLATE_N, v1a);
            x -= fb.v1a_cathodeBypass.process(x) * CATHODE_BYPASS_DEPTH;
            x = fb.v1a_couplingHP.process(x);
            float y = triodeWaveshaper(0.0f, VS::V_BIAS_12AX7, psu.vPreamp(), PREAMP_RPLATE_N, v1b);
            y -= fb.v1b_cathodeBypass.process(y) * CATHODE_BYPASS_DEPTH;
            y = fb.v1b_couplingHP.process(y);
            const LTPOutput pi = solveLTP(0.0f, psu.vPi(), ltp);
            const float a = fb.pi_couplingHP_A.process(pi.piA);
            const float b = fb.pi_couplingHP_B.process(pi.piB);
            peak = std::max({ peak, std::abs(x), std::abs(y), std::abs(a), std::abs(b) });
        }
        return VS::toVolts(peak);
    };
    REQUIRE(run(true) < 0.001f);    // < 1 mV on any coupling-cap output
    REQUIRE(run(false) > 10.0f);    // the step the seed removes: tens of volts
}
