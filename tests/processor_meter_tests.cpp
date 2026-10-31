// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Processor wiring of the GUI meters and the GRID I readout (review S3, S6)
// — the real SilentRiotProcessor, driven through processBlock like a host.
//
//   - tuner mute and power-off silence the output through processBlock's
//     early return, which skips the metering at the end of the block: the
//     output meter must read 0 there, not hold the last played level
//   - host bypass passes the dry input through, and the meters show it
//   - the output block peak (outputPeakForGUI) is published on all three of
//     those exits, holds a spike the block RMS averages away, and releases at
//     the same rate whatever the host block size
//
// GRID I's current = drop / R relation is unit-tested in
// grid_conduction_tests.cpp. No setting is known that drives it non-zero
// here: with VOLUME_KNOB_HEADROOM the EL84 grids stayed under their 8.6 V
// conduction threshold at every corner measured (sines, a band-limited square,
// a six-note chord) — hottest 5.91 V, a 3.3 dB margin (gain 10, volume 10,
// +12 dB INPUT, BOOST LEVEL and BRIGHT max, SHAPE 10, 1.6 kHz square at 0.5
// peak; docs/records/handoffs/handoff-power-section-fixes.md, "Grid conduction corners"). So
// the gridCurrentMA == 0 checks below are smoke checks: GRID I reads 0 anyway.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "processor_harness.h"

using namespace sr::test;
using Mode = ProcessorHarness::Mode;
namespace IDs = sr::Params::IDs;

namespace {

float inputMeter(const ProcessorHarness& h)  { return h.proc.inputLevelForGUI.load(); }
float outputMeter(const ProcessorHarness& h) { return h.proc.outputLevelForGUI.load(); }
float outputPeak(const ProcessorHarness& h)  { return h.proc.outputPeakForGUI.load(); }

// A sine with exactly 4 periods per 512-sample block, run for whole blocks, so
// a block's RMS is the sine's: amplitude / sqrt(2).
Fill blockSine(float amplitude) { return sine(44100.0f / 128.0f, amplitude); }
double blocks(int n) { return n * 512 / 44100.0; }
float rmsOf(float amplitude) { return amplitude / std::sqrt(2.0f); }

} // namespace

TEST_CASE("Processor: tuner mute zeroes the output meter, the input meter stays live (review S6)", "[processor][meters]")
{
    ProcessorHarness h;
    h.run(0.5, blockSine(0.25f));
    REQUIRE(outputMeter(h) > dB(-40.0));      // playing: the meter is live
    REQUIRE(inputMeter(h) > dB(-20.0));

    h.set(IDs::TUNER_MUTE, 1.0f);
    REQUIRE(h.run(blocks(8), blockSine(0.25f)) == 0.0f);
    CHECK(outputMeter(h) == 0.0f);             // silent output reads silent
    // The tuner is listening to the input, so it is real signal (unity trim).
    CHECK(inputMeter(h) == Catch::Approx(rmsOf(0.25f)).epsilon(0.01));
    CHECK(h.proc.getDSPReadout().gridCurrentMA == 0.0f);   // smoke check (see header)

    h.set(IDs::TUNER_MUTE, 0.0f);
    h.run(0.5, blockSine(0.25f));
    CHECK(outputMeter(h) > dB(-40.0));         // and back
}

TEST_CASE("Processor: tuner-mute input meter is post-trim, like the live one (review S6)", "[processor][meters]")
{
    ProcessorHarness h;
    h.set(IDs::INPUT_LEVEL, 1.0f);             // +12 dB
    h.set(IDs::TUNER_MUTE, 1.0f);
    h.run(blocks(8), blockSine(0.05f));
    CHECK(inputMeter(h) == Catch::Approx(rmsOf(0.05f) * dB(12.0)).epsilon(0.01));
    CHECK(outputMeter(h) == 0.0f);
}

TEST_CASE("Processor: power off zeroes both meters (review S6)", "[processor][meters]")
{
    ProcessorHarness h;
    h.run(0.5, blockSine(0.25f));
    REQUIRE(outputMeter(h) > 0.0f);

    h.set(IDs::POWER, 0.0f);
    h.run(blocks(8), blockSine(0.25f));
    CHECK(outputMeter(h) == 0.0f);
    CHECK(inputMeter(h) == 0.0f);
}

TEST_CASE("Processor: host bypass meters the dry passthrough (review S6)", "[processor][meters]")
{
    ProcessorHarness h;
    h.set(IDs::INPUT_LEVEL, 1.0f);             // bypass skips the trim: meters read the raw DI
    h.run(0.5, blockSine(0.25f));
    h.run(blocks(8), blockSine(0.25f), Mode::Bypassed);
    const float rms = rmsOf(0.25f);
    CHECK(inputMeter(h)  == Catch::Approx(rms).epsilon(0.01));
    CHECK(outputMeter(h) == Catch::Approx(rms).epsilon(0.01));
    CHECK(h.proc.getDSPReadout().gridCurrentMA == 0.0f);   // smoke check (see header)
}

// ── Output block peak (outputPeakForGUI) ────────────────────────
// Host bypass is used where the test needs to know the exact output: there the
// output is the input delayed by getLatencySamples(), so a spike in is a spike
// out, at a known sample. The amp itself would smear a one-sample input spike
// through its filters and limiter. Both bypass and processBlock publish through
// the same fold, so the ballistics proven here are the normal path's too; the
// processBlock cases below check that path is wired.

namespace {

// Seconds that render exactly `samples` samples at 44.1 kHz in run().
double samplesToSec(int samples) { return samples / 44100.0; }

// A one-sample spike at input index `at`, zero elsewhere.
Fill spikeAt(int at, float amplitude)
{
    return [=](int i, int) { return i == at ? amplitude : 0.0f; };
}

float releaseOver(int samples)
{
    return std::exp(-static_cast<float>(samples)
                    / (sr::METER_PEAK_RELEASE_MS * 0.001f * 44100.0f));
}

} // namespace

TEST_CASE("Processor: a one-sample spike lifts the output peak, not the RMS", "[processor][meters]")
{
    ProcessorHarness h;
    h.silence(blocks(4), Mode::Bypassed);
    // Lands mid-block, after the bypass delay (10-12 samples), in this one block.
    h.run(blocks(1), spikeAt(100, 0.5f), Mode::Bypassed);
    CHECK(outputPeak(h) == Catch::Approx(0.5f).epsilon(1e-6));
    CHECK(outputMeter(h) == Catch::Approx(0.5f / std::sqrt(512.0f)).epsilon(1e-4));   // -27 dB
    CHECK(outputPeak(h) > 20.0f * outputMeter(h));

    // processBlock publishes it too: a peak can never sit below the RMS of the
    // block it was taken from.
    h.run(0.5, blockSine(0.25f));
    CHECK(outputPeak(h) > 0.0f);
    CHECK(outputPeak(h) >= outputMeter(h));
}

TEST_CASE("Processor: the output peak releases at the same rate at 64 and 2048 samples per block", "[processor][meters]")
{
    // Every reading is taken at a multiple of 2048 samples, so both block sizes
    // have processed exactly the same samples when they are compared.
    constexpr int kBig = 2048;

    SECTION("exact, on the bypass passthrough")
    {
        float readings[2][3] {};
        const int sizes[2] { 64, kBig };
        for (int s = 0; s < 2; ++s)
        {
            ProcessorHarness h(44100.0, sizes[s]);
            const int latency = h.proc.getLatencySamples();
            // The spike comes out at sample 2040: the last 64-sample block of
            // the first 2048, so both runs hold it un-decayed at sample 2048.
            h.run(samplesToSec(kBig), spikeAt(2040 - latency, 0.5f), Mode::Bypassed);
            readings[s][0] = outputPeak(h);
            h.silence(samplesToSec(2 * kBig), Mode::Bypassed);
            readings[s][1] = outputPeak(h);
            h.silence(samplesToSec(4 * kBig), Mode::Bypassed);
            readings[s][2] = outputPeak(h);
        }
        CHECK(readings[0][0] == Catch::Approx(0.5f).epsilon(1e-6));
        CHECK(readings[1][0] == Catch::Approx(0.5f).epsilon(1e-6));
        CHECK(readings[0][1] == Catch::Approx(0.5f * releaseOver(2 * kBig)).epsilon(1e-4));
        CHECK(readings[1][1] == Catch::Approx(0.5f * releaseOver(2 * kBig)).epsilon(1e-4));
        CHECK(readings[0][2] == Catch::Approx(0.5f * releaseOver(6 * kBig)).epsilon(1e-4));
        CHECK(readings[1][2] == Catch::Approx(0.5f * releaseOver(6 * kBig)).epsilon(1e-4));
    }

    SECTION("through processBlock")
    {
        // 44100/64 Hz: one whole period in every 64-sample block, so every block
        // at either size sees the same steady peak. The input stops `latency`
        // samples before a 2048 boundary, so the output does too.
        float level[2] {}, released[2] {};
        const int sizes[2] { 64, kBig };
        for (int s = 0; s < 2; ++s)
        {
            ProcessorHarness h(44100.0, sizes[s]);
            const int stop = 22 * kBig - h.proc.getLatencySamples();
            const Fill tone = sine(44100.0f / 64.0f, 0.1f);
            h.run(samplesToSec(22 * kBig), [=](int i, int ch) { return i < stop ? tone(i, ch) : 0.0f; });
            level[s] = outputPeak(h);
            h.silence(samplesToSec(6 * kBig));
            released[s] = outputPeak(h);
        }
        const float halfDb = dB(0.5);
        REQUIRE(level[0] > dB(-40.0));
        CHECK(level[1] / level[0] == Catch::Approx(1.0f).margin(halfDb - 1.0f));
        CHECK(released[1] / released[0] == Catch::Approx(1.0f).margin(halfDb - 1.0f));
        // and that is the meter's release, not some other decay
        CHECK(released[0] / level[0] == Catch::Approx(releaseOver(6 * kBig)).epsilon(halfDb - 1.0f));
    }
}

TEST_CASE("Processor: the output peak reads 0 under power off, tuner mute and reset()", "[processor][meters]")
{
    ProcessorHarness h;
    h.run(0.5, blockSine(0.25f));
    REQUIRE(outputPeak(h) > dB(-40.0));

    h.set(IDs::TUNER_MUTE, 1.0f);
    h.run(blocks(1), blockSine(0.25f));
    CHECK(outputPeak(h) == 0.0f);              // at once, not released
    h.set(IDs::TUNER_MUTE, 0.0f);
    h.run(0.5, blockSine(0.25f));
    REQUIRE(outputPeak(h) > dB(-40.0));

    h.set(IDs::POWER, 0.0f);
    h.run(blocks(1), blockSine(0.25f));
    CHECK(outputPeak(h) == 0.0f);
    h.set(IDs::POWER, 1.0f);
    h.run(0.5, blockSine(0.25f));
    REQUIRE(outputPeak(h) > dB(-40.0));

    h.proc.reset();                            // host stop: nothing held over
    CHECK(outputPeak(h) == 0.0f);
}

TEST_CASE("Processor: host bypass publishes the passthrough's peak", "[processor][meters]")
{
    ProcessorHarness h;
    h.run(0.5, blockSine(0.25f));              // the amp's (louder) output is held
    // 1 s of bypass is four release time constants: what was held from the amp
    // has fallen under 2% of itself, well below the dry signal's peak. The sine
    // hits its exact peak once per period, so every block's peak is 0.1.
    h.run(1.0, blockSine(0.1f), Mode::Bypassed);
    CHECK(outputPeak(h) == Catch::Approx(0.1f).epsilon(0.01));
}
