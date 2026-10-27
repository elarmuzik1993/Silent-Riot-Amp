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
                            private juce::AsyncUpdater
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

    // Apply a factory preset (sets gain/tone/volume/7W/cab + preset index).
    // Called on an explicit user selection (message thread) — NOT on state
    // restore, so user tweaks saved over a preset are preserved.
    void applyPreset(int presetIndex);

    // Returns false if the file can't be read or the list is already at
    // sr::MAX_USER_IRS — callers must not assume a new entry was appended.
    bool loadUserIR(const juce::File& file);
    void selectUserIR(int userIndex);  // 0-based index into userIRs
    int getNumUserIRs() const { return static_cast<int>(userIRs.size()); }
    const UserIREntry& getUserIR(int index) const { return userIRs[static_cast<size_t>(index)]; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void handleAsyncUpdate() override;  // deferred IR loading on message thread
    // Sync cabSim to IR_INDEX now (restore: Async; prepareToPlay: Now — the
    // only caller that may, since processBlock can't be running there).
    void applyIRIndexParam(sr::IRLoad load);
    void resetDSPState();               // clear all filter/integrator/cached state
    // Recompute every sample-rate-dependent coefficient for the current base
    // rate + oversample factor (filters, Millers, PSU/sag/OT, grid-charge decay,
    // smoothers) and republish DAW latency. Shared by prepareToPlay and the
    // oversample-switch commit path to avoid silent drift between the two.
    void recalcRateDependentCoeffs();

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
    // docs/handoff-IR-limiter-aliasing.md). Fixed 4x, independent of the
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
    std::atomic<int> cachedIRIndex { 0 };   // last IR index seen by audio thread (written msg+audio)
    std::atomic<int> pendingIRIndex { -1 }; // deferred IR load (message thread)

    // ── Live tuner ring buffer (single-producer audio / single-consumer GUI) ──
    // Power-of-two size; the writer stores a monotonically increasing total
    // sample count and the reader grabs the last N samples ending there. Torn
    // reads are harmless — the next 30Hz frame recovers a clean window.
    static constexpr int kTunerRingSize = 8192;          // must be a power of two
    static constexpr int kTunerRingMask = kTunerRingSize - 1;
    std::array<float, kTunerRingSize> tunerRing {};      // zero-initialised
    std::atomic<uint64_t> tunerWritePos { 0 };
    void pushTunerSamples(const float* data, int n);     // audio thread only

public:
    // GUI level metering (atomic, updated per block from audio thread)
    std::atomic<float> inputLevelForGUI  { 0.0f };
    std::atomic<float> outputLevelForGUI { 0.0f };
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
