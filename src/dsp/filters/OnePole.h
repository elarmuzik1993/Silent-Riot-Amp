#pragma once
// ═══════════════════════════════════════════════════════════════
// OnePoleLP / OnePoleHP — BLT with pre-warping (Spec §2.6.2)
// Used for: coupling caps, cathode bypass, Miller, tone cut
// ═══════════════════════════════════════════════════════════════

#include <cmath>
#include "../../utils/Constants.h"

namespace sr {

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
};

} // namespace sr
