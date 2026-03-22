// ═══════════════════════════════════════════════════════════════
// DI Chain Trace — feeds real DI sample through the signal chain
// and records RMS envelopes at key tap points to identify which
// module contributes to the sag dip / bloom overshoot.
//
// Settings: Gain 0.5, Tone 0.5, Volume 0.5, 7W, IIR cab
// DI: open E plectrum pick, 82.41Hz, 48kHz float32 mono
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"

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

#include <fstream>
#include <vector>
#include <cstdio>

using namespace tt;
using namespace tt::test;

// ── Load raw float32 mono samples from file ─────────────────────
static std::vector<float> loadRawFloat(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    auto size = f.tellg();
    f.seekg(0);
    std::vector<float> buf(static_cast<size_t>(size) / sizeof(float));
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

// ── RMS envelope in fixed windows ────────────────────────────────
static std::vector<float> rmsEnvelope(const std::vector<float>& buf, int winSamples) {
    int n = static_cast<int>(buf.size()) / winSamples;
    std::vector<float> env(n);
    for (int i = 0; i < n; ++i) {
        float sum = 0.0f;
        for (int j = 0; j < winSamples; ++j) {
            float v = buf[i * winSamples + j];
            sum += v * v;
        }
        env[i] = std::sqrt(sum / winSamples);
    }
    return env;
}

static float toDB(float x) { return 20.0f * std::log10(x + 1e-30f); }

// ═══════════════════════════════════════════════════════════════
// Full chain trace with tap points
// ═══════════════════════════════════════════════════════════════

TEST_CASE("DI chain trace: envelope at tap points", "[di_trace]") {
    // Load DI
    auto diSamples = loadRawFloat("tests/di_sample_82hz.raw");
    REQUIRE(diSamples.size() > 10000);

    // Base SR = 48kHz, oversample 4x = 192kHz
    constexpr float baseSR = 48000.0f;
    constexpr int osFactor = 4;
    constexpr float effectiveSR = baseSR * osFactor;
    const int diLen = static_cast<int>(diSamples.size());

    // Upsample by sample-hold (simple — real uses polyphase, but
    // for envelope analysis this is sufficient)
    const int osLen = diLen * osFactor;
    std::vector<float> diOS(osLen);
    for (int i = 0; i < diLen; ++i)
        for (int j = 0; j < osFactor; ++j)
            diOS[i * osFactor + j] = diSamples[i];

    // ── Init all DSP modules ─────────────────────────────────

    // Filters
    FilterBank fb;
    fb.recalcAll(baseSR, osFactor);

    // Triodes
    TriodeState v1aTriode, v1bTriode;

    // Miller caps
    DynamicMillerLP v1a_miller, v1b_miller;
    v1a_miller.init(CGP_12AX7, CGK_12AX7, 38500.0f, StageGains::V1A_GAIN);
    v1b_miller.init(CGP_12AX7, CGK_12AX7, 38500.0f, StageGains::V1B_GAIN);
    v1a_miller.recalc(effectiveSR);
    v1b_miller.recalc(effectiveSR);

    // Grid conduction models
    auto v1aGrid  = makeV1aGrid();
    auto v1bGrid  = makeV1bGrid();
    auto piGrid   = makePIGrid();
    auto el84GridA = makeEL84Grid();
    auto el84GridB = makeEL84Grid();
    recalcGridChargeDecay(v1aGrid,  0.022f, effectiveSR);
    recalcGridChargeDecay(v1bGrid,  0.022f, effectiveSR);
    recalcGridChargeDecay(piGrid,   0.047f, effectiveSR);
    recalcGridChargeDecay(el84GridA, 0.022f, effectiveSR);
    recalcGridChargeDecay(el84GridB, 0.022f, effectiveSR);

    // LTP
    LTPModel ltpModel;
    ltpModel.init();

    // PSU + Sag
    PowerSupplyModel psu;
    psu.init();
    psu.recalcDecayCoeffs(effectiveSR);
    SagEnvelope sag;
    sag.recalcCoeffs(effectiveSR);

    // Speaker coupling + OT
    SpeakerCoupling spk;
    spk.recalcFilters(effectiveSR);
    OTSaturation otSat;
    otSat.recalcCoeffs(effectiveSR);

    // ── Settings: all noon, 7W ───────────────────────────────
    constexpr float gainVal = 0.5f;
    constexpr float volVal  = 0.5f;
    constexpr float toneVal = 0.5f;
    float el84BaseBias = PENTODE_BIAS_7W_N;

    // Tone freq at noon
    constexpr float toneFreqRatio = TONE_MAX_FREQ / TONE_MIN_FREQ;
    float toneFreq = TONE_MIN_FREQ * std::pow(toneFreqRatio, toneVal);
    fb.toneCutFilter.setFreqFast(toneFreq, effectiveSR);

    // V1b Miller source R at gain=0.5
    float gainPotR = (1.0f - gainVal) * GAIN_POT_R_REAL;
    v1b_miller.rSource_real = std::min(V1B_RSOURCE_BASE_REAL + gainPotR, V1B_RSOURCE_MAX_REAL);

    // ── Tap point buffers ────────────────────────────────────
    std::vector<float> tapDI(osLen);          // raw DI (oversampled)
    std::vector<float> tapPreamp(osLen);      // after V1b + gain pot B + loading LP
    std::vector<float> tapLTP(osLen);         // LTP output piA (before volume)
    std::vector<float> tapPentodeIn(osLen);   // piA after volume (pentode grid signal)
    std::vector<float> tapPowerOut(osLen);    // push-pull output (before OT scale)
    std::vector<float> tapPlateCurr(osLen);   // total plate current (|ipA|+|ipB|)
    std::vector<float> tapFinal(osLen);       // after coupling + OT + cab + limiter
    std::vector<float> tapBiasOffset(osLen);  // sag bias offset trace
    std::vector<float> tapGainMod(osLen);     // speaker coupling gainMod

    // ── Also run power section WITHOUT sag for comparison ────
    // (separate pass — needs fresh state)

    // ── Main processing loop ─────────────────────────────────
    for (int i = 0; i < osLen; ++i) {
        psu.update();
        psu.resetCurrentDraw();

        float vPreamp = psu.vPreamp();
        float vPi     = psu.vPi();
        float vPlate  = psu.vPower();
        float vScreen = psu.vScreen();

        float x = diOS[i] * VS::V_GUITAR;
        tapDI[i] = x;

        // Stage 1: V1a
        x = fb.inputHP.process(x);
        x = applyGridConduction(x, v1aGrid);
        float v1a_in = x;
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, PREAMP_RPLATE_N, v1aTriode);
        x -= fb.v1a_cathodeBypass.process(x) * CATHODE_BYPASS_DEPTH;
        x = fb.v1a_couplingHP.process(x);
        float v1aGainEst = (std::abs(v1a_in) > 1e-8f) ? std::abs(x) / std::abs(v1a_in) : StageGains::V1A_GAIN;
        x = v1a_miller.process(x, v1aGainEst, effectiveSR);
        psu.reportPreampLevel(x);

        // Gain pot A (audio taper: Spec §3.3)
        x *= tt::audioTaper(gainVal);

        // Stage 2: V1b
        x = applyGridConduction(x, v1bGrid);
        float v1b_in = x;
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, PREAMP_RPLATE_N, v1bTriode);
        x -= fb.v1b_cathodeBypass.process(x) * CATHODE_BYPASS_DEPTH;
        x = fb.v1b_couplingHP.process(x);
        float v1bGainEst = (std::abs(v1b_in) > 1e-8f) ? std::abs(x) / std::abs(v1b_in) : StageGains::V1B_GAIN;
        x = v1b_miller.process(x, v1bGainEst, effectiveSR);
        psu.reportPreampLevel(x);

        // Gain pot B (audio taper: Spec §3.3) + loading LP
        x *= tt::audioTaper(gainVal);
        x = fb.gainLoadingLP.process(x);
        tapPreamp[i] = x;

        // Stage 3: Phase Inverter (LTP)
        x = applyGridConduction(x, piGrid);
        LTPOutput ltp_out = solveLTP(x, vPi, ltpModel);
        float piA = fb.pi_couplingHP_A.process(ltp_out.piA);
        float piB = fb.pi_couplingHP_B.process(ltp_out.piB);
        psu.reportPiLevel(std::max(std::abs(piA), std::abs(piB)));

        // Tone (Vox cut — differential only)
        float diff = piA - piB;
        float sum  = piA + piB;
        float outDiff = fb.toneCutFilter.process(diff);
        piA = (sum + outDiff) * 0.5f;
        piB = (sum - outDiff) * 0.5f;
        tapLTP[i] = piA;

        // Master volume
        float vol = tt::audioTaper(volVal);
        piA *= vol;
        piB *= vol;
        tapPentodeIn[i] = piA;

        // Stage 4: Power Amp (EL84 push-pull)
        float effectiveBias = el84BaseBias + sag.biasOffset();
        tapBiasOffset[i] = sag.biasOffset();

        piA = applyGridConduction(piA, el84GridA);
        piB = applyGridConduction(piB, el84GridB);
        float condA = softConductionCurve(piA, effectiveBias);
        float condB = softConductionCurve(piB, effectiveBias);
        PentodeOutput ppA = pentodeWaveshaper(piA, effectiveBias, vPlate, vScreen);
        PentodeOutput ppB = pentodeWaveshaper(piB, effectiveBias, vPlate, vScreen);
        float powerOut = ppA.plateCurrent * condA - ppB.plateCurrent * condB;
        tapPowerOut[i] = powerOut;

        float totalPlate = std::abs(ppA.plateCurrent) + std::abs(ppB.plateCurrent);
        tapPlateCurr[i] = totalPlate;
        psu.reportPowerDirect(totalPlate, ppA.screenCurrent + ppB.screenCurrent);
        sag.update(totalPlate);

        // OT current scale
        powerOut *= OT_CURRENT_SCALE;

        // Speaker coupling + OT
        float otIn = spk.process(powerOut);
        tapGainMod[i] = spk.gainModSmooth;
        float otOut_sat = otSat.process(otIn);
        float otOut = fb.ot_leakageLP.process(otOut_sat);

        // IIR cab
        float cabOut = fb.cab_presence.process(otOut);
        cabOut = fb.cab_rolloff.process(cabOut);
        cabOut = fb.cab_notch.process(cabOut);

        // Output
        float finalOut = cabOut * 1.0f;  // outputGainCompensation = 1.0
        finalOut = std::tanh(finalOut);
        tapFinal[i] = finalOut;
    }

    // ── Second pass: power section WITHOUT sag (bypass biasOffset) ──
    // Reset all state
    FilterBank fb2;
    fb2.recalcAll(baseSR, osFactor);
    fb2.toneCutFilter.setFreqFast(toneFreq, effectiveSR);
    TriodeState v1aT2, v1bT2;
    DynamicMillerLP v1a_m2, v1b_m2;
    v1a_m2.init(CGP_12AX7, CGK_12AX7, 38500.0f, StageGains::V1A_GAIN);
    v1b_m2.init(CGP_12AX7, CGK_12AX7, 38500.0f, StageGains::V1B_GAIN);
    v1a_m2.recalc(effectiveSR);
    v1b_m2.recalc(effectiveSR);
    v1b_m2.rSource_real = std::min(V1B_RSOURCE_BASE_REAL + gainPotR, V1B_RSOURCE_MAX_REAL);
    auto v1aG2  = makeV1aGrid();
    auto v1bG2  = makeV1bGrid();
    auto piG2   = makePIGrid();
    auto elA2   = makeEL84Grid();
    auto elB2   = makeEL84Grid();
    recalcGridChargeDecay(v1aG2,  0.022f, effectiveSR);
    recalcGridChargeDecay(v1bG2,  0.022f, effectiveSR);
    recalcGridChargeDecay(piG2,   0.047f, effectiveSR);
    recalcGridChargeDecay(elA2, 0.022f, effectiveSR);
    recalcGridChargeDecay(elB2, 0.022f, effectiveSR);
    LTPModel ltp2;
    ltp2.init();
    PowerSupplyModel psu2;
    psu2.init();
    psu2.recalcDecayCoeffs(effectiveSR);
    // NO sag — use fixed bias
    SpeakerCoupling spk2;
    spk2.recalcFilters(effectiveSR);
    OTSaturation otSat2;
    otSat2.recalcCoeffs(effectiveSR);

    std::vector<float> tapNoSag(osLen);

    for (int i = 0; i < osLen; ++i) {
        psu2.update();
        psu2.resetCurrentDraw();
        float vPreamp = psu2.vPreamp();
        float vPi = psu2.vPi();
        float vPlate = psu2.vPower();
        float vScreen = psu2.vScreen();

        float x = diOS[i] * VS::V_GUITAR;
        x = fb2.inputHP.process(x);
        x = applyGridConduction(x, v1aG2);
        float v1a_in = x;
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, PREAMP_RPLATE_N, v1aT2);
        x -= fb2.v1a_cathodeBypass.process(x) * CATHODE_BYPASS_DEPTH;
        x = fb2.v1a_couplingHP.process(x);
        float g1 = (std::abs(v1a_in) > 1e-8f) ? std::abs(x) / std::abs(v1a_in) : StageGains::V1A_GAIN;
        x = v1a_m2.process(x, g1, effectiveSR);
        psu2.reportPreampLevel(x);
        x *= tt::audioTaper(gainVal);
        x = applyGridConduction(x, v1bG2);
        float v1b_in = x;
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, PREAMP_RPLATE_N, v1bT2);
        x -= fb2.v1b_cathodeBypass.process(x) * CATHODE_BYPASS_DEPTH;
        x = fb2.v1b_couplingHP.process(x);
        float g2 = (std::abs(v1b_in) > 1e-8f) ? std::abs(x) / std::abs(v1b_in) : StageGains::V1B_GAIN;
        x = v1b_m2.process(x, g2, effectiveSR);
        psu2.reportPreampLevel(x);
        x *= tt::audioTaper(gainVal);
        x = fb2.gainLoadingLP.process(x);
        x = applyGridConduction(x, piG2);
        LTPOutput l = solveLTP(x, vPi, ltp2);
        float pA = fb2.pi_couplingHP_A.process(l.piA);
        float pB = fb2.pi_couplingHP_B.process(l.piB);
        psu2.reportPiLevel(std::max(std::abs(pA), std::abs(pB)));
        float d = pA - pB, s = pA + pB;
        float od = fb2.toneCutFilter.process(d);
        pA = (s + od) * 0.5f;
        pB = (s - od) * 0.5f;
        float v = tt::audioTaper(volVal);
        pA *= v; pB *= v;

        // FIXED bias — no sag
        float fixedBias = el84BaseBias;
        pA = applyGridConduction(pA, elA2);
        pB = applyGridConduction(pB, elB2);
        float cA = softConductionCurve(pA, fixedBias);
        float cB = softConductionCurve(pB, fixedBias);
        PentodeOutput ppA = pentodeWaveshaper(pA, fixedBias, vPlate, vScreen);
        PentodeOutput ppB = pentodeWaveshaper(pB, fixedBias, vPlate, vScreen);
        float pw = ppA.plateCurrent * cA - ppB.plateCurrent * cB;
        float tp = std::abs(ppA.plateCurrent) + std::abs(ppB.plateCurrent);
        psu2.reportPowerDirect(tp, ppA.screenCurrent + ppB.screenCurrent);
        // Still update sag internally for PSU but don't apply to bias
        // (skip sag.update entirely so PSU sees realistic current)

        pw *= OT_CURRENT_SCALE;
        float oi = spk2.process(pw);
        float os2 = otSat2.process(oi);
        float oo = fb2.ot_leakageLP.process(os2);
        float co = fb2.cab_presence.process(oo);
        co = fb2.cab_rolloff.process(co);
        co = fb2.cab_notch.process(co);
        co *= 1.0f;
        co = std::tanh(co);
        tapNoSag[i] = co;
    }

    // ── Compute envelopes (50ms windows at base SR = 2400 OS samples) ──
    int envWin = static_cast<int>(baseSR * 0.05f) * osFactor;  // 50ms at OS rate
    auto envDI       = rmsEnvelope(tapDI, envWin);
    auto envPreamp   = rmsEnvelope(tapPreamp, envWin);
    auto envLTP      = rmsEnvelope(tapLTP, envWin);
    auto envPentIn   = rmsEnvelope(tapPentodeIn, envWin);
    auto envPower    = rmsEnvelope(tapPowerOut, envWin);
    auto envFinal    = rmsEnvelope(tapFinal, envWin);
    auto envNoSag    = rmsEnvelope(tapNoSag, envWin);

    // Average per window for non-signal traces
    int nWins = osLen / envWin;
    std::vector<float> envBias(nWins), envGainMod(nWins), envPlateI(nWins);
    for (int w = 0; w < nWins; ++w) {
        float biasSum = 0.0f, gmSum = 0.0f, piSum = 0.0f;
        for (int j = 0; j < envWin; ++j) {
            int idx = w * envWin + j;
            biasSum += tapBiasOffset[idx];
            gmSum   += tapGainMod[idx];
            piSum   += tapPlateCurr[idx];
        }
        envBias[w]    = biasSum / envWin;
        envGainMod[w] = gmSum / envWin;
        envPlateI[w]  = piSum / envWin;
    }

    // ── Print gain staging table ─────────────────────────────
    int show = std::min(nWins, 40);
    printf("\n=== GAIN STAGING TRACE (all dBFS except noted) ===\n");
    printf("%-5s  %-7s  %-7s  %-7s  %-7s  %-7s  %-7s  %-7s  %-7s  %-8s\n",
           "Time", "DI", "Preamp", "LTP", "PentIn", "PwrOut", "Final", "NoSag", "PlateI", "GainMod");

    for (int i = 0; i < show; ++i) {
        float t = i * 0.05f;
        printf("%4.2f  %6.1f  %6.1f  %6.1f  %6.1f  %6.1f  %6.1f  %6.1f  %6.1f  %6.3f\n",
               t,
               toDB(envDI[i]),
               toDB(envPreamp[i]),
               toDB(envLTP[i]),
               toDB(envPentIn[i]),
               toDB(envPower[i]),
               toDB(envFinal[i]),
               toDB(envNoSag[i]),
               envPlateI[i],  // raw normalized current, not dB
               envGainMod[i]);
    }

    // ── Gain per stage at key moments ────────────────────────
    printf("\n=== STAGE GAINS (dB) ===\n");
    printf("%-5s  %-12s  %-12s  %-12s  %-12s  %-12s\n",
           "Time", "DI→Preamp", "Preamp→LTP", "LTP→PentIn", "PentIn→Pwr", "Pwr→Final");

    int keyFrames[] = {0, 1, 3, 5, 10, 20, 30, 39};
    for (int idx : keyFrames) {
        if (idx >= nWins) continue;
        float t = idx * 0.05f;
        float g1 = toDB(envPreamp[idx]) - toDB(envDI[idx]);
        float g2 = toDB(envLTP[idx]) - toDB(envPreamp[idx]);
        float g3 = toDB(envPentIn[idx]) - toDB(envLTP[idx]);
        float g4 = toDB(envPower[idx]) - toDB(envPentIn[idx]);
        float g5 = toDB(envFinal[idx]) - toDB(envPower[idx]);
        printf("%4.2f  %+10.1f   %+10.1f   %+10.1f   %+10.1f   %+10.1f\n",
               t, g1, g2, g3, g4, g5);
    }

    // ── Bias and plate current summary ───────────────────────
    printf("\n=== PENTODE OPERATING POINT ===\n");
    printf("7W bias (nominal): %.5f (%.1f V real)\n", PENTODE_BIAS_7W_N, VS::toVolts(PENTODE_BIAS_7W_N));
    printf("Quiescent plate I: %.1f normalized\n", SAG_QUIESCENT_I_N);
    printf("\n%-5s  %-10s  %-10s  %-12s\n", "Time", "BiasOff", "EffBias", "PlateI(avg)");
    for (int idx : keyFrames) {
        if (idx >= nWins) continue;
        float t = idx * 0.05f;
        float effBias = PENTODE_BIAS_7W_N + envBias[idx];
        printf("%4.2f  %+9.5f  %+9.5f  %10.1f\n",
               t, envBias[idx], effBias, envPlateI[idx]);
    }

    // ── Sag comparison ───────────────────────────────────────
    float peakFinal = -200.0f, dipFinal = 200.0f;
    int peakIdx = 0, dipIdx = 0;
    for (int i = 0; i < nWins && i < 40; ++i) {
        float db = toDB(envFinal[i]);
        if (db > peakFinal) { peakFinal = db; peakIdx = i; }
        if (db < dipFinal && i > 1)  { dipFinal = db; dipIdx = i; }
    }
    float peakNoSag = -200.0f, dipNoSag = 200.0f;
    for (int i = 0; i < nWins && i < 40; ++i) {
        float db = toDB(envNoSag[i]);
        if (db > peakNoSag) peakNoSag = db;
        if (db < dipNoSag && i > 1) dipNoSag = db;
    }
    float sagDip = peakFinal - dipFinal;
    float noSagDip = peakNoSag - dipNoSag;

    printf("\n=== SAG CONTRIBUTION ===\n");
    printf("With sag:    peak %.1f dB at %.2fs, dip %.1f dB at %.2fs → swing = %.1f dB\n",
           peakFinal, peakIdx * 0.05f, dipFinal, dipIdx * 0.05f, sagDip);
    printf("Without sag: peak %.1f dB, dip %.1f dB → swing = %.1f dB\n",
           peakNoSag, dipNoSag, noSagDip);
    printf("Sag contribution: %.1f dB\n", sagDip - noSagDip);

    float sagContrib = sagDip - noSagDip;
    CAPTURE(sagDip, noSagDip, sagContrib);
    // Sag should not add significant dip beyond natural signal dynamics
    REQUIRE(std::abs(sagContrib) < 3.0f);
}
