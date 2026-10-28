// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// TempoSync Tests — pure BPM/division → delay-ms mapping (no JUCE deps)
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/effects/TempoSync.h"
#include <cstring>
#include <cmath>
#include <limits>

using namespace sr;

namespace {
int findDivisionIndex(const char* name)
{
    for (int i = 0; i < kNumSyncDivisions; ++i)
        if (std::strcmp(kSyncDivisions[i].name, name) == 0) return i;
    return -1;
}
} // namespace

TEST_CASE("computeSyncMs: quarter note at 120 BPM is 500 ms", "[tempoSync]")
{
    const int idx = findDivisionIndex("1/4");
    REQUIRE(idx > 0);
    REQUIRE(computeSyncMs(idx, 120.0) == Catch::Approx(500.0f));
}

TEST_CASE("computeSyncMs: eighth note is half the quarter note", "[tempoSync]")
{
    const int idxQuarter = findDivisionIndex("1/4");
    const int idxEighth  = findDivisionIndex("1/8");
    REQUIRE(idxQuarter > 0);
    REQUIRE(idxEighth > 0);

    const double bpm = 90.0;
    REQUIRE(computeSyncMs(idxEighth, bpm) == Catch::Approx(computeSyncMs(idxQuarter, bpm) * 0.5f));
}

TEST_CASE("computeSyncMs: MANUAL index and invalid bpm return the floor", "[tempoSync]")
{
    REQUIRE(computeSyncMs(0, 120.0) == Catch::Approx(DELAY_TIME_MIN_MS));
    REQUIRE(computeSyncMs(1, 0.0) == Catch::Approx(DELAY_TIME_MIN_MS));
    REQUIRE(computeSyncMs(-1, 120.0) == Catch::Approx(DELAY_TIME_MIN_MS));
    REQUIRE(computeSyncMs(kNumSyncDivisions, 120.0) == Catch::Approx(DELAY_TIME_MIN_MS));
}

TEST_CASE("computeSyncMs: clamps to the delay's usable time range", "[tempoSync]")
{
    // A half-note at a very slow tempo would exceed DELAY_SYNC_MAX_MS.
    const int idxHalf = findDivisionIndex("1/2");
    REQUIRE(idxHalf > 0);
    REQUIRE(computeSyncMs(idxHalf, 20.0) == Catch::Approx(DELAY_SYNC_MAX_MS));
    REQUIRE(computeSyncMs(idxHalf, 59.0) == Catch::Approx(DELAY_SYNC_MAX_MS));

    // A 1/16th at a very fast tempo would fall below DELAY_TIME_MIN_MS.
    const int idx16 = findDivisionIndex("1/16");
    REQUIRE(idx16 > 0);
    REQUIRE(computeSyncMs(idx16, 400.0) >= DELAY_TIME_MIN_MS);
}

TEST_CASE("computeSyncMs: slow divisions run past the TIME knob's 1000 ms (review S7)", "[tempoSync]")
{
    // The sync clamp used to be the knob's 1000 ms, which silently cut 1/2 short
    // below 120 BPM, 1/4D below 90 and 1/4 below 60. It is 2000 ms now; the
    // knob's range is unchanged.
    REQUIRE(DELAY_SYNC_MAX_MS == 2000.0f);
    REQUIRE(DELAY_TIME_KNOB_MAX_MS == 1000.0f);
    REQUIRE(computeSyncMs(findDivisionIndex("1/2"),  60.0) == Catch::Approx(2000.0f));
    REQUIRE(computeSyncMs(findDivisionIndex("1/2"),  90.0) == Catch::Approx(1333.333f));
    REQUIRE(computeSyncMs(findDivisionIndex("1/4D"), 60.0) == Catch::Approx(1500.0f));
    REQUIRE(computeSyncMs(findDivisionIndex("1/4"),  40.0) == Catch::Approx(1500.0f));
    REQUIRE(computeSyncMs(findDivisionIndex("1/4"),  20.0) == Catch::Approx(DELAY_SYNC_MAX_MS));
}

TEST_CASE("computeSyncMs: a non-finite or NaN host bpm never poisons the result", "[tempoSync]")
{
    // A malformed host could report NaN/inf tempo. bpm<=0.0 alone is false for
    // NaN (all comparisons with NaN are false), so this guards a real gap:
    // without it, a NaN would flow straight into the delay line and its
    // filters, which never recover from NaN state.
    const int idxQuarter = findDivisionIndex("1/4");
    REQUIRE(idxQuarter > 0);

    const float nanResult = computeSyncMs(idxQuarter, std::nan(""));
    const float infResult = computeSyncMs(idxQuarter, std::numeric_limits<double>::infinity());
    const float negInfResult = computeSyncMs(idxQuarter, -std::numeric_limits<double>::infinity());

    REQUIRE(std::isfinite(nanResult));
    REQUIRE(nanResult == Catch::Approx(DELAY_TIME_MIN_MS));
    REQUIRE(std::isfinite(infResult));
    REQUIRE(infResult == Catch::Approx(DELAY_TIME_MIN_MS));
    REQUIRE(std::isfinite(negInfResult));
    REQUIRE(negInfResult == Catch::Approx(DELAY_TIME_MIN_MS));
}
