// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Processor robustness — the audio input as a system boundary (RT-1,
// docs/records/reviews/review-rt-stress-2026-10-29.md). The real
// SilentRiotProcessor, driven through processBlock like a host.
//
// Before the input scrub, ONE NaN or ±Inf input sample latched in every
// stateful stage of the amp and silenced it until the next prepareToPlay;
// reset(), PWR off/on and every knob left it silent, and the output scrubs
// hid it from the host. Here:
//   - a single NaN / +Inf / -Inf sample, and a whole block of NaN: the amp
//     plays on at the rms it had before the event
//   - the ADVANCED panel's readout stays finite throughout
//   - host bypass passes no NaN, and neither the tuner ring nor the bypass
//     line holds one
//   - a scrubbed sample is exactly a 0 sample (bit-identical render), and a
//     finite sample is never touched (bypass passes it through bit-exact)
//
// Each case runs a few seconds of a 4x-oversampled amp; keep them short.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include "processor_harness.h"

#include <cfloat>
#include <cstring>
#include <limits>

using namespace sr::test;
using Mode = ProcessorHarness::Mode;

namespace {

constexpr double kSR = 44100.0;
const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// Where the poison lands: mid-block, on both channels, 0.5 s in.
constexpr int kEventAt = 22050 + 100;

float rmsOver(const std::vector<float>& x, double fromS, double toS)
{
    const auto a = static_cast<size_t>(fromS * kSR);
    const auto b = std::min(x.size(), static_cast<size_t>(toS * kSR));
    double sumSq = 0.0;
    for (size_t i = a; i < b; ++i) sumSq += static_cast<double>(x[i]) * x[i];
    return b > a ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(b - a))) : 0.0f;
}

bool allFinite(const std::vector<float>& x)
{
    return std::all_of(x.begin(), x.end(), [](float v) { return std::isfinite(v); });
}

bool readoutFinite(const SilentRiotProcessor::DSPReadout& r)
{
    return std::isfinite(r.vBPlus) && std::isfinite(r.vScreen) && std::isfinite(r.vPi)
        && std::isfinite(r.vPreamp) && std::isfinite(r.sagPercent)
        && std::isfinite(r.biasV) && std::isfinite(r.gridCurrentMA);
}

// A 220 Hz sine with `value` written over samples [kEventAt, kEventAt + count).
Fill poisoned(float value, int count)
{
    const Fill s = sine(220.0f, 0.18f, kSR);
    return [=](int i, int ch) {
        return (i >= kEventAt && i < kEventAt + count) ? value : s(i, ch);
    };
}

// Plays 0.5 s, the event, then 2 s more; the amp must sound as it did before.
void requireSurvives(float value, int count)
{
    ProcessorHarness h;
    std::vector<float> out;
    h.run(0.5, poisoned(value, count), Mode::Process, &out);
    const float before = rmsOver(out, 0.25, 0.5);
    REQUIRE(before > dB(-40.0));   // the amp is playing
    REQUIRE(readoutFinite(h.proc.getDSPReadout()));

    // The rest of the run, continuing the same signal (Fill indices restart at
    // 0, so offset them back onto the timeline).
    const Fill f = poisoned(value, count);
    std::vector<float> rest;
    h.run(2.0, [&](int i, int ch) { return f(i + 22050, ch); }, Mode::Process, &rest);
    out.insert(out.end(), rest.begin(), rest.end());

    REQUIRE(allFinite(out));
    CHECK(readoutFinite(h.proc.getDSPReadout()));
    const float after = rmsOver(out, 2.25, 2.5);
    INFO("rms before " << before << ", 2 s after the event " << after);
    CHECK(std::abs(after - before) <= 0.05f * before);
}

} // namespace

TEST_CASE("Processor: one NaN input sample does not silence the amp (RT-1)", "[processor][robustness]")
{
    requireSurvives(kNaN, 1);
}

TEST_CASE("Processor: one +Inf or -Inf input sample does not silence the amp (RT-1)", "[processor][robustness]")
{
    SECTION("+Inf") { requireSurvives(kInf, 1); }
    SECTION("-Inf") { requireSurvives(-kInf, 1); }
}

TEST_CASE("Processor: a whole block of NaN input does not silence the amp (RT-1)", "[processor][robustness]")
{
    requireSurvives(kNaN, 512);
}

TEST_CASE("Processor: a NaN input sample renders exactly as a 0 sample (RT-1)", "[processor][robustness]")
{
    // The scrub's whole effect is NaN -> 0: the same input with a 0 where the
    // NaN was must render bit-identically.
    std::vector<float> withNaN, withZero;
    {
        ProcessorHarness h;
        h.run(1.0, poisoned(kNaN, 3), Mode::Process, &withNaN);
    }
    {
        ProcessorHarness h;
        h.run(1.0, poisoned(0.0f, 3), Mode::Process, &withZero);
    }
    REQUIRE(withNaN.size() == withZero.size());
    REQUIRE(std::memcmp(withNaN.data(), withZero.data(), withNaN.size() * sizeof(float)) == 0);
}

TEST_CASE("Processor: host bypass passes no NaN, and keeps finite input bit-exact (RT-1)", "[processor][robustness]")
{
    ProcessorHarness h;
    const int latency = h.proc.getLatencySamples();
    REQUIRE(latency > 0);

    // Finite values a scrub must not touch, among them -0, denormals and the
    // float extremes. Bypass is a pure delay, so they come out bit-exact.
    const float edge[] = { -0.0f, FLT_MIN / 4.0f, -FLT_MIN / 4.0f, FLT_MIN, FLT_MAX, -FLT_MAX,
                           1.0e-30f, 0.25f, -0.75f };
    constexpr int kEdge = static_cast<int>(sizeof(edge) / sizeof(edge[0]));
    const Fill in = [&](int i, int) {
        if (i >= 1000 && i < 1000 + kEdge) return edge[i - 1000];
        if (i >= 2000 && i < 2000 + 64)    return (i & 1) ? kInf : kNaN;
        return 0.1f * std::sin(0.05f * static_cast<float>(i));
    };

    std::vector<float> out;
    h.run(0.1, in, Mode::Bypassed, &out);
    REQUIRE(allFinite(out));
    for (int k = 0; k < kEdge; ++k)
    {
        const float got = out[static_cast<size_t>(1000 + k + latency)];
        INFO("edge sample " << k);
        CHECK(std::memcmp(&got, &edge[k], sizeof(float)) == 0);
    }
    for (int i = 2000; i < 2064; ++i)
        CHECK(out[static_cast<size_t>(i + latency)] == 0.0f);

    // Neither the tuner ring nor the bypass line kept a NaN: the tuner reads
    // finite, and processing (which reads the line's history on re-entry to
    // bypass) stays finite too.
    std::vector<float> tuner(4096);
    const int got = h.proc.readLatestTunerSamples(tuner.data(), static_cast<int>(tuner.size()));
    tuner.resize(static_cast<size_t>(got));
    CHECK(allFinite(tuner));
    CHECK(readoutFinite(h.proc.getDSPReadout()));
}

TEST_CASE("Processor: NaN input under processBlock never reaches the tuner ring (RT-1)", "[processor][robustness]")
{
    ProcessorHarness h;
    h.run(0.1, [](int i, int) { return (i % 7 == 0) ? kNaN : 0.1f; });
    std::vector<float> tuner(4096);
    const int got = h.proc.readLatestTunerSamples(tuner.data(), static_cast<int>(tuner.size()));
    REQUIRE(got > 0);
    tuner.resize(static_cast<size_t>(got));
    CHECK(allFinite(tuner));

    // And bypass, entered right after, plays the scrubbed line: finite.
    std::vector<float> out;
    h.run(0.05, [](int, int) { return 0.1f; }, Mode::Bypassed, &out);
    CHECK(allFinite(out));
}
