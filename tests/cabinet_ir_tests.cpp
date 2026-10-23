// Cabinet IR Loader Tests — exercises convolution pipeline, mode switching, IR registry
// Requires JUCE (separate test target: SilentRiotJuceTests)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

#include "dsp/cabinet/IRLibrary.h"
#include "dsp/cabinet/CabinetSim.h"

using namespace sr;

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

// ═══════════════════════════════════════════════════════════════
// User IR File Loading
// ═══════════════════════════════════════════════════════════════

TEST_CASE("loadIRFromFile loads a WAV and enables IR mode", "[cabinet][ir]")
{
    // Create a temporary WAV file with a short impulse
    auto tempFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("test_ir.wav");

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(new juce::FileOutputStream(tempFile),
                                48000.0, 1, 16, {}, 0));
        REQUIRE(writer != nullptr);

        juce::AudioBuffer<float> impulse(1, 512);
        impulse.clear();
        impulse.setSample(0, 0, 1.0f); // unit impulse
        writer->writeFromAudioSampleBuffer(impulse, 0, 512);
    }

    CabinetSim cab;
    cab.prepare(48000.0, 512, 1);

    REQUIRE(cab.useIR.load() == false);

    cab.loadIRFromFile(tempFile);
    REQUIRE(cab.useIR.load() == true);
    REQUIRE(cab.getIRTailSeconds() > 0.0);

    // Process a block — should not crash
    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    auto* data = buffer.getWritePointer(0);
    for (int i = 0; i < 512; ++i)
        data[i] = std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / 48000.0f) * 0.5f;

    juce::dsp::AudioBlock<float> block(buffer);
    REQUIRE_NOTHROW(cab.processBlock(block));

    // Output should have valid samples
    const auto* out = buffer.getReadPointer(0);
    for (int i = 0; i < 512; ++i)
        REQUIRE(std::isfinite(out[i]));

    tempFile.deleteFile();
}

TEST_CASE("loadIRFromFile with nonexistent file keeps IR disabled", "[cabinet][ir]")
{
    CabinetSim cab;
    cab.prepare(48000.0, 512, 1);

    juce::File bogus("/tmp/nonexistent_ir_file_12345.wav");
    cab.loadIRFromFile(bogus);
    REQUIRE(cab.useIR.load() == false);
}

TEST_CASE("loadIRFromFile rejects a float WAV JUCE can't normalise", "[cabinet][ir]")
{
    // Review S4: a non-finite sample inside the IR body, or one so large its
    // square overflows JUCE's float sum of squares, silences IR mode, so the
    // load must be refused.
    for (float bad : { std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::infinity(),
                       1.0e20f })
    {
        auto tempFile = juce::File::getSpecialLocation(
            juce::File::tempDirectory).getChildFile("test_ir_nonfinite.wav");
        tempFile.deleteFile();
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(new juce::FileOutputStream(tempFile),
                                    48000.0, 1, 32, {}, 0));  // 32-bit float
            REQUIRE(writer != nullptr);

            juce::AudioBuffer<float> ir(1, 256);
            ir.clear();
            ir.setSample(0, 0, 1.0f);
            ir.setSample(0, 100, bad);  // inside the body, not trimmed away
            writer->writeFromAudioSampleBuffer(ir, 0, 256);
        }

        CabinetSim cab;
        cab.prepare(48000.0, 512, 1);
        cab.loadIRFromFile(tempFile);
        REQUIRE(cab.useIR.load() == false);
        REQUIRE(cab.getIRTailSeconds() == 0.0);

        tempFile.deleteFile();
    }
}

TEST_CASE("loadIRFromFile rejects a WAV JUCE's -80 dBFS trim cuts to nothing", "[cabinet][ir]")
{
    // JUCE's trim threshold is absolute, so a quiet-but-real IR (peak just
    // above -80 dBFS) still loads; all-zero, or wholly below it, does not.
    struct Case { float peak; bool loads; };
    for (const auto c : { Case { 0.0f, false },
                          Case { 0.5f * IR_TRIM_THRESHOLD, false },
                          Case { 2.0f * IR_TRIM_THRESHOLD, true } })
    {
        auto tempFile = juce::File::getSpecialLocation(
            juce::File::tempDirectory).getChildFile("test_ir_silent.wav");
        tempFile.deleteFile();
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(new juce::FileOutputStream(tempFile),
                                    48000.0, 1, 32, {}, 0));  // 32-bit float: exact
            REQUIRE(writer != nullptr);

            juce::AudioBuffer<float> ir(1, 256);
            for (int i = 0; i < 256; ++i)
                ir.setSample(0, i, c.peak * std::exp(-0.02f * static_cast<float>(i)));
            writer->writeFromAudioSampleBuffer(ir, 0, 256);
        }

        CabinetSim cab;
        cab.prepare(48000.0, 512, 1);
        cab.loadIRFromFile(tempFile);
        REQUIRE(cab.useIR.load() == c.loads);

        tempFile.deleteFile();
    }
}

TEST_CASE("isNormalisableIR flags NaN, Inf and overflowing energy in any channel", "[cabinet][ir]")
{
    juce::AudioBuffer<float> b(2, 64);
    b.clear();
    b.setSample(0, 0, 1.0f);
    REQUIRE(isNormalisableIR(b));

    b.setSample(1, 63, std::numeric_limits<float>::quiet_NaN());
    REQUIRE_FALSE(isNormalisableIR(b));

    b.setSample(1, 63, 0.0f);
    b.setSample(0, 10, -std::numeric_limits<float>::infinity());
    REQUIRE_FALSE(isNormalisableIR(b));

    // Finite, but its square alone overflows a float.
    b.setSample(0, 10, -1.0e20f);
    REQUIRE_FALSE(isNormalisableIR(b));

    // No single square overflows (1e38 < FLT_MAX), but the channel's sum does.
    b.clear();
    for (int i = 0; i < 64; ++i)
        b.setSample(1, i, 1.0e19f);
    REQUIRE_FALSE(isNormalisableIR(b));

    // A loud but sane IR (+60 dB over full scale, both channels) passes.
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            b.setSample(ch, i, 1000.0f);
    REQUIRE(isNormalisableIR(b));
}

TEST_CASE("isNormalisableIR rejects an IR JUCE's -80 dBFS trim cuts to nothing", "[cabinet][ir]")
{
    // juce::dsp::Convolution (Trim::yes) keeps only the span between the first
    // and last sample with |x| >= -80 dBFS over all channels — an ABSOLUTE
    // threshold — and an IR with none plays silence.
    REQUIRE(IR_TRIM_THRESHOLD == juce::Decibels::decibelsToGain(-80.0f));
    REQUIRE_THAT(IR_TRIM_THRESHOLD, Catch::Matchers::WithinRel(1.0e-4f, 1.0e-5f));

    juce::AudioBuffer<float> b(2, 64);
    b.clear();
    REQUIRE_FALSE(isNormalisableIR(b));          // all zero

    // Every sample just below the threshold, both channels: trimmed to nothing.
    const float below = std::nextafter(IR_TRIM_THRESHOLD, 0.0f);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            b.setSample(ch, i, (i % 2) ? below : -below);
    REQUIRE_FALSE(isNormalisableIR(b));

    // One sample AT the threshold (JUCE's test is >=), in either channel, keeps it.
    b.setSample(1, 40, -IR_TRIM_THRESHOLD);
    REQUIRE(isNormalisableIR(b));

    // An empty buffer has nothing to survive the trim either.
    juce::AudioBuffer<float> empty(1, 0);
    REQUIRE_FALSE(isNormalisableIR(empty));
}

namespace {

// Feeds a 1 kHz sine (amplitude 0.5) through `cab` until its convolution has
// swapped in the pending IR (juce::dsp::Convolution builds the engine on a
// background thread, then crossfades to it over 50 ms), and returns the
// settled output peak. A NaN anywhere in the settled output returns NaN.
float settledPeak(CabinetSim& cab)
{
    juce::AudioBuffer<float> buf(1, 512);
    int n = 0;
    float peak = 0.0f;
    for (int block = 0; block < 100; ++block)
    {
        for (int i = 0; i < 512; ++i, ++n)
            buf.setSample(0, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi
                                                * 1000.0f * static_cast<float>(n) / 48000.0f));
        juce::dsp::AudioBlock<float> audio(buf);
        cab.processBlock(audio);

        if (block < 60)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        else if (block >= 80)  // well past the engine swap and its crossfade
            for (int i = 0; i < 512; ++i)
            {
                const float y = buf.getSample(0, i);
                if (std::isnan(y))
                    return y;
                peak = std::max(peak, std::abs(y));
            }
    }
    return peak;
}

// Only NaN-free output above -60 dBFS counts — a NaN output is just as dead.
bool settledOutputIsAudible(CabinetSim& cab)
{
    const float peak = settledPeak(cab);
    return std::isfinite(peak) && peak > 1.0e-3f;
}

} // namespace

TEST_CASE("IRs JUCE can't trim or normalise silence IR mode (why they are rejected)",
          "[cabinet][ir]")
{
    // loadIRFromBuffer() does not validate (callers pass entries that already
    // passed isNormalisableIR()), so it shows what JUCE does without the check.
    auto makeIR = [](float headLevel, float bodyLevel) {
        juce::AudioBuffer<float> ir(1, 2048);
        ir.clear();
        ir.setSample(0, 0, headLevel);
        for (int i = 1; i < 1000; ++i)
            ir.setSample(0, i, bodyLevel * ((i % 2) ? 1.0f : -1.0f));
        return ir;
    };

    SECTION("a sane IR plays — through the IR, not JUCE's pass-through default")
    {
        // A fresh Convolution passes audio through at about unity (a 0.5 sine
        // comes out near 0.5), so "audible" alone would pass with no IR at all.
        // Normalisation is what gives this IR away: JUCE scales it to
        // 0.125/sqrt(energy), so the sine must come out at that gain times the
        // IR's response at 1 kHz — about 0.06, an eighth of pass-through.
        auto ir = makeIR(1.0f, 0.01f);
        REQUIRE(isNormalisableIR(ir));

        double energy = 0.0, re = 0.0, im = 0.0;
        const double w = 2.0 * juce::MathConstants<double>::pi * 1000.0 / 48000.0;
        for (int i = 0; i < ir.getNumSamples(); ++i)
        {
            const double h = ir.getSample(0, i);
            energy += h * h;
            re += h * std::cos(w * i);
            im -= h * std::sin(w * i);
        }
        const float expected = static_cast<float>(
            0.5 * (0.125 / std::sqrt(energy)) * std::sqrt(re * re + im * im));
        REQUIRE(expected < 0.1f);   // far from pass-through's ~0.5

        CabinetSim cab;
        cab.prepare(48000.0, 512, 1);
        cab.loadIRFromBuffer(std::move(ir), 48000.0);
        REQUIRE_THAT(settledPeak(cab), Catch::Matchers::WithinRel(expected, 0.05f));
    }

    SECTION("finite samples whose float sum of squares overflows play silence")
    {
        CabinetSim cab;
        cab.prepare(48000.0, 512, 1);
        auto ir = makeIR(1.0f, 1.0e18f);   // each square 1e36 is finite; the sum is not
        REQUIRE_FALSE(isNormalisableIR(ir));
        cab.loadIRFromBuffer(std::move(ir), 48000.0);
        REQUIRE(cab.useIR.load());   // IR mode is on...
        REQUIRE_FALSE(settledOutputIsAudible(cab));   // ...and dead
    }

    SECTION("an IR wholly below the -80 dBFS trim threshold plays silence")
    {
        CabinetSim cab;
        cab.prepare(48000.0, 512, 1);
        const float below = 0.5f * IR_TRIM_THRESHOLD;
        auto ir = makeIR(below, below);   // normalisation alone would have raised it
        REQUIRE_FALSE(isNormalisableIR(ir));
        cab.loadIRFromBuffer(std::move(ir), 48000.0);
        REQUIRE(cab.useIR.load());
        REQUIRE_FALSE(settledOutputIsAudible(cab));
    }
}

TEST_CASE("User IR -> stock IR transition", "[cabinet][ir]")
{
    // Create temp WAV
    auto tempFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("test_ir2.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(new juce::FileOutputStream(tempFile),
                                44100.0, 1, 16, {}, 0));
        REQUIRE(writer != nullptr);
        juce::AudioBuffer<float> impulse(1, 256);
        impulse.clear();
        impulse.setSample(0, 0, 1.0f);
        writer->writeFromAudioSampleBuffer(impulse, 0, 256);
    }

    CabinetSim cab;
    cab.prepare(44100.0, 512, 1);

    // Load user IR
    cab.loadIRFromFile(tempFile);
    REQUIRE(cab.useIR.load() == true);

    // Switch to stock IR
    cab.setIRIndex(3);
    REQUIRE(cab.useIR.load() == true);
    REQUIRE(cab.currentIRIndex == 3);

    // Switch to IIR
    cab.setIRIndex(0);
    REQUIRE(cab.useIR.load() == false);

    // Process — no crash
    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    juce::dsp::AudioBlock<float> block(buffer);
    REQUIRE_NOTHROW(cab.processBlock(block));

    tempFile.deleteFile();
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
