#pragma once
// ═══════════════════════════════════════════════════════════════
// TinyTerrorProcessor — Spec Section 6 class hierarchy
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

class TinyTerrorProcessor : public juce::AudioProcessor,
                            private juce::AsyncUpdater
{
public:
    TinyTerrorProcessor();
    ~TinyTerrorProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
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

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void handleAsyncUpdate() override;  // deferred IR loading on message thread

    // ── DSP state (matches spec Section 6 hierarchy) ──
    tt::FilterBank          filterBank;
    tt::DynamicMillerLP     v1a_miller, v1b_miller;
    tt::SpeakerCoupling     speakerCoupling;
    tt::OTSaturation        otSaturation;
    tt::PowerSupplyModel    psu;
    tt::SagEnvelope         sag;
    tt::LTPModel            ltpModel;
    tt::CabinetSim          cabSim;
    tt::OversampleSwitcher  oversampleSwitcher;

    // Grid conduction per stage
    tt::GridConductionModel v1aGrid, v1bGrid, piGrid, el84GridA, el84GridB;

    // Parameter smoothers
    tt::ParamSmoother gainSmoother, toneSmoother, volumeSmoother;

    // State
    float el84BaseBias = tt::PENTODE_BIAS_15W_N;
    float outputGainCompensation = 2.0f;  // tuned so max cab output ≈ ±1.0 after tanh
    bool  psuFrozen    = false;
    double baseSampleRate = 44100.0;
    int   cachedIRIndex = 0;              // last IR index seen by audio thread
    std::atomic<int> pendingIRIndex { -1 }; // deferred IR load (message thread)

public:
    // GUI level metering (atomic, updated per block from audio thread)
    std::atomic<float> inputLevelForGUI  { 0.0f };
    std::atomic<float> outputLevelForGUI { 0.0f };

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TinyTerrorProcessor)
};
