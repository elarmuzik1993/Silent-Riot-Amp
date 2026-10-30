// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// Sag-travel probe for docs/plans/2026-10-29-reactive-panel-glow.md, Task 0 — evidence,
// not product code.
//
// Question: does the sag bias offset (sagOffsetForGUI = SagEnvelope::biasOffset(),
// normalized volts, 300 V = 1.0; cold clamp SAG_BIAS_CLAMP_COLD_N = -0.01 = -3 V, warm
// clamp SAG_BIAS_CLAMP_WARM_N = +0.005 = +1.5 V) travel far enough in play to carry the
// reactive glow's hue (accent 24 deg -> red 0 deg)? If not, the glow falls back to
// compression (output RMS vs input RMS relative to their clean ratio), so the probe also
// logs inputLevelForGUI / outputLevelForGUI.
//
// It drives the REAL SilentRiotProcessor through tests/processor_harness.h
// (ProcessorHarness: 44.1 kHz, 512-sample host blocks, stereo, default OVERSAMPLE = 4x,
// built-in IIR cab, every effect off), and reads the three GUI atomics after each host
// block, exactly where the editor's timer would read them.
//
// Sections (each cell is a fresh harness, so it starts on the seeded idle point, B1):
//   1. Grid: gain {2,5,8,10} x volume {3,6,10} x input {nominal DI = -18 dBFS-RMS 110 Hz
//      sine, hot = 0.5-peak 110 Hz sine} x {15W, 7W}. 2 s per cell; statistics skip the
//      first 0.5 s (parameter smoothing + the input's own onset), i.e. cover 0.5..2.0 s.
//      Prints sag min/mean/max (normalized and as sagPercent = biasOffset / -0.01 * 100,
//      i.e. % of the way to the cold clamp), and mean input / output RMS + out/in ratio.
//   2. Soft vs hard: same knobs, a 110 Hz sine swept in level (-36 .. -6 dBFS RMS), so the
//      gate's actual question — how far apart do a soft and a hard phrase land — is read
//      off one column.
//   3. Pluck: a decaying 110 Hz note (tau 0.35 s) retriggered every 1.0 s for 4 s at a
//      soft and a hard peak; prints the per-block sag trace's deepest (coldest) point per
//      pluck and the value just before the next pluck.
//   4. Idle: sag on silence (the baseline every phrase starts from).
//   5. Clean ratio: out/in RMS at -48 dBFS-RMS over gain x volume, 15W and 7W — the
//      reference a compression-based glow measures against.
//
// Not part of any CMake target. To re-run (from the repo root, against a tests-on build
// dir, e.g. build-verify/ configured with -DSR_BUILD_TESTS=ON):
//   1. Temporarily add this file to SilentRiotProcessorTests in tests/CMakeLists.txt:
//        add_executable(SilentRiotProcessorTests
//            ../docs/records/handoffs/handoff-sag-travel-probe.cpp
//            processor_delay_time_tests.cpp ...
//   2. cmake --build build-verify --target SilentRiotProcessorTests -j4
//   3. build-verify/tests/SilentRiotProcessorTests "[.sag-probe]"     (~3 min, Release)
//   4. Revert the tests/CMakeLists.txt line.
// The hidden tag ([.sag-probe]) keeps it out of ctest and plain test runs even while it
// is linked in.

#include "processor_harness.h"
#include "utils/Parameters.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

using namespace sr::test;

namespace {

constexpr double kSR       = 44100.0;
constexpr int    kBlock    = 512;
constexpr double kSettleS  = 0.5;
constexpr float  kCold     = -0.01f;   // SAG_BIAS_CLAMP_COLD_N

float dbfsRmsToSinePeak(double dbfsRms) { return dB(dbfsRms) * std::sqrt(2.0f); }

struct Stats
{
    float sMin = std::numeric_limits<float>::max(), sMax = -std::numeric_limits<float>::max();
    double sSum = 0.0, inSum = 0.0, outSum = 0.0;
    int n = 0;
    void add(float sag, float in, float out)
    {
        sMin = std::min(sMin, sag); sMax = std::max(sMax, sag);
        sSum += sag; inSum += in; outSum += out; ++n;
    }
    float sMean() const { return static_cast<float>(sSum / n); }
    float inMean() const { return static_cast<float>(inSum / n); }
    float outMean() const { return static_cast<float>(outSum / n); }
};

float pct(float sag) { return sag / kCold * 100.0f; }

// One fresh processor per cell. Knobs in natural units (0..1), halfPower 0/1.
std::unique_ptr<ProcessorHarness> makeAmp(float gain, float volume, bool sevenW)
{
    auto h = std::make_unique<ProcessorHarness>(kSR, kBlock, 2);
    h->set(sr::Params::IDs::GAIN, gain);
    h->set(sr::Params::IDs::VOLUME, volume);
    h->set(sr::Params::IDs::HALF_POWER, sevenW ? 1.0f : 0.0f);
    return h;
}

// Runs `seconds` of `in` block by block; calls `onBlock(tSeconds, sag, inRms, outRms)`
// after each host block with the processor's GUI atomics.
template <typename F>
void drive(ProcessorHarness& h, double seconds, const Fill& in, F&& onBlock)
{
    const int total = static_cast<int>(std::lround(seconds * kSR));
    for (int start = 0; start < total; start += kBlock)
    {
        const int s0 = start;
        h.run(static_cast<double>(kBlock) / kSR,
              [&](int i, int ch) { return in(s0 + i, ch); });
        const double t = static_cast<double>(start + kBlock) / kSR;
        onBlock(t,
                h.proc.sagOffsetForGUI.load(),
                h.proc.inputLevelForGUI.load(),
                h.proc.outputLevelForGUI.load());
    }
}

Stats measure(float gain, float volume, bool sevenW, const Fill& in, double seconds = 2.0)
{
    auto h = makeAmp(gain, volume, sevenW);
    Stats st;
    drive(*h, seconds, in, [&](double t, float sag, float i, float o) {
        if (t > kSettleS) st.add(sag, i, o);
    });
    return st;
}

} // namespace

TEST_CASE("Sag travel probe (reactive panel glow, Task 0)", "[.sag-probe]")
{
    const float nominal = dbfsRmsToSinePeak(-18.0);   // 0.178 peak
    const float hot     = 0.5f;

    std::printf("\n== 1. Grid (stats over 0.5..2.0 s, read after each 512-sample block) ==\n");
    std::printf("mode gain vol input  | sag min     mean        max        | pct min  mean   max  "
                "| in RMS   out RMS  out/in\n");
    for (int w = 0; w < 2; ++w)
        for (float g : { 0.2f, 0.5f, 0.8f, 1.0f })
            for (float v : { 0.3f, 0.6f, 1.0f })
                for (int k = 0; k < 2; ++k)
                {
                    const float amp = k == 0 ? nominal : hot;
                    const Stats s = measure(g, v, w == 1, sine(110.0f, amp));
                    std::printf("%-4s %4.0f %3.0f %-6s | %+.6f  %+.6f  %+.6f | %5.1f  %5.1f  %5.1f "
                                "| %.5f  %.5f  %.3f\n",
                                w ? "7W" : "15W", g * 10, v * 10, k ? "hot" : "DI",
                                s.sMin, s.sMean(), s.sMax,
                                pct(s.sMax), pct(s.sMean()), pct(s.sMin),
                                s.inMean(), s.outMean(), s.outMean() / s.inMean());
                }

    std::printf("\n== 2. Soft vs hard: input level sweep, 110 Hz sine, 15W ==\n");
    std::printf("gain vol  dBFS-RMS | sag mean    pct  | in RMS   out RMS  out/in\n");
    struct Knobs { float g, v; };
    for (Knobs kn : { Knobs { 0.6f, 0.5f }, Knobs { 0.5f, 0.6f }, Knobs { 0.8f, 0.6f },
                      Knobs { 1.0f, 1.0f } })
        for (double lvl : { -36.0, -30.0, -24.0, -18.0, -12.0, -6.0 })
        {
            const Stats s = measure(kn.g, kn.v, false, sine(110.0f, dbfsRmsToSinePeak(lvl)));
            std::printf("%4.0f %3.0f  %+6.0f   | %+.6f  %5.1f | %.5f  %.5f  %.3f\n",
                        kn.g * 10, kn.v * 10, lvl, s.sMean(), pct(s.sMean()),
                        s.inMean(), s.outMean(), s.outMean() / s.inMean());
        }

    std::printf("\n== 3. Pluck: 110 Hz, tau 0.35 s, retrigger 1.0 s, 4 s, 15W; per-pluck "
                "deepest sag (pct) and the trough before the next pluck ==\n");
    for (Knobs kn : { Knobs { 0.6f, 0.5f }, Knobs { 0.8f, 0.6f }, Knobs { 1.0f, 1.0f } })
        for (double peakDb : { -24.0, -6.0 })
        {
            const float pk = dB(peakDb);
            const Fill pluck = [pk](int i, int) {
                const double t = static_cast<double>(i) / kSR;
                const double tn = std::fmod(t, 1.0);
                return static_cast<float>(pk * std::exp(-tn / 0.35)
                                          * std::sin(2.0 * 3.14159265358979 * 110.0 * t));
            };
            auto h = makeAmp(kn.g, kn.v, false);
            const float lo = -std::numeric_limits<float>::max();
            float deep[4] = { lo, lo, lo, lo }, trough[4] = { lo, lo, lo, lo };
            drive(*h, 4.0, pluck, [&](double t, float sag, float, float) {
                const int p = std::min(3, static_cast<int>(t - 1e-9));
                deep[p] = std::max(deep[p], pct(sag));
                if (std::fmod(t, 1.0) > 0.9) trough[p] = pct(sag);
            });
            std::printf("gain %2.0f vol %2.0f peak %+3.0f dBFS | deepest %5.1f %5.1f %5.1f %5.1f "
                        "| trough %5.1f %5.1f %5.1f %5.1f\n",
                        kn.g * 10, kn.v * 10, peakDb, deep[0], deep[1], deep[2], deep[3],
                        trough[0], trough[1], trough[2], trough[3]);
        }

    std::printf("\n== 4. Idle sag (silence, 2 s, stats over 0.5..2.0 s) ==\n");
    for (int w = 0; w < 2; ++w)
        for (Knobs kn : { Knobs { 0.2f, 0.3f }, Knobs { 1.0f, 1.0f } })
        {
            const Stats s = measure(kn.g, kn.v, w == 1, zeros());
            std::printf("%-3s gain %2.0f vol %2.0f | sag %+.6f .. %+.6f (pct %5.1f .. %5.1f)\n",
                        w ? "7W" : "15W", kn.g * 10, kn.v * 10, s.sMin, s.sMax,
                        pct(s.sMax), pct(s.sMin));
        }

    std::printf("\n== 5. Clean out/in RMS ratio (-48 dBFS-RMS 110 Hz sine, 1.5 s, stats over "
                "0.5..1.5 s), rows gain, columns volume 0 2 4 6 8 10 ==\n");
    for (int w = 0; w < 2; ++w)
    {
        std::printf("%s\n", w ? "7W" : "15W");
        for (int gi = 0; gi <= 10; gi += 2)
        {
            std::printf("  gain %2d |", gi);
            for (int vi = 0; vi <= 10; vi += 2)
            {
                const Stats s = measure(gi / 10.0f, vi / 10.0f, w == 1,
                                        sine(110.0f, dbfsRmsToSinePeak(-48.0)), 1.5);
                std::printf(" %8.4f", s.outMean() / s.inMean());
            }
            std::printf("\n");
        }
    }
    SUCCEED();
}
