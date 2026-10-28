// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// Cabinet IR Loader Tests — exercises convolution pipeline, mode switching, IR registry
// Requires JUCE (separate test target: SilentRiotJuceTests)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

#include "dsp/cabinet/IRLibrary.h"
#include "dsp/cabinet/CabinetSim.h"
#include "dsp/cabinet/IRLevel.h"

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

TEST_CASE("loadIRFromFile rejects a dead channel among the two it convolves, not past them",
          "[cabinet][ir]")
{
    // Stereo::yes convolves with the first two channels only, so a dead
    // channel 0 or 1 is refused but a dead third channel is never looked at.
    struct Case { int numChannels; int deadChannel; bool loads; };
    for (const auto c : { Case { 2, 0, false }, Case { 2, 1, false }, Case { 3, 2, true } })
    {
        auto tempFile = juce::File::getSpecialLocation(
            juce::File::tempDirectory).getChildFile("test_ir_dead_channel.wav");
        tempFile.deleteFile();
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(new juce::FileOutputStream(tempFile), 48000.0,
                                    static_cast<unsigned int>(c.numChannels), 32, {}, 0));
            REQUIRE(writer != nullptr);

            juce::AudioBuffer<float> ir(c.numChannels, 256);
            for (int ch = 0; ch < c.numChannels; ++ch)
                for (int i = 0; i < 256; ++i)
                    ir.setSample(ch, i, ch == c.deadChannel
                                            ? 0.0f : std::exp(-0.02f * static_cast<float>(i)));
            writer->writeFromAudioSampleBuffer(ir, 0, 256);
        }

        CabinetSim cab;
        cab.prepare(48000.0, 512, 2);
        cab.loadIRFromFile(tempFile);
        REQUIRE(cab.useIR.load() == c.loads);

        tempFile.deleteFile();
    }
}

TEST_CASE("isSafeToNormaliseIR flags NaN, Inf and overflowing energy in any channel", "[cabinet][ir]")
{
    juce::AudioBuffer<float> b(2, 64);
    b.clear();
    b.setSample(0, 0, 1.0f);
    REQUIRE(isSafeToNormaliseIR(b));   // channel 1 is silent: harmless, not unsafe

    b.setSample(1, 63, std::numeric_limits<float>::quiet_NaN());
    REQUIRE_FALSE(isSafeToNormaliseIR(b));

    b.setSample(1, 63, 0.0f);
    b.setSample(0, 10, -std::numeric_limits<float>::infinity());
    REQUIRE_FALSE(isSafeToNormaliseIR(b));

    // Finite, but its square alone overflows a float.
    b.setSample(0, 10, -1.0e20f);
    REQUIRE_FALSE(isSafeToNormaliseIR(b));

    // No single square overflows (1e38 < FLT_MAX), but the channel's sum does.
    b.clear();
    for (int i = 0; i < 64; ++i)
        b.setSample(1, i, 1.0e19f);
    REQUIRE_FALSE(isSafeToNormaliseIR(b));

    // A loud but sane IR (+60 dB over full scale, both channels) passes.
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            b.setSample(ch, i, 1000.0f);
    REQUIRE(isSafeToNormaliseIR(b));
}

TEST_CASE("isAudibleOnEveryChannel needs a sample at or above JUCE's -80 dBFS trim in each channel",
          "[cabinet][ir]")
{
    // juce::dsp::Convolution (Trim::yes) keeps only the span between the first
    // and last sample with |x| >= -80 dBFS over all channels — an ABSOLUTE
    // threshold — and an IR with none plays silence. Each IR channel then
    // plays on its own output channel, so a dead channel is a silent side (and
    // on a mono bus, where only channel 0 plays, a dead channel 0 is silence).
    juce::AudioBuffer<float> b(2, 64);
    b.clear();
    REQUIRE_FALSE(isAudibleOnEveryChannel(b));   // all zero
    REQUIRE(isSafeToNormaliseIR(b));             // ...but harmless: restore keeps it

    // Every sample just below the threshold, both channels: trimmed to nothing.
    const float below = std::nextafter(IR_TRIM_THRESHOLD, 0.0f);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            b.setSample(ch, i, (i % 2) ? below : -below);
    REQUIRE_FALSE(isAudibleOnEveryChannel(b));

    // One sample AT the threshold (JUCE's test is >=) in channel 1 only:
    // JUCE's union trim keeps the IR, but channel 0 is still dead.
    b.setSample(1, 40, -IR_TRIM_THRESHOLD);
    REQUIRE_FALSE(isAudibleOnEveryChannel(b));
    REQUIRE_FALSE(isLoadableIR(b));

    // ...and in channel 0 too: every channel survives.
    b.setSample(0, 3, IR_TRIM_THRESHOLD);
    REQUIRE(isAudibleOnEveryChannel(b));
    REQUIRE(isLoadableIR(b));

    // Mono: its one channel feeds every output channel.
    juce::AudioBuffer<float> mono(1, 16);
    mono.clear();
    mono.setSample(0, 8, 0.5f);
    REQUIRE(isAudibleOnEveryChannel(mono));

    // An empty buffer is rejected too. (JUCE would not play silence for it:
    // fixNumChannels() turns a 0-sample buffer into a unity impulse, i.e.
    // pass-through — but that is no IR either.)
    juce::AudioBuffer<float> empty(1, 0);
    REQUIRE_FALSE(isAudibleOnEveryChannel(empty));
    juce::AudioBuffer<float> noChannels(0, 16);
    REQUIRE_FALSE(isAudibleOnEveryChannel(noChannels));
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

TEST_CASE("IRs that can't be level-matched or trimmed silence IR mode (why loads reject them)",
          "[cabinet][ir]")
{
    // loadIRFromBuffer() does not validate (callers pass entries that already
    // passed the checks), so it shows what the convolution does without them.
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
        // The IR is a 64-tap moving average, whose 1 kHz response at 48 kHz
        // sits in its first sidelobe, 13.7 dB under DC: after the level match
        // (IRLevel.h: band level -> the built-in cab's) the 0.5 sine comes out
        // near 0.18, far from pass-through's 0.5 and from silence.
        juce::AudioBuffer<float> ir(1, 64);
        for (int i = 0; i < 64; ++i)
            ir.setSample(0, i, 1.0f / 64.0f);
        REQUIRE(isLoadableIR(ir));

        double re = 0.0, im = 0.0;
        const double w = 2.0 * juce::MathConstants<double>::pi * 1000.0 / 48000.0;
        for (int i = 0; i < ir.getNumSamples(); ++i)
        {
            const double h = ir.getSample(0, i);
            re += h * std::cos(w * i);
            im -= h * std::sin(w * i);
        }
        const float* ch[] = { ir.getReadPointer(0) };
        const double gain = irlevel::levelMatchGain(
            irlevel::bandPower(ch, 1, ir.getNumSamples(), 48000.0), 48000.0);
        const float expected = static_cast<float>(0.5 * gain * std::sqrt(re * re + im * im));
        INFO("expected settled peak " << expected);
        REQUIRE(expected > 0.05f);    // not silence
        REQUIRE(expected < 0.3f);     // and nowhere near pass-through's 0.5

        CabinetSim cab;
        cab.prepare(48000.0, 512, 1);
        cab.loadIRFromBuffer(std::move(ir), 48000.0);
        REQUIRE_THAT(settledPeak(cab), Catch::Matchers::WithinRel(expected, 0.05f));
    }

    SECTION("a NaN sample poisons the output")
    {
        // The band power is NaN, so the level match leaves the gain at 1 and
        // the NaN reaches the convolution. (Before Task 2 the guard's energy
        // ceiling also mattered: finite samples whose float sum of squares
        // overflowed turned JUCE's normalisation gain into 0. The level match
        // sums in double and scales such an IR down, so it would now play;
        // loads and restores still reject it, so the accepted set is unchanged.)
        CabinetSim cab;
        cab.prepare(48000.0, 512, 1);
        auto ir = makeIR(1.0f, 0.01f);
        ir.setSample(0, 500, std::numeric_limits<float>::quiet_NaN());
        REQUIRE_FALSE(isSafeToNormaliseIR(ir));
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
        REQUIRE_FALSE(isAudibleOnEveryChannel(ir));
        cab.loadIRFromBuffer(std::move(ir), 48000.0);
        REQUIRE(cab.useIR.load());
        REQUIRE_FALSE(settledOutputIsAudible(cab));
    }

    SECTION("a stereo IR with a dead channel 0 plays silence on a mono bus")
    {
        // JUCE's trim is a union over channels, so channel 1 keeps this IR
        // alive — but on a mono bus MultichannelEngine convolves only IR
        // channel 0 and ignores channel 1.
        CabinetSim cab;
        cab.prepare(48000.0, 512, 1);
        auto mono = makeIR(1.0f, 0.01f);
        juce::AudioBuffer<float> ir(2, mono.getNumSamples());
        ir.clear();
        ir.copyFrom(1, 0, mono, 0, 0, mono.getNumSamples());
        REQUIRE(isSafeToNormaliseIR(ir));
        REQUIRE_FALSE(isAudibleOnEveryChannel(ir));
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

// ═══════════════════════════════════════════════════════════════
// Synchronous install (installIRNow) — IR-path plan Task 1
// ═══════════════════════════════════════════════════════════════

namespace {

// 64-tap moving average: a low-pass whose 10 kHz response at 44.1 kHz is
// -32.4 dB (|sin(pi f N/fs) / (N sin(pi f/fs))|) below DC. The level match
// raises it by 4.8 dB (band level -5.7 dB → the built-in cab's -0.9 dB), so it
// plays 10 kHz at -27.7 dB; a unit impulse, like JUCE's pass-through, at
// -0.9 / 0 dB. A -20 dB threshold tells them apart with margin either way.
juce::AudioBuffer<float> movingAverageIR()
{
    juce::AudioBuffer<float> ir(1, 64);
    for (int i = 0; i < 64; ++i)
        ir.setSample(0, i, 1.0f / 64.0f);
    return ir;
}

juce::AudioBuffer<float> unitImpulseIR()
{
    juce::AudioBuffer<float> ir(1, 64);
    ir.clear();
    ir.setSample(0, 0, 1.0f);
    return ir;
}

// Processes one block of a 10 kHz tone (amplitude 0.5, phase continuous from
// `startSample`) and returns output RMS / input RMS in dB, skipping the
// filter's first 64 samples on the first block.
double tenKHzGainDb(CabinetSim& cab, int startSample)
{
    constexpr int n = 512;
    juce::AudioBuffer<float> buffer(2, n);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            buffer.setSample(ch, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi
                                                    * 10000.0f * static_cast<float>(startSample + i)
                                                    / 44100.0f));
    juce::dsp::AudioBlock<float> block(buffer);
    cab.processBlock(block);

    const int skip = startSample == 0 ? 64 : 0;
    double sumOut = 0.0;
    for (int i = skip; i < n; ++i)
        sumOut += static_cast<double>(buffer.getSample(0, i)) * buffer.getSample(0, i);
    const double rmsOut = std::sqrt(sumOut / (n - skip));
    return 20.0 * std::log10(std::max(rmsOut, 1e-12) / (0.5 / std::sqrt(2.0)));
}

} // namespace

TEST_CASE("installIRNow plays the IR on the very first block", "[cabinet][ir]")
{
    // Before Task 1 every load went through bufferTransfer: the first block
    // ran JUCE's pass-through engine (0 dB at 10 kHz) while the real one was
    // built on JUCE's background thread. installIRNow builds it on this
    // thread, so no sleep or polling is needed.
    CabinetSim cab;
    cab.prepare(44100.0, 512, 2);
    cab.installIRNow(movingAverageIR(), 44100.0);
    REQUIRE(cab.useIR.load());
    REQUIRE(cab.getIRTailSeconds() > 0.0);

    const double gainDb = tenKHzGainDb(cab, 0);
    INFO("10 kHz through the first block: " << gainDb << " dB");
    REQUIRE(gainDb <= -20.0);
}

TEST_CASE("installIRNow drops a pending async load instead of replaying it", "[cabinet][ir]")
{
    // An async load still waiting in bufferTransfer would otherwise be picked
    // up by the next processBlock and crossfade, at a wall-clock time, back to
    // the older IR — here a unit impulse, which passes 10 kHz at 0 dB.
    CabinetSim cab;
    cab.prepare(44100.0, 512, 2);
    cab.loadIRFromBuffer(unitImpulseIR(), 44100.0);   // queued, not yet picked up
    cab.installIRNow(movingAverageIR(), 44100.0);

    double worstDb = -200.0;
    for (int b = 0; b < 40; ++b)   // 0.46 s: past JUCE's 10 ms poll and 50 ms crossfade
    {
        worstDb = std::max(worstDb, tenKHzGainDb(cab, b * 512));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    INFO("loudest 10 kHz block: " << worstDb << " dB");
    REQUIRE(worstDb <= -20.0);
}

// ═══════════════════════════════════════════════════════════════
// Level match (IR-path plan Task 2): every IR plays at the built-in cab's
// band level (dsp/cabinet/IRLevel.h), whatever the session rate
// ═══════════════════════════════════════════════════════════════

namespace {

juce::AudioBuffer<float> readFixtureIR(const char* name, double& rate)
{
    const juce::File f = juce::File(SOURCE_DIR).getChildFile("tests/fixtures/ir").getChildFile(name);
    REQUIRE(f.existsAsFile());
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> ir(static_cast<int>(reader->numChannels),
                                static_cast<int>(reader->lengthInSamples));
    reader->read(&ir, 0, ir.getNumSamples(), 0, true, true);
    rate = reader->sampleRate;
    return ir;
}

// Feeds a unit impulse through a prepared CabinetSim and returns what comes
// out — the IR as the convolution actually plays it at `sessionRate`.
std::vector<float> playedIR(CabinetSim& cab, double sessionRate, int irSamples, double irRate)
{
    const int total = static_cast<int>(std::ceil(irSamples * sessionRate / irRate)) + 4096;
    std::vector<float> out;
    juce::AudioBuffer<float> buffer(1, 512);
    for (int start = 0; start < total; start += 512)
    {
        buffer.clear();
        if (start == 0)
            buffer.setSample(0, 0, 1.0f);
        juce::dsp::AudioBlock<float> block(buffer);
        cab.processBlock(block);
        out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + 512);
    }
    return out;
}

} // namespace

TEST_CASE("A loaded IR plays at the built-in cab's band level at any session rate",
          "[cabinet][ir][level]")
{
    double irRate = 0.0;
    const auto fixture = readFixtureIR("neutral.wav", irRate);
    REQUIRE(irRate == 48000.0);
    const double refDb = irlevel::powerToDb(irlevel::builtInCabBandPower());

    double levels[2] {};
    const double rates[2] { 44100.0, 96000.0 };
    for (int i = 0; i < 2; ++i)
    {
        CabinetSim cab;
        cab.prepare(rates[i], 512, 1);
        cab.installIRNow(juce::AudioBuffer<float>(fixture), irRate);
        const auto played = playedIR(cab, rates[i], fixture.getNumSamples(), irRate);
        const float* ch[] = { played.data() };
        const auto p = irlevel::bandPower(ch, 1, static_cast<int>(played.size()), rates[i]);
        REQUIRE(p.has_value());
        levels[i] = irlevel::powerToDb(*p);
    }
    INFO("reference " << refDb << " dB; played at 44.1 kHz " << levels[0]
         << " dB, at 96 kHz " << levels[1] << " dB");
    // Before Task 2 (JUCE's Normalise::yes): -11.4 and -8.3 dB, i.e. ~10.5 dB
    // under the reference and 3 dB apart.
    CHECK(std::abs(levels[0] - refDb) < 0.3);
    CHECK(std::abs(levels[1] - refDb) < 0.3);
}

TEST_CASE("The level match scales a copy, one gain for both channels", "[cabinet][ir][level]")
{
    // Left at full level, right 6 dB down: after the match the played IR keeps
    // that 6 dB balance (one gain), and the channel average meets the reference.
    // The buffer the caller holds (userIRs, in the processor) is not touched.
    double irRate = 0.0;
    const auto mono = readFixtureIR("neutral.wav", irRate);
    juce::AudioBuffer<float> stereo(2, mono.getNumSamples());
    stereo.copyFrom(0, 0, mono, 0, 0, mono.getNumSamples());
    stereo.copyFrom(1, 0, mono, 0, 0, mono.getNumSamples());
    stereo.applyGain(1, 0, mono.getNumSamples(), 0.5f);

    const juce::AudioBuffer<float> before(stereo);
    CabinetSim cab;
    cab.prepare(48000.0, 512, 2);
    cab.installIRNow(juce::AudioBuffer<float>(stereo), irRate);
    {
        CabinetSim async;   // the async path too, on its own instance
        async.prepare(48000.0, 512, 2);
        async.loadIRFromBuffer(juce::AudioBuffer<float>(stereo), irRate);
    }
    for (int ch = 0; ch < 2; ++ch)
        REQUIRE(std::equal(stereo.getReadPointer(ch), stereo.getReadPointer(ch) + stereo.getNumSamples(),
                           before.getReadPointer(ch)));

    std::vector<float> out[2];
    juce::AudioBuffer<float> buffer(2, 512);
    for (int start = 0; start < mono.getNumSamples() + 4096; start += 512)
    {
        buffer.clear();
        if (start == 0) { buffer.setSample(0, 0, 1.0f); buffer.setSample(1, 0, 1.0f); }
        juce::dsp::AudioBlock<float> block(buffer);
        cab.processBlock(block);
        for (int ch = 0; ch < 2; ++ch)
            out[ch].insert(out[ch].end(), buffer.getReadPointer(ch), buffer.getReadPointer(ch) + 512);
    }
    const int n = static_cast<int>(out[0].size());
    const float* l[] = { out[0].data() };
    const float* r[] = { out[1].data() };
    const float* both[] = { out[0].data(), out[1].data() };
    const double lDb = irlevel::powerToDb(*irlevel::bandPower(l, 1, n, 48000.0));
    const double rDb = irlevel::powerToDb(*irlevel::bandPower(r, 1, n, 48000.0));
    const double avgDb = irlevel::powerToDb(*irlevel::bandPower(both, 2, n, 48000.0));
    INFO("L " << lDb << " dB, R " << rDb << " dB, average " << avgDb << " dB");
    CHECK_THAT(lDb - rDb, Catch::Matchers::WithinAbs(20.0 * std::log10(2.0), 0.05));
    CHECK(std::abs(avgDb - irlevel::powerToDb(irlevel::builtInCabBandPower())) < 0.3);
}

// ═══════════════════════════════════════════════════════════════
// Readiness for the cab-mode switcher (IR-path plan Task 3): prime() and
// hasRealIR()
// ═══════════════════════════════════════════════════════════════

TEST_CASE("A fresh convolution has no real IR, at any session rate", "[cabinet][ir][switcher]")
{
    // JUCE's pass-through is a 1-sample impulse at 44.1 kHz, resampled to the
    // session rate: 1 sample at 44.1/48k, 2 at 96k, 4 at 192k. So "IR size
    // > 1" does not tell it from a real IR at 96k and up; hasRealIR() compares
    // against the engine size the queued IR will have instead.
    const double rate = GENERATE(44100.0, 48000.0, 96000.0, 192000.0);
    CabinetSim cab;
    cab.prepare(rate, 512, 2);
    INFO("rate " << rate);
    REQUIRE_FALSE(cab.hasRealIR());
    cab.loadIRFromBuffer(movingAverageIR(), 44100.0);   // queued only
    REQUIRE_FALSE(cab.hasRealIR());
}

TEST_CASE("An IR pushed while not in IR mode becomes real through prime()", "[cabinet][ir][switcher]")
{
    // The switcher keeps the built-in cab (or CAB off) playing while the
    // first IR of a session is built: processBlock is not called, so prime()
    // must hand JUCE the IR and run its engine swap on a silent block.
    const double rate = GENERATE(44100.0, 96000.0);
    CabinetSim cab;
    cab.prepare(rate, 512, 2);
    cab.loadIRFromBuffer(movingAverageIR(), 44100.0);

    bool ready = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!ready && std::chrono::steady_clock::now() < deadline)
    {
        cab.prime(512);
        ready = cab.hasRealIR();
        if (!ready) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    INFO("rate " << rate);
    REQUIRE(ready);

    // And it is the IR, not the pass-through: 10 kHz is cut once JUCE's own
    // pass-through crossfade has been dropped (the switcher's commit resets).
    // (tenKHzGainDb's tone assumes 44.1 kHz.)
    cab.reset();
    if (rate == 44100.0)
        REQUIRE(tenKHzGainDb(cab, 0) <= -20.0);
}

TEST_CASE("hasRealIR matches JUCE's engine size after an install, at any rate", "[cabinet][ir][switcher]")
{
    // hasRealIR() predicts the engine size from JUCE's trim and resampling
    // rules (CabinetSim::engineSizeFor). Pin that replica against the real
    // thing, so a JUCE upgrade that changes either rule shows up here.
    const double sessionRate = GENERATE(44100.0, 48000.0, 88200.0, 96000.0, 192000.0);
    const double irRate = GENERATE(8000.0, 44100.0, 48000.0, 96000.0);
    juce::AudioBuffer<float> ir(2, 1000);
    ir.clear();
    for (int i = 0; i < 1000; ++i)   // quiet head and tail for the trim to cut
    {
        const float env = std::exp(-static_cast<float>(i) / 80.0f);
        ir.setSample(0, i, i < 37 ? 0.0f : env * std::cos(0.3f * static_cast<float>(i)));
        ir.setSample(1, i, i < 52 ? 0.0f : 0.5f * env * std::sin(0.2f * static_cast<float>(i)));
    }
    CabinetSim cab;
    cab.prepare(sessionRate, 256, 2);
    cab.installIRNow(juce::AudioBuffer<float>(ir), irRate);
    INFO("session " << sessionRate << ", IR " << irRate);
    REQUIRE(cab.hasRealIR());

    // A re-prepare at another rate rebuilds the same IR there.
    cab.prepare(sessionRate == 44100.0 ? 96000.0 : 44100.0, 256, 2);
    REQUIRE(cab.hasRealIR());
}

TEST_CASE("hasRealIR fails open: a mispredicted engine size still becomes ready", "[cabinet][ir][switcher]")
{
    // The switcher waits for hasRealIR() with no timeout. If the size replica
    // ever mispredicts (a JUCE change, rounding), IR mode must still be
    // reachable once a real engine is installed — never stuck on the
    // built-in cab — while the pass-through must still read as not ready.
    const double rate = GENERATE(44100.0, 96000.0);
    CabinetSim cab;
    cab.prepare(rate, 512, 2);
    cab.loadIRFromBuffer(movingAverageIR(), 44100.0);
    cab.setExpectedEngineSizeForTesting(12345);   // wrong on purpose
    INFO("rate " << rate);
    REQUIRE_FALSE(cab.hasRealIR());               // the pass-through is never ready

    bool ready = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!ready && std::chrono::steady_clock::now() < deadline)
    {
        cab.prime(512);
        ready = cab.hasRealIR();
        if (!ready) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(ready);
    cab.reset();
    if (rate == 44100.0)
        REQUIRE(tenKHzGainDb(cab, 0) <= -20.0);   // and it is the IR
}
