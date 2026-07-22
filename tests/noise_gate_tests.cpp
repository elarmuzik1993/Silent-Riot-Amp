// ═══════════════════════════════════════════════════════════════
// NoiseGate Tests — front-of-amp downward gate
//
//   - Gate opens for signal above threshold (gain → 1)
//   - Gate closes for signal below threshold (gain → 0)
//   - Hysteresis: a signal between close/open thresholds holds state
//   - Coefficients are valid (0..1) after recalc
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/effects/NoiseGate.h"
#include <cmath>

using namespace sr;

namespace {
constexpr float kSR = 48000.0f;

// Run a steady sine of the given amplitude through the gate long enough for the
// gain envelope to settle, then return the per-sample gain it converged to.
float settledGain(NoiseGate& g, float amplitude, float thresholdDb, int samples = 24000)
{
    float gain = 1.0f;
    for (int i = 0; i < samples; ++i)
    {
        float x = amplitude * std::sin(2.0f * PI_F * 200.0f * i / kSR);
        float y = g.process(x, thresholdDb);
        gain = (std::abs(x) > 1e-9f) ? std::abs(y / x) : gain;
    }
    return gain;
}
} // namespace

TEST_CASE("NoiseGate recalc gives valid coefficients", "[gate]")
{
    NoiseGate g;
    g.recalcCoeffs(kSR);
    REQUIRE(g.attackCoeff  > 0.0f);  REQUIRE(g.attackCoeff  < 1.0f);
    REQUIRE(g.releaseCoeff > 0.0f);  REQUIRE(g.releaseCoeff < 1.0f);
    REQUIRE(g.detectCoeff  > 0.0f);  REQUIRE(g.detectCoeff  < 1.0f);
    // Release is slower than attack → larger (closer to 1) decay coefficient.
    REQUIRE(g.releaseCoeff > g.attackCoeff);
}

TEST_CASE("NoiseGate opens for loud signal", "[gate]")
{
    NoiseGate g;
    g.recalcCoeffs(kSR);
    // -40 dB threshold, signal well above it → gate fully open.
    float gain = settledGain(g, 0.5f, -40.0f);
    REQUIRE(gain > 0.95f);
}

TEST_CASE("NoiseGate closes for quiet signal", "[gate]")
{
    NoiseGate g;
    g.recalcCoeffs(kSR);
    // -20 dB threshold, signal far below it → gate closed.
    float gain = settledGain(g, 0.005f, -20.0f);
    REQUIRE(gain < 0.05f);
}

TEST_CASE("NoiseGate hysteresis holds open between thresholds", "[gate]")
{
    NoiseGate g;
    g.recalcCoeffs(kSR);

    // Open with a loud burst at -30 dB threshold.
    settledGain(g, 0.5f, -30.0f);
    REQUIRE(g.open == true);

    // A signal between the close threshold (-34 dB ≈ 0.0200) and the open
    // threshold (-30 dB ≈ 0.0316) must NOT close an already-open gate.
    float between = std::pow(10.0f, -32.0f / 20.0f);  // ≈ 0.0251
    settledGain(g, between, -30.0f);
    REQUIRE(g.open == true);
}

TEST_CASE("NoiseGate is a true bypass at threshold 0 (GATE_THRESH_MIN_DB)", "[gate]")
{
    NoiseGate g;
    g.recalcCoeffs(kSR);

    // A signal far BELOW -80 dB would normally be gated shut, but with the knob
    // parked at 0 the gate is OFF: output must equal input, sample for sample.
    const float quiet = std::pow(10.0f, -100.0f / 20.0f);  // -100 dB
    for (int i = 0; i < 24000; ++i)
    {
        float x = quiet * std::sin(2.0f * PI_F * 200.0f * i / kSR);
        float y = g.process(x, GATE_THRESH_MIN_DB);
        REQUIRE(y == x);
    }
    REQUIRE(g.gain == Catch::Approx(1.0f));
    REQUIRE(g.open == true);
}

TEST_CASE("NoiseGate reset restores open state", "[gate]")
{
    NoiseGate g;
    g.recalcCoeffs(kSR);
    g.reset();
    REQUIRE(g.gain == Catch::Approx(1.0f));
    REQUIRE(g.open == false);
    REQUIRE(g.envelope == Catch::Approx(0.0f));
}
