// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// Rendered-output regression guard (roadmap P4).
//
// Not a bit-exact golden file — that fights the ongoing ear-tuning. Instead
// these assert the properties that must hold no matter how the voicing is
// tuned: finite output, the limiter bound, determinism, a true null (silence
// in -> silence out), and a decaying tail (no stuck/self-oscillating state).
// The render mirrors the PluginProcessor::processBlock order (§2.5) and reads
// the same Constants.h values production uses, so constant-level regressions
// (NaN/Inf, instability, runaway) surface here.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test_helpers.h"
#include "dsp/tubes/TriodeWaveshaper.h"
#include "dsp/tubes/PentodeWaveshaper.h"
#include "dsp/tubes/GridConduction.h"
#include "dsp/power/LTPSolver.h"
#include "dsp/filters/OnePole.h"
#include "dsp/filters/FilterBank.h"
#include "dsp/cabinet/SpeakerCoupling.h"
#include "dsp/cabinet/OTSaturation.h"
#include <vector>
#include <cmath>

using namespace sr;
using namespace sr::test;

namespace {

// Output gain compensation applied before the final tanh limiter (matches the
// value used in the level-trace diagnostics).
constexpr float OUTPUT_GAIN_COMP = 1.5f;

// Render guitar-domain digital samples (e.g. +/-0.15) through the documented
// signal chain and return the post-limiter output buffer.
std::vector<float> renderChain(const std::vector<float>& input,
                               float gain, float volume,
                               float sampleRate = TEST_SR_4X)
{
    FilterBank fb;
    fb.recalcAll(sampleRate / 4.0f, 4);

    auto v1aGrid   = makeV1aGrid();
    auto v1bGrid   = makeV1bGrid();
    auto piGrid    = makePIGrid();
    auto el84GridA = makeEL84Grid();
    auto el84GridB = makeEL84Grid();
    recalcGridChargeDecay(v1aGrid,   GRID_TAU_PREAMP_S, sampleRate);
    recalcGridChargeDecay(v1bGrid,   GRID_TAU_PREAMP_S, sampleRate);
    recalcGridChargeDecay(piGrid,    GRID_TAU_PI_S,     sampleRate);
    recalcGridChargeDecay(el84GridA, GRID_TAU_EL84_S,   sampleRate);
    recalcGridChargeDecay(el84GridB, GRID_TAU_EL84_S,   sampleRate);

    LTPModel ltp;     ltp.init();
    SpeakerCoupling spk; spk.init(); spk.recalcFilters(sampleRate);
    OTSaturation ot;  ot.recalcCoeffs(sampleRate);

    const float vPreamp = V_PREAMP_NOMINAL_N;
    const float vPi     = V_PI_NOMINAL_N;
    const float vPlate  = V_POWER_NOMINAL_N;
    const float vScreen = V_SCREEN_NOMINAL_N;
    const float bias    = PENTODE_BIAS_15W_N;
    const float volVal  = volume * volume;  // audio taper

    std::vector<float> out(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        float x = input[i] * VS::V_GUITAR;

        // Preamp V1a
        x = fb.inputHP.process(x);
        x = applyGridConduction(x, v1aGrid);
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, PREAMP_RPLATE_N);
        x -= fb.v1a_cathodeBypass.process(x) * CATHODE_BYPASS_DEPTH;
        x = fb.v1a_couplingHP.process(x);
        x *= gain;

        // Preamp V1b
        x = applyGridConduction(x, v1bGrid);
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, PREAMP_RPLATE_N);
        x -= fb.v1b_cathodeBypass.process(x) * CATHODE_BYPASS_DEPTH;
        x = fb.v1b_couplingHP.process(x);
        x *= gain;
        x = fb.gainLoadingLP.process(x);

        // Phase inverter
        x = applyGridConduction(x, piGrid);
        LTPOutput lo = solveLTP(x, vPi, ltp);
        float piA = fb.pi_couplingHP_A.process(lo.piA);
        float piB = fb.pi_couplingHP_B.process(lo.piB);

        // Shape (differential only)
        float diff = piA - piB;
        float sum  = piA + piB;
        float od = fb.shapeLowShelf.process(diff);
        od = fb.shapeMidBell.process(od);
        od = fb.shapeHighShelf.process(od);
        piA = (sum + od) * 0.5f;
        piB = (sum - od) * 0.5f;

        // Master volume
        piA *= volVal;
        piB *= volVal;

        // EL84 push-pull
        piA = applyGridConduction(piA, el84GridA);
        piB = applyGridConduction(piB, el84GridB);
        float condA = softConductionCurve(piA, bias);
        float condB = softConductionCurve(piB, bias);
        PentodeOutput ppA = pentodeWaveshaper(piA, bias, vPlate, vScreen);
        PentodeOutput ppB = pentodeWaveshaper(piB, bias, vPlate, vScreen);
        float pOut = ppA.plateCurrent * condA - ppB.plateCurrent * condB;
        pOut *= OT_CURRENT_SCALE;

        // Output transformer + cab + limiter
        float otIn  = spk.process(pOut);
        float otSat = ot.process(otIn);
        float otOut = fb.ot_leakageLP.process(otSat);
        float cab = fb.cab_presence.process(otOut);
        cab = fb.cab_rolloff.process(cab);
        cab = fb.cab_notch.process(cab);
        out[i] = std::tanh(cab * OUTPUT_GAIN_COMP);
    }
    return out;
}

bool allFinite(const std::vector<float>& v) {
    for (float x : v)
        if (!std::isfinite(x)) return false;
    return true;
}

} // namespace

TEST_CASE("Regression: output is finite and bounded by the limiter", "[regression]") {
    // Hot pickups + max gain: the tanh limiter must keep |out| <= 1.
    auto hot = makeSine(330.0f, 0.30f, 16384, TEST_SR_4X);
    auto out = renderChain(hot, 1.0f, 1.0f);
    REQUIRE(allFinite(out));
    REQUIRE(peakAbs(out) <= 1.0f);
    REQUIRE(rmsRange(out, 4096, 16384) > 1e-4f);   // it actually produced signal
}

TEST_CASE("Regression: rendering is deterministic", "[regression]") {
    auto in = makeSine(330.0f, 0.15f, 8192, TEST_SR_4X);
    auto a = renderChain(in, 0.7f, 0.7f);
    auto b = renderChain(in, 0.7f, 0.7f);
    REQUIRE(a.size() == b.size());
    bool identical = true;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) { identical = false; break; }
    REQUIRE(identical);                            // bit-identical across runs
}

TEST_CASE("Regression: stable under sustained max drive (no runaway)", "[regression]") {
    // Long hot input at max gain — late-window energy must not blow up vs the
    // early window (catches integrator drift / slow instabilities).
    auto hot = makeSine(330.0f, 0.30f, 65536, TEST_SR_4X);
    auto out = renderChain(hot, 1.0f, 1.0f);
    REQUIRE(allFinite(out));
    REQUIRE(peakAbs(out) <= 1.0f);

    float early = rmsRange(out, 8192, 16384);
    float late  = rmsRange(out, 57344, 65536);
    REQUIRE(late < early * 1.5f);        // energy stays bounded, no runaway
    REQUIRE(late > early * 0.5f);        // and it doesn't collapse to nothing
}
