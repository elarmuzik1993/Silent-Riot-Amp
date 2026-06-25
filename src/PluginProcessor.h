#pragma once
// ═══════════════════════════════════════════════════════════════
// SilentRiotProcessor — Spec Section 6 class hierarchy
// ═══════════════════════════════════════════════════════════════

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

#include "utils/VoltageScale.h"
#include "utils/Constants.h"
#include "utils/Parameters.h"
#include "utils/ParamSmoother.h"

#include "dsp/filters/FilterBank.h"
#include "dsp/filters/DynamicMillerLP.h"
#include "dsp/tubes/TriodeWaveshaper.h"
#include "dsp/tubes/PentodeWaveshaper.h"
#include "dsp/tubes/GridConduction.h"
#include "dsp/power/PowerSupplyModel.h"
#include "dsp/power/SagEnvelope.h"
#include "dsp/power/LTPSolver.h"
#include "dsp/cabinet/SpeakerCoupling.h"
#include "dsp/cabinet/OTSaturation.h"
#include "dsp/cabinet/CabinetSim.h"
#include "dsp/OversampleSwitcher.h"

class SilentRiotProcessor : public juce::AudioProcessor,
                            private juce::AsyncUpdater
{
public:
    SilentRiotProcessor();
    ~SilentRiotProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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

    // ── User IR management (message thread only) ──
    struct UserIREntry {
        juce::String name;
        double sampleRate = 0;
        int numChannels = 0;
        int numSamples = 0;
        juce::AudioBuffer<float> audioData;
    };

    // Apply a factory preset (sets gain/tone/volume/7W/cab + preset index).
    // Called on an explicit user selection (message thread) — NOT on state
    // restore, so user tweaks saved over a preset are preserved.
    void applyPreset(int presetIndex);

    void loadUserIR(const juce::File& file);
    void selectUserIR(int userIndex);  // 0-based index into userIRs
    int getNumUserIRs() const { return static_cast<int>(userIRs.size()); }
    const UserIREntry& getUserIR(int index) const { return userIRs[static_cast<size_t>(index)]; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void handleAsyncUpdate() override;  // deferred IR loading on message thread
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

    // Oversamples just the IR-path output limiter (tanh) so its harmonics
    // don't alias back into the band — the IIR path's limiter already runs
    // inside the main oversampled loop and doesn't need this (see
    // docs/handoff-IR-limiter-aliasing.md). Fixed 4x, independent of the
    // main engine's switchable oversample factor.
    std::unique_ptr<juce::dsp::Oversampling<float>> limiterOversampler;

    // Triode plate voltage cache (inter-sample)
    sr::TriodeState v1aTriode, v1bTriode;

    // Grid conduction per stage
    sr::GridConductionModel v1aGrid, v1bGrid, piGrid, el84GridA, el84GridB;

    // Parameter smoothers
    sr::ParamSmoother gainSmoother, toneSmoother, volumeSmoother;

    // State
    float el84BaseBias = sr::PENTODE_BIAS_15W_N;
    float lastShapeCoeffVal = -1.0f;            // shape coeff update threshold tracker
    float outputGainCompensation = 1.0f;   // scales cab output into tanh soft-clip range
    bool  psuFrozen    = false;
    double baseSampleRate = 44100.0;
    std::atomic<int> cachedIRIndex { 0 };   // last IR index seen by audio thread (written msg+audio)
    std::atomic<int> pendingIRIndex { -1 }; // deferred IR load (message thread)

public:
    // GUI level metering (atomic, updated per block from audio thread)
    std::atomic<float> inputLevelForGUI  { 0.0f };
    std::atomic<float> outputLevelForGUI { 0.0f };
    std::atomic<float> gridCurrentForGUI { 0.0f };

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
