// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
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

// The DELAY TIME knob doubles as the division selector while SYNC is on: its
// 0..1 travel is cut into one equal detent per division (MANUAL excluded),
// slowest division at the top. These two are each other's inverse on the
// detents, so a division round-trips through the knob unchanged.
inline constexpr int kNumSyncSteps = kNumSyncDivisions - 1;
inline constexpr int kDefaultSyncDivision = 5;   // "1/4" — what SYNC selects first

inline int syncDivisionFromKnob(float knob)
{
    if (!(knob > 0.0f)) return 1;                       // also catches NaN
    const float step = std::round(std::min(knob, 1.0f) * static_cast<float>(kNumSyncSteps - 1));
    return 1 + static_cast<int>(step);
}

inline float knobFromSyncDivision(int divisionIndex)
{
    const int idx = std::clamp(divisionIndex, 1, kNumSyncDivisions - 1);
    return static_cast<float>(idx - 1) / static_cast<float>(kNumSyncSteps - 1);
}

// Delay time in ms for a given division index (1..kNumSyncDivisions-1) at bpm,
// clamped to DELAY_TIME_MIN_MS..DELAY_SYNC_MAX_MS — further than the TIME knob
// reaches (DELAY_TIME_KNOB_MAX_MS), so slow divisions keep their length. divisionIndex==0 (MANUAL) returns
// DELAY_TIME_MIN_MS — callers should not sync-drive the delay in that case.
inline float computeSyncMs(int divisionIndex, double bpm)
{
    // A malformed/buggy host could report a non-finite or non-positive tempo;
    // guard against it explicitly since bpm<=0.0 alone is false for NaN and
    // would otherwise poison the delay line (and its filters) permanently.
    if (divisionIndex <= 0 || divisionIndex >= kNumSyncDivisions
        || !(bpm > 0.0) || !std::isfinite(bpm))
        return DELAY_TIME_MIN_MS;

    const float ms = static_cast<float>(60000.0 / bpm) * kSyncDivisions[divisionIndex].qnMultiplier;
    return std::clamp(ms, DELAY_TIME_MIN_MS, DELAY_SYNC_MAX_MS);
}

} // namespace sr
