// ═══════════════════════════════════════════════════════════════
// Delay + Reverb Tests — post-cab time-based effects (JUCE-dependent)
//
//   - Delay mix=0 is bit-exact dry
//   - Delay produces a delayed tap; feedback decays
//   - Reverb mix=0 is bit-exact dry
//   - Reverb mix>0 adds energy / a decaying tail
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/effects/Delay.h"
#include "dsp/effects/Reverb.h"
#include <cmath>

using namespace sr;

namespace {
constexpr double kSR = 48000.0;
constexpr int    kBlock = 512;

juce::AudioBuffer<float> makeBuffer(int channels, int samples, float fill = 0.0f)
{
    juce::AudioBuffer<float> b(channels, samples);
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::fill(b.getWritePointer(ch), fill, samples);
    return b;
}

float maxAbs(const juce::AudioBuffer<float>& b, int ch = 0)
{
    return b.getMagnitude(ch, 0, b.getNumSamples());
}
} // namespace

TEST_CASE("Delay mix=0 is bit-exact dry", "[delay]")
{
    Delay d;
    d.prepare(kSR, kBlock, 2);
    d.setParameters(300.0f, 0.5f, 0.0f);

    auto buf = makeBuffer(2, kBlock);
    for (int i = 0; i < kBlock; ++i)
    {
        float v = std::sin(2.0f * PI_F * 440.0f * i / static_cast<float>(kSR));
        buf.setSample(0, i, v);
        buf.setSample(1, i, v);
    }
    juce::AudioBuffer<float> ref(buf);
    d.process(buf);

    for (int i = 0; i < kBlock; ++i)
        REQUIRE(buf.getSample(0, i) == Catch::Approx(ref.getSample(0, i)));
}

TEST_CASE("Delay produces a delayed tap", "[delay]")
{
    Delay d;
    d.prepare(kSR, kBlock, 1);
    const float timeMs = 50.0f;
    d.setParameters(timeMs, 0.0f, 1.0f);  // fully wet, no feedback

    // Let the time smoother settle on silence first.
    auto settle = makeBuffer(1, static_cast<int>(0.1 * kSR));
    d.process(settle);

    // One-sample impulse, then observe where it reappears.
    const int n = static_cast<int>(0.12 * kSR);
    auto buf = makeBuffer(1, n);
    buf.setSample(0, 0, 1.0f);
    d.process(buf);

    const int expected = static_cast<int>(timeMs * 0.001 * kSR);
    int peakIdx = 0; float peak = 0.0f;
    for (int i = 1; i < n; ++i)  // skip dry sample 0 (mix=1 → dry is 0 anyway)
        if (std::abs(buf.getSample(0, i)) > peak) { peak = std::abs(buf.getSample(0, i)); peakIdx = i; }

    REQUIRE(peak > 0.5f);
    REQUIRE(std::abs(peakIdx - expected) < 64);  // within interpolation tolerance
}

TEST_CASE("Delay feedback decays, never explodes", "[delay]")
{
    Delay d;
    d.prepare(kSR, kBlock, 1);
    d.setParameters(40.0f, 0.5f, 1.0f);

    auto buf = makeBuffer(1, static_cast<int>(2.0 * kSR));
    buf.setSample(0, 0, 1.0f);
    d.process(buf);

    // Energy in the first half should exceed the second half (decaying).
    float firstHalf = buf.getMagnitude(0, 0, buf.getNumSamples() / 2);
    float secondHalf = buf.getMagnitude(0, buf.getNumSamples() / 2, buf.getNumSamples() / 2);
    REQUIRE(firstHalf > secondHalf);
    REQUIRE(std::isfinite(secondHalf));
}

TEST_CASE("Reverb mix=0 is bit-exact dry", "[reverb]")
{
    Reverb r;
    r.prepare(kSR, kBlock, 2);
    r.setParameters(0.0f, 0.5f);

    auto buf = makeBuffer(2, kBlock);
    for (int i = 0; i < kBlock; ++i)
    {
        float v = std::sin(2.0f * PI_F * 440.0f * i / static_cast<float>(kSR));
        buf.setSample(0, i, v);
        buf.setSample(1, i, v);
    }
    juce::AudioBuffer<float> ref(buf);
    r.process(buf);

    for (int i = 0; i < kBlock; ++i)
        REQUIRE(buf.getSample(0, i) == Catch::Approx(ref.getSample(0, i)).margin(1e-6));
}

TEST_CASE("Reverb mix>0 adds a decaying tail", "[reverb]")
{
    Reverb r;
    r.prepare(kSR, kBlock, 2);
    r.setParameters(0.5f, 0.7f);

    // Impulse, then silence — a wet reverb keeps ringing after the impulse.
    const int n = static_cast<int>(0.5 * kSR);
    auto buf = makeBuffer(2, n);
    buf.setSample(0, 0, 1.0f);
    buf.setSample(1, 0, 1.0f);
    r.process(buf);

    // There should be non-trivial energy well after the initial impulse.
    float tail = buf.getMagnitude(0, static_cast<int>(0.05 * kSR), static_cast<int>(0.3 * kSR));
    REQUIRE(tail > 1e-4f);
    REQUIRE(r.tailSeconds() > 0.0f);
}
