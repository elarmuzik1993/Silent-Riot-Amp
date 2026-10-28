// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Dynamics Diagnostic Tests — medium-level artifact investigation
//
// User-reported symptoms:
//   1. Medium dynamics: "LFO-like filtering", "not full", "broken"
//   2. Soft and aggressive playing: OK
//   3. Hard palm mutes: "muffled and filtered"
//
// These tests isolate each dynamics subsystem to identify which
// module(s) produce artifacts at medium playing levels.
// Tagged [diagnostic] for selective runs.
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "dsp/cabinet/SpeakerCoupling.h"
#include "dsp/cabinet/OTSaturation.h"
#include "dsp/power/SagEnvelope.h"

using namespace sr;
using namespace sr::test;
// Approx not needed for these tests — all checks use relational operators

// ── Helpers ─────────────────────────────────────────────────────

// Record gainModSmooth over a sustained tone
struct ModTrace {
    float minVal, maxVal, mean;
    float acRMS;        // RMS of AC component (ripple power)
    float modDepthPct;  // acRMS / mean × 100 (perceptual modulation depth)
};

static ModTrace traceSpeakerCouplingMod(float amplitude, float freqHz,
                                         float sr, int warmupSamples,
                                         int measureSamples) {
    SpeakerCoupling spk;
    spk.recalcFilters(sr);

    // Warmup — let envelopes settle
    for (int i = 0; i < warmupSamples; ++i)
        spk.process(amplitude * std::sin(2.0f * PI_F * freqHz * i / sr));

    // Measure — record gainModSmooth each sample
    std::vector<float> trace(measureSamples);
    for (int i = 0; i < measureSamples; ++i) {
        int t = warmupSamples + i;
        spk.process(amplitude * std::sin(2.0f * PI_F * freqHz * t / sr));
        trace[i] = spk.gainModSmooth;
    }

    float minVal = *std::min_element(trace.begin(), trace.end());
    float maxVal = *std::max_element(trace.begin(), trace.end());
    float sum = 0.0f;
    for (float v : trace) sum += v;
    float mean = sum / static_cast<float>(measureSamples);

    // RMS of AC component — measures ripple power regardless of frequency
    float acSum = 0.0f;
    for (float v : trace) {
        float ac = v - mean;
        acSum += ac * ac;
    }
    float acRMS = std::sqrt(acSum / static_cast<float>(measureSamples));
    float modDepthPct = (mean > 1e-10f) ? (acRMS / mean) * 100.0f : 0.0f;

    return { minVal, maxVal, mean, acRMS, modDepthPct };
}

// Measure OT saturation gain for a tone burst (attack + sustain)
static float measureOTBurstGain(float freqHz, float amplitude, float sr,
                                 int burstSamples, int silenceBefore = 0) {
    OTSaturation sat;
    sat.recalcCoeffs(sr);

    // Optional silence to reset state
    for (int i = 0; i < silenceBefore; ++i)
        sat.process(0.0f);

    // Warmup with the burst signal
    int warmup = static_cast<int>(sr * 0.05f);
    for (int i = 0; i < warmup; ++i)
        sat.process(amplitude * std::sin(2.0f * PI_F * freqHz * i / sr));

    // Measure steady-state gain
    float inPeak = 0.0f, outPeak = 0.0f;
    for (int i = warmup; i < warmup + burstSamples; ++i) {
        float in = amplitude * std::sin(2.0f * PI_F * freqHz * i / sr);
        float out = sat.process(in);
        inPeak = std::max(inPeak, std::abs(in));
        outPeak = std::max(outPeak, std::abs(out));
    }
    return (inPeak > 1e-10f) ? outPeak / inPeak : 1.0f;
}

// ═══════════════════════════════════════════════════════════════
// SUSPECT #1: Speaker Coupling — gainMod ripple at medium dynamics
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Diag: speaker coupling gainMod ripple at medium 82Hz", "[diagnostic][speaker]") {
    // 82Hz = low E string. Medium amplitude = 0.15 (between soft 0.02 and loud 0.5).
    float sr = TEST_SR_4X;
    int warmup = static_cast<int>(sr * 0.3f);   // 300ms settle
    int measure = static_cast<int>(sr * 0.5f);   // 500ms window

    auto trace = traceSpeakerCouplingMod(0.15f, 82.0f, sr, warmup, measure);
    float ripple = trace.maxVal - trace.minVal;

    CAPTURE(trace.minVal, trace.maxVal, trace.mean, ripple, trace.modDepthPct);

    // Peak-to-peak ripple < 10% of mean
    REQUIRE(ripple < trace.mean * 0.10f);
    // RMS modulation depth < 2% (above ~2% AM is perceptible on sustained tones)
    REQUIRE(trace.modDepthPct < 2.0f);
}

TEST_CASE("Diag: speaker coupling gainMod ripple at medium 150Hz", "[diagnostic][speaker]") {
    // 150Hz = palm mute region / power chord fundamental.
    float sr = TEST_SR_4X;
    int warmup = static_cast<int>(sr * 0.3f);
    int measure = static_cast<int>(sr * 0.5f);

    auto trace = traceSpeakerCouplingMod(0.15f, 150.0f, sr, warmup, measure);
    float ripple = trace.maxVal - trace.minVal;

    CAPTURE(trace.minVal, trace.maxVal, trace.mean, ripple, trace.modDepthPct);

    REQUIRE(ripple < trace.mean * 0.10f);
    REQUIRE(trace.modDepthPct < 2.0f);
}

TEST_CASE("Diag: speaker coupling medium ripple not worse than soft or loud", "[diagnostic][speaker]") {
    // If medium dynamics have MORE ripple than both soft and loud,
    // the coupling's tanh sensitivity curve is the culprit.
    float sr = TEST_SR_4X;
    int warmup = static_cast<int>(sr * 0.3f);
    int measure = static_cast<int>(sr * 0.5f);

    auto soft   = traceSpeakerCouplingMod(0.03f, 82.0f, sr, warmup, measure);
    auto medium = traceSpeakerCouplingMod(0.15f, 82.0f, sr, warmup, measure);
    auto loud   = traceSpeakerCouplingMod(0.50f, 82.0f, sr, warmup, measure);

    CAPTURE(soft.modDepthPct, medium.modDepthPct, loud.modDepthPct);
    CAPTURE(soft.mean, medium.mean, loud.mean);

    // Medium modulation depth should not exceed the worse of soft/loud by >2x
    float worstOther = std::max(soft.modDepthPct, loud.modDepthPct);
    REQUIRE(medium.modDepthPct < worstOther * 2.0f);
}

TEST_CASE("Diag: speaker coupling gainMod modulation depth below audible threshold", "[diagnostic][speaker]") {
    // Core perceptual test: at medium 82Hz, the RMS modulation depth of
    // gainModSmooth must be below the audibility threshold for AM (~2%).
    // This replaces the zero-crossings metric — frequency of the ripple doesn't
    // matter if the depth is inaudible.
    float sr = TEST_SR_4X;
    int warmup = static_cast<int>(sr * 0.3f);
    int measure = static_cast<int>(sr * 0.5f);

    auto trace = traceSpeakerCouplingMod(0.15f, 82.0f, sr, warmup, measure);

    CAPTURE(trace.acRMS, trace.mean, trace.modDepthPct);

    // AM perception threshold is ~1-2% for sustained tones.
    // Use 2% as the hard limit.
    REQUIRE(trace.modDepthPct < 2.0f);
}

// ═══════════════════════════════════════════════════════════════
// SUSPECT #2: Sag Envelope — oscillation at medium dynamics
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Diag: sag biasOffset stability at medium plate current", "[diagnostic][sag]") {
    SagEnvelope sag;
    sag.recalcCoeffs(TEST_SR_4X);

    // Simulate medium-dynamics plate current: oscillating around quiescent+20%
    // Real EL84 push-pull produces rectified-sine-like current.
    float baseCurrent = SAG_QUIESCENT_I_N * 1.2f;  // 20% above idle
    float rippleAmp = SAG_QUIESCENT_I_N * 0.3f;    // ±30% ripple
    float freq = 82.0f;
    float sr = TEST_SR_4X;

    // Warmup
    int warmup = static_cast<int>(sr * 0.5f);
    for (int i = 0; i < warmup; ++i) {
        float current = baseCurrent + rippleAmp * std::abs(
            std::sin(2.0f * PI_F * freq * i / sr));
        sag.update(current);
    }

    // Measure biasOffset stability over 500ms
    int measure = static_cast<int>(sr * 0.5f);
    float minOffset = 1.0f, maxOffset = -1.0f;
    std::vector<float> offsets(measure);
    for (int i = 0; i < measure; ++i) {
        int t = warmup + i;
        float current = baseCurrent + rippleAmp * std::abs(
            std::sin(2.0f * PI_F * freq * t / sr));
        sag.update(current);
        float off = sag.biasOffset();
        offsets[i] = off;
        minOffset = std::min(minOffset, off);
        maxOffset = std::max(maxOffset, off);
    }

    float offsetRange = maxOffset - minOffset;
    float meanOffset = (minOffset + maxOffset) * 0.5f;

    // Count direction changes (oscillation indicator)
    int dirChanges = 0;
    for (int i = 2; i < measure; ++i) {
        float d1 = offsets[i - 1] - offsets[i - 2];
        float d2 = offsets[i]     - offsets[i - 1];
        if (d1 * d2 < 0.0f) ++dirChanges;
    }
    float dirChangesPerMs = static_cast<float>(dirChanges) / (measure / sr * 1000.0f);

    CAPTURE(minOffset, maxOffset, offsetRange, meanOffset, dirChangesPerMs);

    // biasOffset should be relatively stable (not oscillating wildly)
    // Offset range > 0.005 (1.5V real) would indicate audible bias pumping
    REQUIRE(offsetRange < 0.005f);
}

TEST_CASE("Diag: sag biasOffset doesn't hit clamp at medium dynamics", "[diagnostic][sag]") {
    SagEnvelope sag;
    sag.recalcCoeffs(TEST_SR_4X);

    // Medium dynamics should NOT slam into the -0.02 clamp floor
    // (that would cause hard compression artifacts)
    float baseCurrent = SAG_QUIESCENT_I_N * 1.3f;
    float sr = TEST_SR_4X;

    for (int i = 0; i < static_cast<int>(sr * 1.0f); ++i)
        sag.update(baseCurrent);

    float offset = sag.biasOffset();
    CAPTURE(offset);

    // Should not be at the clamp floor (-0.02) for medium dynamics
    REQUIRE(offset > -0.019f);  // leave margin from the -0.02 clamp
}

// ═══════════════════════════════════════════════════════════════
// SUSPECT #3: OT Saturation — palm mute muffling
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Diag: OT saturation bass vs mid gain ratio at medium level", "[diagnostic][ot_sat]") {
    // Palm mutes live at ~80-150Hz. If OT compresses this band much harder
    // than 300-800Hz (the "body" of a normal note), palm mutes lose their punch.
    float sr = TEST_SR_4X;
    float amp = 0.15f;  // medium power amp output
    int burst = static_cast<int>(sr * 0.1f);

    float gain80  = measureOTBurstGain(80.0f,  amp, sr, burst);
    float gain150 = measureOTBurstGain(150.0f, amp, sr, burst);
    float gain400 = measureOTBurstGain(400.0f, amp, sr, burst);
    float gain1k  = measureOTBurstGain(1000.0f, amp, sr, burst);

    CAPTURE(gain80, gain150, gain400, gain1k);

    // Bass should not be compressed more than 6dB harder than mids
    // (> 6dB difference = audibly muffled palm mutes)
    float bassToMidRatio = gain80 / gain400;
    float palmToMidRatio = gain150 / gain400;
    CAPTURE(bassToMidRatio, palmToMidRatio);

    REQUIRE(bassToMidRatio > 0.5f);   // no more than 6dB extra compression
    REQUIRE(palmToMidRatio > 0.5f);
}

TEST_CASE("Diag: OT saturation palm mute burst preserves attack", "[diagnostic][ot_sat]") {
    // Palm mute = short, low-frequency burst. The OT integrator accumulates
    // flux during the burst. If drive is too high, the attack gets crushed.
    OTSaturation sat;
    float sr = TEST_SR_4X;
    sat.recalcCoeffs(sr);

    // Simulate a palm mute: 100Hz burst, 50ms duration, medium level
    float freq = 100.0f;
    float amp = 0.20f;
    int burstLen = static_cast<int>(sr * 0.05f);  // 50ms burst (5 cycles at 100Hz)

    // Process the burst
    float inPeak = 0.0f, outPeak = 0.0f;
    float inRMS = 0.0f, outRMS = 0.0f;
    for (int i = 0; i < burstLen; ++i) {
        float in = amp * std::sin(2.0f * PI_F * freq * i / sr);
        // Apply a fast exponential envelope (palm mute decay)
        float envelope = std::exp(-3.0f * static_cast<float>(i) / burstLen);
        in *= envelope;

        float out = sat.process(in);
        inPeak = std::max(inPeak, std::abs(in));
        outPeak = std::max(outPeak, std::abs(out));
        inRMS += in * in;
        outRMS += out * out;
    }
    inRMS = std::sqrt(inRMS / burstLen);
    outRMS = std::sqrt(outRMS / burstLen);

    float peakGain = outPeak / inPeak;
    float rmsGain = outRMS / inRMS;

    CAPTURE(peakGain, rmsGain, inPeak, outPeak);

    // Peak gain should stay above 0.7 (no more than ~3dB of attack crushing)
    REQUIRE(peakGain > 0.7f);
    // RMS shouldn't drop more than 6dB
    REQUIRE(rmsGain > 0.5f);
}

TEST_CASE("Diag: OT saturation drive=400 vs lower drive comparison", "[diagnostic][ot_sat]") {
    // Sweep OT_SAT_DRIVE to see how much the current value (400) squashes
    // palm-mute-range frequencies vs a gentler setting
    float sr = TEST_SR_4X;
    float freq = 100.0f;
    float amp = 0.20f;

    auto measureWithDrive = [&](float drive) {
        OTSaturation sat;
        sat.recalcCoeffs(sr);
        sat.drive = drive;  // override

        int warmup = static_cast<int>(sr * 0.05f);
        int measure = static_cast<int>(sr * 0.1f);

        for (int i = 0; i < warmup; ++i)
            sat.process(amp * std::sin(2.0f * PI_F * freq * i / sr));

        float inPeak = 0.0f, outPeak = 0.0f;
        for (int i = warmup; i < warmup + measure; ++i) {
            float in = amp * std::sin(2.0f * PI_F * freq * i / sr);
            float out = sat.process(in);
            inPeak = std::max(inPeak, std::abs(in));
            outPeak = std::max(outPeak, std::abs(out));
        }
        return outPeak / inPeak;
    };

    float gain100 = measureWithDrive(100.0f);
    float gain200 = measureWithDrive(200.0f);
    float gain400 = measureWithDrive(400.0f);

    CAPTURE(gain100, gain200, gain400);

    // At drive=400, bass should not be more than 50% quieter than drive=100
    float dropRatio = gain400 / gain100;
    CAPTURE(dropRatio);

    REQUIRE(dropRatio > 0.5f);
}

// ═══════════════════════════════════════════════════════════════
// COMBINED: Speaker coupling → OT at medium dynamics
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Diag: coupling + OT combined modulation at medium level", "[diagnostic][combined]") {
    // Chain speaker coupling → OT saturation with a sustained medium-level
    // 82Hz tone. Measure the output amplitude modulation depth.
    float sr = TEST_SR_4X;
    float freq = 82.0f;
    float amp = 0.15f;

    SpeakerCoupling spk;
    spk.recalcFilters(sr);
    OTSaturation ot;
    ot.recalcCoeffs(sr);

    // Warmup
    int warmup = static_cast<int>(sr * 0.3f);
    for (int i = 0; i < warmup; ++i) {
        float in = amp * std::sin(2.0f * PI_F * freq * i / sr);
        float coupled = spk.process(in);
        ot.process(coupled);
    }

    // Measure: collect peak per cycle
    int samplesPerCycle = static_cast<int>(sr / freq);
    int numCycles = 40;
    std::vector<float> cyclePeaks(numCycles);

    for (int c = 0; c < numCycles; ++c) {
        float peak = 0.0f;
        for (int s = 0; s < samplesPerCycle; ++s) {
            int t = warmup + c * samplesPerCycle + s;
            float in = amp * std::sin(2.0f * PI_F * freq * t / sr);
            float coupled = spk.process(in);
            float out = ot.process(coupled);
            peak = std::max(peak, std::abs(out));
        }
        cyclePeaks[c] = peak;
    }

    float minPeak = *std::min_element(cyclePeaks.begin(), cyclePeaks.end());
    float maxPeak = *std::max_element(cyclePeaks.begin(), cyclePeaks.end());
    float modDepth = (maxPeak - minPeak) / (maxPeak + minPeak);  // AM modulation index

    CAPTURE(minPeak, maxPeak, modDepth);

    // AM modulation index > 0.15 (~1.5dB cycle-to-cycle variation)
    // would be audible as "not full / broken"
    REQUIRE(modDepth < 0.15f);
}

TEST_CASE("Diag: coupling + OT palm mute chain", "[diagnostic][combined]") {
    // Palm mute through the full coupling + OT chain at hard level.
    // Measure whether the combined processing muffles the attack.
    float sr = TEST_SR_4X;
    float freq = 100.0f;
    float amp = 0.30f;  // hard palm mute

    SpeakerCoupling spk;
    spk.recalcFilters(sr);
    OTSaturation ot;
    ot.recalcCoeffs(sr);

    // Small warmup with silence
    for (int i = 0; i < 1000; ++i) {
        spk.process(0.0f);
        ot.process(0.0f);
    }

    // Feed palm mute burst: 50ms, fast exponential decay
    int burstLen = static_cast<int>(sr * 0.05f);
    float directPeak = 0.0f, chainPeak = 0.0f;

    SpeakerCoupling spkRef;
    spkRef.recalcFilters(sr);
    OTSaturation otRef;
    otRef.recalcCoeffs(sr);
    for (int i = 0; i < 1000; ++i) {
        spkRef.process(0.0f);
        otRef.process(0.0f);
    }

    for (int i = 0; i < burstLen; ++i) {
        float in = amp * std::sin(2.0f * PI_F * freq * i / sr);
        float envelope = std::exp(-3.0f * static_cast<float>(i) / burstLen);
        in *= envelope;

        float coupled = spk.process(in);
        float out = ot.process(coupled);

        directPeak = std::max(directPeak, std::abs(in));
        chainPeak  = std::max(chainPeak, std::abs(out));
    }

    float chainGain = chainPeak / directPeak;
    CAPTURE(directPeak, chainPeak, chainGain);

    // Combined chain should not crush palm mute attack below 60% (-4.4dB)
    REQUIRE(chainGain > 0.60f);
}
