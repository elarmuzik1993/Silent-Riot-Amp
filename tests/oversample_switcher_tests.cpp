#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/OversampleSwitcher.h"
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
