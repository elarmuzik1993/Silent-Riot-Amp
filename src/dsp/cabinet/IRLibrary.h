#pragma once
// IRLibrary — Stock IR metadata registry + synthetic IR generator (Spec §3.10)
//
// IR index 0 = IIR mode (existing cab filters), 1–8 = stock IRs, 9+ = user slots.
// Synthetic IRs are placeholders that exercise the full convolution pipeline.
// When real WAV assets arrive, replace generateSyntheticIR() or load from binary resources.

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace tt {

struct IRDescriptor {
    const char* id;            // e.g. "tt_1x12_v30"
    const char* displayName;   // e.g. "TT 1x12 V30"
    const char* mic;           // e.g. "SM57"
    int samples;               // IR length in samples
    int sampleRate;            // native sample rate
};

// 8 stock IRs (indices 1–8 in the parameter)
inline constexpr int NUM_STOCK_IRS = 8;

inline const IRDescriptor& getStockIRDescriptor(int stockIndex)
{
    // stockIndex 0–7 maps to IR_INDEX 1–8
    static const IRDescriptor descriptors[NUM_STOCK_IRS] = {
        { "tt_1x12_v30",       "TT 1x12 V30",       "SM57",   1024, 48000 },
        { "tt_1x12_g12m",      "TT 1x12 G12M",      "SM57",   1024, 48000 },
        { "tt_2x12_v30",       "2x12 V30",           "R121",   1024, 48000 },
        { "tt_2x12_g12h",      "2x12 G12H",          "MD421",  1024, 48000 },
        { "tt_4x12_v30",       "4x12 V30",           "SM57",   2048, 48000 },
        { "tt_4x12_t75",       "4x12 T75",           "R121",   2048, 48000 },
        { "tt_1x12_alnico",    "1x12 Alnico Blue",   "C414",   1024, 48000 },
        { "tt_1x12_jensen",    "1x12 Jensen P12R",   "SM57",   1024, 48000 },
    };

    jassert(stockIndex >= 0 && stockIndex < NUM_STOCK_IRS);
    return descriptors[stockIndex];
}

// Generate a synthetic IR for development/testing.
// Uses exponential decay with a resonant bump to approximate a cab response.
// Each stock IR gets unique character via per-index synthesis parameters.
// No manual normalization — JUCE's Convolution::Normalise::yes handles it uniformly
// for both stock and user IRs, preventing level jumps when switching between them.
inline juce::AudioBuffer<float> generateSyntheticIR(int stockIndex, const IRDescriptor& desc)
{
    const int numSamples = desc.samples;
    const float sr = static_cast<float>(desc.sampleRate);
    juce::AudioBuffer<float> buffer(1, numSamples);
    auto* data = buffer.getWritePointer(0);

    // Unique synthesis parameters per stock IR
    struct SynthParams { float resFreq; float decayRate; float brightness; };
    static constexpr SynthParams params[NUM_STOCK_IRS] = {
        { 100.0f, 7.0f, 0.60f },  // 0: TT 1x12 V30      — tight, bright
        {  90.0f, 6.0f, 0.45f },  // 1: TT 1x12 G12M     — warm, medium
        {  80.0f, 5.0f, 0.40f },  // 2: 2x12 V30          — wider, balanced
        {  75.0f, 4.5f, 0.35f },  // 3: 2x12 G12H         — round, smooth
        {  60.0f, 4.0f, 0.30f },  // 4: 4x12 V30          — deep, big room
        {  55.0f, 3.5f, 0.25f },  // 5: 4x12 T75          — dark, heavy
        { 110.0f, 8.0f, 0.70f },  // 6: 1x12 Alnico Blue  — chimey, sparkly
        {  95.0f, 6.5f, 0.55f },  // 7: 1x12 Jensen P12R  — vintage, midrange
    };

    jassert(stockIndex >= 0 && stockIndex < NUM_STOCK_IRS);
    const auto& p = params[stockIndex];

    const float omega = 2.0f * juce::MathConstants<float>::pi * p.resFreq / sr;

    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / sr;

        // Exponential decay envelope
        float env = std::exp(-p.decayRate * t);

        // Initial impulse + resonant oscillation
        float impulse = (i == 0) ? 1.0f : 0.0f;
        float resonance = std::sin(omega * static_cast<float>(i)) * env;

        // HF content via third harmonic
        float hf = p.brightness * std::sin(3.0f * omega * static_cast<float>(i)) * env * 0.3f;

        data[i] = impulse * 0.5f + resonance * 0.7f + hf;
    }

    return buffer;
}

} // namespace tt
