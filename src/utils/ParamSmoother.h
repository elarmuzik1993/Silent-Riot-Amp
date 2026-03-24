#pragma once
#include <cmath>

namespace sr {

struct ParamSmoother {
    float current = 0.0f;
    float target  = 0.0f;
    float decay   = 0.999f;

    void setTarget(float t) { target = t; }

    float tick() {
        current = target + (current - target) * decay;
        return current;
    }

    void init(float tauMs, float sampleRate) {
        decay = std::exp(-1.0f / (tauMs * 0.001f * sampleRate));
    }

    void setImmediate(float value) { current = target = value; }
};

} // namespace sr
