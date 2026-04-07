#pragma once
// ═══════════════════════════════════════════════════════════════
// Biquad — Transposed Direct Form II (Spec §2.6.2)
// RBJ cookbook designs. Zero-alloc, anti-denormal.
// Used for: OT leakage, cab sim, speaker coupling band extraction
// ═══════════════════════════════════════════════════════════════

#include <cmath>
#include <algorithm>
#include "../../utils/Constants.h"

namespace sr {

struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
    float fc = 0.0f, Q = 0.707f, gainDb = 0.0f;

    float process(float x) {
        float y = x * b0 + z1;
        z1 = x * b1 - y * a1 + z2 + ANTI_DENORMAL;
        z2 = x * b2 - y * a2 + ANTI_DENORMAL;
        return y;
    }

    void reset() { z1 = z2 = 0.0f; }

    void makeLowPass(float newFc, float newQ, float sr) {
        fc = newFc; Q = newQ;
        float w0 = 2.0f * PI_F * fc / sr;
        float sinW0 = std::sin(w0), cosW0 = std::cos(w0);
        float alpha = sinW0 / (2.0f * Q);
        float a0_inv = 1.0f / (1.0f + alpha);
        b0 = ((1.0f - cosW0) * 0.5f) * a0_inv;
        b1 = (1.0f - cosW0) * a0_inv;
        b2 = b0;
        a1 = (-2.0f * cosW0) * a0_inv;
        a2 = (1.0f - alpha) * a0_inv;
    }

    void makeHighPass(float newFc, float newQ, float sr) {
        fc = newFc; Q = newQ;
        float w0 = 2.0f * PI_F * fc / sr;
        float sinW0 = std::sin(w0), cosW0 = std::cos(w0);
        float alpha = sinW0 / (2.0f * Q);
        float a0_inv = 1.0f / (1.0f + alpha);
        b0 = ((1.0f + cosW0) * 0.5f) * a0_inv;
        b1 = -(1.0f + cosW0) * a0_inv;
        b2 = b0;
        a1 = (-2.0f * cosW0) * a0_inv;
        a2 = (1.0f - alpha) * a0_inv;
    }

    void makeBandPass(float newFc, float newQ, float sr) {
        fc = newFc; Q = newQ;
        float w0 = 2.0f * PI_F * fc / sr;
        float sinW0 = std::sin(w0), cosW0 = std::cos(w0);
        float alpha = sinW0 / (2.0f * Q);
        float a0_inv = 1.0f / (1.0f + alpha);
        b0 = alpha * a0_inv;
        b1 = 0.0f;
        b2 = -alpha * a0_inv;
        a1 = (-2.0f * cosW0) * a0_inv;
        a2 = (1.0f - alpha) * a0_inv;
    }

    void makePeakEQ(float newFc, float newQ, float newGainDb, float sr) {
        fc = newFc; Q = newQ; gainDb = newGainDb;
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * PI_F * fc / sr;
        float sinW0 = std::sin(w0), cosW0 = std::cos(w0);
        float alpha = sinW0 / (2.0f * Q);
        float a0_inv = 1.0f / (1.0f + alpha / A);
        b0 = (1.0f + alpha * A) * a0_inv;
        b1 = (-2.0f * cosW0) * a0_inv;
        b2 = (1.0f - alpha * A) * a0_inv;
        a1 = b1;
        a2 = (1.0f - alpha / A) * a0_inv;
    }

    void makeNotch(float newFc, float newQ, float sr) {
        fc = newFc; Q = newQ;
        float w0 = 2.0f * PI_F * fc / sr;
        float sinW0 = std::sin(w0), cosW0 = std::cos(w0);
        float alpha = sinW0 / (2.0f * Q);
        float a0_inv = 1.0f / (1.0f + alpha);
        b0 = a0_inv;
        b1 = (-2.0f * cosW0) * a0_inv;
        b2 = b0;
        a1 = b1;
        a2 = (1.0f - alpha) * a0_inv;
    }

    void makeLowShelf(float newFc, float newQ, float newGainDb, float sr) {
        fc = newFc; Q = newQ; gainDb = newGainDb;
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * PI_F * fc / sr;
        float sinW0 = std::sin(w0), cosW0 = std::cos(w0);
        float alpha = sinW0 / (2.0f * Q);
        float twoSqrtAalpha = 2.0f * std::sqrt(A) * alpha;
        float a0_inv = 1.0f / ((A + 1.0f) + (A - 1.0f) * cosW0 + twoSqrtAalpha);
        b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosW0 + twoSqrtAalpha)) * a0_inv;
        b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW0)) * a0_inv;
        b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosW0 - twoSqrtAalpha)) * a0_inv;
        a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosW0)) * a0_inv;
        a2 = ((A + 1.0f) + (A - 1.0f) * cosW0 - twoSqrtAalpha) * a0_inv;
    }

    void makeHighShelf(float newFc, float newQ, float newGainDb, float sr) {
        fc = newFc; Q = newQ; gainDb = newGainDb;
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * PI_F * fc / sr;
        float sinW0 = std::sin(w0), cosW0 = std::cos(w0);
        float alpha = sinW0 / (2.0f * Q);
        float twoSqrtAalpha = 2.0f * std::sqrt(A) * alpha;
        float a0_inv = 1.0f / ((A + 1.0f) - (A - 1.0f) * cosW0 + twoSqrtAalpha);
        b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosW0 + twoSqrtAalpha)) * a0_inv;
        b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW0)) * a0_inv;
        b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosW0 - twoSqrtAalpha)) * a0_inv;
        a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosW0)) * a0_inv;
        a2 = ((A + 1.0f) - (A - 1.0f) * cosW0 - twoSqrtAalpha) * a0_inv;
    }
};

} // namespace sr
