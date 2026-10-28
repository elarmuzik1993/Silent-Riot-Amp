// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
// ═══════════════════════════════════════════════════════════════
// OnePoleLP / OnePoleHP — BLT with pre-warping (Spec §2.6.2)
// Used for: coupling caps, cathode bypass, Miller, tone cut
// ═══════════════════════════════════════════════════════════════

#include <cmath>
#include "../../utils/Constants.h"

namespace sr {

// Run a filter on a constant input until its float output stops changing (or a
// cap is hit) and return where it rests. For seedDC, prepare-time only.
template <typename Filter>
float settleOn(Filter& f, float x) {
    constexpr int kMaxSteps = 1 << 17, kStillFor = 256;
    float y = f.process(x);
    for (int n = 0, still = 0; n < kMaxSteps && still < kStillFor; ++n) {
        const float next = f.process(x);
        // Below float resolution relative to the output: no longer moving.
        still = (std::abs(next - y) <= 1e-9f * std::abs(y)) ? still + 1 : 0;
        y = next;
    }
    return y;
}

struct OnePoleLP {
    float b0 = 0.0f, b1 = 0.0f, a1 = 0.0f;
    float z1 = 0.0f;
    float fc = 0.0f;

    void setFreq(float newFc, float sampleRate) {
        fc = newFc;
        float wa = 2.0f * sampleRate * std::tan(PI_F * fc / sampleRate);
        float g  = wa / (2.0f * sampleRate);
        b0 = g / (1.0f + g);
        b1 = b0;
        a1 = (g - 1.0f) / (1.0f + g);
    }

    void setFreqFast(float newFc, float sampleRate) {
        fc = newFc;
        float g = PI_F * fc / sampleRate;
        b0 = g / (1.0f + g);
        b1 = b0;
        a1 = (g - 1.0f) / (1.0f + g);
    }

    float process(float x) {
        float y = x * b0 + z1;
        z1 = x * b1 - y * a1 + ANTI_DENORMAL;
        return y;
    }

    void reset() { z1 = 0.0f; }

    // Set the state to where a constant input x settles, so a filter that sits
    // on a DC operating point starts there instead of stepping from zero
    // (review B1). Returns that settled output, for seeding the next stage.
    float seedDC(float x) {
        // Exact steady state, using the realised DC gain (b0+b1)/(1+a1)...
        const double y = x * (static_cast<double>(b0) + b1) / (1.0 + a1);
        z1 = static_cast<float>(x * static_cast<double>(b1) - y * a1);
        // ...then let the float recurrence come to rest. At a few Hz on an
        // oversampled rate its loop gain 1/(1+a1) is ~7e3, so rounding alone
        // can park it ~1e-4 away from the exact value, which a coupling cap
        // behind it passes on as a mV step (measured: 4.4 mV in 7W).
        return settleOn(*this, x);
    }
};

struct OnePoleHP {
    float b0 = 0.0f, b1 = 0.0f, a1 = 0.0f;
    float z1 = 0.0f;
    float fc = 0.0f;

    void setFreq(float newFc, float sampleRate) {
        fc = newFc;
        float wa = 2.0f * sampleRate * std::tan(PI_F * fc / sampleRate);
        float g  = wa / (2.0f * sampleRate);
        b0 = 1.0f / (1.0f + g);
        b1 = -b0;
        a1 = (g - 1.0f) / (1.0f + g);
    }

    void setFreqFast(float newFc, float sampleRate) {
        fc = newFc;
        float g = PI_F * fc / sampleRate;
        b0 = 1.0f / (1.0f + g);
        b1 = -b0;
        a1 = (g - 1.0f) / (1.0f + g);
    }

    float process(float x) {
        float y = x * b0 + z1;
        z1 = x * b1 - y * a1 + ANTI_DENORMAL;
        return y;
    }

    void reset() { z1 = 0.0f; }

    // Set the state to where a constant input x settles (output ~0), so a
    // coupling cap behind a DC plate voltage starts charged (review B1).
    // Returns that settled output. See OnePoleLP::seedDC for the float settle.
    float seedDC(float x) {
        z1 = x * b1;
        return settleOn(*this, x);
    }
};

} // namespace sr
