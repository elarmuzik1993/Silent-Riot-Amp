// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Boost Tests — front-of-amp clean boost + tight low-cut + bright shelf
//
//   - levelGain maps 0..1 → 0..+BOOST_LEVEL_MAX_DB
//   - Mid-band gain tracks the level control
//   - "tight" low-cut attenuates lows relative to mids
//   - "bright" shelf lifts highs relative to mids
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/effects/Boost.h"
#include <cmath>

using namespace sr;

namespace {
constexpr float kSR = 48000.0f;

// RMS of a sine of given frequency after passing through the boost.
float rmsAt(Boost& b, float freq, float level, float tight, float bright,
            int samples = 8192)
{
    double sumSq = 0.0;
    for (int i = 0; i < samples; ++i)
    {
        float x = std::sin(2.0f * PI_F * freq * i / kSR);
        float y = b.process(x, level, tight, bright);
        sumSq += static_cast<double>(y) * y;
    }
    return static_cast<float>(std::sqrt(sumSq / samples));
}
} // namespace

TEST_CASE("Boost levelGain maps dB correctly", "[boost]")
{
    REQUIRE(Boost::levelGain(0.0f) == Catch::Approx(1.0f));
    REQUIRE(Boost::levelGain(1.0f) ==
            Catch::Approx(std::pow(10.0f, BOOST_LEVEL_MAX_DB / 20.0f)).epsilon(1e-4));
}

TEST_CASE("Boost mid-band gain follows level control", "[boost]")
{
    Boost b; b.prepare(kSR);
    // 1 kHz is well inside the band (above tight HP, below bright corner).
    float unity = rmsAt(b, 1000.0f, 0.0f, 0.0f, 0.0f);
    b.prepare(kSR);
    float boosted = rmsAt(b, 1000.0f, 1.0f, 0.0f, 0.0f);
    float ratio = boosted / unity;
    REQUIRE(ratio == Catch::Approx(std::pow(10.0f, BOOST_LEVEL_MAX_DB / 20.0f)).epsilon(0.05));
}

TEST_CASE("Boost tight control attenuates lows", "[boost]")
{
    Boost b; b.prepare(kSR);
    float lowNoTight = rmsAt(b, 80.0f, 0.0f, 0.0f, 0.0f);
    b.prepare(kSR);
    float lowTight   = rmsAt(b, 80.0f, 0.0f, 1.0f, 0.0f);
    REQUIRE(lowTight < lowNoTight * 0.7f);  // clearly rolled off
}

TEST_CASE("Boost bright control lifts highs", "[boost]")
{
    Boost b; b.prepare(kSR);
    float highFlat   = rmsAt(b, 6000.0f, 0.0f, 0.0f, 0.0f);
    b.prepare(kSR);
    float highBright = rmsAt(b, 6000.0f, 0.0f, 0.0f, 1.0f);
    REQUIRE(highBright > highFlat * 1.3f);  // shelf lift above corner
}
