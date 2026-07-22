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

TEST_CASE("Width at 0 is off (unity passthrough, stereo preserved)", "[stereo_width]") {
    Run r(0.0f);
    for (size_t i = 0; i < r.L.size(); ++i) {
        // At off, the input passes through untouched — existing stereo content
        // is preserved (NOT collapsed to mono).
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

TEST_CASE("Turning up widens: side energy increases vs off", "[stereo_width]") {
    const int from = 24000;   // measure only the settled tail
    Run off (0.0f);
    Run wide(1.0f);
    REQUIRE(sideEnergy(wide.L, wide.R, from) > sideEnergy(off.L, off.R, from));
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

TEST_CASE("Micro-detune makes the side voice drift over time", "[stereo_width]") {
    // Feed a steady mono sine whose period is an exact integer (200 Hz @ 48 kHz
    // = 240 samples). A STATIC Haas/all-pass widener would make the synthesized
    // side voice exactly periodic too → side[k] == side[k+240] in steady state.
    // The LFO detune continuously pitch-shifts that voice, so it slips phase and
    // side[k] != side[k+240]. That drift is the detune signature.
    const int n = 20000, P = 240;
    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; ++i) {
        const float s = 0.6f * std::sin(2.0f * 3.14159265f * 200.0f * i / kSR);
        L[i] = s; R[i] = s;   // dual-mono
    }
    StereoWidth w; w.prepare(kSR);
    w.process(L.data(), R.data(), n, 1.0f);

    float maxDrift = 0.0f;
    for (int k = 13000; k < n - P; ++k) {
        const float sideK  = 0.5f * (L[k]     - R[k]);
        const float sideKP = 0.5f * (L[k + P] - R[k + P]);
        maxDrift = std::max(maxDrift, std::abs(sideK - sideKP));
    }
    REQUIRE(maxDrift > 5e-4f);   // static widener would be ~1e-6 (numerical floor)
}

TEST_CASE("Widener never emits non-finite samples", "[stereo_width]") {
    Run r(1.0f);
    for (size_t i = 0; i < r.L.size(); ++i) {
        REQUIRE(std::isfinite(r.L[i]));
        REQUIRE(std::isfinite(r.R[i]));
    }
}
