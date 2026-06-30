#pragma once
// ═══════════════════════════════════════════════════════════════
// softClip — stateless soft-clip safety limiter (pure C++, header-only)
//
// The amp's output tanh limiter runs BEFORE the post-cab delay/reverb, so the
// wet tails can sum past full-scale and hard-clip the DAC. This catches that:
// it is exactly transparent (identity) below `threshold`, and smoothly
// saturates toward ±1 above it. C¹-continuous at the knee (unit slope on both
// sides), so it never colours normal-level signal — only tames peaks.
//
// Only invoked when an effect is active, so the amp's dry path is unchanged.
// ═══════════════════════════════════════════════════════════════

#include "../../utils/Constants.h"
#include <cmath>

namespace sr {

inline float softClip(float x, float threshold = SAFETY_LIMIT_THRESH) {
    const float a = std::abs(x);
    if (a <= threshold) return x;          // transparent below the knee
    const float s = (x < 0.0f) ? -1.0f : 1.0f;
    const float over = (a - threshold) / (1.0f - threshold);
    return s * (threshold + (1.0f - threshold) * std::tanh(over));
}

} // namespace sr
