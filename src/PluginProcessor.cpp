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

TinyTerrorProcessor::~TinyTerrorProcessor()
{
    cancelPendingUpdate(); // prevent callback after destruction
}

void TinyTerrorProcessor::handleAsyncUpdate()
{
    // Runs on message thread — safe to allocate, do file I/O, acquire locks.
    int idx = pendingIRIndex.exchange(-1);
    if (idx < 0) return;

    if (idx == 0)
    {
        // IIR mode — disable convolution
        cabSim.setIRIndex(0);
        return;
    }

    // User IR (1-based index → 0-based into userIRs)
    int userIdx = idx - 1;
    if (userIdx >= 0 && userIdx < static_cast<int>(userIRs.size()))
    {
        auto& ir = userIRs[static_cast<size_t>(userIdx)];
        juce::AudioBuffer<float> bufCopy(ir.audioData);
        cabSim.loadIRFromBuffer(std::move(bufCopy), ir.sampleRate);
    }
}

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

    // Reload active user IR after prepare (convolution state was reset)
    int irIdx = static_cast<int>(apvts.getRawParameterValue(tt::Params::IDs::IR_INDEX)->load());
    if (irIdx > 0 && irIdx <= static_cast<int>(userIRs.size()))
    {
        auto& ir = userIRs[static_cast<size_t>(irIdx - 1)];
        juce::AudioBuffer<float> bufCopy(ir.audioData);
        cabSim.loadIRFromBuffer(std::move(bufCopy), ir.sampleRate);
        cachedIRIndex = irIdx;
    }

    // Smoothers run per-sample at the oversampled rate
    gainSmoother.init(5.0f, effectiveSR);
    toneSmoother.init(5.0f, effectiveSR);
    volumeSmoother.init(5.0f, effectiveSR);
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

    // Detect IR parameter changes — defer actual loading to message thread
    if (irIndex != cachedIRIndex)
    {
        cachedIRIndex = irIndex;
        pendingIRIndex.store(irIndex);
        triggerAsyncUpdate();  // handleAsyncUpdate() calls cabSim.setIRIndex() on message thread
    }

    // IR mode: cab on AND convolution has an active IR.
    // useIR is atomic — set by message thread after IR is loaded into bufferTransfer.
    bool irMode = cabOn && cabSim.useIR.load();

    // Set smoother targets (per-sample tick happens inside the loop)
    gainSmoother.setTarget(gainParam);
    toneSmoother.setTarget(toneParam);
    volumeSmoother.setTarget(volParam);

    // ── Input level metering (RMS before processing) ──
    {
        const int n = buffer.getNumSamples();
        if (n > 0)
        {
            const float* ch0 = buffer.getReadPointer(0);
            float sumSq = 0.0f;
            for (int i = 0; i < n; ++i)
                sumSq += ch0[i] * ch0[i];
            inputLevelForGUI.store(std::sqrt(sumSq / static_cast<float>(n)), std::memory_order_relaxed);
        }
    }

    // ── Oversample mode switching ──
    {
        int osParam = static_cast<int>(apvts.getRawParameterValue(Params::IDs::OVERSAMPLE)->load());
        if (osParam != oversampleSwitcher.currentMode) {
            oversampleSwitcher.currentMode = osParam;
            oversampleSwitcher.oversamplers[static_cast<size_t>(osParam)]->reset();

            int osFactor = oversampleSwitcher.activeFactor();
            float effectiveSR = static_cast<float>(baseSampleRate) * osFactor;

            filterBank.recalcAll(static_cast<float>(baseSampleRate), osFactor);
            v1a_miller.recalc(effectiveSR);
            v1b_miller.recalc(effectiveSR);
            speakerCoupling.recalcFilters(effectiveSR);
            psu.recalcDecayCoeffs(effectiveSR);
            sag.recalcCoeffs(effectiveSR);
            otSaturation.recalcCoeffs(effectiveSR);

            recalcGridChargeDecay(v1aGrid,  0.022f, effectiveSR);
            recalcGridChargeDecay(v1bGrid,  0.022f, effectiveSR);
            recalcGridChargeDecay(piGrid,   0.047f, effectiveSR);
            recalcGridChargeDecay(el84GridA, 0.100f, effectiveSR);
            recalcGridChargeDecay(el84GridB, 0.100f, effectiveSR);

            gainSmoother.init(5.0f, effectiveSR);
            toneSmoother.init(5.0f, effectiveSR);
            volumeSmoother.init(5.0f, effectiveSR);

            setLatencySamples(static_cast<int>(oversampleSwitcher.active()->getLatencyInSamples()));
        }
    }

    // Oversampled processing (Spec §2.5 processBlock)
    auto* oversampler = oversampleSwitcher.active();
    auto osBlock = oversampler->processSamplesUp(juce::dsp::AudioBlock<float>(buffer));

    for (size_t i = 0; i < osBlock.getNumSamples(); ++i)
    {
        // Per-sample parameter smoothing
        float gainVal = gainSmoother.tick();
        float volVal  = volumeSmoother.tick();
        float toneVal = toneSmoother.tick();

        // Update modulated filters from smoothed tone
        constexpr float toneFreqRatio = TONE_MAX_FREQ / TONE_MIN_FREQ;
        float toneFreq = TONE_MIN_FREQ * std::pow(toneFreqRatio, toneVal);
        filterBank.updateModulated(toneFreq, GAIN_LOADING_LP_FREQ);
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
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, PREAMP_RPLATE_N);
        x = filterBank.v1a_couplingHP.process(x);
        float v1aGainEst = (std::abs(v1a_in) > 1e-8f) ? std::abs(x) / std::abs(v1a_in) : StageGains::V1A_GAIN;
        x = v1a_miller.process(x, v1aGainEst, filterBank.effectiveSR);
        psu.reportPreampLevel(x);

        // ── Gain Pot Gang A ──
        float gainA = gainVal;
        x *= gainA;

        // ── Stage 2: V1b ──
        x = applyGridConduction(x, v1bGrid);
        float v1b_in = x;
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, PREAMP_RPLATE_N);
        x = filterBank.v1b_couplingHP.process(x);
        float gainPotR = (1.0f - gainA) * GAIN_POT_R_REAL;
        v1b_miller.rSource_real = std::min(V1B_RSOURCE_BASE_REAL + gainPotR, V1B_RSOURCE_MAX_REAL);
        float v1bGainEst = (std::abs(v1b_in) > 1e-8f) ? std::abs(x) / std::abs(v1b_in) : StageGains::V1B_GAIN;
        x = v1b_miller.process(x, v1bGainEst, filterBank.effectiveSR);
        psu.reportPreampLevel(x);

        // ── Gain Pot Gang B (linear — taper already applied on Gang A) ──
        x *= gainVal;
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
        float outDiff = filterBank.toneCutFilter.process(diff);
        piA = (sum + outDiff) * 0.5f;
        piB = (sum - outDiff) * 0.5f;

        // ── Master Volume ──
        float vol = audioTaper(volVal);
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

        // Sag tracks raw plate current before OT scaling
        sag.update(powerOut);

        // Convert plate current to OT primary voltage
        powerOut *= OT_CURRENT_SCALE;

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

    // ── GUI level metering (RMS of channel 0) ──
    {
        const int n = buffer.getNumSamples();
        if (n > 0)
        {
            const float* ch0 = buffer.getReadPointer(0);
            float sumSq = 0.0f;
            for (int i = 0; i < n; ++i)
                sumSq += ch0[i] * ch0[i];
            float rms = std::sqrt(sumSq / static_cast<float>(n));
            outputLevelForGUI.store(rms, std::memory_order_relaxed);
        }
    }

    // ── Grid current readout (peak across EL84 stages) ──
    {
        float peakGridI = std::max(el84GridA.lastGridCurrent, el84GridB.lastGridCurrent);
        gridCurrentForGUI.store(peakGridI, std::memory_order_relaxed);
    }
}

TinyTerrorProcessor::DSPReadout TinyTerrorProcessor::getDSPReadout() const
{
    DSPReadout r;
    r.vBPlus   = VS::toVolts(psu.vPower());
    r.vScreen  = VS::toVolts(psu.vScreen());
    r.vPi      = VS::toVolts(psu.vPi());
    r.vPreamp  = VS::toVolts(psu.vPreamp());
    r.sagPercent = sag.biasOffset() / 0.01f; // approximate %
    r.biasV    = VS::toVolts(el84BaseBias + sag.biasOffset());
    r.gridCurrentMA = VS::toAmps(gridCurrentForGUI.load(std::memory_order_relaxed)) * 1000.0f;
    r.osFactor = oversampleSwitcher.activeFactor();
    r.is7W     = std::abs(el84BaseBias - PENTODE_BIAS_7W_N) < 1e-6f;
    return r;
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

// ── User IR Management ──

void TinyTerrorProcessor::loadUserIR(const juce::File& file)
{
    // Called from message thread (editor).
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(file));
    if (!reader) return;

    int numSamples = static_cast<int>(
        std::min(reader->lengthInSamples, static_cast<juce::int64>(tt::MAX_USER_IR_SAMPLES)));
    int numChannels = static_cast<int>(reader->numChannels);

    UserIREntry entry;
    entry.name = file.getFileNameWithoutExtension();
    entry.sampleRate = reader->sampleRate;
    entry.numChannels = numChannels;
    entry.numSamples = numSamples;
    entry.audioData.setSize(numChannels, numSamples);
    reader->read(&entry.audioData, 0, numSamples, 0, true, true);

    // Load into convolution immediately
    juce::AudioBuffer<float> bufCopy(entry.audioData);
    cabSim.loadIRFromBuffer(std::move(bufCopy), entry.sampleRate);

    userIRs.push_back(std::move(entry));

    // Set IR_INDEX to the new IR (1-based)
    int newIndex = static_cast<int>(userIRs.size());
    cachedIRIndex = newIndex;  // prevent audio thread from re-triggering async load
    if (auto* param = apvts.getParameter(tt::Params::IDs::IR_INDEX))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(newIndex)));
}

void TinyTerrorProcessor::selectUserIR(int userIndex)
{
    // 0-based index into userIRs
    if (userIndex < 0 || userIndex >= static_cast<int>(userIRs.size())) return;

    auto& ir = userIRs[static_cast<size_t>(userIndex)];
    juce::AudioBuffer<float> bufCopy(ir.audioData);
    cabSim.loadIRFromBuffer(std::move(bufCopy), ir.sampleRate);

    int paramIndex = userIndex + 1; // 1-based
    cachedIRIndex = paramIndex;
    if (auto* param = apvts.getParameter(tt::Params::IDs::IR_INDEX))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(paramIndex)));
}

void TinyTerrorProcessor::saveUserIRsToState()
{
    // Remove any existing UserIRs node
    auto existing = apvts.state.getChildWithName("UserIRs");
    if (existing.isValid())
        apvts.state.removeChild(existing, nullptr);

    if (userIRs.empty()) return;

    juce::ValueTree irsNode("UserIRs");
    for (auto& ir : userIRs)
    {
        juce::ValueTree irNode("IR");
        irNode.setProperty("name", ir.name, nullptr);
        irNode.setProperty("sampleRate", ir.sampleRate, nullptr);
        irNode.setProperty("numChannels", ir.numChannels, nullptr);
        irNode.setProperty("numSamples", ir.numSamples, nullptr);

        // Store audio data as raw floats per channel
        juce::MemoryBlock audioBlock;
        for (int ch = 0; ch < ir.numChannels; ++ch)
            audioBlock.append(ir.audioData.getReadPointer(ch),
                              static_cast<size_t>(ir.numSamples) * sizeof(float));

        irNode.setProperty("audioData", juce::var(audioBlock), nullptr);
        irsNode.addChild(irNode, -1, nullptr);
    }
    apvts.state.addChild(irsNode, -1, nullptr);
}

void TinyTerrorProcessor::loadUserIRsFromState()
{
    userIRs.clear();

    auto irsNode = apvts.state.getChildWithName("UserIRs");
    if (!irsNode.isValid()) return;

    for (int i = 0; i < irsNode.getNumChildren(); ++i)
    {
        auto irNode = irsNode.getChild(i);
        if (!irNode.hasType("IR")) continue;

        UserIREntry entry;
        entry.name = irNode.getProperty("name").toString();
        entry.sampleRate = static_cast<double>(irNode.getProperty("sampleRate"));
        entry.numChannels = static_cast<int>(irNode.getProperty("numChannels"));
        entry.numSamples = static_cast<int>(irNode.getProperty("numSamples"));

        auto* audioBlob = irNode.getProperty("audioData").getBinaryData();
        if (audioBlob && audioBlob->getSize() > 0)
        {
            entry.audioData.setSize(entry.numChannels, entry.numSamples);
            const auto* rawData = static_cast<const float*>(audioBlob->getData());
            for (int ch = 0; ch < entry.numChannels; ++ch)
                entry.audioData.copyFrom(ch, 0, rawData + ch * entry.numSamples, entry.numSamples);
        }

        userIRs.push_back(std::move(entry));
    }

    // Reload active IR if saved
    int irIndex = static_cast<int>(apvts.getRawParameterValue(tt::Params::IDs::IR_INDEX)->load());
    if (irIndex > 0 && irIndex <= static_cast<int>(userIRs.size()))
    {
        auto& ir = userIRs[static_cast<size_t>(irIndex - 1)];
        juce::AudioBuffer<float> bufCopy(ir.audioData);
        cabSim.loadIRFromBuffer(std::move(bufCopy), ir.sampleRate);
        cachedIRIndex = irIndex;
    }
}

// ── State Persistence ──

void TinyTerrorProcessor::getStateInformation(juce::MemoryBlock& destData) {
    saveUserIRsToState();
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void TinyTerrorProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml && xml->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        loadUserIRsFromState();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TinyTerrorProcessor(); }
