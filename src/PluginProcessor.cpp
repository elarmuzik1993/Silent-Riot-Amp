#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <algorithm>

using namespace tt;

TinyTerrorProcessor::TinyTerrorProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Init grid conduction models (Spec §4.1.1)
    v1aGrid  = makeV1aGrid();
    v1bGrid  = makeV1bGrid();
    piGrid   = makePIGrid();
    el84GridA = makeEL84Grid();
    el84GridB = makeEL84Grid();

    // Init LTP model (Spec §3.5)
    ltpModel.init();

    // Init PSU (Spec §3.11)
    psu.init();

    // Init speaker coupling (Spec §3.9)
    speakerCoupling.init();

    // Init Miller caps (Spec §3.3)
    v1a_miller.init(CGP_12AX7, CGK_12AX7, V1A_RSOURCE_REAL, StageGains::V1A_GAIN);
    v1b_miller.init(CGP_12AX7, CGK_12AX7, V1B_RSOURCE_BASE_REAL, StageGains::V1B_GAIN);
}

TinyTerrorProcessor::~TinyTerrorProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout TinyTerrorProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Knobs: internal 0–1, UI displays 0–10 (see UI spec §7.1)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::GAIN, 1 }, "Gain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.6f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::TONE, 1 }, "Tone",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::VOLUME, 1 }, "Volume",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ Params::IDs::HALF_POWER, 1 }, "7W Mode", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ Params::IDs::CAB_SIM, 1 }, "Cab Sim", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ Params::IDs::POWER, 1 }, "Power", true));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ Params::IDs::OVERSAMPLE, 1 }, "Oversample",
        juce::StringArray{ "2x", "4x", "8x" }, 1));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ Params::IDs::PRESET_IDX, 1 }, "Preset", 0, 4, 0));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ Params::IDs::IR_INDEX, 1 }, "IR Selection", 0, 32, 0));

    return { params.begin(), params.end() };
}

void TinyTerrorProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;

    oversampleSwitcher.init(getTotalNumOutputChannels(), samplesPerBlock);
    int osFactor = oversampleSwitcher.activeFactor();
    float effectiveSR = static_cast<float>(sampleRate) * osFactor;

    filterBank.recalcAll(static_cast<float>(sampleRate), osFactor);
    v1a_miller.recalc(effectiveSR);
    v1b_miller.recalc(effectiveSR);
    speakerCoupling.recalcFilters(effectiveSR);
    psu.recalcDecayCoeffs(effectiveSR);
    sag.recalcCoeffs(effectiveSR);
    otSaturation.recalcCoeffs(effectiveSR);

    // Grid conduction charge decay
    recalcGridChargeDecay(v1aGrid,  0.022f, effectiveSR);
    recalcGridChargeDecay(v1bGrid,  0.022f, effectiveSR);
    recalcGridChargeDecay(piGrid,   0.047f, effectiveSR);
    recalcGridChargeDecay(el84GridA, 0.100f, effectiveSR);
    recalcGridChargeDecay(el84GridB, 0.100f, effectiveSR);

    // Report oversampler latency to DAW
    setLatencySamples(static_cast<int>(oversampleSwitcher.active()->getLatencyInSamples()));

    // Cabinet IR convolution engine (runs at base SR, after downsample)
    cabSim.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    // Smoothers run at block rate (base SR / block size), NOT oversampled rate
    float controlRate = static_cast<float>(sampleRate) / static_cast<float>(samplesPerBlock);
    gainSmoother.init(5.0f, controlRate);
    toneSmoother.init(5.0f, controlRate);
    volumeSmoother.init(5.0f, controlRate);
}

void TinyTerrorProcessor::releaseResources() {}

bool TinyTerrorProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    auto mainIn  = layouts.getMainInputChannelSet();
    auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    return mainIn == mainOut;
}

void TinyTerrorProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Read parameters
    float gainParam = apvts.getRawParameterValue(Params::IDs::GAIN)->load();
    float toneParam = apvts.getRawParameterValue(Params::IDs::TONE)->load();
    float volParam  = apvts.getRawParameterValue(Params::IDs::VOLUME)->load();
    bool  is7W      = apvts.getRawParameterValue(Params::IDs::HALF_POWER)->load() > 0.5f;
    bool  cabOn     = apvts.getRawParameterValue(Params::IDs::CAB_SIM)->load() > 0.5f;
    bool  powerOn   = apvts.getRawParameterValue(Params::IDs::POWER)->load() > 0.5f;
    int   irIndex   = static_cast<int>(apvts.getRawParameterValue(Params::IDs::IR_INDEX)->load());

    // Master power bypass — pass audio through unprocessed
    if (!powerOn) return;

    el84BaseBias = is7W ? PENTODE_BIAS_7W_N : PENTODE_BIAS_15W_N;
    cabSim.setEnabled(cabOn);

    // Detect IR parameter changes
    if (irIndex != cachedIRIndex)
    {
        cabSim.setIRIndex(irIndex);
        cachedIRIndex = irIndex;
    }

    // IR mode: cab on AND convolution active (IR_INDEX > 0)
    bool irMode = cabOn && cabSim.useIR;

    // Smooth parameters
    gainSmoother.setTarget(gainParam);
    toneSmoother.setTarget(toneParam);
    volumeSmoother.setTarget(volParam);

    // Update modulated filters
    float toneFreq = TONE_MIN_FREQ + toneSmoother.current * (TONE_MAX_FREQ - TONE_MIN_FREQ);
    filterBank.updateModulated(toneFreq, GAIN_LOADING_LP_FREQ);

    // Oversampled processing (Spec §2.5 processBlock)
    auto* oversampler = oversampleSwitcher.active();
    auto osBlock = oversampler->processSamplesUp(juce::dsp::AudioBlock<float>(buffer));

    for (size_t i = 0; i < osBlock.getNumSamples(); ++i)
    {
        if (!psuFrozen) psu.update();
        psu.resetCurrentDraw();

        // Read supply voltages
        float vPreamp = psu.vPreamp();
        float vPi     = psu.vPi();
        float vPlate  = psu.vPower();
        float vScreen = psu.vScreen();

        // Process mono from channel 0
        float x = osBlock.getSample(0, static_cast<int>(i)) * VS::V_GUITAR;

        // ── Stage 1: V1a ──
        x = filterBank.inputHP.process(x);
        x = applyGridConduction(x, v1aGrid);
        float v1a_in = x;
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, StageGains::V1A_GAIN);
        x = std::clamp(x, -vPreamp * HEADROOM_FACTOR, vPreamp * HEADROOM_FACTOR);
        x = filterBank.v1a_cathodeBypass.process(x);
        x = filterBank.v1a_couplingHP.process(x);
        float v1aGainEst = (std::abs(v1a_in) > 1e-8f) ? std::abs(x) / std::abs(v1a_in) : StageGains::V1A_GAIN;
        x = v1a_miller.process(x, v1aGainEst, filterBank.effectiveSR);
        psu.reportPreampLevel(x);

        // ── Gain Pot Gang A ──
        float gainA = audioTaper(gainSmoother.current);
        x *= gainA;

        // ── Stage 2: V1b ──
        x = applyGridConduction(x, v1bGrid);
        float v1b_in = x;
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, StageGains::V1B_GAIN);
        x = std::clamp(x, -vPreamp * HEADROOM_FACTOR, vPreamp * HEADROOM_FACTOR);
        x = filterBank.v1b_cathodeBypass.process(x);
        x = filterBank.v1b_couplingHP.process(x);
        float gainPotR = (1.0f - gainA) * GAIN_POT_R_REAL;
        v1b_miller.rSource_real = V1B_RSOURCE_BASE_REAL + gainPotR;
        float v1bGainEst = (std::abs(v1b_in) > 1e-8f) ? std::abs(x) / std::abs(v1b_in) : StageGains::V1B_GAIN;
        x = v1b_miller.process(x, v1bGainEst, filterBank.effectiveSR);
        psu.reportPreampLevel(x);

        // ── Gain Pot Gang B ──
        float gainB = audioTaper(gainSmoother.current);
        x *= gainB;
        x = filterBank.gainLoadingLP.process(x);

        // ── Stage 3: Phase Inverter (LTP) ──
        x = applyGridConduction(x, piGrid);
        LTPOutput ltp_out = solveLTP(x, vPi, ltpModel);
        float piA = filterBank.pi_couplingHP_A.process(ltp_out.piA);
        float piB = filterBank.pi_couplingHP_B.process(ltp_out.piB);
        psu.reportPiLevel(std::max(std::abs(piA), std::abs(piB)));

        // ── Tone Control (Vox cut — differential only) ──
        float diff = piA - piB;
        float sum  = piA + piB;
        float filtDiff = filterBank.toneCutFilter.process(diff);
        float toneAmt = 1.0f - toneSmoother.current;
        float outDiff = diff + (filtDiff - diff) * toneAmt;
        piA = (sum + outDiff) * 0.5f;
        piB = (sum - outDiff) * 0.5f;

        // ── Master Volume ──
        float vol = audioTaper(volumeSmoother.current);
        piA *= vol;
        piB *= vol;

        // ── Stage 4: Power Amp (EL84 push-pull) ──
        float effectiveBias = el84BaseBias + sag.biasOffset();
        piA = applyGridConduction(piA, el84GridA);
        piB = applyGridConduction(piB, el84GridB);
        float condA = softConductionCurve(piA, effectiveBias);
        float condB = softConductionCurve(piB, effectiveBias);
        PentodeOutput ppA = pentodeWaveshaper(piA, effectiveBias, vPlate, vScreen);
        PentodeOutput ppB = pentodeWaveshaper(piB, effectiveBias, vPlate, vScreen);
        float powerOut = ppA.plateCurrent * condA - ppB.plateCurrent * condB;

        psu.reportPowerDirect(
            std::abs(ppA.plateCurrent) + std::abs(ppB.plateCurrent),
            ppA.screenCurrent + ppB.screenCurrent);
        sag.update(powerOut);

        // ── Speaker coupling + OT ──
        float otIn = speakerCoupling.process(powerOut);
        float otSat = otSaturation.process(otIn);
        float otOut = filterBank.ot_leakageLP.process(otSat);

        // ── Cabinet Sim (IIR path only — IR path runs after downsample) ──
        float cabOut = otOut;
        if (cabSim.enabled && !irMode) {
            cabOut = filterBank.cab_presence.process(cabOut);
            cabOut = filterBank.cab_rolloff.process(cabOut);
            cabOut = filterBank.cab_notch.process(cabOut);
        }

        // ── Output: scale + limiter (skip in IR mode — applied after convolution) ──
        float finalOut = cabOut;
        if (!irMode) {
            finalOut *= outputGainCompensation;
            finalOut = std::tanh(finalOut);
        }

        osBlock.setSample(0, static_cast<int>(i), finalOut);
        // Copy to stereo
        if (osBlock.getNumChannels() > 1)
            osBlock.setSample(1, static_cast<int>(i), finalOut);
    }

    juce::dsp::AudioBlock<float> outputBlock(buffer);
    oversampler->processSamplesDown(outputBlock);

    // ── IR Convolution path (runs at base SR, after downsample) ──
    if (irMode)
    {
        cabSim.processBlock(outputBlock);

        // Apply output scaling + tanh limiter after convolution
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* samples = buffer.getWritePointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                samples[i] *= outputGainCompensation;
                samples[i] = std::tanh(samples[i]);
            }
        }
    }
}

// ── Boilerplate ──
juce::AudioProcessorEditor* TinyTerrorProcessor::createEditor() { return new TinyTerrorEditor(*this); }
bool TinyTerrorProcessor::hasEditor() const { return true; }
const juce::String TinyTerrorProcessor::getName() const { return JucePlugin_Name; }
bool TinyTerrorProcessor::acceptsMidi() const { return false; }
bool TinyTerrorProcessor::producesMidi() const { return false; }
bool TinyTerrorProcessor::isMidiEffect() const { return false; }
double TinyTerrorProcessor::getTailLengthSeconds() const { return cabSim.getIRTailSeconds(); }
int TinyTerrorProcessor::getNumPrograms() { return 1; }
int TinyTerrorProcessor::getCurrentProgram() { return 0; }
void TinyTerrorProcessor::setCurrentProgram(int) {}
const juce::String TinyTerrorProcessor::getProgramName(int) { return {}; }
void TinyTerrorProcessor::changeProgramName(int, const juce::String&) {}

void TinyTerrorProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void TinyTerrorProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TinyTerrorProcessor(); }
