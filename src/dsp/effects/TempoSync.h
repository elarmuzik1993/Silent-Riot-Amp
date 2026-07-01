#pragma once
// ═══════════════════════════════════════════════════════════════
// TempoSync — pure, unit-testable helper mapping a host BPM + a named
// note division to a delay time in ms. No JUCE playhead dependency;
// PluginProcessor reads the host tempo and calls computeSyncMs().
// Index 0 ("MANUAL") means "use the TIME knob instead" — callers should
// check for that before calling computeSyncMs().
// ═══════════════════════════════════════════════════════════════

#include "../../utils/Constants.h"
#include <algorithm>
#include <cmath>

namespace sr {

struct SyncDivision {
    const char* name;
    float       qnMultiplier; // relative to a quarter note
};

// Common guitar-delay divisions. Index 0 is the "off" sentinel.
inline constexpr SyncDivision kSyncDivisions[] = {
    { "MANUAL", 0.0f },
    { "1/16",   0.25f },
    { "1/8T",   1.0f / 3.0f },
    { "1/8",    0.5f },
    { "1/8D",   0.75f },
    { "1/4",    1.0f },
    { "1/4D",   1.5f },
    { "1/2",    2.0f },
};
inline constexpr int kNumSyncDivisions = static_cast<int>(sizeof(kSyncDivisions) / sizeof(kSyncDivisions[0]));

// Delay time in ms for a given division index (1..kNumSyncDivisions-1) at bpm,
// clamped to the delay's usable time range. divisionIndex==0 (MANUAL) returns
// DELAY_TIME_MIN_MS — callers should not sync-drive the delay in that case.
inline float computeSyncMs(int divisionIndex, double bpm)
{
    if (divisionIndex <= 0 || divisionIndex >= kNumSyncDivisions || bpm <= 0.0)
        return DELAY_TIME_MIN_MS;

    const float ms = static_cast<float>(60000.0 / bpm) * kSyncDivisions[divisionIndex].qnMultiplier;
    return std::clamp(ms, DELAY_TIME_MIN_MS, DELAY_TIME_MAX_MS);
}

} // namespace sr
