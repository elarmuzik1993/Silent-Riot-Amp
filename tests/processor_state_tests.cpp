// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Session state and the threads that touch it. A host may save or restore
// from its own thread while the editor and the message thread use the
// processor, so the live apvts.state is only read through the APVTS's locked
// copyState() and is never written by the editor: its settings go through
// SilentRiotProcessor::setUIProperty (uiStateLock) and travel as the saved
// tree's root properties, the format older sessions already use.
//
// rt_stress's hostsave scenario runs saves on a host thread against the
// message and audio threads under ThreadSanitizer in CI; verify.sh's check 8
// keeps editor code off the live tree.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include "processor_harness.h"

using namespace sr::test;

namespace {

juce::MemoryBlock saveOf(const juce::ValueTree& tree)
{
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), block);
    return block;
}

juce::ValueTree treeOf(const juce::MemoryBlock& block)
{
    auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(),
                                                      static_cast<int>(block.getSize()));
    REQUIRE(xml != nullptr);
    return juce::ValueTree::fromXml(*xml);
}

} // namespace

TEST_CASE("State: editor settings never touch the live state tree", "[processor][state]")
{
    ProcessorHarness h;
    h.proc.setUIProperty("theme", 2);
    h.proc.setUIProperty("irPanelOpen", true);
    CHECK_FALSE(h.proc.apvts.state.hasProperty("theme"));
    CHECK_FALSE(h.proc.apvts.state.hasProperty("irPanelOpen"));

    CHECK(h.proc.hasUIProperty("theme"));
    CHECK(static_cast<int>(h.proc.getUIProperty("theme")) == 2);
    CHECK_FALSE(h.proc.hasUIProperty("uiScale"));
    CHECK(static_cast<int>(h.proc.getUIProperty("uiScale", 1)) == 1);   // the fallback
}

TEST_CASE("State: editor settings round-trip as root properties of the saved tree",
          "[processor][state]")
{
    juce::MemoryBlock saved;
    {
        ProcessorHarness h;
        h.proc.setUIProperty("theme", 2);
        h.proc.setUIProperty("irPanelOpen", true);
        h.proc.getStateInformation(saved);
    }
    CHECK(static_cast<int>(treeOf(saved).getProperty("theme")) == 2);

    ProcessorHarness restored;
    restored.proc.setUIProperty("knobMode", 1);   // this session's; the restore replaces it
    restored.proc.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(static_cast<int>(restored.proc.getUIProperty("theme")) == 2);
    CHECK(static_cast<bool>(restored.proc.getUIProperty("irPanelOpen")));
    CHECK_FALSE(restored.proc.hasUIProperty("knobMode"));
}

TEST_CASE("State: a session from before the settings store restores its settings",
          "[processor][state]")
{
    // Older versions wrote the settings straight onto apvts.state, so their
    // sessions hold them as root properties next to the parameter nodes.
    ProcessorHarness h;
    auto tree = h.proc.apvts.copyState();
    tree.setProperty("uiScale", 3, nullptr);
    tree.setProperty("delayLastSyncDiv", 5, nullptr);
    const juce::MemoryBlock old = saveOf(tree);

    ProcessorHarness restored;
    restored.proc.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    CHECK(static_cast<int>(restored.proc.getUIProperty("uiScale")) == 3);
    CHECK(static_cast<int>(restored.proc.getUIProperty("delayLastSyncDiv")) == 5);

    // Saved again, they are still root properties, once each.
    juce::MemoryBlock again;
    restored.proc.getStateInformation(again);
    const auto out = treeOf(again);
    CHECK(static_cast<int>(out.getProperty("uiScale")) == 3);
    CHECK(static_cast<int>(out.getProperty("delayLastSyncDiv")) == 5);
}

TEST_CASE("State: the IR list is read through copies that fail safely", "[processor][state][ir]")
{
    // copyUserIR returns false past the list instead of indexing it: a count
    // read a moment earlier can be stale once a host restores on its own thread.
    ProcessorHarness h;
    SilentRiotProcessor::UserIREntry entry;
    CHECK(h.proc.getNumUserIRs() == 0);
    CHECK_FALSE(h.proc.copyUserIR(0, entry));
    CHECK_FALSE(h.proc.copyUserIR(-1, entry));
}
