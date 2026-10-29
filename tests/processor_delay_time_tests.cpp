// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// DelayTimeAttachment — the DELAY card's one TIME knob drives two parameters:
// DELAY_TIME while SYNC is off, the DELAY_SYNC_DIV division while it is on.
// Wired to the real processor's APVTS, the way the editor wires it.
// ═══════════════════════════════════════════════════════════════

#include "processor_harness.h"
#include "PluginEditor.h"
#include "utils/Parameters.h"

#include <catch2/catch_approx.hpp>

using namespace sr;
using namespace sr::test;
namespace IDs = sr::Params::IDs;

namespace {
struct DelayTimeRig
{
    ProcessorHarness h;
    NotchKnob knob { "TIME", 0.337f };
    ToggleSwitch sync { "SYNC" };
    std::unique_ptr<DelayTimeAttachment> attachment =
        std::make_unique<DelayTimeAttachment>(h.proc.apvts, knob, sync);

    float param(const juce::String& id) { return h.proc.apvts.getRawParameterValue(id)->load(); }
    juce::Slider& slider() { return knob.getSlider(); }
    void clickSync(bool on) { sync.getButton().setToggleState(on, juce::sendNotificationSync); }
    void turnKnob(double v) { slider().setValue(v, juce::sendNotificationSync); }
    void automate(const juce::String& id, float natural)
    {
        auto* p = h.proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(natural));
    }
};
} // namespace

TEST_CASE("Delay TIME knob: SYNC off drives the time in ms", "[processor][delayTime]")
{
    DelayTimeRig r;
    REQUIRE_FALSE(r.sync.getButton().getToggleState());
    REQUIRE(r.slider().getValue() == Catch::Approx(r.param(IDs::DELAY_TIME)).margin(0.001));

    r.turnKnob(0.8);
    REQUIRE(r.param(IDs::DELAY_TIME) == Catch::Approx(0.8f).margin(0.001));
    REQUIRE(r.param(IDs::DELAY_SYNC_DIV) == 0.0f);
}

TEST_CASE("Delay TIME knob: SYNC on steps the divisions and leaves the ms time alone",
          "[processor][delayTime]")
{
    DelayTimeRig r;
    r.turnKnob(0.8);
    r.clickSync(true);

    // First switch-on lands on the quarter note, and the knob shows it
    REQUIRE(static_cast<int>(r.param(IDs::DELAY_SYNC_DIV)) == kDefaultSyncDivision);
    REQUIRE(r.slider().getValue() == Catch::Approx(knobFromSyncDivision(kDefaultSyncDivision)));

    // Turning the knob between detents snaps to one division
    r.turnKnob(knobFromSyncDivision(2) + 0.02);
    REQUIRE(static_cast<int>(r.param(IDs::DELAY_SYNC_DIV)) == 2);
    REQUIRE(r.param(IDs::DELAY_TIME) == Catch::Approx(0.8f).margin(0.001));
}

TEST_CASE("Delay TIME knob: SYNC off restores the ms time, on restores the last division",
          "[processor][delayTime]")
{
    DelayTimeRig r;
    r.turnKnob(0.6);
    r.clickSync(true);
    r.turnKnob(knobFromSyncDivision(7));
    REQUIRE(static_cast<int>(r.param(IDs::DELAY_SYNC_DIV)) == 7);

    r.clickSync(false);
    REQUIRE(r.param(IDs::DELAY_SYNC_DIV) == 0.0f);
    REQUIRE(r.slider().getValue() == Catch::Approx(0.6).margin(0.001));

    r.clickSync(true);
    REQUIRE(static_cast<int>(r.param(IDs::DELAY_SYNC_DIV)) == 7);

    // The remembered division outlives the editor (it rides in the saved state)
    r.clickSync(false);
    r.attachment.reset();   // first: its destructor unhooks the knob and toggle
    r.attachment = std::make_unique<DelayTimeAttachment>(r.h.proc.apvts, r.knob, r.sync);
    r.clickSync(true);
    REQUIRE(static_cast<int>(r.param(IDs::DELAY_SYNC_DIV)) == 7);
}

TEST_CASE("Delay TIME knob: host automation of the division moves the toggle and knob",
          "[processor][delayTime]")
{
    DelayTimeRig r;
    r.automate(IDs::DELAY_SYNC_DIV, 3.0f);
    REQUIRE(r.sync.getButton().getToggleState());
    REQUIRE(r.slider().getValue() == Catch::Approx(knobFromSyncDivision(3)));

    r.automate(IDs::DELAY_SYNC_DIV, 0.0f);
    REQUIRE_FALSE(r.sync.getButton().getToggleState());
    REQUIRE(r.slider().getValue() == Catch::Approx(r.param(IDs::DELAY_TIME)).margin(0.001));
}
