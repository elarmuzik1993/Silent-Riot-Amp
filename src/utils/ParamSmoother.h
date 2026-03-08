#pragma once
#include <cmath>

namespace tt {

struct ParamSmoother {
    float current = 0.0f;
    float decay   = 0.999f;

    void setTarget(float target) {
        current = target + (current - target) * decay;
    }

    void init(float tauMs, float controlRate) {
        decay = std::exp(-1.0f / (tauMs * 0.001f * controlRate));
    }

    void setImmediate(float value) { current = value; }
};

} // namespace tt
