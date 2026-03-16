// Level trace diagnostic — measures signal levels through the full DSP chain
// to calibrate outputGainCompensation.

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

#include <cstdio>

using namespace tt;
using namespace tt::test;

// Simulate the full signal chain for N samples of a sine wave at given gain/volume,
// return peak |output| at each stage.
struct LevelTrace {
    float v1aOut, gangAOut, v1bOut, gangBOut;
    float ltpPiA, ltpPiB;          // after HP coupling
    float powerOut;                  // push-pull differential
    float speakerOut, otOut, cabOut;
    float finalOut;                  // after gain comp + tanh
};

static LevelTrace traceChain(float digitalPeak, float gain, float volume,
                             float sampleRate = TEST_SR_4X, int numSamples = 44100)
{
    // Init DSP
    FilterBank fb;
    fb.recalcAll(sampleRate / 4.0f, 4);  // base SR, 4x OS

    auto v1aGrid  = makeV1aGrid();
    auto v1bGrid  = makeV1bGrid();
    auto piGrid   = makePIGrid();
    auto el84GridA = makeEL84Grid();
    auto el84GridB = makeEL84Grid();
    recalcGridChargeDecay(v1aGrid,  0.022f, sampleRate);
    recalcGridChargeDecay(v1bGrid,  0.022f, sampleRate);
    recalcGridChargeDecay(piGrid,   0.047f, sampleRate);
    recalcGridChargeDecay(el84GridA, 0.100f, sampleRate);
    recalcGridChargeDecay(el84GridB, 0.100f, sampleRate);

    LTPModel ltpModel;
    ltpModel.init();

    SpeakerCoupling spk;
    spk.init();
    spk.recalcFilters(sampleRate);

    OTSaturation ot;
    ot.recalcCoeffs(sampleRate);

    float vPreamp = V_PREAMP_NOMINAL_N;
    float vPi     = V_PI_NOMINAL_N;
    float vPlate  = V_POWER_NOMINAL_N;
    float vScreen = V_SCREEN_NOMINAL_N;
    float bias    = PENTODE_BIAS_15W_N;

    float gainVal = gain;
    float volVal  = volume * volume;  // audioTaper

    // Peak trackers (skip first 10% for settling)
    int settleEnd = numSamples / 10;
    LevelTrace peak{};

    for (int i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float guitar = digitalPeak * std::sin(2.0f * PI_F * 330.0f * t);  // low E
        float x = guitar * VS::V_GUITAR;

        // V1a
        x = fb.inputHP.process(x);
        x = applyGridConduction(x, v1aGrid);
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, StageGains::V1A_GAIN);
        x = std::clamp(x, -vPreamp * HEADROOM_FACTOR, vPreamp * HEADROOM_FACTOR);
        x = fb.v1a_couplingHP.process(x);

        if (i > settleEnd) peak.v1aOut = std::max(peak.v1aOut, std::abs(x));

        // Gang A
        float gainA = gainVal;
        x *= gainA;
        if (i > settleEnd) peak.gangAOut = std::max(peak.gangAOut, std::abs(x));

        // V1b
        x = applyGridConduction(x, v1bGrid);
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, StageGains::V1B_GAIN);
        x = std::clamp(x, -vPreamp * HEADROOM_FACTOR, vPreamp * HEADROOM_FACTOR);
        x = fb.v1b_couplingHP.process(x);

        if (i > settleEnd) peak.v1bOut = std::max(peak.v1bOut, std::abs(x));

        // Gang B
        x *= gainVal;
        x = fb.gainLoadingLP.process(x);
        if (i > settleEnd) peak.gangBOut = std::max(peak.gangBOut, std::abs(x));

        // LTP
        x = applyGridConduction(x, piGrid);
        LTPOutput ltp_out = solveLTP(x, vPi, ltpModel);
        float piA = fb.pi_couplingHP_A.process(ltp_out.piA);
        float piB = fb.pi_couplingHP_B.process(ltp_out.piB);

        if (i > settleEnd) {
            peak.ltpPiA = std::max(peak.ltpPiA, std::abs(piA));
            peak.ltpPiB = std::max(peak.ltpPiB, std::abs(piB));
        }

        // Tone (max = 5kHz LP on diff — just pass through for simplicity)
        float diff = piA - piB;
        float sum  = piA + piB;
        float outDiff = fb.toneCutFilter.process(diff);
        piA = (sum + outDiff) * 0.5f;
        piB = (sum - outDiff) * 0.5f;

        // Volume
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

        if (i > settleEnd) peak.powerOut = std::max(peak.powerOut, std::abs(pOut));

        // Speaker coupling + OT
        float otIn = spk.process(pOut);
        if (i > settleEnd) peak.speakerOut = std::max(peak.speakerOut, std::abs(otIn));

        float otSat = ot.process(otIn);
        float otOut = fb.ot_leakageLP.process(otSat);
        if (i > settleEnd) peak.otOut = std::max(peak.otOut, std::abs(otOut));

        // Cab (IIR)
        float cabOut = fb.cab_presence.process(otOut);
        cabOut = fb.cab_rolloff.process(cabOut);
        cabOut = fb.cab_notch.process(cabOut);
        if (i > settleEnd) peak.cabOut = std::max(peak.cabOut, std::abs(cabOut));

        // Output
        float finalOut = cabOut * 0.02f;
        finalOut = std::tanh(finalOut);
        if (i > settleEnd) peak.finalOut = std::max(peak.finalOut, std::abs(finalOut));
    }

    return peak;
}

TEST_CASE("Level trace: max gain, typical guitar input", "[levels]") {
    // Typical guitar DI: ±0.15 digital peak
    auto t = traceChain(0.15f, 1.0f, 1.0f);

    printf("\n=== Level Trace: digital=0.15, gain=1.0, vol=1.0 ===\n");
    printf("  V1a out:      %.4f\n", t.v1aOut);
    printf("  Gang A out:   %.4f\n", t.gangAOut);
    printf("  V1b out:      %.4f\n", t.v1bOut);
    printf("  Gang B out:   %.4f\n", t.gangBOut);
    printf("  LTP piA:      %.4f\n", t.ltpPiA);
    printf("  LTP piB:      %.4f\n", t.ltpPiB);
    printf("  Power out:    %.4f\n", t.powerOut);
    printf("  Speaker out:  %.4f\n", t.speakerOut);
    printf("  OT out:       %.4f\n", t.otOut);
    printf("  Cab out:      %.4f\n", t.cabOut);
    printf("  Final (0.02): %.4f\n", t.finalOut);
    printf("  Needed comp for tanh(2): %.4f\n", 2.0f / t.cabOut);
    printf("  Needed comp for tanh(1): %.4f\n", 1.0f / t.cabOut);

    // Just verify the chain produces output
    REQUIRE(t.finalOut > 0.0f);
}

TEST_CASE("Level trace: moderate gain", "[levels]") {
    auto t = traceChain(0.15f, 0.5f, 0.5f);

    printf("\n=== Level Trace: digital=0.15, gain=0.5, vol=0.5 ===\n");
    printf("  V1a out:      %.4f\n", t.v1aOut);
    printf("  Gang A out:   %.4f\n", t.gangAOut);
    printf("  V1b out:      %.4f\n", t.v1bOut);
    printf("  Gang B out:   %.4f\n", t.gangBOut);
    printf("  LTP piA:      %.4f\n", t.ltpPiA);
    printf("  LTP piB:      %.4f\n", t.ltpPiB);
    printf("  Power out:    %.4f\n", t.powerOut);
    printf("  Speaker out:  %.4f\n", t.speakerOut);
    printf("  OT out:       %.4f\n", t.otOut);
    printf("  Cab out:      %.4f\n", t.cabOut);
    printf("  Final (0.02): %.4f\n", t.finalOut);

    REQUIRE(t.finalOut > 0.0f);
}

TEST_CASE("Level trace: clean tone", "[levels]") {
    auto t = traceChain(0.15f, 0.2f, 0.3f);

    printf("\n=== Level Trace: digital=0.15, gain=0.2, vol=0.3 ===\n");
    printf("  V1a out:      %.4f\n", t.v1aOut);
    printf("  Gang A out:   %.4f\n", t.gangAOut);
    printf("  V1b out:      %.4f\n", t.v1bOut);
    printf("  Gang B out:   %.4f\n", t.gangBOut);
    printf("  LTP piA:      %.4f\n", t.ltpPiA);
    printf("  LTP piB:      %.4f\n", t.ltpPiB);
    printf("  Power out:    %.4f\n", t.powerOut);
    printf("  Speaker out:  %.4f\n", t.speakerOut);
    printf("  OT out:       %.4f\n", t.otOut);
    printf("  Cab out:      %.4f\n", t.cabOut);
    printf("  Final (0.02): %.4f\n", t.finalOut);

    REQUIRE(t.finalOut > 0.0f);
}
