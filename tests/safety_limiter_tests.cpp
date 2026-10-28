// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// SafetyLimiter (softClip) Tests
//
//   - Transparent (identity) below the threshold
//   - Output hard-bounded to (−1, 1) for any input, incl. huge values
//   - Monotonic and odd-symmetric
//   - Continuous through the knee
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/effects/SafetyLimiter.h"
#include <cmath>

using namespace sr;

TEST_CASE("softClip is transparent below threshold", "[limiter]")
{
    for (float x : { -0.9f, -0.5f, 0.0f, 0.25f, 0.85f, 0.9f })
        REQUIRE(softClip(x) == Catch::Approx(x));
}

TEST_CASE("softClip bounds output to [-1, 1]", "[limiter]")
{
    for (float x : { 1.0f, 1.5f, 2.0f, 50.0f, 1000.0f, -1.0f, -3.0f, -1000.0f })
    {
        float y = softClip(x);
        REQUIRE(std::abs(y) <= 1.0f);                 // never exceeds full scale
        REQUIRE(std::abs(y) > SAFETY_LIMIT_THRESH);   // above the knee → compressed, not zeroed
    }
}

TEST_CASE("softClip is odd-symmetric", "[limiter]")
{
    for (float x : { 0.3f, 0.95f, 1.4f, 8.0f })
        REQUIRE(softClip(-x) == Catch::Approx(-softClip(x)));
}

TEST_CASE("softClip is monotonic increasing", "[limiter]")
{
    float prev = softClip(-5.0f);
    for (float x = -5.0f + 0.01f; x <= 5.0f; x += 0.01f)
    {
        float y = softClip(x);
        REQUIRE(y >= prev);
        prev = y;
    }
}

TEST_CASE("softClip is continuous through the knee", "[limiter]")
{
    const float t = SAFETY_LIMIT_THRESH;
    // Just below and just above the knee should be nearly equal (C0/C1 continuity).
    REQUIRE(softClip(t - 1e-4f) == Catch::Approx(softClip(t + 1e-4f)).margin(1e-3));
}
