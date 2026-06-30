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
        float level = std::abs(x);
        envelope = level + (envelope - level) * detectCoeff;

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
