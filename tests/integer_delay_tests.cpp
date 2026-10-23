// ═══════════════════════════════════════════════════════════════
// IntegerDelay tests — the whole-sample delay that aligns the IIR cab
// path with the IR path's limiter-oversampler latency (review B4).
// Pure C++ (no JUCE).
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include "dsp/filters/IntegerDelay.h"

#include <vector>

using sr::IntegerDelay;

TEST_CASE("IntegerDelay: an impulse comes out exactly N samples later, across blocks", "[delay][latency]") {
    for (int n : { 1, 4, 6, 16 }) {
        IntegerDelay<2, 16> d;
        d.setDelay(n);
        REQUIRE(d.getDelay() == n);

        // 64 samples in uneven blocks, impulse at sample 3.
        std::vector<float> x(64, 0.0f);
        x[3] = 1.0f;
        int start = 0;
        for (int block : { 5, 1, 17, 41 }) {
            float* ch[] = { x.data() + start };
            d.process(ch, 1, block);
            start += block;
        }
        for (int i = 0; i < 64; ++i)
            REQUIRE(x[static_cast<size_t>(i)] == (i == 3 + n ? 1.0f : 0.0f));
    }
}

TEST_CASE("IntegerDelay: channels are delayed independently", "[delay][latency]") {
    IntegerDelay<2, 16> d;
    d.setDelay(6);
    std::vector<float> l(32, 0.0f), r(32, 0.0f);
    l[0] = 1.0f;
    r[2] = -0.5f;
    float* ch[] = { l.data(), r.data() };
    d.process(ch, 2, 32);
    for (int i = 0; i < 32; ++i) {
        REQUIRE(l[static_cast<size_t>(i)] == (i == 6 ? 1.0f : 0.0f));
        REQUIRE(r[static_cast<size_t>(i)] == (i == 8 ? -0.5f : 0.0f));
    }
}

TEST_CASE("IntegerDelay: zero delay is bit-exact passthrough", "[delay][latency]") {
    IntegerDelay<2, 16> d;
    d.setDelay(0);
    std::vector<float> x { 0.1f, -0.2f, 0.3f };
    float* ch[] = { x.data() };
    d.process(ch, 1, 3);
    REQUIRE(x == std::vector<float>{ 0.1f, -0.2f, 0.3f });
}

TEST_CASE("IntegerDelay: setDelay clamps to capacity, reset clears the line", "[delay][latency]") {
    IntegerDelay<1, 8> d;
    d.setDelay(100);
    REQUIRE(d.getDelay() == 8);
    d.setDelay(-3);
    REQUIRE(d.getDelay() == 0);

    d.setDelay(4);
    std::vector<float> x(4, 1.0f);
    float* ch[] = { x.data() };
    d.process(ch, 1, 4);           // line now holds four 1.0s
    d.reset();
    std::vector<float> y(4, 0.0f);
    float* ch2[] = { y.data() };
    d.process(ch2, 1, 4);
    REQUIRE(y == std::vector<float>(4, 0.0f));   // nothing stale replayed
}
