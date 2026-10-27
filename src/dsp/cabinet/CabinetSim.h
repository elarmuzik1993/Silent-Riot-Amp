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
#include <algorithm>
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

    // Drops a buffer set() but not yet picked up by get(). Blocking, like
    // set(): never from the audio thread.
    void clear()
    {
        const juce::SpinLock::ScopedLockType lock(mutex);
        buffer = {};
        newBuffer = false;
    }

private:
    BufferWithSampleRate buffer;
    bool newBuffer = false;
    juce::SpinLock mutex;
};

// ── Cabinet Simulation Engine ──

inline constexpr int MAX_USER_IR_SAMPLES = 8192;

// Ceiling on one IR channel's energy (float sum of squares) — see
// isSafeToNormaliseIR(). A real cabinet IR sits around 1–100; this is ~3e32.
// JUCE sums the IR *after* resampling it to the session rate, which scales
// the energy by up to the rate ratio (96x for an 8 kHz IR in a 768 kHz
// session), so the ceiling keeps 2^20 of headroom below FLT_MAX rather than
// sitting on it: an IR accepted here must still normalise at any rate.
inline constexpr float MAX_IR_CHANNEL_ENERGY =
    std::numeric_limits<float>::max() / 1048576.0f;

// JUCE's Convolution (Trim::yes) cuts an IR down to the span between its
// first and last sample with |x| >= this, taken over every channel together,
// BEFORE it normalises. The threshold is ABSOLUTE (-80 dBFS, not relative to
// the IR's peak): trimImpulseResponse() in juce_Convolution.cpp compares
// against Decibels::decibelsToGain(-80.0f), and an IR with no such sample in
// any channel comes out as one zero sample per channel — IR mode plays
// silence. constexpr, so no TU can read it before a dynamic initialiser has
// run (a zero-initialised read would accept every IR); 1.0e-4f is the same
// float as decibelsToGain(-80.0f), pinned bit for bit by a test.
inline constexpr float IR_TRIM_THRESHOLD = 1.0e-4f;

// True when juce::dsp::Convolution's normalisation (Normalise::yes) cannot be
// pushed into garbage by this IR. normaliseImpulseResponse() takes the largest
// per-channel FLOAT sum of squares and scales every channel by
// 0.125/sqrt(sum). Finite samples whose sum overflows to Inf turn that gain
// into 0 (silence); an Inf sample does the same and Inf × 0 is NaN; a NaN
// sample survives as NaN × gain (jmax drops a NaN channel sum, so the gain
// itself stays finite). The energy is accumulated here exactly as JUCE does it
// — in float, sample by sample — and a NaN/Inf sample needs no separate test:
// it carries the sum to NaN/Inf, which fails the comparison.
//
// Deliberately conservative: it checks the whole buffer, but JUCE trims before
// it normalises, so a NaN or a huge sample confined to the head or tail that
// the -80 dBFS trim cuts off would in fact be discarded. Rejecting those too
// is simpler and costs nothing a real IR needs.
//
// This is the predicate every user-IR path applies, restore included: an IR
// that fails it poisons or kills the output. Compare isAudibleOnEveryChannel().
inline bool isSafeToNormaliseIR(const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        float energy = 0.0f;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            energy += data[i] * data[i];
        if (!(energy <= MAX_IR_CHANNEL_ENERGY))  // also false for NaN
            return false;
    }
    return true;
}

// True when EVERY channel has at least one sample at or above
// IR_TRIM_THRESHOLD, i.e. each channel JUCE convolves with has something left
// after the trim. JUCE's own trim is a union over channels, but each IR
// channel is played on its own output channel — juce::dsp::Convolution's
// MultichannelEngine convolves output channel i with IR channel
// min(i, numIRChannels - 1) — so:
//  - a mono IR feeds both channels of a stereo bus;
//  - a stereo IR on a stereo bus: IR channel 0 → left, channel 1 → right
//    (the amp's output is dual mono, so a dead channel is one silent side);
//  - a stereo IR on a MONO bus (the plugin accepts mono): only IR channel 0
//    is ever convolved and channel 1 is ignored — a stereo IR whose channel 0
//    is dead plays pure silence there.
// A zero-channel or zero-sample buffer fails too (JUCE's fixNumChannels()
// would turn it into a unity impulse, i.e. pass-through — not an IR).
//
// Required at LOAD (file browser, drag-and-drop, CabinetSim::loadIRFromFile),
// where refusing just leaves the user's IR list as it was. NOT required at
// state restore: a silent IR is harmless (it plays silence, as it always
// did), and refusing one there would drop it and every later slot.
inline bool isAudibleOnEveryChannel(const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() < 1)
        return false;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        const float* end = data + buffer.getNumSamples();
        if (std::none_of(data, end, [](float x) { return std::abs(x) >= IR_TRIM_THRESHOLD; }))
            return false;
    }
    return true;
}

// The accept criterion for a NEWLY loaded IR: safe to normalise, and
// audible on every channel.
inline bool isLoadableIR(const juce::AudioBuffer<float>& buffer)
{
    return isSafeToNormaliseIR(buffer) && isAudibleOnEveryChannel(buffer);
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

    // Installs an IR into the convolution on the CALLING thread, so the very
    // next processBlock already convolves with it — loadIRFromBuffer() leaves
    // that to the audio thread and JUCE's background loader, which runs the
    // pass-through engine until the real one is built, then crossfades at a
    // wall-clock time. Any load still waiting in bufferTransfer is dropped.
    // Allocates and builds the FFT engine: must NOT run concurrently with
    // processBlock (prepareToPlay only), and needs a prior prepare().
    void installIRNow(juce::AudioBuffer<float>&& buffer, double sampleRate);

    // IR tail length in seconds (for getTailLengthSeconds)
    double getIRTailSeconds() const;

private:
    // Queues an IR on JUCE's loader with this class's fixed load options.
    void queueConvolutionLoad(juce::AudioBuffer<float>&& buffer, double sampleRate);

    juce::dsp::Convolution convolution;
    BufferTransfer bufferTransfer;
    juce::dsp::ProcessSpec spec { 44100.0, 512, 2 };   // as last passed to prepare()
    bool prepared = false;                              // prepare() has run (installIRNow needs it)

    double currentSampleRate = 44100.0;
    double irNativeSampleRate = 48000.0;  // sample rate of the loaded IR
    int    currentIRLengthSamples = 0;
};

} // namespace sr
