// ═══════════════════════════════════════════════════════════════
// Delay + Reverb Tests — post-cab time-based effects (JUCE-dependent)
//
//   - Delay mix=0 is bit-exact dry (incl. with tone + feedback engaged)
//   - Delay produces a delayed tap; feedback decays
//   - Delay TONE darkens the repeats; modulation stays finite at max feedback
//   - Delay loop gain is FEEDBACK: repeats decay even at DELAY_MAX_FEEDBACK (B8)
//   - Delay/Reverb restart() drop the tail frozen while off, ramp mix in (B6)
//   - Delay holds a tempo-synced 2000 ms, past the knob's 1000 ms (S7)
//   - Reverb SIZE glides when turned, no block-rate zipper; restart lands it (S5)
//   - Reverb mix=0 is bit-exact dry
//   - Reverb mix>0 adds energy / a decaying tail
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/effects/Delay.h"
#include "dsp/effects/Reverb.h"
#include "dsp/filters/Biquad.h"
#include <algorithm>
#include <cmath>
#include <vector>

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

// ── Review B8: the feedback loop's small-signal gain is the FEEDBACK value ──
// The saturator used to be tanh(k·x)/tanh(k), whose slope at low level is
// k/tanh(k) ≈ 1.9, so the loop ran at feedback × 1.9 and self-oscillated above
// feedback ≈ 0.53 — the top ~45 % of the knob.

namespace {
// Runs `seconds` of audio through `d` in host-sized blocks, input from `fill`
// (sample index → value, channel 0 and 1 alike), and returns the peak of
// channel `ch` over [fromSec, toSec).
template <typename Fill>
float runAndPeak(Delay& d, int channels, double seconds, double fromSec, double toSec,
                 Fill&& fill, int ch = 0)
{
    const int total = static_cast<int>(seconds * kSR);
    const int from  = static_cast<int>(fromSec * kSR);
    const int to    = static_cast<int>(toSec * kSR);
    float peak = 0.0f;
    auto buf = makeBuffer(channels, kBlock);
    for (int start = 0; start < total; start += kBlock)
    {
        const int n = std::min(kBlock, total - start);
        buf.setSize(channels, n, false, false, true);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < n; ++i)
                buf.setSample(c, i, fill(start + i));
        d.process(buf);
        for (int i = 0; i < n; ++i)
            if (start + i >= from && start + i < to)
                peak = std::max(peak, std::abs(buf.getSample(ch, i)));
    }
    return peak;
}

// 10 ms burst of a 0.1-amplitude 1 kHz sine, then silence.
float burst(int i)
{
    return i < static_cast<int>(0.01 * kSR)
        ? 0.1f * std::sin(2.0f * PI_F * 1000.0f * static_cast<float>(i) / static_cast<float>(kSR))
        : 0.0f;
}
} // namespace

TEST_CASE("Delay repeats decay to silence at DELAY_MAX_FEEDBACK (review B8)", "[delay]")
{
    Delay d;
    d.prepare(kSR, kBlock, 1);
    d.setParameters(100.0f, DELAY_MAX_FEEDBACK, 1.0f, 1.0f);   // bright: least loop loss

    // Old code: the repeats grew to and held at ~1.0 full scale.
    const float late = runAndPeak(d, 1, 30.0, 29.0, 30.0, burst);
    REQUIRE(std::isfinite(late));
    REQUIRE(late < 1e-4f);   // the burst was 0.1: at least -60 dB below it
}

TEST_CASE("Delay ping-pong repeats decay to silence at DELAY_MAX_FEEDBACK (review B8)", "[delay]")
{
    Delay d;
    d.prepare(kSR, kBlock, 2);
    d.setParameters(100.0f, DELAY_MAX_FEEDBACK, 1.0f, 1.0f, true);

    const float lateL = runAndPeak(d, 2, 30.0, 29.0, 30.0, burst, 0);
    d.prepare(kSR, kBlock, 2);
    d.setParameters(100.0f, DELAY_MAX_FEEDBACK, 1.0f, 1.0f, true);
    const float lateR = runAndPeak(d, 2, 30.0, 29.0, 30.0, burst, 1);
    REQUIRE(lateL < 1e-4f);
    REQUIRE(lateR < 1e-4f);
}

TEST_CASE("A freshly reset delay at DELAY_MAX_FEEDBACK stays silent on silence (review B8)", "[delay]")
{
    // The loop filters inject ANTI_DENORMAL; with loop gain > 1 that floor grew
    // into a full-scale self-oscillation with nothing played.
    Delay d;
    d.prepare(kSR, kBlock, 1);
    d.setParameters(100.0f, DELAY_MAX_FEEDBACK, 1.0f, 1.0f);
    const float peak = runAndPeak(d, 1, 30.0, 0.0, 30.0, [](int) { return 0.0f; });
    REQUIRE(peak < 1e-9f);
}

TEST_CASE("Delay repeat-to-repeat level is the FEEDBACK value at low level (review B8)", "[delay]")
{
    // Default FEEDBACK (0.35): each repeat sits at ~0.35 of the one before
    // (-9.1 dB), not 0.35 × 1.9 = 0.67 (-3.5 dB) as before the fix.
    Delay d;
    d.prepare(kSR, kBlock, 1);
    const float timeMs = 200.0f;
    d.setParameters(timeMs, 0.35f, 1.0f, 1.0f);
    runAndPeak(d, 1, 0.2, 0.0, 0.2, [](int) { return 0.0f; });   // settle the smoothers

    const double t = timeMs * 0.001;
    // Repeats k = 1, 2, 3 land at k·T after the burst; read each in a window.
    const int total = static_cast<int>(4.0 * t * kSR);
    auto buf = makeBuffer(1, total);
    for (int i = 0; i < total; ++i) buf.setSample(0, i, burst(i));
    d.process(buf);
    auto repeatPeak = [&](int k)
    {
        const int lo = static_cast<int>((k * t - 0.02) * kSR);
        return buf.getMagnitude(0, lo, static_cast<int>(0.05 * kSR));
    };
    const float r1 = repeatPeak(1), r2 = repeatPeak(2), r3 = repeatPeak(3);
    REQUIRE(r1 > 0.05f);   // first repeat unchanged by the fix (still ~0.19 for a 0.1 burst)
    REQUIRE(r2 / r1 == Catch::Approx(0.35f).margin(0.03f));
    REQUIRE(r3 / r2 == Catch::Approx(0.35f).margin(0.03f));
}

// ── Review B6: re-enabling an effect must not replay the tail it froze with ──
// While an effect is off the processor stops calling process(), so its state
// is whatever it held when it stopped. The processor calls restart() on the
// off→on edge (and after power-off, tuner mute or a host reset).

TEST_CASE("Delay restart() drops the frozen tail and ramps the wet in (review B6)", "[delay]")
{
    Delay d;
    d.prepare(kSR, kBlock, 2);
    d.setParameters(1000.0f, 0.35f, 0.5f, 0.5f);
    // Fill the line, then "switch off" (stop calling process) right after.
    runAndPeak(d, 2, 1.5, 0.0, 1.5, [](int i) {
        return 0.5f * std::sin(2.0f * PI_F * 220.0f * static_cast<float>(i) / static_cast<float>(kSR));
    });

    d.setParameters(1000.0f, 0.35f, 0.5f, 0.5f);
    d.restart();

    // Re-enabled on silence: without restart() this replays the old repeats
    // at ~-20 dBFS for seconds.
    const float replay = runAndPeak(d, 2, 3.0, 0.0, 3.0, [](int) { return 0.0f; });
    REQUIRE(replay < 1e-9f);
}

TEST_CASE("Delay restart() starts fully dry and snaps TIME (review B6)", "[delay]")
{
    Delay d;
    d.prepare(kSR, kBlock, 1);
    d.setParameters(400.0f, 0.0f, 1.0f, 1.0f);
    runAndPeak(d, 1, 0.2, 0.0, 0.2, [](int) { return 0.0f; });

    // TIME moved DOWN while off: 400 → 20 ms. (Moving it up can't show the snap:
    // a tap gliding longer only ever reads the region restart() just cleared.)
    const float timeMs = 20.0f;
    d.setParameters(timeMs, 0.0f, 1.0f, 1.0f);
    d.restart();

    const int n = static_cast<int>(0.2 * kSR);
    auto buf = makeBuffer(1, n);
    buf.setSample(0, 0, 1.0f);
    d.process(buf);

    // MIX ramps in from 0, so the first sample is (almost exactly) the dry input,
    // not the fully-wet mix's silence.
    REQUIRE(buf.getSample(0, 0) == Catch::Approx(1.0f).margin(1e-3f));
    // The repeat lands at 20 ms. Without the snap the tap glides 400 → 20 ms over
    // 50 ms and meets the impulse at ~46 ms instead, with nothing at 20 ms.
    const int expected = static_cast<int>(timeMs * 0.001 * kSR);
    int peakAt = 1;
    for (int i = 1; i < n; ++i)
        if (std::abs(buf.getSample(0, i)) > std::abs(buf.getSample(0, peakAt))) peakAt = i;
    REQUIRE(std::abs(peakAt - expected) <= 4);
    REQUIRE(buf.getMagnitude(0, 1, expected - 20) < 1e-6f);   // nothing before the tap
}

TEST_CASE("Delay restart() snaps FEEDBACK (review B6)", "[delay]")
{
    // FEEDBACK turned from 0.9 to 0 while off. Without the snap it glides down
    // over 50 ms, so the first repeat (at 20 ms) recirculates at ~0.5 and a
    // second repeat sounds at 40 ms.
    Delay d;
    d.prepare(kSR, kBlock, 1);
    d.setParameters(20.0f, 0.9f, 1.0f, 1.0f);
    runAndPeak(d, 1, 0.2, 0.0, 0.2, [](int) { return 0.0f; });

    d.setParameters(20.0f, 0.0f, 1.0f, 1.0f);
    d.restart();

    const int n = static_cast<int>(0.1 * kSR);
    auto buf = makeBuffer(1, n);
    buf.setSample(0, 0, 1.0f);
    d.process(buf);

    const int t = static_cast<int>(0.02 * kSR);
    const float first  = buf.getMagnitude(0, t - 20, 40);
    const float second = buf.getMagnitude(0, 2 * t - 20, 40);
    REQUIRE(first > 0.01f);
    REQUIRE(second < 1e-6f);
}

TEST_CASE("Reverb restart() drops the frozen tail and ramps the wet in (review B6)", "[reverb]")
{
    Reverb r;
    r.prepare(kSR, kBlock, 2);
    r.setParameters(0.5f, 1.0f, 0.5f);
    auto buf = makeBuffer(2, static_cast<int>(0.5 * kSR));
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        const float v = 0.5f * std::sin(2.0f * PI_F * 220.0f * static_cast<float>(i) / static_cast<float>(kSR));
        buf.setSample(0, i, v);
        buf.setSample(1, i, v);
    }
    r.process(buf);

    r.setParameters(0.5f, 1.0f, 0.5f);
    r.restart();

    auto quiet = makeBuffer(2, static_cast<int>(1.0 * kSR));
    r.process(quiet);
    REQUIRE(maxAbs(quiet, 0) < 1e-9f);
    REQUIRE(maxAbs(quiet, 1) < 1e-9f);

    // And the first sample after restart is the dry signal, not a -3 dB step.
    r.restart();
    auto step = makeBuffer(2, 1, 1.0f);
    r.process(step);
    REQUIRE(step.getSample(0, 0) == Catch::Approx(1.0f).margin(1e-3f));
}

// ── Review S7: a tempo-synced time runs past the TIME knob's 1000 ms ──
// TempoSync now clamps at DELAY_SYNC_MAX_MS (2000 ms, a 1/2 note at 60 BPM).
// The delay used to clamp every time to the knob's 1000 ms and size its line
// for it, so a synced 2000 ms repeat came back at 1000 ms. Run at 192 kHz: the
// line is sized in samples, so the highest common host rate is where an
// undersized line shows.

TEST_CASE("Delay plays a synced 2000 ms repeat at 192 kHz (review S7)", "[delay]")
{
    constexpr double fs = 192000.0;
    Delay d;
    d.prepare(fs, kBlock, 1);
    d.setParameters(DELAY_SYNC_MAX_MS, 0.0f, 1.0f, 1.0f);   // fully wet, one repeat
    d.restart();                                            // snap TIME, no glide

    const int total = static_cast<int>(2.2 * fs);
    std::vector<float> out(static_cast<size_t>(total));
    auto buf = makeBuffer(1, kBlock);
    for (int start = 0; start < total; start += kBlock)
    {
        const int n = std::min(kBlock, total - start);
        buf.setSize(1, n, false, false, true);
        buf.clear();
        if (start == 0) buf.setSample(0, 0, 1.0f);
        d.process(buf);
        for (int i = 0; i < n; ++i) out[static_cast<size_t>(start + i)] = buf.getSample(0, i);
    }

    int peakAt = 1;
    for (int i = 1; i < total; ++i)
        if (std::abs(out[static_cast<size_t>(i)]) > std::abs(out[static_cast<size_t>(peakAt)])) peakAt = i;

    // The wow/flutter sway moves the tap by at most 0.36 ms.
    const double peakMs = 1000.0 * peakAt / fs;
    REQUIRE(std::abs(out[static_cast<size_t>(peakAt)]) > 0.2f);
    REQUIRE(peakMs == Catch::Approx(2000.0).margin(0.5));
    // Nothing earlier — in particular no repeat at the old 1000 ms clamp.
    float early = 0.0f;
    for (int i = 1; i < static_cast<int>(1.99 * fs); ++i)
        early = std::max(early, std::abs(out[static_cast<size_t>(i)]));
    REQUIRE(early < 1e-6f);
}

// ── Review S5: SIZE glides instead of jumping the loop length per block ──
// SIZE reaches the reverb once per host block. It used to snap each spring's
// loop to the new whole-sample length, so turning the knob jumped the read tap
// 3–5 samples per block (moderate turn) or 42 (fast turn): a block-rate zipper
// and, on a fast turn, clicks. Measured as in the review: 44.1 kHz, 512-sample
// blocks, a 220 Hz tone into a fully wet reverb. The loop's damping caps the
// wet at a few kHz, so energy above 8 kHz is the zipper.

namespace {
struct SweepStats { double hfEnergy; float maxStep; };

// sweepSec > 0 turns SIZE 0 → 1 over sweepSec (one setParameters per block, as
// the processor does) after 1 s at SIZE 0; sweepSec == 0 holds `staticSize`.
// Measures from the start of the sweep to 0.1 s after it ends.
SweepStats reverbSizeSweep(double sweepSec, float staticSize, double windowSec)
{
    constexpr double fs = 44100.0;
    constexpr int    block = 512;
    const bool sweep = sweepSec > 0.0;
    Reverb r;
    r.prepare(fs, block, 2);
    r.setParameters(1.0f, sweep ? 0.0f : staticSize, 0.5f);
    r.restart();

    Biquad hp1, hp2;
    hp1.makeHighPass(8000.0f, 0.707f, static_cast<float>(fs));
    hp2.makeHighPass(8000.0f, 0.707f, static_cast<float>(fs));

    const int warm  = static_cast<int>(1.0 * fs);
    const int total = warm + static_cast<int>(windowSec * fs);
    SweepStats st { 0.0, 0.0f };
    float prev = 0.0f;
    auto buf = makeBuffer(2, block);
    for (int start = 0; start < total; start += block)
    {
        if (sweep && start >= warm)
            r.setParameters(1.0f, static_cast<float>(std::min(1.0, (start - warm) / (sweepSec * fs))), 0.5f);
        const int n = std::min(block, total - start);
        buf.setSize(2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.25f * std::sin(2.0f * PI_F * 220.0f * static_cast<float>(start + i) / static_cast<float>(fs));
            buf.setSample(0, i, x);
            buf.setSample(1, i, x);
        }
        r.process(buf);
        for (int i = 0; i < n; ++i)
        {
            const float y  = buf.getSample(0, i);
            const float hf = hp2.process(hp1.process(y));
            if (start + i >= warm)
            {
                st.hfEnergy += static_cast<double>(hf) * hf;
                st.maxStep = std::max(st.maxStep, std::abs(y - prev));
            }
            prev = y;
        }
    }
    return st;
}
} // namespace

TEST_CASE("Reverb SIZE turned fast or slow adds no block-rate zipper (review S5)", "[reverb]")
{
    for (const double sweepSec : { 0.25, 2.5 })   // 0 → 1 in 0.25 s; 0.4 of the range per second
    {
        CAPTURE(sweepSec);
        const double window = sweepSec + 0.1;
        const auto swept = reverbSizeSweep(sweepSec, 0.0f, window);
        const auto lo    = reverbSizeSweep(0.0, 0.0f, window);
        const auto hi    = reverbSizeSweep(0.0, 1.0f, window);
        const double refHf  = std::max(lo.hfEnergy, hi.hfEnergy);
        const float refStep = std::max(lo.maxStep, hi.maxStep);

        // Snapping the loop: +56.7 dB (fast) / +34.7 dB (slow) above 8 kHz over
        // a static SIZE. Gliding it: +1.5 / -0.4 dB.
        const double hfDb = 10.0 * std::log10(swept.hfEnergy / refHf);
        CAPTURE(hfDb);
        REQUIRE(hfDb < 6.0);
        // The fast turn's clicks: steps 10.6x a static SIZE's largest; gliding, 1.5x.
        if (sweepSec < 1.0)
            REQUIRE(swept.maxStep < 2.5f * refStep);
    }
}

TEST_CASE("Reverb restart() lands a SIZE glide on its target (review S5)", "[reverb]")
{
    // Re-enabled after SIZE moved while the reverb was off, the tank must start
    // at the new size, not sweep in from the old one: restart() then has to give
    // exactly what a reverb prepared at the new size gives.
    auto impulseAfter = [](Reverb& r) {
        auto buf = makeBuffer(2, static_cast<int>(0.3 * kSR));
        buf.setSample(0, 0, 1.0f);
        buf.setSample(1, 0, 1.0f);
        r.process(buf);
        return buf;
    };

    Reverb fresh;
    fresh.prepare(kSR, kBlock, 2);
    fresh.setParameters(1.0f, 1.0f, 0.5f);
    fresh.restart();
    const auto want = impulseAfter(fresh);

    Reverb r;
    r.prepare(kSR, kBlock, 2);
    r.setParameters(1.0f, 0.0f, 0.5f);
    r.restart();
    auto tone = makeBuffer(2, kBlock, 0.1f);
    r.process(tone);
    r.setParameters(1.0f, 1.0f, 0.5f);   // SIZE 0 → 1: a glide starts...
    r.process(tone);                     // ...and is mid-way when the reverb stops
    r.restart();
    const auto got = impulseAfter(r);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < want.getNumSamples(); ++i)
            if (got.getSample(ch, i) != want.getSample(ch, i))
                FAIL("channel " << ch << " differs at sample " << i);
}
