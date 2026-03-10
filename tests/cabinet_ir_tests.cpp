// Cabinet IR Loader Tests — exercises convolution pipeline, mode switching, IR registry
// Requires JUCE (separate test target: TinyTerrorJuceTests)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

#include "dsp/cabinet/IRLibrary.h"
#include "dsp/cabinet/CabinetSim.h"

using namespace tt;

// ═══════════════════════════════════════════════════════════════
// Stock IR Registry
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Stock IR registry has 8 entries", "[cabinet][ir]")
{
    REQUIRE(NUM_STOCK_IRS == 8);

    for (int i = 0; i < NUM_STOCK_IRS; ++i)
    {
        const auto& desc = getStockIRDescriptor(i);
        REQUIRE(desc.id != nullptr);
        REQUIRE(desc.displayName != nullptr);
        REQUIRE(desc.mic != nullptr);
        REQUIRE(desc.samples > 0);
        REQUIRE(desc.sampleRate > 0);
    }
}

// ═══════════════════════════════════════════════════════════════
// Synthetic IR Generator
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Synthetic IR has correct length and non-zero content", "[cabinet][ir]")
{
    for (int i = 0; i < NUM_STOCK_IRS; ++i)
    {
        const auto& desc = getStockIRDescriptor(i);
        auto buffer = generateSyntheticIR(i, desc);

        REQUIRE(buffer.getNumChannels() == 1);
        REQUIRE(buffer.getNumSamples() == desc.samples);

        // Check that IR is not silent
        float peak = 0.0f;
        const auto* data = buffer.getReadPointer(0);
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            peak = std::max(peak, std::abs(data[s]));

        REQUIRE(peak > 0.0f);
    }
}

TEST_CASE("Each stock IR has unique synthesis parameters", "[cabinet][ir]")
{
    // Verify that all 8 synthetic IRs produce different waveforms
    std::vector<float> firstSamples;
    for (int i = 0; i < NUM_STOCK_IRS; ++i)
    {
        const auto& desc = getStockIRDescriptor(i);
        auto buffer = generateSyntheticIR(i, desc);
        // Sample at index 5 (past the impulse) captures resonance character
        firstSamples.push_back(buffer.getReadPointer(0)[5]);
    }
    // All 8 values should be unique (no duplicate synthesis params)
    for (int i = 0; i < NUM_STOCK_IRS; ++i)
        for (int j = i + 1; j < NUM_STOCK_IRS; ++j)
            REQUIRE(firstSamples[i] != firstSamples[j]);
}

// ═══════════════════════════════════════════════════════════════
// Mode Switching
// ═══════════════════════════════════════════════════════════════

TEST_CASE("IR index 0 = IIR mode", "[cabinet][ir]")
{
    CabinetSim cab;
    cab.prepare(48000.0, 512, 1);

    cab.setIRIndex(0);
    REQUIRE(cab.useIR.load() == false);
    REQUIRE(cab.currentIRIndex == 0);
}

TEST_CASE("IR index > 0 = IR mode", "[cabinet][ir]")
{
    CabinetSim cab;
    cab.prepare(48000.0, 512, 1);

    cab.setIRIndex(1);
    REQUIRE(cab.useIR.load() == true);
    REQUIRE(cab.currentIRIndex == 1);

    cab.setIRIndex(5);
    REQUIRE(cab.useIR.load() == true);
    REQUIRE(cab.currentIRIndex == 5);
}

// ═══════════════════════════════════════════════════════════════
// Pipeline Integrity
// ═══════════════════════════════════════════════════════════════

TEST_CASE("processBlock with IR doesn't crash", "[cabinet][ir]")
{
    CabinetSim cab;
    cab.prepare(48000.0, 512, 1);
    cab.setIRIndex(1); // Load stock IR 1

    // Create a test buffer with some signal
    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    auto* data = buffer.getWritePointer(0);
    for (int i = 0; i < 512; ++i)
        data[i] = std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / 48000.0f) * 0.5f;

    juce::dsp::AudioBlock<float> block(buffer);

    // Should not crash or throw
    REQUIRE_NOTHROW(cab.processBlock(block));

    // Buffer should still have valid (non-NaN) samples
    const auto* out = buffer.getReadPointer(0);
    for (int i = 0; i < 512; ++i)
        REQUIRE(std::isfinite(out[i]));
}

TEST_CASE("processBlock with stereo IR doesn't crash", "[cabinet][ir]")
{
    CabinetSim cab;
    cab.prepare(48000.0, 512, 2);
    cab.setIRIndex(3);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < 512; ++i)
            data[i] = std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / 48000.0f) * 0.5f;
    }

    juce::dsp::AudioBlock<float> block(buffer);
    REQUIRE_NOTHROW(cab.processBlock(block));
}

// ═══════════════════════════════════════════════════════════════
// State Cleanup
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Reset doesn't crash", "[cabinet][ir]")
{
    CabinetSim cab;
    cab.prepare(48000.0, 512, 1);
    cab.setIRIndex(2);

    REQUIRE_NOTHROW(cab.reset());

    // Should still be able to process after reset
    juce::AudioBuffer<float> buffer(1, 256);
    buffer.clear();
    juce::dsp::AudioBlock<float> block(buffer);
    REQUIRE_NOTHROW(cab.processBlock(block));
}

// ═══════════════════════════════════════════════════════════════
// Mode Transitions
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Switch IR -> IIR -> IR", "[cabinet][ir]")
{
    CabinetSim cab;
    cab.prepare(48000.0, 512, 1);

    // Start in IR mode
    cab.setIRIndex(4);
    REQUIRE(cab.useIR.load() == true);

    // Process a block in IR mode
    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    juce::dsp::AudioBlock<float> block(buffer);
    REQUIRE_NOTHROW(cab.processBlock(block));

    // Switch to IIR mode
    cab.setIRIndex(0);
    REQUIRE(cab.useIR.load() == false);

    // Switch back to IR mode (different IR)
    cab.setIRIndex(7);
    REQUIRE(cab.useIR.load() == true);
    REQUIRE(cab.currentIRIndex == 7);

    // Process another block — should not crash
    buffer.clear();
    REQUIRE_NOTHROW(cab.processBlock(block));
}

TEST_CASE("IR tail length is positive when IR active", "[cabinet][ir]")
{
    CabinetSim cab;
    cab.prepare(48000.0, 512, 1);

    REQUIRE(cab.getIRTailSeconds() == 0.0);

    cab.setIRIndex(1);
    REQUIRE(cab.getIRTailSeconds() > 0.0);

    cab.setIRIndex(0);
    REQUIRE(cab.getIRTailSeconds() == 0.0);
}
