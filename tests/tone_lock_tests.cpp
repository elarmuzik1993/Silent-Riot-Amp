// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Tone lock, part 2: the frozen sound. Renders a fixed matrix of settings
// through the real processBlock (a real guitar DI, or a chord where the DI's
// 48 kHz rate does not fit) and compares each render's fingerprint with the
// reference committed under tests/tone/golden/. Any change that moves the
// steady-state sound of the amp, the cab or an effect fails here.
//
// The references move only on purpose: the owner approves the change after
// an A/B, then `SR_TONE_LOCK_WRITE=1` rewrites them and the commit gets a new
// tone-vN tag. See docs/dev/tone-lock.md.
// ═══════════════════════════════════════════════════════════════
#include "processor_harness.h"
#include "tone_lock.h"
#include "utils/Parameters.h"

#include <cstring>
#include <functional>
#include <vector>

using namespace sr::test;
using namespace sr::test::tone;
namespace IDs = sr::Params::IDs;

namespace {

constexpr double kSeconds = 3.0;   // per render; the first 0.5 s is skipped
constexpr int    kBlock = 512;

const juce::File kGoldenDir { SOURCE_DIR "/tests/tone/golden" };

bool writeMode()
{
    return juce::SystemStats::getEnvironmentVariable("SR_TONE_LOCK_WRITE", {}) == "1";
}

// tests/di_sample_82hz.raw: a real guitar DI, mono 32-bit float at 48 kHz.
const std::vector<float>& di()
{
    static const std::vector<float> samples = [] {
        juce::MemoryBlock data;
        juce::File(SOURCE_DIR "/tests/di_sample_82hz.raw").loadFileAsData(data);
        std::vector<float> x(data.getSize() / sizeof(float));
        std::memcpy(x.data(), data.getData(), x.size() * sizeof(float));
        return x;
    }();
    REQUIRE(samples.size() >= static_cast<size_t>(kSeconds * 48000.0));
    return samples;
}

// An E major chord (E2 B2 E3 G#3 B3) decaying like a strum, for the rates the
// 48 kHz DI cannot play at.
std::vector<float> chord(double sampleRate)
{
    const double notes[] = { 82.41, 123.47, 164.81, 207.65, 246.94 };
    const int n = static_cast<int>(kSeconds * sampleRate);
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sampleRate;
        double v = 0.0;
        for (double f : notes)
            v += std::sin(2.0 * juce::MathConstants<double>::pi * f * t);
        x[static_cast<size_t>(i)] = static_cast<float>(0.06 * v * std::exp(-t / 1.5));
    }
    return x;
}

struct Render { std::vector<float> left, right; };

// Stereo render of `input` (mono, fed to both channels, silence past its end).
Render render(ProcessorHarness& h, const std::vector<float>& input, float inputGain = 1.0f)
{
    const int total = static_cast<int>(std::lround(kSeconds * h.sampleRate));
    Render r;
    r.left.reserve(static_cast<size_t>(total));
    r.right.reserve(static_cast<size_t>(total));
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    for (int start = 0; start < total; start += kBlock)
    {
        const int n = std::min(kBlock, total - start);
        buf.setSize(2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const auto idx = static_cast<size_t>(start + i);
            const float s = idx < input.size() ? input[idx] * inputGain : 0.0f;
            buf.setSample(0, i, s);
            buf.setSample(1, i, s);
        }
        h.proc.processBlock(buf, midi);
        r.left.insert(r.left.end(), buf.getReadPointer(0), buf.getReadPointer(0) + n);
        r.right.insert(r.right.end(), buf.getReadPointer(1), buf.getReadPointer(1) + n);
    }
    return r;
}

struct Case
{
    juce::String name;
    double sampleRate = 48000.0;
    bool useChord = false;
    float inputGain = 1.0f;
    int oversample = 1;                                    // 0 = 2x, 1 = 4x, 2 = 8x
    std::function<void(ProcessorHarness&)> setup = [](ProcessorHarness&) {};
};

Fingerprint renderCase(const Case& c)
{
    ProcessorHarness h(c.sampleRate, kBlock, 2);
    if (c.oversample != 1)
    {
        // prepareToPlay starts in the saved oversample mode, with no transition.
        h.set(IDs::OVERSAMPLE, static_cast<float>(c.oversample));
        h.proc.prepareToPlay(c.sampleRate, kBlock);
    }
    c.setup(h);
    const auto input = c.useChord ? chord(c.sampleRate) : di();
    const auto r = render(h, input, c.inputGain);
    return fingerprint(r.left, r.right, c.sampleRate);
}

juce::File goldenFile(const Case& c) { return kGoldenDir.getChildFile(c.name + ".srtl"); }

void lock(const std::vector<Case>& cases)
{
    for (const auto& c : cases)
    {
        INFO("tone-lock case: " << c.name);
        const auto got = renderCase(c);
        REQUIRE(got.frames > 0);

        if (writeMode())
        {
            REQUIRE(writeFingerprint(goldenFile(c), got));
            WARN("tone lock: wrote reference " << goldenFile(c).getFileName());
            continue;
        }

        const auto ref = readFingerprint(goldenFile(c));
        INFO("missing or unreadable reference " << goldenFile(c).getFullPathName()
             << " (references are rewritten only with SR_TONE_LOCK_WRITE=1, after the owner approves a tone change)");
        REQUIRE(ref.has_value());

        const auto cmp = compare(*ref, got);
        INFO("worst band delta " << cmp.worstBandDeltaDb << " dB at " << cmp.where
             << "; RMS delta " << cmp.rmsDeltaDb << " dB");
        CHECK(cmp.matches);
    }
}

std::function<void(ProcessorHarness&)> preset(int index)
{
    return [index](ProcessorHarness& h) {
        const auto& p = sr::Presets::table[index];
        h.set(IDs::GAIN, p.gain);
        h.set(IDs::TONE, p.tone);
        h.set(IDs::VOLUME, p.volume);
        h.set(IDs::HALF_POWER, p.halfPower ? 1.0f : 0.0f);
        h.set(IDs::CAB_SIM, p.cabSim ? 1.0f : 0.0f);
    };
}

juce::String presetName(int index)
{
    return "preset-" + juce::String(index + 1) + "-"
         + juce::String(sr::Presets::table[index].name).toLowerCase().replaceCharacter(' ', '-');
}

Case knob(const juce::String& name, const juce::String& id, float value)
{
    Case c;
    c.name = name;
    c.setup = [id, value](ProcessorHarness& h) { h.set(id, value); };
    return c;
}

} // namespace

TEST_CASE("Tone lock: factory presets", "[tone-lock]")
{
    std::vector<Case> cases;
    for (int i = 0; i < sr::Presets::count; ++i)
    {
        Case c;
        c.name = presetName(i);
        c.setup = preset(i);
        cases.push_back(c);
    }
    lock(cases);
}

TEST_CASE("Tone lock: amp knobs at their extremes", "[tone-lock]")
{
    lock({
        knob("knob-gain-0", IDs::GAIN, 0.0f),
        knob("knob-gain-10", IDs::GAIN, 1.0f),
        knob("knob-shape-0", IDs::TONE, 0.0f),
        knob("knob-shape-10", IDs::TONE, 1.0f),
        knob("knob-volume-2", IDs::VOLUME, 0.2f),
        knob("knob-volume-10", IDs::VOLUME, 1.0f),
    });
}

TEST_CASE("Tone lock: 7W, CAB off and a loaded IR", "[tone-lock]")
{
    Case ir;
    ir.name = "switch-ir-neutral";
    ir.setup = [](ProcessorHarness& h) { h.loadIR(juce::File(SOURCE_DIR "/tests/fixtures/ir/neutral.wav")); };
    lock({
        knob("switch-7w", IDs::HALF_POWER, 1.0f),
        knob("switch-cab-off", IDs::CAB_SIM, 0.0f),
        ir,
    });
}

TEST_CASE("Tone lock: effects", "[tone-lock]")
{
    Case boost;
    boost.name = "fx-boost";
    boost.setup = [](ProcessorHarness& h) {
        h.set(IDs::BOOST_ON, 1.0f);
        h.set(IDs::BOOST_LEVEL, 0.5f);
        h.set(IDs::BOOST_TIGHT, 0.5f);
        h.set(IDs::BOOST_BRIGHT, 0.5f);
    };
    Case gate;
    gate.name = "fx-gate-quiet-di";
    gate.inputGain = 0.063f;   // -24 dB: a quiet DI, so the gate acts on the decay
    gate.setup = [](ProcessorHarness& h) { h.set(IDs::GATE_THRESHOLD, 0.5f); };
    Case trims;
    trims.name = "fx-input-up-output-down";
    trims.setup = [](ProcessorHarness& h) {
        h.set(IDs::INPUT_LEVEL, 0.75f);
        h.set(IDs::OUTPUT_LEVEL, 0.35f);
    };
    lock({
        boost,
        knob("fx-delay", IDs::DELAY_ON, 1.0f),
        knob("fx-reverb", IDs::REVERB_ON, 1.0f),
        gate,
        knob("fx-width-10", IDs::WIDTH, 1.0f),
        trims,
    });
}

TEST_CASE("Tone lock: sample rates and oversampling", "[tone-lock]")
{
    Case r44;  r44.name = "rate-44k1-chord"; r44.sampleRate = 44100.0; r44.useChord = true;
    Case r96;  r96.name = "rate-96k-chord";  r96.sampleRate = 96000.0; r96.useChord = true;
    Case os2;  os2.name = "oversample-2x";   os2.oversample = 0;
    Case os8;  os8.name = "oversample-8x";   os8.oversample = 2;
    lock({ r44, r96, os2, os8 });
}

TEST_CASE("Tone lock: renders are deterministic", "[tone-lock]")
{
    Case c;
    c.name = presetName(0);
    c.setup = preset(0);
    const auto a = renderCase(c);
    const auto b = renderCase(c);
    const auto cmp = compare(a, b, 0.0f, 0.0f);
    INFO("worst band delta " << cmp.worstBandDeltaDb << " dB at " << cmp.where);
    CHECK(cmp.matches);
}

TEST_CASE("Tone lock: small changes do not pass", "[tone-lock]")
{
    if (writeMode())
        return;   // the reference is being rewritten; nothing to compare against

    Case base;
    base.name = presetName(0);
    const auto ref = readFingerprint(goldenFile(base));
    REQUIRE(ref.has_value());

    Case louder = base;   // OUTPUT trim 0.500 -> 0.505
    louder.setup = [](ProcessorHarness& h) { preset(0)(h); h.set(IDs::OUTPUT_LEVEL, 0.505f); };
    const auto l = compare(*ref, renderCase(louder));
    INFO("OUTPUT +0.005: RMS delta " << l.rmsDeltaDb << " dB, worst band " << l.worstBandDeltaDb << " dB");
    CHECK_FALSE(l.matches);

    Case shaped = base;   // SHAPE +0.02 over the preset's value
    shaped.setup = [](ProcessorHarness& h) {
        preset(0)(h);
        h.set(IDs::TONE, sr::Presets::table[0].tone + 0.02f);
    };
    const auto s = compare(*ref, renderCase(shaped));
    INFO("SHAPE +0.02: RMS delta " << s.rmsDeltaDb << " dB, worst band " << s.worstBandDeltaDb << " dB");
    CHECK_FALSE(s.matches);
}
