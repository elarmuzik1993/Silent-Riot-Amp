#pragma once
// ═══════════════════════════════════════════════════════════════
// StereoWidth — final-stage stereo widener (pure C++, header-only)
//
// A single autonomous "width" knob (no on/off toggle): 0 = OFF — the dual-mono
// dry amp is left mono/unchanged — rising to 1 = maximum wide. Runs LAST in the
// chain, on the finished stereo bus, so it widens the whole mix (amp + delay +
// reverb). Additive only: it never narrows below the input.
//
// The amp/cab path is dual-mono, so a plain M/S width scale has no "side" to
// act on until the stereo time-FX add some. To get width on the dry amp too
// (the way a NeuralDSP-style doubler does), we SYNTHESISE decorrelated content
// — a short Haas delay of the Mid through an all-pass — and inject it into the
// SIDE only. Because the output is Mid ± Side, the mono sum L+R = 2·Mid is
// invariant at any setting: perfect mono compatibility, no comb filtering when
// a listener folds to mono, regardless of how wide the knob is pushed.
// ═══════════════════════════════════════════════════════════════

#include "../../utils/Constants.h"
#include <cmath>
#include <vector>
#include <algorithm>

namespace sr {

struct StereoWidth {
    // Decorrelator: Haas delay line (fed by Mid) → one-pole all-pass.
    std::vector<float> delayBuf;
    int   writePos    = 0;
    int   haasSamples = 1;
    float apX1 = 0.0f, apY1 = 0.0f;   // all-pass state

    // Smoothed knob value (one-pole toward target). 0 = off.
    float width       = 0.0f;
    float smoothCoeff = 0.0f;

    void prepare(float sampleRate) {
        haasSamples = std::max(1, static_cast<int>(std::round(WIDTH_HAAS_MS * 0.001f * sampleRate)));
        delayBuf.assign(static_cast<size_t>(haasSamples) + 4, 0.0f);
        writePos    = 0;
        smoothCoeff = std::exp(-1.0f / (std::max(WIDTH_SMOOTH_MS, 0.01f) * 0.001f * sampleRate));
        reset();
    }

    void reset() {
        std::fill(delayBuf.begin(), delayBuf.end(), 0.0f);
        writePos = 0;
        apX1 = apY1 = 0.0f;
        width = 0.0f;   // park at OFF so isActive() reports dormant until moved
    }

    // True once the smoothed width has left 0 — lets the caller skip the whole
    // stage (bit-exact dry) while the knob sits parked at OFF.
    bool isActive() const { return width > 1e-5f; }

    // Widen a stereo pair in place. `target` is the raw 0..1 knob value.
    // Caller guarantees left/right are two valid channels of `numSamples`.
    void process(float* left, float* right, int numSamples, float target) {
        for (int i = 0; i < numSamples; ++i) {
            width = target + (width - target) * smoothCoeff;

            const float L = left[i];
            const float R = right[i];
            const float mid  = 0.5f * (L + R);
            const float side = 0.5f * (L - R);

            // Knob → side gain + synthesised-decorrelation amount. Additive:
            //   0 : off — sideGain 1 (preserve), synth 0 (unity passthrough)
            //   1 : max — sideGain MAX (lift existing side) + full decorrelation
            // The synth term is what gives the dual-mono dry amp its width.
            const float sideGain = 1.0f + width * (WIDTH_MAX_SIDE_GAIN - 1.0f);
            const float synth    = width * WIDTH_SYNTH_MAX;

            // Decorrelated signal from delayed Mid (Haas) → all-pass diffusion.
            float d = 0.0f;
            if (synth > 0.0f) {
                int readPos = writePos - haasSamples;
                if (readPos < 0) readPos += static_cast<int>(delayBuf.size());
                const float delayed = delayBuf[static_cast<size_t>(readPos)];
                const float ap = -WIDTH_ALLPASS_COEFF * delayed + apX1 + WIDTH_ALLPASS_COEFF * apY1;
                apX1 = delayed;
                apY1 = ap;
                d = ap;
            }

            // Prime the delay line every sample so it's ready when synth engages.
            delayBuf[static_cast<size_t>(writePos)] = mid + ANTI_DENORMAL;
            if (++writePos >= static_cast<int>(delayBuf.size())) writePos = 0;

            const float newSide = side * sideGain + d * synth;
            left[i]  = mid + newSide;
            right[i] = mid - newSide;   // L+R = 2·mid always → mono sum preserved
        }
    }
};

} // namespace sr
