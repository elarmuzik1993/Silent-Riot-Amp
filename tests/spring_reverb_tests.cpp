// ═══════════════════════════════════════════════════════════════
// SpringReverbCore Tests — spring-tank reverb (pure C++)
//
//   - Impulse → a decaying tail lasting well beyond the loop length
//   - Dispersion spreads energy across time (echo train, not a single delta)
//   - TONE controls tail brightness (loop damping)
//   - SIZE controls decay length
//   - Stable under sustained full-scale noise at max settings
//   - rampSize: a SIZE turn moves the read tap at the same speed at any block
//     size, and reset() lands a ramp in progress (review S5)
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include "dsp/effects/SpringReverb.h"
#include <vector>
#include <cmath>
#include <algorithm>

using namespace sr;

namespace {
constexpr float kSR = 48000.0f;

std::vector<float> impulseResponse(SpringReverbCore& c, int n)
{
    std::vector<float> y(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        y[static_cast<size_t>(i)] = c.process(i == 0 ? 1.0f : 0.0f);
    return y;
}

double energy(const std::vector<float>& y, int a, int b)
{
    double s = 0.0;
    for (int i = a; i < b && i < static_cast<int>(y.size()); ++i)
        s += static_cast<double>(y[static_cast<size_t>(i)]) * y[static_cast<size_t>(i)];
    return s;
}
} // namespace

TEST_CASE("SpringReverb tail lasts well beyond the loop length", "[spring]")
{
    SpringReverbCore c;
    c.prepare(kSR);
    c.setSize(0.7f);
    c.setTone(0.5f);
    c.reset();   // land the SIZE glide from prepare()'s 0.5: measure a settled tank

    auto y = impulseResponse(c, static_cast<int>(1.0f * kSR));
    // Loop is tens of ms; require real energy ~0.5 s later → feedback loop works.
    double late = energy(y, static_cast<int>(0.4f * kSR), static_cast<int>(0.6f * kSR));
    REQUIRE(late > 1e-7);
    REQUIRE(c.loopSeconds() < 0.1f);  // sanity: loop is tens of ms, tail is much longer
}

TEST_CASE("SpringReverb dispersion spreads energy over time", "[spring]")
{
    SpringReverbCore c;
    c.prepare(kSR);
    c.setSize(0.6f);
    c.setTone(0.6f);
    c.reset();   // land the SIZE glide from prepare()'s 0.5: measure a settled tank

    auto y = impulseResponse(c, static_cast<int>(0.5f * kSR));
    // A single delta would put energy in one window; dispersion + feedback
    // should populate most of several spaced 30 ms windows.
    const int w = static_cast<int>(0.03f * kSR);
    int nonzero = 0;
    for (int k = 0; k < 8; ++k)
        if (energy(y, k * w, (k + 1) * w) > 1e-8)
            ++nonzero;
    REQUIRE(nonzero >= 6);
}

TEST_CASE("SpringReverb TONE controls tail brightness", "[spring]")
{
    auto hfContent = [](float tone) {
        SpringReverbCore c;
        c.prepare(kSR);
        c.setSize(0.6f);
        c.setTone(tone);
        c.reset();   // land the SIZE glide from prepare()'s 0.5: measure a settled tank
        auto y = impulseResponse(c, static_cast<int>(1.0f * kSR));
        double d = 0.0;  // total variation ~ high-frequency content
        for (size_t i = 1; i < y.size(); ++i)
            d += std::abs(y[i] - y[i - 1]);
        return d;
    };
    REQUIRE(hfContent(0.95f) > hfContent(0.1f));
}

TEST_CASE("SpringReverb SIZE controls decay length", "[spring]")
{
    auto decaySeconds = [](float size) {
        SpringReverbCore c;
        c.prepare(kSR);
        c.setSize(size);
        c.setTone(0.5f);
        c.reset();   // land the SIZE glide from prepare()'s 0.5: measure a settled tank
        auto y = impulseResponse(c, static_cast<int>(4.0f * kSR));
        int last = 0;
        for (int i = 0; i < static_cast<int>(y.size()); ++i)
            if (std::abs(y[static_cast<size_t>(i)]) > 1e-3f) last = i;
        return last / kSR;
    };
    REQUIRE(decaySeconds(0.9f) > decaySeconds(0.3f));
}

TEST_CASE("SpringReverb stays finite under sustained full-scale noise", "[spring]")
{
    SpringReverbCore c;
    c.prepare(kSR);
    c.setSize(1.0f);
    c.setTone(1.0f);
    c.reset();   // land the SIZE glide from prepare()'s 0.5: measure a settled tank

    unsigned r = 12345u;
    float peak = 0.0f;
    bool finite = true;
    const int n = static_cast<int>(5.0f * kSR);
    for (int i = 0; i < n; ++i)
    {
        r = r * 1103515245u + 12345u;
        float noise = (static_cast<int>(r >> 9) / 4194304.0f) - 1.0f;
        float o = c.process(noise);
        if (!std::isfinite(o)) finite = false;
        peak = std::max(peak, std::abs(o));
    }
    REQUIRE(finite);
    REQUIRE(peak < 10.0f);  // bounded — downstream tanh limiter handles the rest
}

namespace {
// Per-sample read-speed range of spring 2 (the longest loop) while SIZE turns
// 0 → 1 in 0.25 s at 44.1 kHz, handed over once per `block` as Reverb does.
struct SpeedRange { float lo, hi; };
SpeedRange readSpeedDuringTurn(int block)
{
    constexpr float fs = 44100.0f;
    constexpr int turnStart = 22 * 2048, turnLen = 11025;
    SpringReverbCore c;
    c.prepare(fs);
    c.setSize(0.0f);
    c.reset();
    const Spring& s = c.springs[1];
    float prev = s.lenTarget + s.lenOffset;
    SpeedRange r { 1e9f, -1e9f };
    for (int start = 0; start < turnStart + turnLen; start += block)
    {
        if (start >= turnStart)
            c.rampSize(std::min(1.0f, static_cast<float>(start - turnStart) / turnLen), block);
        for (int i = 0; i < block; ++i)
        {
            c.process(0.0f);
            const float len = s.lenTarget + s.lenOffset;
            // Skip the turn's first 6144 samples, where the glide is still
            // catching up with the knob starting to move.
            if (start + i >= turnStart + 3 * 2048)
            {
                r.lo = std::min(r.lo, len - prev);
                r.hi = std::max(r.hi, len - prev);
            }
            prev = len;
        }
    }
    return r;
}
} // namespace

TEST_CASE("SpringReverb SIZE turn reads at the same speed at any block size (review S5)", "[spring]")
{
    // The turn moves spring 2's loop ~0.081 samples per sample: the read is
    // pitched ~8% while the knob moves. Stepping SIZE once per block and only
    // gliding it swung that between 0.060–0.108 at 512-sample blocks and
    // 0.021–0.210 at 2048 (a ~21 Hz pitch warble); ramped, 0.081–0.082 at both.
    const auto a = readSpeedDuringTurn(512);
    const auto b = readSpeedDuringTurn(2048);
    CAPTURE(a.lo, a.hi, b.lo, b.hi);
    for (const auto& r : { a, b })
    {
        REQUIRE(r.lo > 0.075f);
        REQUIRE(r.hi < 0.088f);
    }
}

TEST_CASE("SpringReverb reset() lands a SIZE ramp in progress (review S5)", "[spring]")
{
    SpringReverbCore want;
    want.prepare(kSR);
    want.setSize(0.9f);
    want.reset();

    SpringReverbCore c;
    c.prepare(kSR);
    c.rampSize(0.9f, 4096);
    for (int i = 0; i < 1000; ++i) c.process(0.1f);   // part-way along the ramp
    c.reset();

    for (size_t k = 0; k < c.springs.size(); ++k)
    {
        REQUIRE(c.springs[k].lenTarget == want.springs[k].lenTarget);
        REQUIRE(c.springs[k].lenOffset == 0.0f);
        REQUIRE(c.springs[k].fbTarget == want.springs[k].fbTarget);
        REQUIRE(c.springs[k].fbOffset == 0.0f);
    }
    const auto y = impulseResponse(c, 4800);
    const auto z = impulseResponse(want, 4800);
    REQUIRE(y == z);
}
