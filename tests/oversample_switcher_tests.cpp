// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/OversampleSwitcher.h"
#include "dsp/filters/IntegerDelay.h"
#include <cmath>
using namespace sr;
using Catch::Approx;

// OversampleSwitcher drives the deferred crossfade state machine
// (FadingOut -> Reconfiguring -> FadingIn). CLAUDE.md flags it as
// pop-sensitive but it had no tests. These cover the pure state-machine
// transitions and the fade envelope.

TEST_CASE("OversampleSwitcher: activeFactor maps mode to 2x/4x/8x", "[oversample]") {
    OversampleSwitcher sw;
    sw.currentMode = 0; REQUIRE(sw.activeFactor() == 2);
    sw.currentMode = 1; REQUIRE(sw.activeFactor() == 4);
    sw.currentMode = 2; REQUIRE(sw.activeFactor() == 8);
}

TEST_CASE("OversampleSwitcher: requestModeChange from idle starts fade-out", "[oversample]") {
    OversampleSwitcher sw;
    sw.currentMode = 1;
    sw.requestModeChange(2);
    REQUIRE(sw.pendingMode == 2);
    REQUIRE(sw.state == OversampleTransition::FadingOut);
    REQUIRE(sw.isTransitioning());
}

TEST_CASE("OversampleSwitcher: requestModeChange ignored for same mode", "[oversample]") {
    OversampleSwitcher sw;
    sw.currentMode = 1;
    sw.requestModeChange(1);
    REQUIRE(sw.state == OversampleTransition::Idle);
    REQUIRE_FALSE(sw.isTransitioning());
}

TEST_CASE("OversampleSwitcher: requestModeChange ignored mid-transition", "[oversample]") {
    OversampleSwitcher sw;
    sw.currentMode = 1;
    sw.requestModeChange(2);   // -> FadingOut, pending 2
    sw.requestModeChange(0);   // should be ignored while transitioning
    REQUIRE(sw.pendingMode == 2);
    REQUIRE(sw.state == OversampleTransition::FadingOut);
}

TEST_CASE("OversampleSwitcher: fade-out ramps gain 1->0 then reconfigures", "[oversample]") {
    OversampleSwitcher sw;
    sw.currentMode = 1;
    sw.requestModeChange(2);   // FadingOut, fadeCounter = 0, fadeLength = 128

    juce::AudioBuffer<float> buf(1, 300);
    for (int i = 0; i < buf.getNumSamples(); ++i) buf.setSample(0, i, 1.0f);
    sw.applyTransitionFade(buf);

    const float* d = buf.getReadPointer(0);
    REQUIRE(d[0] == Approx(1.0f));                      // counter 0 → unity
    REQUIRE(d[1] == Approx(1.0f - 1.0f / 128.0f));      // first ramp step
    for (int i = 1; i < 128; ++i)
        REQUIRE(d[i] <= d[i - 1] + 1e-7f);             // monotonically falling
    REQUIRE(d[200] == Approx(0.0f));                    // past ramp → silent
    REQUIRE(sw.state == OversampleTransition::Reconfiguring);
}

TEST_CASE("OversampleSwitcher: full transition reaches Idle at the new mode", "[oversample]") {
    OversampleSwitcher sw;
    sw.init(1, 512);                 // allocate the three oversamplers
    REQUIRE(sw.currentMode == 1);    // default 4x

    sw.requestModeChange(0);         // switch to 2x

    juce::AudioBuffer<float> buf(1, 64);
    int guard = 0;
    while (sw.isTransitioning() && guard++ < 100) {
        for (int i = 0; i < buf.getNumSamples(); ++i) buf.setSample(0, i, 0.5f);
        sw.applyTransitionFade(buf);
        if (sw.readyToSwitch()) sw.commitSwitch();
    }

    REQUIRE_FALSE(sw.isTransitioning());
    REQUIRE(sw.currentMode == 0);
    REQUIRE(sw.activeFactor() == 2);
}

// ── Review B5: the saved mode is applied at prepare ──

TEST_CASE("OversampleSwitcher: init starts in the requested mode with no transition", "[oversample][prepare]") {
    for (int mode : { 0, 1, 2 }) {
        OversampleSwitcher sw;
        sw.init(2, 512, mode);
        REQUIRE(sw.currentMode == mode);
        REQUIRE_FALSE(sw.isTransitioning());
        REQUIRE(sw.pendingMode == -1);
    }
}

TEST_CASE("OversampleSwitcher: init clamps an out-of-range mode", "[oversample][prepare]") {
    OversampleSwitcher sw;
    sw.init(1, 64, -1);
    REQUIRE(sw.currentMode == 0);
    sw.init(1, 64, 7);
    REQUIRE(sw.currentMode == 2);
}

TEST_CASE("OversampleSwitcher: re-init mid-transition clears the transition", "[oversample][prepare]") {
    OversampleSwitcher sw;
    sw.init(1, 64, 1);
    sw.requestModeChange(2);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    sw.applyTransitionFade(buf);              // part-way through the fade-out
    REQUIRE(sw.isTransitioning());

    sw.init(1, 64, 0);                        // host re-prepares
    REQUIRE(sw.currentMode == 0);
    REQUIRE(sw.state == OversampleTransition::Idle);
    REQUIRE(sw.fadeCounter == 0);
    REQUIRE(sw.pendingMode == -1);

    for (int i = 0; i < 64; ++i) buf.setSample(0, i, 1.0f);
    sw.applyTransitionFade(buf);              // Idle → untouched, no dropout
    REQUIRE(buf.getSample(0, 0) == 1.0f);
    REQUIRE(buf.getSample(0, 63) == 1.0f);
}

TEST_CASE("OversampleSwitcher: requestModeChange ignores out-of-range modes", "[oversample]") {
    OversampleSwitcher sw;
    sw.currentMode = 1;
    sw.requestModeChange(3);
    REQUIRE_FALSE(sw.isTransitioning());
    sw.requestModeChange(-1);
    REQUIRE_FALSE(sw.isTransitioning());
}

// ── Review B4: latency is reported exactly, and the IIR path matches it ──

TEST_CASE("Oversamplers report whole-sample latency in every mode", "[oversample][latency]") {
    OversampleSwitcher sw;
    for (int mode : { 0, 1, 2 }) {
        sw.init(2, 512, mode);
        const float lat = sw.active()->getLatencyInSamples();
        REQUIRE(lat == Approx(std::round(lat)).margin(1e-4));
        REQUIRE(lat > 0.0f);
    }
    auto limiter = makeIntegerLatencyOversampler(2, 2, 512);   // the IR-path limiter's config
    const float lat = limiter->getLatencyInSamples();
    REQUIRE(lat == Approx(std::round(lat)).margin(1e-4));
    REQUIRE(lat <= 16.0f);   // fits the processor's IntegerDelay<2, 16>
}

TEST_CASE("IIR-path delay lines up with the IR-path limiter oversampler", "[oversample][latency]") {
    // In IR mode the output passes through the limiter oversampler; in IIR mode
    // through an IntegerDelay of the same reported latency. A low tone must come
    // out of both at the same time, or the reported latency is wrong in one mode.
    auto limiter = makeIntegerLatencyOversampler(1, 2, 256);
    IntegerDelay<1, 16> align;
    align.setDelay(static_cast<int>(std::lround(limiter->getLatencyInSamples())));

    const float fs = 44100.0f, f = 100.0f;
    juce::AudioBuffer<float> a(1, 256), b(1, 256);
    float maxDiff = 0.0f;
    int n = 0;
    for (int block = 0; block < 40; ++block) {
        for (int i = 0; i < 256; ++i, ++n) {
            const float s = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * f * n / fs);
            a.setSample(0, i, s);
            b.setSample(0, i, s);
        }
        juce::dsp::AudioBlock<float> blk(a);
        limiter->processSamplesUp(blk);
        limiter->processSamplesDown(blk);
        align.process(b.getArrayOfWritePointers(), 1, 256);
        if (block >= 10)   // past the filters' start-up
            for (int i = 0; i < 256; ++i)
                maxDiff = std::max(maxDiff, std::abs(a.getSample(0, i) - b.getSample(0, i)));
    }
    // 0.5 amplitude at 100 Hz: one sample of misalignment is a 7e-3 error.
    REQUIRE(maxDiff < 1e-3f);
}
