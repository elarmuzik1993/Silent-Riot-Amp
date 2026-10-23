// Power-stage telemetry probe for docs/handoff-power-section-fixes.md (plan
// docs/superpowers/plans/2026-10-23-power-section-fixes.md, Task 0) — evidence,
// not product code.
//
// Logs, per sign-off operating point: min / average / peak EL84 plate current
// per tube (mA), the peak push-pull difference current ip*cond (what the OT
// sees), sum(ip) (what the PSU and sag are fed today) against sum(ip*cond)
// (what flows), the minimum softConductionCurve factor, the sag biasOffset()
// range and the minimum B+ / screen. Tasks 2-4 are each checked against these numbers.
//
// The amp loop is copied from docs/review-dsp-2026-10-15-probe.cpp (main at
// b8e20a9; src/ unchanged through 9970ddd): processBlock's per-sample loop
// minus the oversampling filters, input fed at the oversampled rate. When a
// task changes the power stage, mirror it in Amp::tick here before re-running,
// or the numbers stop describing the plugin.
//
// Build from the repo root (pure C++, no JUCE; one command, wrapped here):
//   g++ -O2 -std=c++17 -Isrc docs/handoff-power-section-fixes-probe.cpp
//       src/dsp/tubes/TriodeWaveshaper.cpp src/dsp/tubes/PentodeWaveshaper.cpp
//       src/dsp/tubes/GridConduction.cpp src/dsp/filters/FilterBank.cpp
//       src/dsp/power/PowerSupplyModel.cpp src/dsp/power/LTPSolver.cpp
//       src/dsp/cabinet/SpeakerCoupling.cpp -o /tmp/sr_power_probe
//   /tmp/sr_power_probe
//
// Each run: 1 s of silence to get past the start-up transient (review B1), then
// the signal; statistics cover the signal only. 44.1 kHz, 4x, IIR cab.

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
    float lastIpA = 0, lastIpB = 0, lastCondA = 1, lastCondB = 1, lastBPlus = 0, lastScreen = 0;

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
    // Copied from docs/review-dsp-2026-10-15-probe.cpp; Shape stays at the filter
    // bank's default (noon), which every preset below uses.
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
        lastIpA = a.plateCurrent; lastIpB = b.plateCurrent; lastCondA = condA; lastCondB = condB;
        lastBPlus = vPl; lastScreen = vSc;
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

struct Point { const char* name; float gain, vol; bool is7W; };
struct Signal { const char* name; float freq, amp, attackS, seconds; };   // freq 0 = silence

static void run(const Point& p, const Signal& s) {
    Amp amp(44100.0f, 4, p.is7W);
    for (int i = 0; i < static_cast<int>(amp.esr * 1.0f); ++i) amp.tick(0.0f, p.gain, p.vol);

    const int n = static_cast<int>(amp.esr * s.seconds), atk = static_cast<int>(amp.esr * s.attackS);
    float pkA = 0, pkB = 0, loA = 1e9f, loB = 1e9f, dPk = 0, cMin = 1, sagMin = 1, sagMax = -1, bMin = 1, scMin = 1;
    double sumA = 0, sumB = 0, sumScaled = 0;
    int cutoff = 0;
    for (int i = 0; i < n; ++i) {
        const float env = (atk > 0 && i < atk) ? static_cast<float>(i) / atk : 1.0f;
        const float in = s.freq > 0 ? s.amp * env * std::sin(2.0f * PI_F * s.freq * i / amp.esr) : 0.0f;
        amp.tick(in, p.gain, p.vol);
        pkA = std::max(pkA, amp.lastIpA); pkB = std::max(pkB, amp.lastIpB);
        loA = std::min(loA, amp.lastIpA); loB = std::min(loB, amp.lastIpB);
        dPk = std::max(dPk, std::abs(amp.lastIpA * amp.lastCondA - amp.lastIpB * amp.lastCondB));
        sumA += amp.lastIpA; sumB += amp.lastIpB;
        sumScaled += amp.lastIpA * amp.lastCondA + amp.lastIpB * amp.lastCondB;
        const float c = std::min(amp.lastCondA, amp.lastCondB);
        cMin = std::min(cMin, c);
        if (c < 0.05f) ++cutoff;
        sagMin = std::min(sagMin, amp.sag.biasOffset()); sagMax = std::max(sagMax, amp.sag.biasOffset());
        bMin = std::min(bMin, amp.lastBPlus); scMin = std::min(scMin, amp.lastScreen);
    }
    const auto mA = [](double i) { return VS::toAmps(static_cast<float>(i)) * 1e3f; };
    std::printf("%-8s %-14s | ipA min/avg/pk %4.1f/%4.1f/%4.1f  ipB %4.1f/%4.1f/%4.1f | diff pk %4.1f"
                " | sum(ip) %5.1f  sum(ip*cond) %5.1f | cond min %.3f (<0.05 %4.1f%%) | sag %+.2f..%+.2f V"
                " | B+ min %5.1f  screen %5.1f V\n",
                p.name, s.name, mA(loA), mA(sumA / n), mA(pkA), mA(loB), mA(sumB / n), mA(pkB), mA(dPk),
                mA((sumA + sumB) / n), mA(sumScaled / n), cMin, 100.0 * cutoff / n,
                VS::toVolts(sagMin), VS::toVolts(sagMax), VS::toVolts(bMin), VS::toVolts(scMin));
}

int main() {
    // Operating points from tools/signoff_gates.json; P-CLEAN/EDGE/CRANK also in 7W.
    const Point points[] = {
        { "P-CLEAN", 0.375f, 0.5f, false }, { "P-EDGE", 0.625f, 0.5f, false }, { "P-CRANK", 1.0f, 0.7f, false },
        { "7W-CLEAN", 0.375f, 0.5f, true }, { "7W-EDGE", 0.625f, 0.5f, true }, { "7W-CRANK", 1.0f, 0.7f, true },
        { "P-7W", 0.75f, 0.7f, true },
        // Not a sign-off point: every knob at 10, where the EL84s are driven hardest.
        { "MAX-15W", 1.0f, 1.0f, false }, { "MAX-7W", 1.0f, 1.0f, true },
    };
    // The sign-off gates' own signals: thd220_* (sine:220:0.18), note_* (note:110:0.5), idle.
    const Signal signals[] = {
        { "silence", 0.0f, 0.0f, 0.0f, 1.0f },
        { "sine220 @0.18", 220.0f, 0.18f, 0.0f, 1.0f },
        { "note110 @0.5", 110.0f, 0.5f, 0.005f, 2.0f },
    };
    for (const auto& p : points)
        for (const auto& s : signals) run(p, s);
    return 0;
}
