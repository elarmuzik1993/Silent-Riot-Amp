// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
// ═══════════════════════════════════════════════════════════════
// Processor test harness — builds and prepares the REAL SilentRiotProcessor
// (linked from the plugin's SharedCode target) and drives it the way a host
// does: parameters through the APVTS, audio through processBlock /
// processBlockBypassed in host-sized blocks, reset() between blocks.
//
// Use it for anything that lives in the processor's wiring rather than in a
// DSP class — edge detection, flags, the order of stages. A unit test of the
// class it calls cannot see that wiring at all.
//
//   ProcessorHarness h;                               // 44.1 kHz, 512, stereo
//   h.set(sr::Params::IDs::DELAY_ON, 1.0f);           // natural units
//   h.run(1.0, sine(220.0f, 0.25f));                  // returns the output peak
//   REQUIRE(h.silence(1.0) < dB(-120.0));
// ═══════════════════════════════════════════════════════════════

#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <thread>
#include <vector>

namespace sr::test {

inline float dB(double decibels) { return static_cast<float>(std::pow(10.0, decibels / 20.0)); }

// Input generators: (sample index from the start of the run, channel) → value.
using Fill = std::function<float(int, int)>;

inline Fill sine(float freqHz, float amplitude, double sampleRate = 44100.0)
{
    return [=](int i, int) {
        return amplitude * std::sin(2.0f * 3.14159265358979f * freqHz
                                    * static_cast<float>(i) / static_cast<float>(sampleRate));
    };
}

inline Fill zeros() { return [](int, int) { return 0.0f; }; }

struct ProcessorHarness
{
    enum class Mode { Process, Bypassed };

    // MessageManager + JUCE statics (the APVTS and the processor's timer need
    // it). Nothing pumps the message loop unless a test does: a parameter
    // change to IR_INDEX, and the host's copy of the latency after an
    // oversample switch, only land while one does (withMessageLoop()).
    // First member, so it outlives the processor.
    juce::ScopedJuceInitialiser_GUI juceInit;
    SilentRiotProcessor proc;
    const double sampleRate;
    const int blockSize;
    const int numChannels;

    explicit ProcessorHarness(double sr = 44100.0, int block = 512, int channels = 2)
        : sampleRate(sr), blockSize(block), numChannels(channels)
    {
        proc.setPlayConfigDetails(channels, channels, sr, block);
        proc.prepareToPlay(sr, block);
    }

    // Parameter in its natural units (knobs 0..1, toggles 0/1, choices by index).
    void set(const juce::String& id, float natural)
    {
        auto* p = proc.apvts.getParameter(id);
        REQUIRE(p != nullptr);
        p->setValueNotifyingHost(p->convertTo0to1(natural));
    }

    // set() without the REQUIRE, for withMessageLoop()'s audio thread, where
    // Catch2 assertions are not safe. Host automation arrives like this.
    void setFromAudioThread(const juce::String& id, float natural)
    {
        if (auto* p = proc.apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(natural));
    }

    // Runs `audio` on a thread of its own while this thread — the message
    // thread — pumps the message loop, as in a host. The processor's 20 Hz
    // timer (latency report, IR_INDEX changes) only fires while it does.
    // Once per harness: JUCE's stopDispatchLoop() is final for a
    // MessageManager, and this harness owns the only one. `audio` must not
    // REQUIRE or CHECK (Catch2 assertions are not thread-safe): record what
    // it sees and assert after this returns.
    void withMessageLoop(const std::function<void()>& audio)
    {
        auto* mm = juce::MessageManager::getInstance();
        std::exception_ptr failed;
        std::thread th([&] {
            try { audio(); } catch (...) { failed = std::current_exception(); }
            mm->stopDispatchLoop();
        });
        mm->runDispatchLoop();
        th.join();
        if (failed) std::rethrow_exception(failed);
    }

    // Renders `seconds` of `in` and returns the peak |output| over all channels.
    // `out`, when given, receives channel 0 of the output.
    float run(double seconds, const Fill& in, Mode mode = Mode::Process,
              std::vector<float>* out = nullptr)
    {
        const int total = static_cast<int>(std::lround(seconds * sampleRate));
        juce::AudioBuffer<float> buf(numChannels, blockSize);
        juce::MidiBuffer midi;
        float peak = 0.0f;
        for (int start = 0; start < total; start += blockSize)
        {
            const int n = std::min(blockSize, total - start);
            buf.setSize(numChannels, n, false, false, true);
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < n; ++i)
                    buf.setSample(ch, i, in(start + i, ch));
            if (mode == Mode::Process) proc.processBlock(buf, midi);
            else                       proc.processBlockBypassed(buf, midi);
            for (int ch = 0; ch < numChannels; ++ch)
                peak = std::max(peak, buf.getMagnitude(ch, 0, n));
            if (out != nullptr)
                out->insert(out->end(), buf.getReadPointer(0), buf.getReadPointer(0) + n);
        }
        return peak;
    }

    float silence(double seconds, Mode mode = Mode::Process) { return run(seconds, zeros(), mode); }

    // Loads a user IR (as the editor does) and processes silence until the
    // convolution has picked it up: juce::dsp::Convolution builds the new
    // engine on its own background thread, then crossfades to it.
    void loadIR(const juce::File& wav)
    {
        REQUIRE(proc.loadUserIR(wav));
        for (int i = 0; i < 60; ++i)
        {
            silence(static_cast<double>(blockSize) / sampleRate);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        silence(0.5);   // past the crossfade
    }
};

// Writes a mono 24-bit WAV and returns it; the file lives as long as `tmp`.
inline juce::File writeWav(const juce::TemporaryFile& tmp, const std::vector<float>& samples,
                           double sampleRate = 44100.0)
{
    const juce::File f = tmp.getFile();
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = std::make_unique<juce::FileOutputStream>(f);
    std::unique_ptr<juce::AudioFormatWriter> w(
        wav.createWriterFor(stream.get(), sampleRate, 1, 24, {}, 0));
    REQUIRE(w != nullptr);
    stream.release();   // the writer owns it now
    juce::AudioBuffer<float> buf(1, static_cast<int>(samples.size()));
    buf.copyFrom(0, 0, samples.data(), static_cast<int>(samples.size()));
    REQUIRE(w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples()));
    return f;
}

} // namespace sr::test
