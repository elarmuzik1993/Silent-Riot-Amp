// ═══════════════════════════════════════════════════════════════
// Processor wiring of the effect tails (review B6) — the real
// SilentRiotProcessor, driven through processBlock like a host.
//
// The DSP classes are unit-tested in effects_delay_reverb_tests.cpp; these
// cover what only the processor does, each of which a unit test cannot see:
//   - delay / reverb restart() on the off→on edge, the IR convolution and its
//     limiter oversampler cleared when the IR path comes back
//   - tails flagged stale by power-off / tuner mute, host reset() and host
//     bypass, and cleared on the next processed block
//   - the on→off fade: the wet ramps out over the mix smoother, then the
//     effect is skipped and the output is bit-exact dry; back on mid-fade,
//     the ramp reverses on the live tail
//   - host bypass passes the input through delayed by the reported latency
//
// Every "starts clean" test first leaves the effect ringing with the amp
// settled (the amp idles at ~-268 dBFS within 0.5 s of silence), so anything
// above -120 dBFS on silence afterwards is the effect replaying old audio.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "processor_harness.h"

#include <cstdint>
#include <vector>

using namespace sr::test;
using Mode = ProcessorHarness::Mode;
namespace IDs = sr::Params::IDs;

namespace {

enum class Fx { Delay, Reverb, IR };

const char* name(Fx fx)
{
    switch (fx)
    {
        case Fx::Delay:  return "delay";
        case Fx::Reverb: return "reverb";
        case Fx::IR:     return "IR convolution";
    }
    return "?";
}

// The parameter that switches the effect on and off. The IR path's is the cab.
const juce::String& toggleOf(Fx fx)
{
    switch (fx)
    {
        case Fx::Delay:  return IDs::DELAY_ON;
        case Fx::Reverb: return IDs::REVERB_ON;
        case Fx::IR:     return IDs::CAB_SIM;
    }
    return IDs::CAB_SIM;
}

// A mono IR with a ~1 s tail: 8192 samples at 8 kHz (the longest
// MAX_USER_IR_SAMPLES allows), decaying noise. It has to outlast the amp's
// own ~0.5 s settling, or a replayed IR tail could not be told apart from it.
std::vector<float> longDecayIR()
{
    constexpr int n = 8192;
    constexpr double irRate = 8000.0;
    std::vector<float> ir(static_cast<size_t>(n));
    uint32_t seed = 12345u;
    for (int i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const float noise = static_cast<float>(seed >> 8) / static_cast<float>(1u << 24) - 0.5f;
        ir[static_cast<size_t>(i)] =
            noise * static_cast<float>(std::exp(-static_cast<double>(i) / (0.25 * irRate)));
    }
    ir[0] = 1.0f;
    return ir;
}

// Switches one effect on, at settings with a long tail.
void enable(ProcessorHarness& h, Fx fx, const juce::TemporaryFile& irFile)
{
    switch (fx)
    {
        case Fx::Delay:
            h.set(IDs::DELAY_ON, 1.0f);
            h.set(IDs::DELAY_TIME, 1.0f);       // 1000 ms
            h.set(IDs::DELAY_FEEDBACK, 0.35f);
            h.set(IDs::DELAY_MIX, 0.5f);
            break;
        case Fx::Reverb:
            h.set(IDs::REVERB_ON, 1.0f);
            h.set(IDs::REVERB_SIZE, 1.0f);
            h.set(IDs::REVERB_MIX, 0.5f);
            break;
        case Fx::IR:
            h.loadIR(writeWav(irFile, longDecayIR(), 8000.0));
            break;
    }
}

// Plays a note, then silence until the amp has settled, and checks the effect
// is still ringing: the state each "starts clean" test freezes the effect in.
void ringOut(ProcessorHarness& h, Fx fx)
{
    h.run(1.0, sine(220.0f, 0.25f));
    h.silence(0.6);
    INFO(name(fx) << " should still be ringing with the amp settled");
    REQUIRE(h.silence(0.1) > dB(-100.0));
}

// Asserts nothing but the settled amp comes out on silence: no replayed tail.
void requireClean(ProcessorHarness& h, Fx fx)
{
    INFO(name(fx) << " replayed its old tail");
    REQUIRE(h.silence(1.5) < dB(-120.0));
}

std::vector<float> slice(const std::vector<float>& v, size_t from, size_t to)
{
    return { v.begin() + static_cast<std::ptrdiff_t>(from), v.begin() + static_cast<std::ptrdiff_t>(to) };
}

float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = from; i < to; ++i) m = std::max(m, std::abs(a[i] - b[i]));
    return m;
}

} // namespace

TEST_CASE("Processor: an effect switched off and back on starts clean (review B6)", "[processor][effects]")
{
    const Fx fx = GENERATE(Fx::Delay, Fx::Reverb, Fx::IR);
    juce::TemporaryFile irFile(".wav");
    ProcessorHarness h;
    enable(h, fx, irFile);
    ringOut(h, fx);

    h.set(toggleOf(fx), 0.0f);
    h.silence(1.0);              // off: the wet fades out, then the effect is skipped and freezes
    h.set(toggleOf(fx), 1.0f);
    requireClean(h, fx);
}

TEST_CASE("Processor: effects start clean after tuner mute and power off (review B6)", "[processor][effects]")
{
    const Fx fx = GENERATE(Fx::Delay, Fx::Reverb, Fx::IR);
    const bool viaPower = GENERATE(false, true);
    juce::TemporaryFile irFile(".wav");
    ProcessorHarness h;
    enable(h, fx, irFile);
    ringOut(h, fx);

    const juce::String& id = viaPower ? IDs::POWER : IDs::TUNER_MUTE;
    h.set(id, viaPower ? 0.0f : 1.0f);
    REQUIRE(h.run(0.5, sine(220.0f, 0.25f)) < dB(-300.0));   // silenced (tuning)
    h.set(id, viaPower ? 1.0f : 0.0f);
    requireClean(h, fx);
}

TEST_CASE("Processor: effects start clean after a host reset() (review B6)", "[processor][effects]")
{
    const Fx fx = GENERATE(Fx::Delay, Fx::Reverb, Fx::IR);
    juce::TemporaryFile irFile(".wav");
    ProcessorHarness h;
    enable(h, fx, irFile);
    ringOut(h, fx);

    h.proc.reset();              // host stop; the next block is the next start
    requireClean(h, fx);
}

TEST_CASE("Processor: effects start clean after host bypass (review B6)", "[processor][effects]")
{
    const Fx fx = GENERATE(Fx::Delay, Fx::Reverb, Fx::IR);
    juce::TemporaryFile irFile(".wav");
    ProcessorHarness h;
    enable(h, fx, irFile);
    ringOut(h, fx);

    h.run(0.5, sine(220.0f, 0.25f), Mode::Bypassed);
    requireClean(h, fx);
}

TEST_CASE("Processor: host bypass passes the input through, delayed by the reported latency", "[processor][bypass]")
{
    ProcessorHarness h;
    const int latency = h.proc.getLatencySamples();
    REQUIRE(latency > 0);   // 12 at 44.1 kHz / 4x

    // Processed blocks keep the passthrough primed, so the first bypassed
    // samples are the last processed block's input: no gap, no stale audio.
    const int n = 2205;
    auto ramp = [](int i, int) { return 1e-4f * static_cast<float>(i + 1); };
    h.run(static_cast<double>(n) / h.sampleRate, ramp);

    std::vector<float> out;
    auto impulseAt100 = [](int i, int) { return i == 100 ? 1.0f : -0.5f; };
    h.run(0.01, impulseAt100, Mode::Bypassed, &out);

    std::vector<float> expected;
    for (int k = 0; k < latency; ++k) expected.push_back(ramp(n - latency + k, 0));
    for (int i = 0; i < static_cast<int>(out.size()) - latency; ++i)
        expected.push_back(impulseAt100(i, 0));
    REQUIRE(out == expected);

    // The tuner still hears the DI while bypassed.
    float latest = 0.0f;
    REQUIRE(h.proc.readLatestTunerSamples(&latest, 1) == 1);
    REQUIRE(latest == Catch::Approx(-0.5f).margin(1e-9));
}

TEST_CASE("Processor: switching an effect off fades its wet out, then is bit-exact dry (review B6)", "[processor][effects]")
{
    // Three identical processors on the same note: `off` switches the effect
    // off at the measurement point, `on` keeps it on, `dry` never had it. The
    // amp is deterministic, so any difference between them is the effect.
    const Fx fx = GENERATE(Fx::Delay, Fx::Reverb);
    juce::TemporaryFile unused(".wav");
    ProcessorHarness off, on, dry;
    enable(off, fx, unused);
    enable(on, fx, unused);
    const auto note = sine(220.0f, 0.25f);
    for (auto* h : { &off, &on, &dry }) h->run(1.0, note);

    off.set(toggleOf(fx), 0.0f);
    std::vector<float> outOff, outOn, outDry;
    // Continue the same note (phase-continuous), 0.3 s.
    auto cont = [&](int i, int ch) { return note(i + 44100, ch); };
    off.run(0.3, cont, Mode::Process, &outOff);
    on.run(0.3, cont, Mode::Process, &outOn);
    dry.run(0.3, cont, Mode::Process, &outDry);

    INFO(name(fx));
    // No step at the switch: over the first 128 samples (6 % of the ramp) the
    // output stays far closer to the effect left on than to the dry signal a
    // hard cut would jump straight to (the +3 dB dry step).
    const float cutStep = maxAbsDiff(outDry, outOn, 0, 128);
    REQUIRE(cutStep > 0.02f);
    REQUIRE(maxAbsDiff(outOff, outOn, 0, 128) < 0.1f * cutStep);
    // The wet is still fading during the 50 ms ramp...
    REQUIRE(maxAbsDiff(outOff, outDry, 1000, 1500) > 0.01f);
    // ...and once it has finished (50 ms + one block) the output is the dry
    // path bit for bit: the effect is skipped exactly as if it had never run.
    const size_t settled = static_cast<size_t>(0.05 * 44100.0) + 512;
    REQUIRE(slice(outOff, settled, outOff.size()) == slice(outDry, settled, outDry.size()));
}

TEST_CASE("Processor: an effect switched back on mid-fade resumes its live tail (review B6)", "[processor][effects]")
{
    // Back on while still fading out, the effect never stopped running, so its
    // tail is live, not stale: the mix ramp just reverses. Once it is back up
    // the output is exactly that of an effect that was never switched off.
    const Fx fx = GENERATE(Fx::Delay, Fx::Reverb);
    juce::TemporaryFile unused(".wav");
    ProcessorHarness blip, on;
    for (auto* h : { &blip, &on })
    {
        enable(*h, fx, unused);
        h->run(1.0, sine(220.0f, 0.25f));
        h->silence(0.2);
    }

    std::vector<float> outBlip, outOn;
    blip.set(toggleOf(fx), 0.0f);
    blip.run(512.0 / 44100.0, zeros(), Mode::Process, &outBlip);   // one block: mid-fade
    blip.set(toggleOf(fx), 1.0f);
    blip.run(0.3, zeros(), Mode::Process, &outBlip);
    on.run(512.0 / 44100.0 + 0.3, zeros(), Mode::Process, &outOn);
    REQUIRE(outBlip.size() == outOn.size());

    INFO(name(fx));
    REQUIRE(maxAbsDiff(outBlip, outOn, 0, 512) > 0.0f);        // it did start fading
    const size_t back = 512 + static_cast<size_t>(0.05 * 44100.0) + 512;
    REQUIRE(slice(outOn, back, outOn.size()) != std::vector<float>(outOn.size() - back, 0.0f));
    REQUIRE(slice(outBlip, back, outBlip.size()) == slice(outOn, back, outOn.size()));
}
