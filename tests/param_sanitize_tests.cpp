// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// ParamSanitize Tests — NaN/inf/out-of-range guard (no JUCE deps)
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "utils/ParamSanitize.h"
#include <cmath>
#include <limits>

using namespace sr;

TEST_CASE("sanitizeUnitParam: normal in-range values pass through unchanged", "[paramSanitize]")
{
    REQUIRE(sanitizeUnitParam(0.5f) == Catch::Approx(0.5f));
    REQUIRE(sanitizeUnitParam(0.0f) == Catch::Approx(0.0f));
    REQUIRE(sanitizeUnitParam(1.0f) == Catch::Approx(1.0f));
}

TEST_CASE("sanitizeUnitParam: out-of-range values clamp to [0,1]", "[paramSanitize]")
{
    REQUIRE(sanitizeUnitParam(-3.0f) == Catch::Approx(0.0f));
    REQUIRE(sanitizeUnitParam(3.0f) == Catch::Approx(1.0f));
}

TEST_CASE("sanitizeUnitParam: NaN and infinities fall back to 0.0 (safe/neutral)", "[paramSanitize]")
{
    REQUIRE(sanitizeUnitParam(std::nan("")) == Catch::Approx(0.0f));
    REQUIRE(sanitizeUnitParam(std::numeric_limits<float>::infinity()) == Catch::Approx(0.0f));
    REQUIRE(sanitizeUnitParam(-std::numeric_limits<float>::infinity()) == Catch::Approx(0.0f));
}

TEST_CASE("sanitizeUnitParamOr: in-range values pass through unchanged", "[paramSanitize]")
{
    REQUIRE(sanitizeUnitParamOr(0.8f, 0.5f) == Catch::Approx(0.8f));
    REQUIRE(sanitizeUnitParamOr(0.0f, 0.5f) == Catch::Approx(0.0f));
    REQUIRE(sanitizeUnitParamOr(1.0f, 0.5f) == Catch::Approx(1.0f));
}

TEST_CASE("sanitizeUnitParamOr: out-of-range values clamp, not fall back", "[paramSanitize]")
{
    REQUIRE(sanitizeUnitParamOr(-3.0f, 0.5f) == Catch::Approx(0.0f));
    REQUIRE(sanitizeUnitParamOr(3.0f, 0.5f)  == Catch::Approx(1.0f));
}

TEST_CASE("sanitizeUnitParamOr: NaN and infinities use the caller's fallback", "[paramSanitize]")
{
    // The centre-detented trims pass 0.5 here. Falling back to 0.0 would mean
    // -12 dB on the input trim and total silence on the output trim.
    REQUIRE(sanitizeUnitParamOr(std::nan(""), 0.5f) == Catch::Approx(0.5f));
    REQUIRE(sanitizeUnitParamOr(std::numeric_limits<float>::infinity(), 0.5f)
            == Catch::Approx(0.5f));
    REQUIRE(sanitizeUnitParamOr(-std::numeric_limits<float>::infinity(), 0.5f)
            == Catch::Approx(0.5f));
}

TEST_CASE("sanitizeUnitParam still falls back to 0.0 for bypass-at-zero params", "[paramSanitize]")
{
    REQUIRE(sanitizeUnitParam(std::nan("")) == Catch::Approx(0.0f));
    REQUIRE(sanitizeUnitParam(0.7f) == Catch::Approx(0.7f));
}
