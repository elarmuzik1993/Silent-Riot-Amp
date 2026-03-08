#pragma once
// ═══════════════════════════════════════════════════════════════
// TinyTerrorProcessor — Spec Section 6 class hierarchy
// ═══════════════════════════════════════════════════════════════

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

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
#include "dsp/cabinet/CabinetSim.h"
#include "dsp/OversampleSwitcher.h"

class TinyTerrorProcessor : public juce::AudioProcessor
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

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── DSP state (matches spec Section 6 hierarchy) ──
    tt::FilterBank          filterBank;
    tt::DynamicMillerLP     v1a_miller, v1b_miller;
    tt::SpeakerCoupling     speakerCoupling;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TinyTerrorProcessor)
};
