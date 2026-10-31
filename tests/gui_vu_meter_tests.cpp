// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// VU meter scale. The meters were fed linear RMS and filled the bar in
// proportion, so a nominal -18 dBFS DI lit 3 of 24 segments and the meter
// looked dead. The bar now spans -60..0 dBFS; these pin that mapping.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gui/VUMeter.h"

#include <cmath>
#include <limits>

using sr::VUMeter;
using Catch::Matchers::WithinAbs;

namespace {
float dbToAmp(float db) { return std::pow(10.0f, db / 20.0f); }
}

TEST_CASE("VUMeter: full scale fills the bar, silence empties it", "[gui][vu]")
{
    CHECK_THAT(VUMeter::barPositionFromAmplitude(1.0f), WithinAbs(1.0, 1e-5));
    CHECK(VUMeter::barPositionFromAmplitude(0.0f) == 0.0f);
    CHECK(VUMeter::barPositionFromAmplitude(dbToAmp(-60.0f)) < 1e-3f);
    CHECK(VUMeter::barPositionFromAmplitude(dbToAmp(-90.0f)) == 0.0f);
}

TEST_CASE("VUMeter: the scale is linear in dB", "[gui][vu]")
{
    CHECK_THAT(VUMeter::barPositionFromAmplitude(dbToAmp(-30.0f)), WithinAbs(0.5, 1e-3));
    CHECK_THAT(VUMeter::barPositionFromAmplitude(dbToAmp(-18.0f)), WithinAbs(0.7, 1e-3));
}

TEST_CASE("VUMeter: a nominal DI is well up the bar, not in the bottom segments", "[gui][vu]")
{
    // -18 dBFS RMS: linear, it was 0.126 of the bar (3 of 24 segments).
    const float pos = VUMeter::barPositionFromAmplitude(dbToAmp(-18.0f));
    CHECK(pos * 24.0f > 15.0f);
}

TEST_CASE("VUMeter: over-range and bad values stay inside the bar", "[gui][vu]")
{
    CHECK(VUMeter::barPositionFromAmplitude(4.0f) == 1.0f);
    CHECK(VUMeter::barPositionFromAmplitude(-0.5f) == 0.0f);
    CHECK(VUMeter::barPositionFromAmplitude(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(VUMeter::barPositionFromAmplitude(std::numeric_limits<float>::infinity()) == 0.0f);
}
