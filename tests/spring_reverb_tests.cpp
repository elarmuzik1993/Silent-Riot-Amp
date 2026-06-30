// ═══════════════════════════════════════════════════════════════
// SpringReverbCore Tests — spring-tank reverb (pure C++)
//
//   - Impulse → a decaying tail lasting well beyond the loop length
//   - Dispersion spreads energy across time (echo train, not a single delta)
//   - TONE controls tail brightness (loop damping)
//   - SIZE controls decay length
//   - Stable under sustained full-scale noise at max settings
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include "dsp/effects/SpringReverb.h"
#include <vector>
#include <cmath>

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
