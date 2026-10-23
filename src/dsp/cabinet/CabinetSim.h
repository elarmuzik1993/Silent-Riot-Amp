#pragma once
// Cabinet Sim (Spec §3.10) — IIR fallback + IR convolution engine
//
// Dual-path architecture:
//   IR_INDEX = 0: IIR mode (cab_presence, cab_rolloff, cab_notch in FilterBank)
//   IR_INDEX > 0: Convolution mode (runs at base SR, after downsample)
//
// Uses the canonical JUCE BufferTransfer pattern (from ConvolutionDemo) for
// thread-safe IR loading: message thread sets buffer, audio thread picks it
// up lock-free via ScopedTryLockType.

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <cmath>
#include <limits>

namespace sr {

// ── Thread-safe IR buffer transfer (JUCE ConvolutionDemo pattern) ──

struct BufferWithSampleRate
{
    BufferWithSampleRate() = default;

    BufferWithSampleRate(juce::AudioBuffer<float>&& bufferIn, double sampleRateIn)
        : buffer(std::move(bufferIn)), sampleRate(sampleRateIn) {}

    juce::AudioBuffer<float> buffer;
    double sampleRate = 0.0;
};

class BufferTransfer
{
public:
    void set(BufferWithSampleRate&& p)
    {
        const juce::SpinLock::ScopedLockType lock(mutex);
        buffer = std::move(p);
        newBuffer = true;
    }

    template <typename Fn>
    void get(Fn&& fn)
    {
        const juce::SpinLock::ScopedTryLockType lock(mutex);

        if (lock.isLocked() && newBuffer)
        {
            fn(buffer);
            newBuffer = false;
        }
    }

private:
    BufferWithSampleRate buffer;
    bool newBuffer = false;
    juce::SpinLock mutex;
};

// ── Cabinet Simulation Engine ──

inline constexpr int MAX_USER_IR_SAMPLES = 8192;

// Ceiling on one IR channel's energy (float sum of squares) — see
// isNormalisableIR(). A real cabinet IR sits around 1–100; this is ~3e32.
// JUCE sums the IR *after* resampling it to the session rate, which scales
// the energy by up to the rate ratio (96x for an 8 kHz IR in a 768 kHz
// session), so the ceiling keeps 2^20 of headroom below FLT_MAX rather than
// sitting on it: an IR accepted here must still normalise at any rate.
inline constexpr float MAX_IR_CHANNEL_ENERGY =
    std::numeric_limits<float>::max() / 1048576.0f;

// JUCE's Convolution (Trim::yes) cuts an IR down to the span between its
// first and last sample with |x| >= this, taken over every channel, BEFORE it
// normalises. The threshold is ABSOLUTE (-80 dBFS, not relative to the IR's
// peak): trimImpulseResponse() in juce_Convolution.cpp compares against
// Decibels::decibelsToGain(-80.0f), and an IR with no such sample in any
// channel comes out as one zero sample — IR mode plays silence. Computed the
// same way here so the two thresholds are the same float.
inline const float IR_TRIM_THRESHOLD = juce::Decibels::decibelsToGain(-80.0f);

// True when juce::dsp::Convolution can trim and normalise this IR into
// something audible (Trim::yes, Normalise::yes). Two ways it can't:
//  - Trimmed to nothing: no sample in any channel reaches IR_TRIM_THRESHOLD
//    (an all-zero IR, or one wholly below -80 dBFS).
//  - Normalised into garbage: normaliseImpulseResponse() takes the largest
//    per-channel FLOAT sum of squares and scales by 0.125/sqrt(sum). Finite
//    samples whose sum overflows to Inf turn that gain into 0 — silence. A
//    NaN/Inf sample (review S4) poisons the output itself: JUCE's jmax drops
//    a NaN channel sum, so the gain stays finite, but NaN × gain is NaN (and
//    an Inf sample's channel sum drives the gain to 0, and Inf × 0 is NaN).
// The energy is accumulated here exactly as JUCE does it — in float, sample by
// sample — and a NaN/Inf sample needs no separate test: it carries the sum to
// NaN/Inf, which fails the comparison. Each user-IR entry point (file load,
// state restore) rejects a failing IR outright rather than altering it.
inline bool isNormalisableIR(const juce::AudioBuffer<float>& buffer)
{
    bool survivesTrim = false;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        float energy = 0.0f;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            energy += data[i] * data[i];
            survivesTrim = survivesTrim || std::abs(data[i]) >= IR_TRIM_THRESHOLD;
        }
        if (!(energy <= MAX_IR_CHANNEL_ENERGY))  // also false for NaN
            return false;
    }
    return survivesTrim;
}

struct CabinetSim
{
    bool              enabled = true;
    std::atomic<bool> useIR { false };   // atomic: written on message thread, read on audio thread
    int               currentIRIndex = 0;

    void setEnabled(bool e) { enabled = e; }

    // Called from prepareToPlay (not concurrent with processBlock)
    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    // Called from audio thread only
    void processBlock(juce::dsp::AudioBlock<float>& block);

    // Called from message thread only (via AsyncUpdater)
    void setIRIndex(int newIndex);
    void loadStockIR(int stockIndex);
    void loadIRFromFile(const juce::File& file);
    void loadIRFromBuffer(juce::AudioBuffer<float>&& buffer, double sampleRate);

    // IR tail length in seconds (for getTailLengthSeconds)
    double getIRTailSeconds() const;

private:
    juce::dsp::Convolution convolution;
    BufferTransfer bufferTransfer;

    double currentSampleRate = 44100.0;
    double irNativeSampleRate = 48000.0;  // sample rate of the loaded IR
    int    currentIRLengthSamples = 0;
};

} // namespace sr
