// ═══════════════════════════════════════════════════════════════
// StereoWidth tests — MONO↔STEREO widener invariants.
// Pure C++ (no JUCE): operates on raw float channel buffers.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "dsp/effects/StereoWidth.h"

#include <cmath>
#include <vector>

using sr::StereoWidth;

namespace {
constexpr float kSR = 48000.0f;

// Run the widener over a fresh decorrelated stereo signal at a fixed width.
struct Run {
    std::vector<float> L, R, origSum;
    // 1 s default so the 50 ms width smoother fully settles before the tail
    // checks (mono collapse / width comparison) sample it.
    Run(float width, int n = 48000) {
        L.resize(n); R.resize(n); origSum.resize(n);
        for (int i = 0; i < n; ++i) {
            // Distinct L/R content so there is genuine side energy to act on.
            L[i] = 0.7f * std::sin(2.0f * 3.14159265f * 220.0f * i / kSR);
            R[i] = 0.5f * std::sin(2.0f * 3.14159265f * 330.0f * i / kSR + 1.1f);
            origSum[i] = L[i] + R[i];
        }
        StereoWidth w;
        w.prepare(kSR);
        w.process(L.data(), R.data(), n, width);
    }
};

float sideEnergy(const std::vector<float>& L, const std::vector<float>& R, int from) {
    double e = 0.0;
    for (int i = from; i < (int) L.size(); ++i) {
        const float s = 0.5f * (L[i] - R[i]);
        e += (double) s * s;
    }
    return (float) e;
}
} // namespace

TEST_CASE("Width at centre (0.5) is unity passthrough", "[stereo_width]") {
    Run r(0.5f);
    for (size_t i = 0; i < r.L.size(); ++i) {
        // Reconstruct the original from origSum is not possible per-channel, so
        // recompute the expected inputs directly.
        const float expL = 0.7f * std::sin(2.0f * 3.14159265f * 220.0f * i / kSR);
        const float expR = 0.5f * std::sin(2.0f * 3.14159265f * 330.0f * i / kSR + 1.1f);
        REQUIRE_THAT(r.L[i], Catch::Matchers::WithinAbs(expL, 1e-5));
        REQUIRE_THAT(r.R[i], Catch::Matchers::WithinAbs(expR, 1e-5));
    }
}

TEST_CASE("Mono sum L+R is invariant at every width", "[stereo_width]") {
    for (float w : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
        Run r(w);
        for (size_t i = 0; i < r.L.size(); ++i)
            REQUIRE_THAT(r.L[i] + r.R[i],
                         Catch::Matchers::WithinAbs(r.origSum[i], 1e-4));
    }
}

TEST_CASE("Width at 0 collapses to mono (L == R)", "[stereo_width]") {
    Run r(0.0f);
    // Check the settled tail (after the smoother reaches the target).
    for (size_t i = r.L.size() - 500; i < r.L.size(); ++i)
        REQUIRE_THAT(r.L[i], Catch::Matchers::WithinAbs(r.R[i], 1e-4));
}

TEST_CASE("Turning past centre widens: side energy increases", "[stereo_width]") {
    const int from = 4000;   // measure only the settled tail
    Run unity(0.5f);
    Run wide (1.0f);
    REQUIRE(sideEnergy(wide.L, wide.R, from) > sideEnergy(unity.L, unity.R, from));
}

TEST_CASE("Widener synthesises width from a MONO (dual-mono) input", "[stereo_width]") {
    // The dry amp path is identical L/R → zero side content. A pure M/S scale
    // could not widen it; the decorrelator must create side energy.
    const int n = 48000;   // let the width smoother settle
    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; ++i) {
        const float s = 0.6f * std::sin(2.0f * 3.14159265f * 200.0f * i / kSR);
        L[i] = s; R[i] = s;   // dual-mono
    }
    StereoWidth w; w.prepare(kSR);
    w.process(L.data(), R.data(), n, 1.0f);

    REQUIRE(sideEnergy(L, R, 24000) > 1e-4f);   // genuine width appeared
    // ...and mono compatibility still holds: L+R == 2·original.
    for (int i = 24000; i < n; ++i) {
        const float s = 0.6f * std::sin(2.0f * 3.14159265f * 200.0f * i / kSR);
        REQUIRE_THAT(L[i] + R[i], Catch::Matchers::WithinAbs(2.0f * s, 1e-4));
    }
}

TEST_CASE("Widener never emits non-finite samples", "[stereo_width]") {
    Run r(1.0f);
    for (size_t i = 0; i < r.L.size(); ++i) {
        REQUIRE(std::isfinite(r.L[i]));
        REQUIRE(std::isfinite(r.R[i]));
    }
}
