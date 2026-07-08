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
