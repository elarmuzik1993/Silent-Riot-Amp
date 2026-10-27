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
// The 2^20 of headroom below FLT_MAX dates from when JUCE normalised the IR
// after resampling it (up to 96x the energy); the value is kept as it was so
// the set of IRs that load and restore did not change with Task 2.
inline constexpr float MAX_IR_CHANNEL_ENERGY =
    std::numeric_limits<float>::max() / 1048576.0f;

// JUCE's Convolution (Trim::yes) cuts an IR down to the span between its
// first and last sample with |x| >= this, taken over every channel together.
// It trims the LEVEL-MATCHED copy CabinetSim hands it, not the raw file: the
// cut sits at -80 dBFS minus the match gain relative to the raw IR's scale —
// -62…-71 dB for the peak-normalised fixtures, which are turned down 9–18 dB —
// so a little more of a quiet onset or tail goes than at a raw -80 dBFS (and
// less for an IR that is turned up). The threshold is ABSOLUTE (not relative
// to the IR's peak): trimImpulseResponse() in juce_Convolution.cpp compares
// against Decibels::decibelsToGain(-80.0f), and an IR with no such sample in
// any channel comes out as one zero sample per channel — IR mode plays
// silence. constexpr, so no TU can read it before a dynamic initialiser has
// run (a zero-initialised read would accept every IR); 1.0e-4f is the same
// float as decibelsToGain(-80.0f), pinned bit for bit by a test.
inline constexpr float IR_TRIM_THRESHOLD = 1.0e-4f;

// True when CabinetSim's level match (IRLevel.h) can measure and scale this
// IR. The name dates from JUCE's Normalise::yes, which the plugin no longer
// uses (IR-path plan Task 2); the band-power metric that replaced it needs the
// same guard. A NaN or Inf sample makes the band power non-finite, so the
// level match falls back to a gain of 1 and the NaN/Inf reaches the
// convolution, which turns IR mode into NaN or silence. The energy ceiling is
// no longer strictly needed — the metric sums in double and scales a huge IR
// down, where JUCE's float normalisation overflowed to a gain of 0 — but is
// kept so the set of IRs that load and restore is the same as before Task 2.
// The energy is accumulated in float, sample by sample, and a NaN/Inf sample
// needs no separate test: it carries the sum to NaN/Inf, which fails the
// comparison.
//
// Deliberately conservative: it checks the whole buffer, including any head or
// tail JUCE's -80 dBFS trim would cut off. Rejecting those too is simpler and
// costs nothing a real IR needs.
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

// The accept criterion for a NEWLY loaded IR: safe to level-match, and
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
    // Scales an IR in place to the built-in cab's 100 Hz–5 kHz band level
    // (IRLevel.h): channel-averaged band power at the IR's native rate, one
    // gain for every channel. Every load path calls it on the copy it hands
    // to the convolution, off the audio thread.
    void matchBuiltInCabLevel(juce::AudioBuffer<float>& buffer, double sampleRate);

    // Queues an IR on JUCE's loader with this class's fixed load options
    // (Normalise::no — the level is already matched).
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
