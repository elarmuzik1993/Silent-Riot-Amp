#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "utils/ParamSmoother.h"
#include <cmath>
using namespace sr;
using Catch::Approx;

// ParamSmoother is a one-pole exponential smoother that runs per-sample at the
// oversampled rate. CLAUDE.md flags it as timing-sensitive but it had no tests.

TEST_CASE("ParamSmoother: setImmediate snaps current and target", "[smoother]") {
    ParamSmoother s;
    s.init(5.0f, 48000.0f);
    s.setImmediate(0.7f);
    REQUIRE(s.current == Approx(0.7f));
    REQUIRE(s.target  == Approx(0.7f));
    REQUIRE(s.tick()  == Approx(0.7f));   // already at target → stays put
}

TEST_CASE("ParamSmoother: tick converges monotonically without overshoot", "[smoother]") {
    ParamSmoother s;
    s.init(5.0f, 48000.0f);
    s.setImmediate(0.0f);
    s.setTarget(1.0f);

    float prev = s.current;
    for (int i = 0; i < 20000; ++i) {
        float v = s.tick();
        REQUIRE(v >= prev - 1e-7f);       // non-decreasing toward target
        REQUIRE(v <= 1.0f + 1e-6f);       // never overshoots the target
        prev = v;
    }
    REQUIRE(s.current == Approx(1.0f).margin(1e-3f));
}

TEST_CASE("ParamSmoother: reaches ~63% after one time constant", "[smoother]") {
    const float tauMs = 5.0f, sr = 48000.0f;
    ParamSmoother s;
    s.init(tauMs, sr);
    s.setImmediate(0.0f);
    s.setTarget(1.0f);

    const int tauSamples = static_cast<int>(std::lround(tauMs * 0.001f * sr)); // 240
    for (int i = 0; i < tauSamples; ++i) s.tick();

    // Step response of a one-pole reaches 1 - e^-1 ≈ 0.632 after one tau.
    REQUIRE(s.current == Approx(1.0f - std::exp(-1.0f)).margin(0.01f));
}

TEST_CASE("ParamSmoother: init decay matches exp(-1/(tau*sr))", "[smoother]") {
    ParamSmoother s;
    s.init(10.0f, 44100.0f);
    REQUIRE(s.decay == Approx(std::exp(-1.0f / (10.0f * 0.001f * 44100.0f))));

    // Longer tau → slower decay (coefficient closer to 1.0).
    ParamSmoother s2;
    s2.init(20.0f, 44100.0f);
    REQUIRE(s2.decay > s.decay);
}

TEST_CASE("ParamSmoother: converges for negative targets and retargeting", "[smoother]") {
    ParamSmoother s;
    s.init(1.0f, 48000.0f);
    s.setImmediate(1.0f);
    s.setTarget(-0.5f);
    for (int i = 0; i < 48000; ++i) s.tick();
    REQUIRE(s.current == Approx(-0.5f).margin(1e-3f));
}
