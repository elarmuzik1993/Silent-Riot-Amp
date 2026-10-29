// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// IR LOADER panel: the CAB view's arrows and the view switch. The arrows
// step through the built-in cab (IR_INDEX 0) and every loaded IR, wrapping
// both ways, and report each step through onIRSelected — the editor turns
// 0 into selectBuiltInCab() and n into selectUserIR(n - 1).
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include "gui/IRLoaderPanel.h"

#include <juce_events/juce_events.h>

#include <vector>

namespace {

struct Panel
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // first: outlives the panel
    sr::IRLoaderPanel panel;
    std::vector<int> reported;

    explicit Panel(int numIRs)
    {
        panel.setSize(700, sr::IRLoaderPanel::panelHeight);
        juce::AudioBuffer<float> ir(1, 64);
        ir.clear();
        ir.setSample(0, 0, 1.0f);
        for (int i = 1; i <= numIRs; ++i)
            panel.addUserIR("ir" + juce::String(i), 64, 48000.0, i, ir);
        panel.onIRSelected = [this](int idx) { reported.push_back(idx); };
    }
};

} // namespace

TEST_CASE("IR loader: the CAB arrows cycle the built-in cab and every IR, wrapping",
          "[gui][ir]")
{
    Panel p(2);
    p.panel.setSelectedIR(0);

    for (int i = 0; i < 4; ++i) p.panel.stepIR(+1);
    REQUIRE(p.reported == std::vector<int> { 1, 2, 0, 1 });
    REQUIRE(p.panel.getSelectedIR() == 1);

    p.reported.clear();
    for (int i = 0; i < 3; ++i) p.panel.stepIR(-1);
    REQUIRE(p.reported == std::vector<int> { 0, 2, 1 });
}

TEST_CASE("IR loader: an index past the IR list steps from the built-in cab", "[gui][ir]")
{
    Panel p(2);
    p.panel.setSelectedIR(7);   // e.g. automation past the list
    p.panel.stepIR(+1);
    REQUIRE(p.reported == std::vector<int> { 1 });
}

TEST_CASE("IR loader: with no IRs loaded the arrows do nothing", "[gui][ir]")
{
    Panel p(0);
    p.panel.stepIR(+1);
    p.panel.stepIR(-1);
    REQUIRE(p.reported.empty());
    REQUIRE(p.panel.getSelectedIR() == 0);
}

TEST_CASE("IR loader: opens in the CAB view; setView switches without reporting", "[gui][ir]")
{
    Panel p(1);
    int changes = 0;
    p.panel.onViewChanged = [&](sr::IRLoaderPanel::View) { ++changes; };
    REQUIRE(p.panel.getView() == sr::IRLoaderPanel::View::Cab);
    p.panel.setView(sr::IRLoaderPanel::View::Library);
    REQUIRE(p.panel.getView() == sr::IRLoaderPanel::View::Library);
    // setView is how the editor restores the saved view; only a click on the
    // switch reports a change back to it.
    REQUIRE(changes == 0);
}
