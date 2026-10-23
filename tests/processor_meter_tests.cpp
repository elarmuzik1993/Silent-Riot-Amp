// ═══════════════════════════════════════════════════════════════
// Processor wiring of the GUI meters and the GRID I readout (review S3, S6)
// — the real SilentRiotProcessor, driven through processBlock like a host.
//
//   - tuner mute and power-off silence the output through processBlock's
//     early return, which skips the metering at the end of the block: the
//     output meter must read 0 there, not hold the last played level
//   - host bypass passes the dry input through, and the meters show it
//
// GRID I's current = drop / R relation is unit-tested in
// grid_conduction_tests.cpp. It can't be driven non-zero here: with
// VOLUME_KNOB_HEADROOM the EL84 grids peak at ~3.6 V even at gain 10 /
// volume 10 / +12 dB INPUT, under their 8.6 V conduction threshold.
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
    CHECK(h.proc.getDSPReadout().gridCurrentMA == 0.0f);

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
    CHECK(h.proc.getDSPReadout().gridCurrentMA == 0.0f);
}
