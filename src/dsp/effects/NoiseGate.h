// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
// ═══════════════════════════════════════════════════════════════
// NoiseGate — front-of-amp downward gate (pure C++, header-only)
//
// Peak envelope follower → threshold compare with hysteresis →
// smoothed gain via one-pole attack/release. Runs at base sample rate
// BEFORE the oversampled amp loop, so it never touches the amp tone.
// Detector keys on the raw guitar input (its own input).
// ═══════════════════════════════════════════════════════════════

#include "../../utils/Constants.h"
#include <cmath>
#include <algorithm>

namespace sr {

// The gate has no ON/OFF toggle — THRESHOLD 0 means off. The caller's gate
// block must therefore keep running while the SMOOTHED threshold is still
// ramping down to the floor, not just while the raw knob is off 0. Cutting the
// block dead the instant the raw knob reaches 0 leaves the gate mid-attenuation
// and steps the applied gain from 0 to 1 in a single sample — an audible click.
// Letting the smoother finish sweeps the threshold below the signal, so the
// gate opens through its own attack ramp and lands on the true-bypass branch
// already at unity.
inline bool gateShouldRun(float rawThreshKnob, float smoothedThreshKnob) {
    return rawThreshKnob > 0.0f || smoothedThreshKnob > 1.0e-5f;
}

struct NoiseGate {
    // Coefficients (recomputed on rate change)
    float detectCoeff  = 0.0f;   // envelope follower smoothing
    float attackCoeff  = 0.0f;   // gain rises (gate opening)
    float releaseCoeff = 0.0f;   // gain falls (gate closing)

    // State
    float envelope = 0.0f;       // detector envelope (linear)
    float gain     = 1.0f;       // current gain applied (smoothed 0..1)
    bool  open     = false;      // hysteresis latch

    static float coeffFor(float ms, float sampleRate) {
        return std::exp(-1.0f / (std::max(ms, 0.01f) * 0.001f * sampleRate));
    }

    void recalcCoeffs(float sampleRate) {
        detectCoeff  = coeffFor(GATE_DETECT_MS,  sampleRate);
        attackCoeff  = coeffFor(GATE_ATTACK_MS,  sampleRate);
        releaseCoeff = coeffFor(GATE_RELEASE_MS, sampleRate);
    }

    void reset() {
        envelope = 0.0f;
        gain     = 1.0f;
        open     = false;
    }

    // thresholdDb: open threshold. Gate closes once the signal drops
    // GATE_HYSTERESIS_DB below it, preventing chatter near the boundary.
    float process(float x, float thresholdDb) {
        // Threshold knob parked at 0 (maps to GATE_THRESH_MIN_DB): the gate is
        // OFF — true bypass, not just "-80 dB / effectively open". Pass the
        // sample untouched and hold the gate fully open so there's no residual
        // gating of near-silence and no click when the knob leaves 0.
        if (thresholdDb <= GATE_THRESH_MIN_DB) {
            envelope = std::abs(x);
            open     = true;
            gain     = 1.0f;
            return x;
        }

        // Peak follower: instant attack, smoothed release. Tracks the signal
        // PEAK (not its average) so the threshold is peak-referenced and the
        // envelope holds near the peak between cycles — keeping the open/close
        // decision stable across the hysteresis window.
        float level = std::abs(x);
        envelope = (level > envelope)
                 ? level
                 : level + (envelope - level) * detectCoeff;

        const float openLin  = std::pow(10.0f, thresholdDb / 20.0f);
        const float closeLin = std::pow(10.0f, (thresholdDb - GATE_HYSTERESIS_DB) / 20.0f);

        if (open) {
            if (envelope < closeLin) open = false;
        } else {
            if (envelope > openLin)  open = true;
        }

        const float targetGain = open ? 1.0f : 0.0f;
        const float c = (targetGain > gain) ? attackCoeff : releaseCoeff;
        gain = targetGain + (gain - targetGain) * c;
        if (gain < 1e-10f) gain = 0.0f;

        return x * gain;
    }
};

} // namespace sr
