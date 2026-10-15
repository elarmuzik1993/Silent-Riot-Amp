// Amp-loop probe for docs/review-dsp-2026-10-15.md — evidence, not product code.
//
// Replicates SilentRiotProcessor::processBlock's per-sample amp loop
// (src/PluginProcessor.cpp, main at b8e20a9) with the repo's own DSP classes,
// minus the oversampling filters: input is fed straight in at the oversampled
// rate and the output is read before downsampling. Not built by CMake; if
// processBlock changes, this copy does not follow it.
//
// Build from the repo root (pure C++, no JUCE; one command, wrapped here):
//   g++ -O2 -std=c++17 -Isrc docs/review-dsp-2026-10-15-probe.cpp
//       src/dsp/tubes/TriodeWaveshaper.cpp src/dsp/tubes/PentodeWaveshaper.cpp
//       src/dsp/tubes/GridConduction.cpp src/dsp/filters/FilterBank.cpp
//       src/dsp/power/PowerSupplyModel.cpp src/dsp/power/LTPSolver.cpp
//       src/dsp/cabinet/SpeakerCoupling.cpp -o /tmp/sr_probe
//
// Modes:
//   /tmp/sr_probe idle          B2: PSU rails, idle current and sag offset after 3 s of silence
//   /tmp/sr_probe thump         B1: startup burst per 10 ms window, with the PSU/preamp isolated
//   /tmp/sr_probe e1            B1: E1-style RMS at P-CRANK after a 250 ms warm-up
//   /tmp/sr_probe miller [amp]  B3: V1b Miller cutoff swing on a 110 Hz sine (default amp 0.125)

#include "dsp/tubes/TriodeWaveshaper.h"
#include "dsp/tubes/PentodeWaveshaper.h"
#include "dsp/tubes/GridConduction.h"
#include "dsp/filters/FilterBank.h"
#include "dsp/filters/DynamicMillerLP.h"
#include "dsp/power/PowerSupplyModel.h"
#include "dsp/power/SagEnvelope.h"
#include "dsp/power/LTPSolver.h"
#include "dsp/cabinet/SpeakerCoupling.h"
#include "dsp/cabinet/OTSaturation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace sr;

struct Amp {
    float esr;
    FilterBank fb;
    DynamicMillerLP m1, m2;
    PowerSupplyModel psu;
    SagEnvelope sag;
    LTPModel ltp;
    SpeakerCoupling spk;
    OTSaturation ot;
    TriodeState t1, t2;
    GridConductionModel g1 = makeV1aGrid(), g2 = makeV1bGrid(), gpi = makePIGrid(),
                        ga = makeEL84Grid(), gb = makeEL84Grid();
    float bias;

    // Isolation switches (not in the plugin): hold the PSU at nominal, zero the sag offset.
    bool freezePSU = false, noSag = false;

    // Probes
    float lastFc1 = 0, lastFc2 = 0, lastGain2 = 0, lastPlate = 0;

    Amp(float baseSR, int os, bool is7W) {
        esr = baseSR * os;
        // Constructor + prepareToPlay + resetDSPState, in the plugin's order.
        ltp.init(); psu.init(); spk.init();
        m1.init(CGP_12AX7, CGK_12AX7, V1A_RSOURCE_REAL, StageGains::V1A_GAIN);
        m2.init(CGP_12AX7, CGK_12AX7, V1B_RSOURCE_BASE_REAL, StageGains::V1B_GAIN);
        fb.recalcAll(baseSR, os);
        m1.recalc(esr); m2.recalc(esr);
        spk.recalcFilters(esr); psu.recalcDecayCoeffs(esr); sag.recalcCoeffs(esr); ot.recalcCoeffs(esr);
        recalcGridChargeDecay(g1, GRID_TAU_PREAMP_S, esr);
        recalcGridChargeDecay(g2, GRID_TAU_PREAMP_S, esr);
        recalcGridChargeDecay(gpi, GRID_TAU_PI_S, esr);
        recalcGridChargeDecay(ga, GRID_TAU_EL84_S, esr);
        recalcGridChargeDecay(gb, GRID_TAU_EL84_S, esr);
        fb.resetAll(); m1.reset(); m2.reset(); spk.reset(); ot.reset();
        psu.reset(); sag.reset(); ltp.reset();   // sag.reset() uses the 96.0 fallback on first prepare
        bias = is7W ? PENTODE_BIAS_7W_N : PENTODE_BIAS_15W_N;
        // First processBlock: sag reference at NOMINAL voltages (PluginProcessor.cpp:406).
        sag.setQuiescent(pentodeQuiescentTotal(bias, V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N));
    }

    // One oversampled sample of PluginProcessor.cpp:518-646 (IIR cab on, no IR).
    float tick(float in, float gainKnob, float volKnob) {
        const float gainDrive = gainDriveTaper(gainKnob);
        if (!freezePSU) psu.update();
        psu.resetCurrentDraw();
        const float vPre = psu.vPreamp(), vPi = psu.vPi(), vPl = psu.vPower(), vSc = psu.vScreen();

        float x = in * VS::V_GUITAR;
        x = fb.inputHP.process(x);
        x = applyGridConduction(x, g1);
        const float v1a_in = x;
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPre, PREAMP_RPLATE_N, t1);
        x -= fb.v1a_cathodeBypass.process(x) * CATHODE_BYPASS_DEPTH;
        x = fb.v1a_couplingHP.process(x);
        const float g1e = (std::abs(v1a_in) > 1e-8f) ? std::abs(x) / std::abs(v1a_in) : StageGains::V1A_GAIN;
        x = m1.process(x, g1e, fb.effectiveSR);
        lastFc1 = m1.fcSmooth;
        psu.reportPreampLevel(x);

        const float gainA = audioTaper(gainDrive);
        x *= gainA;
        x = applyGridConduction(x, g2);
        const float v1b_in = x;
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPre, PREAMP_RPLATE_N, t2);
        x -= fb.v1b_cathodeBypass.process(x) * CATHODE_BYPASS_DEPTH;
        x = fb.v1b_couplingHP.process(x);
        m2.rSource_real = std::min(V1B_RSOURCE_BASE_REAL + (1.0f - gainA) * GAIN_POT_R_REAL, V1B_RSOURCE_MAX_REAL);
        const float g2e = (std::abs(v1b_in) > 1e-8f) ? std::abs(x) / std::abs(v1b_in) : StageGains::V1B_GAIN;
        x = m2.process(x, g2e, fb.effectiveSR);
        lastFc2 = m2.fcSmooth; lastGain2 = g2e;
        psu.reportPreampLevel(x);

        x *= gainDrive;
        x = fb.gainLoadingLP.process(x);
        x = applyGridConduction(x, gpi);
        const LTPOutput lo = solveLTP(x, vPi, ltp);
        float piA = fb.pi_couplingHP_A.process(lo.piA);
        float piB = fb.pi_couplingHP_B.process(lo.piB);
        psu.reportPiLevel(std::max(std::abs(piA), std::abs(piB)));

        const float diff = piA - piB, sum = piA + piB;
        const float outDiff = fb.shapeHighShelf.process(fb.shapeMidBell.process(fb.shapeLowShelf.process(diff)));
        piA = (sum + outDiff) * 0.5f;
        piB = (sum - outDiff) * 0.5f;

        const float vol = volumeDriveTaper(volKnob);
        piA *= vol; piB *= vol;

        const float effBias = bias + (noSag ? 0.0f : sag.biasOffset());
        piA = applyGridConduction(piA, ga);
        piB = applyGridConduction(piB, gb);
        const float condA = softConductionCurve(piA, effBias), condB = softConductionCurve(piB, effBias);
        const PentodeOutput a = pentodeWaveshaper(piA, effBias, vPl, vSc);
        const PentodeOutput b = pentodeWaveshaper(piB, effBias, vPl, vSc);
        float powerOut = a.plateCurrent * condA - b.plateCurrent * condB;
        const float totalPlate = std::abs(a.plateCurrent) + std::abs(b.plateCurrent);
        lastPlate = totalPlate;
        psu.reportPowerDirect(totalPlate, a.screenCurrent + b.screenCurrent);
        sag.update(totalPlate);

        powerOut *= OT_CURRENT_SCALE;
        float out = fb.ot_leakageLP.process(ot.process(spk.process(powerOut)));
        out = fb.cab_notch.process(fb.cab_rolloff.process(fb.cab_presence.process(out)));
        return std::tanh(out * OUTPUT_MAKEUP_GAIN);
    }
};

static double dB(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

static void modeIdle() {
    for (int is7W = 0; is7W < 2; ++is7W) {
        Amp amp(44100.0f, 4, is7W);
        const int n = static_cast<int>(amp.esr * 3.0f);
        for (int i = 0; i < n; ++i) amp.tick(0.0f, 0.6f, 0.5f);
        std::printf("%s after 3 s silence: B+ %.1f V  screen %.1f V  plate %.2f mA  reference %.2f mA  biasOffset %+.3f V\n",
                    is7W ? "7W " : "15W", VS::toVolts(amp.psu.vPower()), VS::toVolts(amp.psu.vScreen()),
                    VS::toAmps(amp.lastPlate) * 1e3f, VS::toAmps(amp.sag.quiescentI) * 1e3f,
                    VS::toVolts(amp.sag.biasOffset()));
    }
}

static void modeThump() {
    const char* names[] = { "as shipped (gain 0.6, vol 0.5, 15W)", "PSU frozen at nominal, sag off",
                            "PSU frozen, sag off, preamp settled first" };
    for (int v = 0; v < 3; ++v) {
        Amp amp(44100.0f, 4, false);
        if (v >= 1) { amp.freezePSU = true; amp.noSag = true; }
        if (v == 2) for (int i = 0; i < static_cast<int>(amp.esr * 0.5f); ++i) amp.tick(0.0f, 0.6f, 0.0f);
        const int w = static_cast<int>(amp.esr * 0.01f);
        double peak[10] = {};
        for (int i = 0; i < w * 10; ++i)
            peak[i / w] = std::max(peak[i / w], static_cast<double>(std::abs(amp.tick(0.0f, 0.6f, 0.5f))));
        std::printf("%-42s peak per 10 ms (dBFS):", names[v]);
        for (double p : peak) std::printf(" %6.1f", dB(p));
        std::printf("\n");
    }
}

static void modeE1() {
    const char* names[] = { "as shipped", "PSU frozen + sag off (preamp tail only)",
                            "preamp settled, PSU from nominal", "whole chain settled 1 s first" };
    for (int v = 0; v < 4; ++v) {
        Amp amp(44100.0f, 4, false);
        if (v == 1) { amp.freezePSU = true; amp.noSag = true; }
        if (v == 2) {
            amp.freezePSU = true; amp.noSag = true;
            for (int i = 0; i < static_cast<int>(amp.esr * 1.0f); ++i) amp.tick(0.0f, 1.0f, 0.7f);
            amp.freezePSU = false; amp.noSag = false;
        }
        if (v == 3) for (int i = 0; i < static_cast<int>(amp.esr * 1.0f); ++i) amp.tick(0.0f, 1.0f, 0.7f);
        // P-CRANK: gain 1.0, volume 0.7, shape 0.5 (filter default), 15W, cab on.
        const int warm = static_cast<int>(amp.esr * 0.25f), n = static_cast<int>(amp.esr * 5.25f);
        double sumSq = 0.0;
        for (int i = 0; i < n; ++i) {
            const double y = amp.tick(0.0f, 1.0f, 0.7f);
            if (i >= warm) sumSq += y * y;
        }
        std::printf("%-42s RMS after a 250 ms warm-up: %.1f dBFS\n", names[v],
                    10.0 * std::log10(sumSq / (n - warm) + 1e-30));
    }
}

static void modeMiller(float amplitude) {
    for (float gainKnob : { 0.0f, 0.3f, 0.6f, 1.0f }) {
        Amp amp(44100.0f, 4, false);
        const int n = static_cast<int>(amp.esr * 1.0f);
        float fcMin = 1e9f, fcMax = 0.0f, gMax = 0.0f, fc1Min = 1e9f, fc1Max = 0.0f;
        double fcSum = 0.0; int count = 0;
        for (int i = 0; i < n; ++i) {
            amp.tick(amplitude * std::sin(2.0f * PI_F * 110.0f * i / amp.esr), gainKnob, 0.5f);
            if (i <= n / 2) continue;
            fcMin = std::min(fcMin, amp.lastFc2); fcMax = std::max(fcMax, amp.lastFc2);
            fc1Min = std::min(fc1Min, amp.lastFc1); fc1Max = std::max(fc1Max, amp.lastFc1);
            gMax = std::max(gMax, amp.lastGain2);
            fcSum += amp.lastFc2; ++count;
        }
        const float rS = amp.m2.rSource_real;
        const float fcNominal = 1.0f / (2.0f * PI_F * rS * (CGK_12AX7 + CGP_12AX7 * (1.0f + StageGains::V1B_GAIN)));
        std::printf("gain %.1f: V1b rSource %.0f ohm, design fc %.0f Hz, smoothed fc min/mean/max %.0f / %.0f / %.0f Hz, "
                    "gain estimate max %.3g | V1a fc %.0f..%.0f Hz\n",
                    gainKnob, rS, fcNominal, fcMin, fcSum / count, fcMax, gMax, fc1Min, fc1Max);
    }
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "idle";
    if (!std::strcmp(mode, "idle"))        modeIdle();
    else if (!std::strcmp(mode, "thump"))  modeThump();
    else if (!std::strcmp(mode, "e1"))     modeE1();
    else if (!std::strcmp(mode, "miller")) modeMiller(argc > 2 ? static_cast<float>(std::atof(argv[2])) : 0.125f);
    else { std::fprintf(stderr, "modes: idle | thump | e1 | miller [amp]\n"); return 1; }
    return 0;
}
