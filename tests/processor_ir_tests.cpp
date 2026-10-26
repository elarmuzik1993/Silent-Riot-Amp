// ═══════════════════════════════════════════════════════════════
// Processor wiring of IR_INDEX on state restore (review S2) — the real
// SilentRiotProcessor, driven like a host that loads a project or preset
// into a live instance.
//
// applyUserIRIndex() is unit-tested in user_ir_state_tests.cpp; what only the
// processor can show is that the restore path actually calls it. processBlock
// reacts to IR_INDEX only when it differs from the last index it saw, so a
// restore that leaves the index unchanged but shrinks the IR list is caught
// by nothing else — the previous IR would keep playing in IIR mode's place.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include "processor_harness.h"

#include <cstdint>
#include <vector>

using namespace sr::test;
namespace IDs = sr::Params::IDs;

namespace {

// A mono IR with a ~1 s tail (8192 samples at 8 kHz), so its ring outlasts the
// amp's own ~0.5 s settling and is unmistakable on silence.
std::vector<float> longDecayIR(uint32_t seed)
{
    constexpr int n = 8192;
    constexpr double irRate = 8000.0;
    std::vector<float> ir(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const float noise = static_cast<float>(seed >> 8) / static_cast<float>(1u << 24) - 0.5f;
        ir[static_cast<size_t>(i)] =
            noise * static_cast<float>(std::exp(-static_cast<double>(i) / (0.25 * irRate)));
    }
    ir[0] = 1.0f;
    return ir;
}

int irIndex(ProcessorHarness& h)
{
    return static_cast<int>(h.proc.apvts.getRawParameterValue(IDs::IR_INDEX)->load());
}

// Peak on silence once a note has ended and the amp alone has settled: an IR
// tail rings well above -100 dBFS here, the IIR path is below -120 dBFS.
float tailAfterNote(ProcessorHarness& h)
{
    h.run(1.0, sine(220.0f, 0.25f));
    h.silence(0.6);
    return h.silence(0.1);
}

} // namespace

TEST_CASE("Processor: restoring a state whose IR_INDEX is past its IR list disables the IR (review S2)",
          "[processor][ir][state]")
{
    juce::TemporaryFile irFile1(".wav"), irFile2(".wav");
    ProcessorHarness h;
    h.loadIR(writeWav(irFile1, longDecayIR(12345u), 8000.0));
    h.loadIR(writeWav(irFile2, longDecayIR(54321u), 8000.0));   // IR_INDEX = 2, playing
    REQUIRE(irIndex(h) == 2);
    REQUIRE(h.proc.getTailLengthSeconds() > 0.5);
    REQUIRE(tailAfterNote(h) > dB(-100.0));

    // The review's repro: a saved state that still says IR_INDEX = 2 but holds
    // only one IR (here: this session's state with its second IR removed),
    // restored into the live instance. The index the processor last saw is
    // unchanged, so only the restore path itself can drop IR 2.
    juce::MemoryBlock saved;
    h.proc.getStateInformation(saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(),
                                                      static_cast<int>(saved.getSize()));
    REQUIRE(xml != nullptr);
    auto state = juce::ValueTree::fromXml(*xml);
    auto irs = state.getChildWithName("UserIRs");
    REQUIRE(irs.getNumChildren() == 2);
    irs.removeChild(1, nullptr);

    juce::MemoryBlock shrunk;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), shrunk);
    h.proc.setStateInformation(shrunk.getData(), static_cast<int>(shrunk.getSize()));

    REQUIRE(irIndex(h) == 2);
    REQUIRE(h.proc.getNumUserIRs() == 1);
    INFO("IR 2 kept playing after a restore that no longer holds it");
    REQUIRE(h.proc.getTailLengthSeconds() <= 0.0);   // no IR tail reported
    REQUIRE(tailAfterNote(h) < dB(-120.0));          // and none heard: IIR path
}
