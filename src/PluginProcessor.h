// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
// ═══════════════════════════════════════════════════════════════
// SilentRiotProcessor — Spec Section 6 class hierarchy
// ═══════════════════════════════════════════════════════════════

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <cstdint>

#include "utils/VoltageScale.h"
#include "utils/Constants.h"
#include "utils/Parameters.h"
#include "utils/ParamSmoother.h"
#include "utils/ParamSanitize.h"

#include "dsp/filters/FilterBank.h"
#include "dsp/filters/DynamicMillerLP.h"
#include "dsp/filters/IntegerDelay.h"
#include "dsp/tubes/TriodeWaveshaper.h"
#include "dsp/tubes/PentodeWaveshaper.h"
#include "dsp/tubes/GridConduction.h"
#include "dsp/power/PowerSupplyModel.h"
#include "dsp/power/SagEnvelope.h"
#include "dsp/power/LTPSolver.h"
#include "dsp/cabinet/SpeakerCoupling.h"
#include "dsp/cabinet/OTSaturation.h"
#include "dsp/cabinet/CabinetSim.h"
#include "dsp/cabinet/CabModeSwitcher.h"
#include "dsp/cabinet/UserIRState.h"
#include "dsp/effects/NoiseGate.h"
#include "dsp/effects/Boost.h"
#include "dsp/effects/SafetyLimiter.h"
#include "dsp/effects/Delay.h"
#include "dsp/effects/TempoSync.h"
#include "dsp/effects/Reverb.h"
#include "dsp/effects/StereoWidth.h"
#include "dsp/OversampleSwitcher.h"

class SilentRiotProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    SilentRiotProcessor();
    ~SilentRiotProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;   // host stop / reset: clear the effect tails (review B6)
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Host bypass: dry input delayed by the reported latency; flags the effect
    // tails stale, since processBlock (and so every effect) stops running.
    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Host blocks larger than prepareToPlay's maximum that were split (RT-5).
    // A host-contract break; counted so a test can tell a split from a single pass.
    int getOversizedBlockCount() const { return oversizedBlocks.load(std::memory_order_relaxed); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override;

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // GUI readout — called from editor timer (message thread)
    struct DSPReadout {
        float vBPlus, vScreen, vPi, vPreamp;
        float sagPercent, biasV, gridCurrentMA;
        int osFactor;
        bool is7W;
    };
    DSPReadout getDSPReadout() const;

    // ── Live tuner input capture ──────────────────────────────────
    // Audio thread writes the raw DI (channel-0) input into a lock-free ring;
    // the editor pulls the most-recent window on its timer and runs pitch
    // detection off the audio thread. Capture happens BEFORE the power-off
    // early-return so the tuner works with the amp silenced.
    // Copies up to maxN of the most recent captured samples into dest (oldest
    // first). Message thread only. Returns the number of samples written.
    int readLatestTunerSamples(float* dest, int maxN) const;

    // ── User IR management (message thread only) ──
    using UserIREntry = sr::UserIREntry;  // see dsp/cabinet/UserIRState.h

    // Returns false if the file can't be read or the list is already at
    // sr::MAX_USER_IRS — callers must not assume a new entry was appended.
    bool loadUserIR(const juce::File& file);
    void selectUserIR(int userIndex);  // 0-based index into userIRs
    void selectBuiltInCab();           // IR_INDEX 0: the IIR cab, convolution off
    int getNumUserIRs() const { return static_cast<int>(userIRs.size()); }
    const UserIREntry& getUserIR(int index) const { return userIRs[static_cast<size_t>(index)]; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    // Message thread, 20 Hz: publishes a latency the audio thread computed
    // (pendingLatency) to the host, and applies an IR_INDEX change (host
    // automation) — so the audio thread never calls the host or posts to the
    // message queue (RT-2, RT-3).
    void timerCallback() override;
    // Sync cabSim to IR_INDEX now (restore, timer, GUI: Async; prepareToPlay:
    // Now — the only caller that may, since processBlock can't be running there).
    void applyIRIndexParam(sr::IRLoad load);
    void resetDSPState();               // clear all filter/integrator/cached state
    // Recompute every sample-rate-dependent coefficient for the current base
    // rate + oversample factor (filters, Millers, PSU/sag/OT, grid-charge decay,
    // smoothers) and the latency. Shared by prepareToPlay and the
    // oversample-switch commit path to avoid silent drift between the two.
    // LatencyPublish::Now (prepareToPlay, message thread) reports the latency
    // to the host at once; Deferred (the oversample commit, audio thread)
    // only stores it in pendingLatency for timerCallback() — setLatencySamples()
    // calls every host listener synchronously, which the audio thread must not
    // do (RT-2).
    enum class LatencyPublish { Now, Deferred };
    void recalcRateDependentCoeffs(LatencyPublish publish);

    // User IR persistence
    std::vector<UserIREntry> userIRs;
    void saveUserIRsToState();
    void loadUserIRsFromState();

    // ── DSP state (matches spec Section 6 hierarchy) ──
    sr::FilterBank          filterBank;
    sr::DynamicMillerLP     v1a_miller, v1b_miller;
    sr::SpeakerCoupling     speakerCoupling;
    sr::OTSaturation        otSaturation;
    sr::PowerSupplyModel    psu;
    sr::SagEnvelope         sag;
    sr::LTPModel            ltpModel;
    sr::CabinetSim          cabSim;
    sr::OversampleSwitcher  oversampleSwitcher;

    // ── Effects module (run at base SR, outside the oversampled amp loop) ──
    // Pre-amp: gate + boost. Post-cab: delay + reverb. All default bypassed.
    sr::NoiseGate           noiseGate;
    sr::Boost               boost;
    sr::Delay               delay;
    sr::Reverb              reverb;
    sr::StereoWidth         stereoWidth;   // final MONO↔STEREO widener (stereo only)
    // Boost level smoothed at base SR (avoids zipper on the make-up gain).
    juce::SmoothedValue<float> boostLevelSm, boostTightSm, boostBrightSm, gateThreshSm;
    // Gain-staging trims, smoothed at base SR. processBlock skips the multiply
    // entirely while a trim sits at unity and is not ramping, which is what keeps
    // the dry path bit-identical at defaults.
    juce::SmoothedValue<float> inputTrimSm, outputTrimSm;

    // Oversamples just the IR-path output limiter (tanh) so its harmonics
    // don't alias back into the band — the IIR path's limiter already runs
    // inside the main oversampled loop and doesn't need this (see
    // docs/records/handoffs/handoff-IR-limiter-aliasing.md). Fixed 4x, independent of the
    // main engine's switchable oversample factor.
    std::unique_ptr<juce::dsp::Oversampling<float>> limiterOversampler;

    // Delays the IIR cab path by limiterOversampler's latency, so the latency
    // reported to the host (main + limiter) is true in both cab modes (review
    // B4). Mono/stereo bus only, hence 2 channels.
    sr::IntegerDelay<2, 16> iirPathAlign;

    // Owns the ACTIVE cab mode (CAB off / built-in / IR) and fades every
    // change between them; processBlock runs each block in its activeMode()
    // and resets the path it enters at the commit (IR-path plan Task 3).
    sr::CabModeSwitcher cabSwitcher;
    void enterCabMode(sr::CabMode entered, sr::CabMode left);   // the commit's resets
    int  cabFadeInHold(sr::CabMode entered, sr::CabMode left) const;   // gain-0 samples after it
    static constexpr int kCabOnsetMarginSamples = 32;   // past the IR's peak: its main lobe
    bool cabWaitLogged = false;       // one debug log per long WaitingForIR
    bool cabStartFresh = false;       // set by prepareToPlay: the next block may switch without a fade

    // Host-bypass passthrough: the dry input delayed by the reported latency, so
    // a bypassed plugin stays time-aligned. processBlock write()s every input
    // block into it, so the first bypassed block continues without a gap.
    sr::IntegerDelay<2, 32> bypassDelay;
    void syncBypassDelay();           // follow latency changes (oversample switch)
    // The latency the engine runs at, as last computed by
    // recalcRateDependentCoeffs() — the bypass line follows this, not
    // getLatencySamples(), which lags it by up to one timer tick after an
    // oversample switch. Audio thread (and prepareToPlay).
    int engineLatency = 0;
    // Latency waiting for timerCallback() to report it to the host.
    std::atomic<int> pendingLatency { 0 };

    // The block size prepareToPlay sized every buffer for. A host block larger
    // than this is split into views of at most this many samples (RT-5); one
    // at or below it goes straight through, bit-identical. 0 = not prepared.
    int preparedBlockSize = 0;
    std::atomic<int> oversizedBlocks { 0 };
    // processBlock / processBlockBypassed for a block of at most preparedBlockSize.
    void processChunk(juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void processChunkBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&);

    // Output peak meter (outputPeakForGUI): the held peak and its release, as
    // a natural-log decay per sample, -1 / (METER_PEAK_RELEASE_MS * fs), set in
    // prepareToPlay. A block of n samples decays it by exp(n * that), so the
    // ballistics follow the block actually processed, not the size the host
    // announced. Atomic only because reset() clears it and the project treats
    // reset() as callable from any thread; the audio thread uses it relaxed.
    std::atomic<float> meterPeak { 0.0f };
    float meterPeakLogDecayPerSample = -1.0f / (sr::METER_PEAK_RELEASE_MS * 0.001f * 44100.0f);
    // Folds channel 0 of a finished block (nullptr / n <= 0: nothing new) into
    // meterPeak and publishes it. Read-only on the samples.
    void publishOutputPeak(const float* ch0, int n);
    void clearOutputPeak();           // meterPeak and outputPeakForGUI to 0

    // Triode plate voltage cache (inter-sample)
    sr::TriodeState v1aTriode, v1bTriode;

    // Grid conduction per stage
    sr::GridConductionModel v1aGrid, v1bGrid, piGrid, el84GridA, el84GridB;

    // Parameter smoothers
    sr::ParamSmoother gainSmoother, toneSmoother, volumeSmoother;

    // Amp knob read, NaN → fallback, clamped to 0..1 (review S1).
    float readAmpKnob(const juce::String& id, float fallback) const;
    // Sets the EL84 base bias and re-derives the sag reference from it.
    void setEl84Bias(float bias);

    // State
    float el84BaseBias = sr::PENTODE_BIAS_15W_N;
    float lastSagQuiescentBias = 0.0f;          // forces a sag-reference recompute on first block
    float lastShapeCoeffVal = -1.0f;            // shape coeff update threshold tracker
    float outputGainCompensation = sr::OUTPUT_MAKEUP_GAIN;  // scales cab output into tanh soft-clip range
    bool  psuFrozen    = false;
    bool  widenerWasActive = false;   // edge-detect widener on/off for tail clear
    bool  delayWasOn  = false;        // edge-detect delay/reverb off→on for restart() (review B6)
    bool  reverbWasOn = false;
    bool  delayFading  = false;       // switched off, wet still ramping to 0 (review B6)
    bool  reverbFading = false;
    // Set by reset(), the power-off / tuner-mute return and host bypass; the next
    // audible block clears every effect tail on the audio thread (review B6).
    std::atomic<bool> tailsStale { false };
    double baseSampleRate = 44100.0;
    // The IR_INDEX cabSim was last synced to: timerCallback() applies the
    // parameter when it differs (RT-3). The audio thread never touches it;
    // every access is under irStateLock (below), so the atomic is only
    // belt and braces.
    std::atomic<int> cachedIRIndex { 0 };
    // Guards userIRs, cachedIRIndex and the cab's IR state against each other
    // on the non-audio threads. The timer polls IR_INDEX all the time (the old
    // AsyncUpdater only fired on a change), so a host that restores state or
    // prepares off the message thread would otherwise race it: a tick could
    // index a userIRs being reassigned, or queue a load beside
    // installIRNow(). Held by the timer's IR branch, setStateInformation's
    // restore, prepareToPlay and the GUI's loadUserIR/selectUserIR/
    // selectBuiltInCab. NEVER on the audio thread (RT-3): processBlock only
    // reads useIR and the try-lock bufferTransfer. Recursive, so the GUI
    // paths may call applyIRIndexParam() under it.
    juce::CriticalSection irStateLock;

    // ── Live tuner ring buffer (single-producer audio / single-consumer GUI) ──
    // Power-of-two size; the writer stores a monotonically increasing total
    // sample count and the reader grabs the last N samples ending there. Each
    // sample is a relaxed atomic (RT-4), so overlapping access is well defined;
    // the release/acquire pair on tunerWritePos orders the samples against the
    // count. A window the writer laps mid-copy can still mix old and new
    // samples — that is tolerated by design: the next 30Hz frame recovers a
    // clean window.
    static constexpr int kTunerRingSize = 8192;          // must be a power of two
    static constexpr int kTunerRingMask = kTunerRingSize - 1;
    static_assert(std::atomic<float>::is_always_lock_free,
                  "the tuner ring must stay lock-free on the audio thread");
    std::array<std::atomic<float>, kTunerRingSize> tunerRing {}; // value-initialised to 0
    std::atomic<uint64_t> tunerWritePos { 0 };
    void pushTunerSamples(const float* data, int n);     // audio thread only

public:
    // GUI level metering (atomic, updated per block from audio thread)
    std::atomic<float> inputLevelForGUI  { 0.0f };
    std::atomic<float> outputLevelForGUI { 0.0f };
    std::atomic<float> outputPeakForGUI  { 0.0f };   // block peak of channel 0 (post-trim), with release
    std::atomic<float> gridCurrentForGUI { 0.0f };   // normalized current, block peak of the EL84 grids

    // GUI DSP-state telemetry — published once per block by the audio thread,
    // read by the editor timer. Atomic snapshots avoid torn reads of the
    // non-atomic PSU/sag floats they mirror. Initialized to nominal so the
    // readout is sane before the first processBlock.
    std::atomic<float> vBPlusForGUI   { sr::V_POWER_NOMINAL_N };
    std::atomic<float> vScreenForGUI  { sr::V_SCREEN_NOMINAL_N };
    std::atomic<float> vPiForGUI      { sr::V_PI_NOMINAL_N };
    std::atomic<float> vPreampForGUI  { sr::V_PREAMP_NOMINAL_N };
    std::atomic<float> sagOffsetForGUI{ 0.0f };
    std::atomic<float> el84BiasForGUI { sr::PENTODE_BIAS_15W_N };
    std::atomic<int>   osFactorForGUI { 1 };

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SilentRiotProcessor)
};
