#pragma once
// Cathode Bias Sag (Spec §3.8)
#include "../../utils/Constants.h"
#include <cmath>
#include <algorithm>

namespace tt {

struct SagEnvelope {
    float envelope     = 0.0f;
    float attackDecay  = 0.999f;
    float releaseDecay = 0.999f;

    void recalcCoeffs(float effectiveSR) {
        attackDecay  = std::exp(-1.0f / (SAG_ATTACK_MS  * 0.001f * effectiveSR));
        releaseDecay = std::exp(-1.0f / (SAG_RELEASE_MS * 0.001f * effectiveSR));
    }

    void update(float powerOut) {
        float level = std::abs(powerOut);
        float d = (level > envelope) ? attackDecay : releaseDecay;
        envelope = level + (envelope - level) * d;
        if (envelope < 1e-10f) envelope = 0.0f;
    }

    // Returns NEGATIVE bias offset (shifts bias colder under load)
    float biasOffset() const {
        return -envelope * SAG_DEPTH;
    }
};

} // namespace tt
