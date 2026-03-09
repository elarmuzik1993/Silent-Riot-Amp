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
// Each stock IR gets a different character via varying resonance frequency and decay.
inline juce::AudioBuffer<float> generateSyntheticIR(const IRDescriptor& desc)
{
    const int numSamples = desc.samples;
    const float sr = static_cast<float>(desc.sampleRate);
    juce::AudioBuffer<float> buffer(1, numSamples);
    auto* data = buffer.getWritePointer(0);

    // Vary character per IR by hashing the id pointer
    // Use simple index-based differentiation
    float resFreq = 80.0f;   // resonant frequency (Hz)
    float decayRate = 6.0f;  // decay speed (higher = faster)
    float brightness = 0.5f; // HF content

    // Differentiate based on descriptor id
    const char firstChar = desc.id[3]; // character after "tt_"
    if (firstChar == '1') { resFreq = 100.0f; decayRate = 7.0f; brightness = 0.6f; }
    if (firstChar == '2') { resFreq = 80.0f;  decayRate = 5.0f; brightness = 0.4f; }
    if (firstChar == '4') { resFreq = 60.0f;  decayRate = 4.0f; brightness = 0.3f; }

    const float omega = 2.0f * juce::MathConstants<float>::pi * resFreq / sr;

    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / sr;

        // Exponential decay envelope
        float env = std::exp(-decayRate * t);

        // Initial impulse + resonant oscillation
        float impulse = (i == 0) ? 1.0f : 0.0f;
        float resonance = std::sin(omega * static_cast<float>(i)) * env;

        // HF content via second harmonic
        float hf = brightness * std::sin(3.0f * omega * static_cast<float>(i)) * env * 0.3f;

        data[i] = impulse * 0.5f + resonance * 0.7f + hf;
    }

    // Normalize peak to 1.0
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max(peak, std::abs(data[i]));

    if (peak > 0.0f)
    {
        float scale = 1.0f / peak;
        for (int i = 0; i < numSamples; ++i)
            data[i] *= scale;
    }

    return buffer;
}

} // namespace tt
