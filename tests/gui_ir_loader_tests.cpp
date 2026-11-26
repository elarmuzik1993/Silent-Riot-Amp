// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// IR LOADER panel: the CAB view's arrows and the view switch. The arrows
// step through the built-in cab (IR_INDEX 0) and every loaded IR, wrapping
// both ways, and report each step through onIRSelected — the editor turns
// 0 into selectBuiltInCab() and n into selectUserIR(n - 1).
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include "processor_harness.h"
#include "PluginEditor.h"
#include "gui/IRLoaderPanel.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <memory>
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

// ── The editor keeps the panel in step with the processor (#43) ──
// Host automation of IR_INDEX and a state restore change the processor
// behind the panel's back; the editor's timer must bring the list, the
// highlight and the IR bar's hint along.

namespace {

std::vector<float> clickIR()
{
    std::vector<float> ir(256, 0.0f);
    for (size_t i = 0; i < ir.size(); ++i)
        ir[i] = std::exp(-static_cast<float>(i) / 16.0f);
    return ir;
}

sr::IRLoaderPanel* findIRPanel(juce::Component& c)
{
    if (auto* p = dynamic_cast<sr::IRLoaderPanel*>(&c))
        return p;
    for (auto* child : c.getChildren())
        if (auto* p = findIRPanel(*child))
            return p;
    return nullptr;
}

struct EditorRig
{
    sr::test::ProcessorHarness h;   // first: the processor outlives the editor
    std::unique_ptr<SilentRiotEditor> editor;
    sr::IRLoaderPanel* panel = nullptr;

    void open()
    {
        editor.reset(dynamic_cast<SilentRiotEditor*>(h.proc.createEditor()));
        REQUIRE(editor != nullptr);
        panel = findIRPanel(*editor);
        REQUIRE(panel != nullptr);
    }

    void frame() { editor->runTimerFrameForTest(1.0 / 30.0); }

    // The panel lists exactly the processor's IRs, in order.
    void requireListMatches() const
    {
        REQUIRE(panel->getNumListedIRs() == h.proc.getNumUserIRs());
        for (int i = 0; i < h.proc.getNumUserIRs(); ++i)
            CHECK(panel->getListedIRName(i) == h.proc.getUserIR(i).name);
    }

    ~EditorRig() { editor.reset(); }
};

} // namespace

TEST_CASE("IR loader editor: host automation of IR_INDEX moves the highlight (#43)",
          "[gui][ir][processor]")
{
    juce::TemporaryFile f1(".wav"), f2(".wav");
    EditorRig r;
    REQUIRE(r.h.proc.loadUserIR(sr::test::writeWav(f1, clickIR(), 48000.0)));
    REQUIRE(r.h.proc.loadUserIR(sr::test::writeWav(f2, clickIR(), 48000.0)));   // IR_INDEX = 2
    r.open();
    r.requireListMatches();
    REQUIRE(r.panel->getSelectedIR() == 2);

    r.h.set(sr::Params::IDs::IR_INDEX, 1.0f);   // as host automation does
    r.frame();
    CHECK(r.panel->getSelectedIR() == 1);

    r.h.set(sr::Params::IDs::IR_INDEX, 0.0f);   // the built-in cab
    r.frame();
    CHECK(r.panel->getSelectedIR() == 0);
    r.requireListMatches();
}

TEST_CASE("IR loader editor: a state restore rebuilds the list and selection (#43)",
          "[gui][ir][processor]")
{
    // A saved session with two IRs, the first one selected.
    juce::TemporaryFile f1(".wav"), f2(".wav");
    juce::MemoryBlock saved;
    {
        sr::test::ProcessorHarness src;
        REQUIRE(src.proc.loadUserIR(sr::test::writeWav(f1, clickIR(), 48000.0)));
        REQUIRE(src.proc.loadUserIR(sr::test::writeWav(f2, clickIR(), 48000.0)));
        src.set(sr::Params::IDs::IR_INDEX, 1.0f);
        src.proc.getStateInformation(saved);
    }
    juce::MemoryBlock empty;
    {
        sr::test::ProcessorHarness blank;
        blank.proc.getStateInformation(empty);
    }

    // Loaded into an instance whose editor is already open with no IRs.
    EditorRig r;
    r.open();
    REQUIRE(r.panel->getNumListedIRs() == 0);

    r.h.proc.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    r.frame();
    REQUIRE(r.h.proc.getNumUserIRs() == 2);
    r.requireListMatches();
    CHECK(r.panel->getSelectedIR() == 1);

    // And back to a session without IRs: the list empties.
    r.h.proc.setStateInformation(empty.getData(), static_cast<int>(empty.getSize()));
    r.frame();
    r.requireListMatches();
    CHECK(r.panel->getSelectedIR() == 0);
}

TEST_CASE("IR loader editor: loading a file appends one entry, which the timer keeps",
          "[gui][ir][processor]")
{
    juce::TemporaryFile f1(".wav"), f2(".wav");
    EditorRig r;
    r.open();
    REQUIRE(r.panel->onUserIRLoaded != nullptr);
    r.panel->onUserIRLoaded(sr::test::writeWav(f1, clickIR(), 48000.0));
    r.panel->onUserIRLoaded(sr::test::writeWav(f2, clickIR(), 48000.0));
    r.requireListMatches();
    CHECK(r.panel->getSelectedIR() == 2);

    r.frame();   // no rebuild, no duplicate
    r.requireListMatches();
    CHECK(r.panel->getSelectedIR() == 2);
}
