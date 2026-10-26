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
// b8e20a9; re-checked against processBlock at c6e276d): processBlock's per-sample loop
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
//   /tmp/sr_power_probe          Task 0 power-stage telemetry (table "Power-stage telemetry")
//   /tmp/sr_power_probe grid     grid-conduction corners (section "Grid conduction corners")
//
// Task 0 runs: 1 s of silence to get past the start-up transient (review B1),
// then the signal; statistics cover the signal only. 44.1 kHz, 4x, IIR cab.
//
// `grid` runs (added 2026-10-23, after B1/B7): the amp starts on its seeded idle
// point exactly as resetDSPState() leaves it, and the input goes through the
// plugin's base-rate front end — INPUT trim, then BOOST (the real Boost class
// at 44.1 kHz) — and a 4x linear-phase windowed-sinc upsampler standing in for
// JUCE's oversampler. JUCE's polyphase-IIR half-band filters are not linear
// phase anywhere in the band, so harmonics reaching V1a's grid clamp and
// triode (before the 5 kHz gain-loading LP) are shaped slightly differently;
// docs/handoff-power-section-fixes.md lists the corners checked against the
// real processBlock (grid peak within 0.02 V). Shape is the
// knob's value, applied the way processBlock applies it on its first sample.

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
#include "dsp/effects/Boost.h"
#include "dsp/IdleOperatingPoint.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

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
    // EL84 grid drive: the larger of the two PI outputs after the master volume,
    // i.e. what applyGridConduction() compares with GRID_CONDUCT_THRESH_EL84_N.
    float lastEl84Grid = 0;

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

    // What resetDSPState() does since review B1: start on the solved idle point
    // instead of zeroed filters and a nominal supply. Then the Shape knob, which
    // processBlock applies on its first sample. Used by the `grid` runs only, so
    // the Task 0 table above keeps describing the build it was measured on.
    void seedIdleAndShape(float shapeKnob) {
        settlePowerStageAtIdle(bias, psu, sag);
        seedPreampAtIdle(fb, t1, t2, ltp, psu);
        fb.updateShape(shapeKnob);
    }

    // One oversampled sample of processBlock's oversampled loop (IIR cab on, no IR).
    // Copied from docs/review-dsp-2026-10-15-probe.cpp. Shape is noon for the Task 0
    // presets; `grid` mode sets the knob in seedIdleAndShape().
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
        lastEl84Grid = std::max(piA, piB);
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

// ── Grid conduction corners (`grid`) ──────────────────────────────────────

struct Knobs {
    const char* name;
    float gain, vol, shape;
    float inTrim;                 // INPUT knob 0..1 (0.5 = unity, 1.0 = +12 dB)
    bool boost;                   // BOOST on: LEVEL, BRIGHT at max, TIGHT 0 (full range)
    bool is7W;
};

// Base-rate input at sample n (44.1 kHz), before the INPUT trim.
using Source = std::function<float(int)>;

static Source sineAt(float f, float a) {
    return [=](int n) { return a * std::sin(2.0f * PI_F * f * n / 44100.0f); };
}

// Band-limited square: odd harmonics below 19.8 kHz, plateau ~a (Gibbs ~9 % over).
static Source squareAt(float f, float a) {
    return [=](int n) {
        float y = 0.0f;
        for (int k = 1; k * f < 19800.0f; k += 2)
            y += std::sin(2.0f * PI_F * k * f * n / 44100.0f) / k;
        return a * 4.0f / PI_F * y;
    };
}

// Open-string E chord (E2 A2 D3 G3 B3 E4), equal amplitudes, scaled so the
// first 2 s peak at `a`.
static Source chordAt(float a) {
    static const float f[] = { 82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f };
    auto raw = [](int n) {
        float y = 0.0f;
        for (float fk : f) y += std::sin(2.0f * PI_F * fk * n / 44100.0f);
        return y;
    };
    float pk = 0.0f;
    for (int n = 0; n < 2 * 44100; ++n) pk = std::max(pk, std::abs(raw(n)));
    const float g = a / pk;
    return [=](int n) { return g * raw(n); };
}

// 4x windowed-sinc interpolation (Blackman, 64 base-rate taps per phase,
// cutoff 0.45 fs), DC gain 1 per phase.
static std::vector<float> upsample4(const std::vector<float>& x) {
    constexpr int L = 4, half = 32 * L;
    std::vector<float> h(2 * half + 1);
    for (int j = -half; j <= half; ++j) {
        const double t = j / static_cast<double>(L), fc = 0.9;   // fc relative to base Nyquist
        const double sinc = j == 0 ? fc : std::sin(M_PI * fc * t) / (M_PI * t);
        const double w = 0.42 + 0.5 * std::cos(M_PI * j / half) + 0.08 * std::cos(2.0 * M_PI * j / half);
        h[j + half] = static_cast<float>(sinc * w);
    }
    for (int ph = 0; ph < L; ++ph) {                // normalise each phase to unity DC
        double sum = 0.0;
        for (int j = ph; j < static_cast<int>(h.size()); j += L) sum += h[j];
        for (int j = ph; j < static_cast<int>(h.size()); j += L) h[j] = static_cast<float>(h[j] / sum);
    }
    std::vector<float> y(x.size() * L, 0.0f);
    for (size_t m = 0; m < y.size(); ++m) {
        // y[m] = sum_k x[k] h[m - L k + half] over taps in range (centred, non-causal)
        float acc = 0.0f;
        const long kLo = (static_cast<long>(m) - half + L - 1) / L, kHi = (static_cast<long>(m) + half) / L;
        for (long k = std::max(0L, kLo); k <= kHi && k < static_cast<long>(x.size()); ++k)
            acc += x[static_cast<size_t>(k)] * h[static_cast<size_t>(static_cast<long>(m) - L * k + half)];
        y[m] = acc;
    }
    return y;
}

// One corner: seeded idle point, 0.5 s of silence, then 2 s of `src` with a 5 ms
// linear fade-in; statistics cover the 2 s. Reports the EL84 grid drive peak
// against GRID_CONDUCT_THRESH_EL84_N and how often the PI (and V1b) coupling-cap
// charge sits on its MAX_BIAS_SHIFT_N rail (>= 99 % of 1.5 V).
static void runGrid(const Knobs& k, const char* sigName, const Source& src) {
    const int baseN = static_cast<int>(44100 * 2.5), sigStart = 44100 / 2, fade = 44100 * 5 / 1000;
    std::vector<float> in(static_cast<size_t>(baseN), 0.0f);
    Boost boost;
    boost.prepare(44100.0f);
    const float trim = inputTrimGain(k.inTrim);
    for (int n = sigStart; n < baseN; ++n) {
        const int i = n - sigStart;
        const float env = i < fade ? static_cast<float>(i) / fade : 1.0f;
        float x = env * src(i) * trim;
        if (k.boost) x = boost.process(x, 1.0f, 0.0f, 1.0f);
        in[static_cast<size_t>(n)] = x;
    }
    const std::vector<float> os = upsample4(in);

    Amp amp(44100.0f, 4, k.is7W);
    amp.seedIdleAndShape(k.shape);
    const float rail = 0.99f * std::abs(MAX_BIAS_SHIFT_N);
    float gridPk = -1e9f, piMax = 0.0f;
    double piSum = 0.0;
    long piRailed = 0, v1bRailed = 0, conducts = 0, count = 0;
    const size_t osStart = static_cast<size_t>(sigStart) * 4;
    for (size_t m = 0; m < os.size(); ++m) {
        amp.tick(os[m], k.gain, k.vol);
        if (m < osStart) continue;
        ++count;
        gridPk = std::max(gridPk, amp.lastEl84Grid);
        if (amp.ga.lastGridDrop > 0.0f || amp.gb.lastGridDrop > 0.0f) ++conducts;
        const float qpi = amp.gpi.couplingCapCharge;
        piMax = std::max(piMax, qpi);
        piSum += qpi;
        if (qpi >= rail) ++piRailed;
        if (amp.g2.couplingCapCharge >= rail) ++v1bRailed;
    }
    const float gridV = VS::toVolts(gridPk), threshV = VS::toVolts(GRID_CONDUCT_THRESH_EL84_N);
    std::printf("%-30s %-18s | EL84 grid pk %6.2f V  margin %+5.1f dB  conducting %5.2f%%"
                " | PI charge avg %.2f max %.2f V  railed %5.1f%% | V1b railed %5.1f%%\n",
                k.name, sigName, gridV, 20.0f * std::log10(threshV / std::max(gridV, 1e-6f)),
                100.0 * conducts / count, VS::toVolts(static_cast<float>(piSum / count)),
                VS::toVolts(piMax), 100.0 * piRailed / count, 100.0 * v1bRailed / count);
}

static void gridCorners() {
    constexpr float nominalDI = 0.178f;   // sine peak at -18 dBFS RMS
    const Knobs defaults { "default (6 / 5 / 5)", 0.6f, 0.5f, 0.5f, 0.5f, false, false };
    const Knobs gain8    { "gain 8", 0.8f, 0.5f, 0.5f, 0.5f, false, false };
    const Knobs gain10   { "gain 10", 1.0f, 0.5f, 0.5f, 0.5f, false, false };
    const Knobs maxed    { "gain 10, vol 10", 1.0f, 1.0f, 0.5f, 0.5f, false, false };
    for (const Knobs* k : { &defaults, &gain8, &gain10, &maxed })
        for (float f : { 110.0f, 220.0f }) {
            char name[32];
            std::snprintf(name, sizeof name, "sine %.0f @0.178", f);
            runGrid(*k, name, sineAt(f, nominalDI));
        }

    // Hot corners: gain 10, volume 10, INPUT +12 dB, BOOST LEVEL + BRIGHT max.
    for (bool is7W : { false, true })
        for (float shape : { 0.0f, 1.0f }) {
            char kname[40];
            std::snprintf(kname, sizeof kname, "HOT shape %.0f %s", shape * 10.0f, is7W ? "7W" : "15W");
            const Knobs hot { kname, 1.0f, 1.0f, shape, 1.0f, true, is7W };
            runGrid(hot, "sine 110 @0.5", sineAt(110.0f, 0.5f));
            runGrid(hot, "sine 800 @0.5", sineAt(800.0f, 0.5f));
            runGrid(hot, "sine 1600 @0.5", sineAt(1600.0f, 0.5f));
            runGrid(hot, "sine 800 @1.0", sineAt(800.0f, 1.0f));
            runGrid(hot, "square 110 @0.5", squareAt(110.0f, 0.5f));
            runGrid(hot, "square 1600 @0.5", squareAt(1600.0f, 0.5f));
            runGrid(hot, "E chord pk 0.5", chordAt(0.5f));
        }
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "grid") == 0) {
        gridCorners();
        return 0;
    }
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
