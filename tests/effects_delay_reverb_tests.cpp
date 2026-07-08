// ═══════════════════════════════════════════════════════════════
// Delay + Reverb Tests — post-cab time-based effects (JUCE-dependent)
//
//   - Delay mix=0 is bit-exact dry (incl. with tone + feedback engaged)
//   - Delay produces a delayed tap; feedback decays
//   - Delay TONE darkens the repeats; modulation stays finite at max feedback
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
    d.setParameters(300.0f, 0.5f, 0.0f, 0.5f);

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
    d.setParameters(timeMs, 0.0f, 1.0f, 1.0f);  // fully wet, no feedback, bright tone

    // Let the time smoother settle on silence first.
    auto settle = makeBuffer(1, static_cast<int>(0.1 * kSR));
    d.process(settle);

    // One-sample impulse, then observe where it reappears.
    const int n = static_cast<int>(0.12 * kSR);
    auto buf = makeBuffer(1, n);
    buf.setSample(0, 0, 1.0f);
    d.process(buf);

    // The repeat is slightly darkened (feedback high-cut) and the wow/flutter
    // LFO can wander the tap a few samples, so check the energy is localised in
    // a window around the expected location rather than a strict single peak.
    const int expected = static_cast<int>(timeMs * 0.001 * kSR);
    const int lo = std::max(1, expected - 200);
    const int hi = std::min(n, expected + 200);
    const float inWindow = buf.getMagnitude(0, lo, hi - lo);
    const float outside  = buf.getMagnitude(0, hi, n - hi);  // after the tap → ~silence (no fb)

    REQUIRE(inWindow > 0.2f);       // a clear delayed copy appears near `expected`
    REQUIRE(inWindow > outside);    // and it is localised there
}

TEST_CASE("Delay feedback decays, never explodes", "[delay]")
{
    Delay d;
    d.prepare(kSR, kBlock, 1);
    d.setParameters(40.0f, 0.5f, 1.0f, 0.5f);

    auto buf = makeBuffer(1, static_cast<int>(2.0 * kSR));
    buf.setSample(0, 0, 1.0f);
    d.process(buf);

    // Energy in the first half should exceed the second half (decaying).
    float firstHalf = buf.getMagnitude(0, 0, buf.getNumSamples() / 2);
    float secondHalf = buf.getMagnitude(0, buf.getNumSamples() / 2, buf.getNumSamples() / 2);
    REQUIRE(firstHalf > secondHalf);
    REQUIRE(std::isfinite(secondHalf));
}

TEST_CASE("Delay mix=0 stays bit-exact dry with tone + feedback engaged", "[delay]")
{
    // The analog voicing runs filters and an LFO on the wet/feedback path; none
    // of that may leak into the output when the effect is fully dry (mix=0).
    Delay d;
    d.prepare(kSR, kBlock, 2);
    d.setParameters(250.0f, 0.7f, 0.0f, 0.0f);  // dark tone, heavy feedback, fully dry

    auto buf = makeBuffer(2, kBlock);
    for (int i = 0; i < kBlock; ++i)
    {
        float v = std::sin(2.0f * PI_F * 330.0f * i / static_cast<float>(kSR));
        buf.setSample(0, i, v);
        buf.setSample(1, i, v);
    }
    juce::AudioBuffer<float> ref(buf);
    d.process(buf);

    for (int i = 0; i < kBlock; ++i)
        REQUIRE(buf.getSample(0, i) == Catch::Approx(ref.getSample(0, i)));
}

TEST_CASE("Delay TONE darkens the repeats", "[delay]")
{
    // Run an impulse through a feedback delay at a dark vs a bright tone and
    // compare high-frequency content of the result. The feedback-path high-cut
    // means the dark setting should have markedly less HF energy.
    auto hfEnergy = [](float tone)
    {
        Delay d;
        d.prepare(kSR, kBlock, 1);
        d.setParameters(60.0f, 0.6f, 1.0f, tone);  // fully wet so we hear the loop

        const int n = static_cast<int>(1.0 * kSR);
        auto buf = makeBuffer(1, n);
        buf.setSample(0, 0, 1.0f);
        d.process(buf);

        // First-difference energy is a crude but monotone HF measure.
        double hf = 0.0;
        for (int i = 1; i < n; ++i)
        {
            const float diff = buf.getSample(0, i) - buf.getSample(0, i - 1);
            hf += static_cast<double>(diff) * diff;
        }
        return hf;
    };

    const double bright = hfEnergy(1.0f);
    const double dark   = hfEnergy(0.0f);
    REQUIRE(dark < bright);
}

TEST_CASE("Delay with modulation + max feedback stays finite", "[delay]")
{
    Delay d;
    d.prepare(kSR, kBlock, 1);
    d.setParameters(120.0f, DELAY_MAX_FEEDBACK, 1.0f, 0.8f);

    auto buf = makeBuffer(1, static_cast<int>(3.0 * kSR));
    buf.setSample(0, 0, 1.0f);
    d.process(buf);

    const float mag = buf.getMagnitude(0, 0, buf.getNumSamples());
    REQUIRE(std::isfinite(mag));
    REQUIRE(mag < 10.0f);  // lossy loop (< unity fb + damping) never runs away
}

TEST_CASE("Delay feedback saturation bounds a hot impulse near unity", "[delay]")
{
    // A loud impulse into a hot feedback loop should be tamed by the tanh
    // saturation stage well below where an unsaturated linear loop would sit.
    Delay d;
    d.prepare(kSR, kBlock, 1);
    d.setParameters(80.0f, DELAY_MAX_FEEDBACK, 1.0f, 1.0f);

    // Let the equal-power mix smoother settle to fully wet on silence first.
    // Otherwise mix is still ramping from 0 at sample 0 (dryG≈1, wetG≈0), so the
    // raw dry impulse passes straight through and masks the wet-path saturation
    // this test targets. (Same settle pattern as the ping-pong test below.)
    auto settle = makeBuffer(1, static_cast<int>(0.1 * kSR));
    d.process(settle);

    auto buf = makeBuffer(1, static_cast<int>(0.5 * kSR));
    buf.setSample(0, 0, 5.0f);   // hot input, well above unity
    d.process(buf);

    const float mag = buf.getMagnitude(0, 0, buf.getNumSamples());
    REQUIRE(std::isfinite(mag));
    REQUIRE(mag < 1.5f);   // saturation keeps repeats close to unity, not 5x
}

TEST_CASE("Delay ping-pong bounces repeats between L and R", "[delay]")
{
    // Cross-feed topology: a mono send triggers the L tap at time T, which
    // feeds forward into the R tap (repeat at 2T), which feeds back into L
    // (repeat at 3T). Verify the energy alternates channel each hop.
    Delay d;
    d.prepare(kSR, kBlock, 2);
    const float timeMs = 50.0f;
    d.setParameters(timeMs, 0.7f, 1.0f, 1.0f, true);  // fully wet, bright tone, ping-pong on

    // Let the time smoother settle on silence first.
    auto settle = makeBuffer(2, static_cast<int>(0.1 * kSR));
    d.process(settle);

    const int n = static_cast<int>(0.3 * kSR);
    auto buf = makeBuffer(2, n);
    buf.setSample(0, 0, 1.0f);   // impulse on L only
    d.process(buf);

    const int hopSamples = static_cast<int>(timeMs * 0.001 * kSR);
    auto energyAt = [&](int ch, int hop)
    {
        const int centre = hopSamples * hop;
        const int lo = std::max(1, centre - 150);
        const int hi = std::min(n, centre + 150);
        return buf.getMagnitude(ch, lo, hi - lo);
    };

    const float lAtHop1 = energyAt(0, 1), rAtHop1 = energyAt(1, 1);
    const float lAtHop2 = energyAt(0, 2), rAtHop2 = energyAt(1, 2);

    REQUIRE(lAtHop1 > 0.05f);
    REQUIRE(lAtHop1 > rAtHop1);   // hop 1 (T): repeat lands on L
    REQUIRE(rAtHop2 > 0.05f);
    REQUIRE(rAtHop2 > lAtHop2);   // hop 2 (2T): bounced to R
}

TEST_CASE("Delay ping-pong is a no-op on mono buffers", "[delay]")
{
    Delay d;
    d.prepare(kSR, kBlock, 1);
    d.setParameters(50.0f, 0.4f, 0.6f, 0.5f, true);

    auto buf = makeBuffer(1, static_cast<int>(0.2 * kSR));
    buf.setSample(0, 0, 1.0f);
    d.process(buf);

    const float mag = buf.getMagnitude(0, 0, buf.getNumSamples());
    REQUIRE(std::isfinite(mag));
    REQUIRE(mag > 0.0f);
}

TEST_CASE("Reverb mix=0 is bit-exact dry", "[reverb]")
{
    Reverb r;
    r.prepare(kSR, kBlock, 2);
    r.setParameters(0.0f, 0.5f, 0.5f);

    // Warm up on silence so juce::dsp::Reverb's smoothed wet/dry gains settle
    // (wet → 0, dry → 1 exactly); reverb state stays zero under zero input.
    for (int k = 0; k < 8; ++k)
    {
        auto warm = makeBuffer(2, kBlock);
        r.process(warm);
    }

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
    r.setParameters(0.5f, 0.7f, 0.6f);

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
