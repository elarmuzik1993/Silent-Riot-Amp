#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <algorithm>

using namespace sr;

SilentRiotProcessor::SilentRiotProcessor()
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

SilentRiotProcessor::~SilentRiotProcessor()
{
    cancelPendingUpdate(); // prevent callback after destruction
}

void SilentRiotProcessor::handleAsyncUpdate()
{
    // Runs on message thread — safe to allocate, do file I/O, acquire locks.
    int idx = pendingIRIndex.exchange(-1);
    if (idx < 0) return;

    // IR_INDEX maps directly onto user IR slots: 0 = IIR mode, 1..N = userIRs[idx-1].
    // The stock-IR table in CabinetSim is intentionally NOT reachable from this
    // parameter today — the loader UI only populates user IRs, and the stock
    // entries are synthetic placeholders. Wiring stock IRs is deferred until real
    // cabinet IRs ship (see ROADMAP.md "Ship real cabinet IRs").
    int userIdx = idx - 1;
    if (userIdx < 0 || userIdx >= static_cast<int>(userIRs.size()))
    {
        // IIR mode, or an out-of-range index from restored state / host automation:
        // disable convolution so we never leave a stale IR active.
        cabSim.setIRIndex(0);
        return;
    }

    auto& ir = userIRs[static_cast<size_t>(userIdx)];
    juce::AudioBuffer<float> bufCopy(ir.audioData);
    cabSim.loadIRFromBuffer(std::move(bufCopy), ir.sampleRate);
}

juce::AudioProcessorValueTreeState::ParameterLayout SilentRiotProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Knobs: internal 0–1, UI displays 0–10 (see UI spec §7.1)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::GAIN, 1 }, "Gain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.6f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::TONE, 1 }, "Shape",
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
        juce::ParameterID{ Params::IDs::IR_INDEX, 1 }, "IR Selection",
        0, sr::MAX_USER_IRS, 0));

    // ── Effects module — all default OFF / dry (tone-neutral when bypassed) ──
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ Params::IDs::GATE_ON, 1 }, "Gate", false));
    // Threshold is a 0–1 knob (maps 0..1 → GATE_THRESH_MIN..MAX dB in processBlock,
    // so NotchKnob's 0–1 arc works). Default 0 = -80 dB (effectively open).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::GATE_THRESHOLD, 1 }, "Gate Threshold",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ Params::IDs::BOOST_ON, 1 }, "Boost", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::BOOST_LEVEL, 1 }, "Boost Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::BOOST_TIGHT, 1 }, "Boost Tight",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::BOOST_BRIGHT, 1 }, "Boost Bright",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ Params::IDs::DELAY_ON, 1 }, "Delay", false));
    // Time is a 0–1 knob (maps 0..1 → DELAY_TIME_MIN..MAX ms in processBlock).
    // Default ~0.34 ≈ 350 ms.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::DELAY_TIME, 1 }, "Delay Time",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.337f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::DELAY_FEEDBACK, 1 }, "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.35f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::DELAY_MIX, 1 }, "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.3f));
    // Tone (feedback high-cut): 0 = dark/tape, 1 = bright/digital. Default 0.5.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::DELAY_TONE, 1 }, "Delay Tone",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ Params::IDs::DELAY_PINGPONG, 1 }, "Delay Ping-Pong", false));
    // MANUAL (index 0) = use the TIME knob; any other choice syncs to host
    // tempo at that note division (see TempoSync.h).
    {
        juce::StringArray syncNames;
        for (const auto& d : kSyncDivisions) syncNames.add(d.name);
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{ Params::IDs::DELAY_SYNC_DIV, 1 }, "Delay Sync", syncNames, 0));
    }

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ Params::IDs::REVERB_ON, 1 }, "Reverb", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::REVERB_MIX, 1 }, "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.25f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::REVERB_SIZE, 1 }, "Reverb Size",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::REVERB_TONE, 1 }, "Reverb Tone",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    return { params.begin(), params.end() };
}

void SilentRiotProcessor::resetDSPState()
{
    // Clear all filter state, integrators, and cached plate voltages so a
    // (re)prepare or sample-rate change starts from a defined idle state
    // instead of carrying stale state computed at the old configuration.
    filterBank.resetAll();
    v1a_miller.reset();
    v1b_miller.reset();
    speakerCoupling.reset();
    otSaturation.reset();
    psu.reset();
    sag.reset();
    ltpModel.reset();

    v1aTriode.reset();
    v1bTriode.reset();

    v1aGrid.reset();
    v1bGrid.reset();
    piGrid.reset();
    el84GridA.reset();
    el84GridB.reset();

    if (limiterOversampler != nullptr)
        limiterOversampler->reset();

    // Effects module
    noiseGate.reset();
    boost.reset();
    delay.reset();
    reverb.reset();

    lastShapeCoeffVal = -1.0f;   // force shape coeff recompute on first sample
    psuFrozen = false;
}

void SilentRiotProcessor::recalcRateDependentCoeffs()
{
    const int osFactor = oversampleSwitcher.activeFactor();
    const float effectiveSR = static_cast<float>(baseSampleRate) * osFactor;

    filterBank.recalcAll(static_cast<float>(baseSampleRate), osFactor);
    v1a_miller.recalc(effectiveSR);
    v1b_miller.recalc(effectiveSR);
    speakerCoupling.recalcFilters(effectiveSR);
    psu.recalcDecayCoeffs(effectiveSR);
    sag.recalcCoeffs(effectiveSR);
    otSaturation.recalcCoeffs(effectiveSR);

    // Grid conduction charge decay
    recalcGridChargeDecay(v1aGrid,   GRID_TAU_PREAMP_S, effectiveSR);
    recalcGridChargeDecay(v1bGrid,   GRID_TAU_PREAMP_S, effectiveSR);
    recalcGridChargeDecay(piGrid,    GRID_TAU_PI_S,     effectiveSR);
    recalcGridChargeDecay(el84GridA, GRID_TAU_EL84_S,   effectiveSR);
    recalcGridChargeDecay(el84GridB, GRID_TAU_EL84_S,   effectiveSR);

    // Smoothers run per-sample at the oversampled rate
    gainSmoother.init(5.0f, effectiveSR);
    toneSmoother.init(5.0f, effectiveSR);
    volumeSmoother.init(5.0f, effectiveSR);

    // Report oversampler latency to DAW. The limiter oversampler's latency is
    // included unconditionally (not just when IR mode is active) so total
    // latency stays constant across cab/IR mode switches instead of requiring
    // a host renegotiation every time irMode flips.
    int totalLatency = static_cast<int>(oversampleSwitcher.active()->getLatencyInSamples());
    if (limiterOversampler != nullptr)
        totalLatency += static_cast<int>(limiterOversampler->getLatencyInSamples());
    setLatencySamples(totalLatency);
}

void SilentRiotProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;

    oversampleSwitcher.init(getTotalNumOutputChannels(), samplesPerBlock);

    // 4x polyphase-IIR oversampler dedicated to the IR-path output limiter
    // (see docs/handoff-IR-limiter-aliasing.md). Runs at base SR -> 4x base SR,
    // independent of the main engine's oversample factor.
    limiterOversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        getTotalNumOutputChannels(), 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
    limiterOversampler->initProcessing(static_cast<size_t>(samplesPerBlock));

    recalcRateDependentCoeffs();

    // Cabinet IR convolution engine (runs at base SR, after downsample)
    cabSim.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    // ── Effects module (base SR, outside the oversampled amp loop) ──
    const float baseSR = static_cast<float>(sampleRate);
    noiseGate.recalcCoeffs(baseSR);
    boost.prepare(baseSR);
    delay.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    reverb.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    const double fxRampSec = 0.05;
    gateThreshSm.reset(sampleRate, fxRampSec);
    boostLevelSm.reset(sampleRate, fxRampSec);
    boostTightSm.reset(sampleRate, fxRampSec);
    boostBrightSm.reset(sampleRate, fxRampSec);
    // Sanitized for the same reason as the processBlock reads below — seeding
    // a smoother's *current* value with NaN poisons its interpolation forever
    // (current += (target-current)*rate never clears NaN), even after a later
    // block sets a sanitized target.
    gateThreshSm.setCurrentAndTargetValue(
        sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::GATE_THRESHOLD)->load()));
    boostLevelSm.setCurrentAndTargetValue(
        sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::BOOST_LEVEL)->load()));
    boostTightSm.setCurrentAndTargetValue(
        sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::BOOST_TIGHT)->load()));
    boostBrightSm.setCurrentAndTargetValue(
        sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::BOOST_BRIGHT)->load()));

    // Reload active user IR after prepare (convolution state was reset)
    int irIdx = static_cast<int>(apvts.getRawParameterValue(sr::Params::IDs::IR_INDEX)->load());
    if (irIdx > 0 && irIdx <= static_cast<int>(userIRs.size()))
    {
        auto& ir = userIRs[static_cast<size_t>(irIdx - 1)];
        juce::AudioBuffer<float> bufCopy(ir.audioData);
        cabSim.loadIRFromBuffer(std::move(bufCopy), ir.sampleRate);
        cachedIRIndex = irIdx;
    }

    // Clear any state carried over from a previous configuration
    resetDSPState();
}

void SilentRiotProcessor::releaseResources() {}

bool SilentRiotProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    auto mainIn  = layouts.getMainInputChannelSet();
    auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    return mainIn == mainOut;
}

void SilentRiotProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    // Effects module parameters (run at base SR, outside the amp loop).
    // Float reads are sanitized at this boundary — a misbehaving host could
    // report NaN via automation, which would otherwise silently poison the
    // gate/boost/delay/reverb filters' IIR state forever (see ParamSanitize.h).
    bool  gateOn    = apvts.getRawParameterValue(Params::IDs::GATE_ON)->load() > 0.5f;
    float gateThr   = sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::GATE_THRESHOLD)->load());
    bool  boostOn   = apvts.getRawParameterValue(Params::IDs::BOOST_ON)->load() > 0.5f;
    float boostLvl  = sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::BOOST_LEVEL)->load());
    float boostTgt  = sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::BOOST_TIGHT)->load());
    float boostBrt  = sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::BOOST_BRIGHT)->load());
    bool  delayOn   = apvts.getRawParameterValue(Params::IDs::DELAY_ON)->load() > 0.5f;
    float delayTime = sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::DELAY_TIME)->load());
    float delayFb   = sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::DELAY_FEEDBACK)->load());
    float delayMix  = sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::DELAY_MIX)->load());
    float delayTone = sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::DELAY_TONE)->load());
    bool  delayPP   = apvts.getRawParameterValue(Params::IDs::DELAY_PINGPONG)->load() > 0.5f;
    int   delaySync = static_cast<int>(apvts.getRawParameterValue(Params::IDs::DELAY_SYNC_DIV)->load());
    bool  reverbOn  = apvts.getRawParameterValue(Params::IDs::REVERB_ON)->load() > 0.5f;
    float reverbMix = sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::REVERB_MIX)->load());
    float reverbSz  = sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::REVERB_SIZE)->load());
    float reverbTn  = sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::REVERB_TONE)->load());

    // Master power off — silence output
    if (!powerOn) { buffer.clear(); return; }

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

    // ── Pre-amp effects (gate → boost) at base SR, BEFORE upsampling ──
    // The amp reads mono from channel 0, so we process channel 0 only; the
    // loop copies it to stereo. Both effects are linear/level operations
    // (no clipping) → safe at base SR with no aliasing.
    if (gateOn || boostOn)
    {
        gateThreshSm.setTargetValue(gateThr);
        boostLevelSm.setTargetValue(boostLvl);
        boostTightSm.setTargetValue(boostTgt);
        boostBrightSm.setTargetValue(boostBrt);

        const int n = buffer.getNumSamples();
        float* ch0 = buffer.getWritePointer(0);
        for (int i = 0; i < n; ++i)
        {
            // Map threshold knob 0..1 → dB
            const float thrDb = GATE_THRESH_MIN_DB
                + gateThreshSm.getNextValue() * (GATE_THRESH_MAX_DB - GATE_THRESH_MIN_DB);
            const float lvl = boostLevelSm.getNextValue();
            const float tgt = boostTightSm.getNextValue();
            const float brt = boostBrightSm.getNextValue();

            float s = ch0[i];
            if (gateOn)  s = noiseGate.process(s, thrDb);
            if (boostOn) s = boost.process(s, lvl, tgt, brt);
            ch0[i] = s;
        }
    }

    // ── Oversample mode switching (crossfade state machine) ──
    {
        // Complete pending switch at block boundary (after fade-out finished last block)
        if (oversampleSwitcher.readyToSwitch()) {
            oversampleSwitcher.commitSwitch();
            recalcRateDependentCoeffs();
        }

        // Request mode change → starts fade-out (actual switch deferred to next block)
        int osParam = static_cast<int>(apvts.getRawParameterValue(Params::IDs::OVERSAMPLE)->load());
        if (osParam != oversampleSwitcher.currentMode && !oversampleSwitcher.isTransitioning()) {
            oversampleSwitcher.requestModeChange(osParam);
        }

        // Freeze PSU during transition — prevents supply recovering to nominal
        // during the silent gap, which would cause a voltage pop on fade-in
        psuFrozen = oversampleSwitcher.isTransitioning();
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

        // Update shape EQ only when value has changed enough to matter
        // (saves sin/cos/sqrt coefficient recalc on ~99% of samples)
        if (std::abs(toneVal - lastShapeCoeffVal) > 0.001f) {
            filterBank.updateShape(toneVal);
            lastShapeCoeffVal = toneVal;
        }
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
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, PREAMP_RPLATE_N, v1aTriode);
        x -= filterBank.v1a_cathodeBypass.process(x) * CATHODE_BYPASS_DEPTH;
        x = filterBank.v1a_couplingHP.process(x);
        float v1aGainEst = (std::abs(v1a_in) > 1e-8f) ? std::abs(x) / std::abs(v1a_in) : StageGains::V1A_GAIN;
        x = v1a_miller.process(x, v1aGainEst, filterBank.effectiveSR);
        psu.reportPreampLevel(x);

        // ── Gain Pot Gang A (audio taper: Spec §3.3) ──
        float gainA = audioTaper(gainVal);
        x *= gainA;

        // ── Stage 2: V1b ──
        x = applyGridConduction(x, v1bGrid);
        float v1b_in = x;
        x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, PREAMP_RPLATE_N, v1bTriode);
        x -= filterBank.v1b_cathodeBypass.process(x) * CATHODE_BYPASS_DEPTH;
        x = filterBank.v1b_couplingHP.process(x);
        float gainPotR = (1.0f - gainA) * GAIN_POT_R_REAL;
        v1b_miller.rSource_real = std::min(V1B_RSOURCE_BASE_REAL + gainPotR, V1B_RSOURCE_MAX_REAL);
        float v1bGainEst = (std::abs(v1b_in) > 1e-8f) ? std::abs(x) / std::abs(v1b_in) : StageGains::V1B_GAIN;
        x = v1b_miller.process(x, v1bGainEst, filterBank.effectiveSR);
        psu.reportPreampLevel(x);

        // ── Gain Pot Gang B (audio taper: Spec §3.3) ──
        x *= audioTaper(gainVal);
        x = filterBank.gainLoadingLP.process(x);

        // ── Stage 3: Phase Inverter (LTP) ──
        x = applyGridConduction(x, piGrid);
        LTPOutput ltp_out = solveLTP(x, vPi, ltpModel);
        float piA = filterBank.pi_couplingHP_A.process(ltp_out.piA);
        float piB = filterBank.pi_couplingHP_B.process(ltp_out.piB);
        psu.reportPiLevel(std::max(std::abs(piA), std::abs(piB)));

        // ── Shape Control (mid scoop/boost — differential only) ──
        float diff = piA - piB;
        float sum  = piA + piB;
        float outDiff = filterBank.shapeLowShelf.process(diff);
        outDiff = filterBank.shapeMidBell.process(outDiff);
        outDiff = filterBank.shapeHighShelf.process(outDiff);
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

        float totalPlate = std::abs(ppA.plateCurrent) + std::abs(ppB.plateCurrent);
        psu.reportPowerDirect(totalPlate, ppA.screenCurrent + ppB.screenCurrent);

        // Sag tracks total cathode current (both tubes through shared R)
        sag.update(totalPlate);

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

        // Final safety net: never emit a non-finite sample into the host
        if (!std::isfinite(finalOut)) finalOut = 0.0f;

        osBlock.setSample(0, static_cast<int>(i), finalOut);
        // Copy to stereo
        if (osBlock.getNumChannels() > 1)
            osBlock.setSample(1, static_cast<int>(i), finalOut);
    }

    juce::dsp::AudioBlock<float> outputBlock(buffer);
    oversampler->processSamplesDown(outputBlock);

    // ── Crossfade envelope for oversample transitions ──
    oversampleSwitcher.applyTransitionFade(buffer);

    // ── IR Convolution path (runs at base SR, after downsample) ──
    if (irMode)
    {
        cabSim.processBlock(outputBlock);

        // Apply output scaling + tanh limiter oversampled (4x) so the
        // harmonics tanh generates don't fold back into the band — running
        // it at base SR (as before) aliased audibly on bright/cranked IR
        // tones (see docs/handoff-IR-limiter-aliasing.md).
        auto limiterBlock = limiterOversampler->processSamplesUp(outputBlock);
        for (size_t ch = 0; ch < limiterBlock.getNumChannels(); ++ch)
        {
            auto* samples = limiterBlock.getChannelPointer(ch);
            for (size_t i = 0; i < limiterBlock.getNumSamples(); ++i)
            {
                samples[i] *= outputGainCompensation;
                samples[i] = std::tanh(samples[i]);
            }
        }
        limiterOversampler->processSamplesDown(outputBlock);

        // Final safety net: never emit a non-finite sample into the host
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* samples = buffer.getWritePointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (!std::isfinite(samples[i])) samples[i] = 0.0f;
        }
    }

    // ── Post-cab effects (delay → reverb) at base SR, after the amp/cab ──
    // Operate per channel: a stereo bus picks up reverb/delay width; mono
    // stays mono. Skipped entirely (bit-exact dry) when toggled off.
    if (delayOn)
    {
        float delayMs;
        if (delaySync <= 0)
        {
            // MANUAL — map time knob 0..1 → ms
            delayMs = DELAY_TIME_MIN_MS
                + delayTime * (DELAY_TIME_MAX_MS - DELAY_TIME_MIN_MS);
        }
        else
        {
            // Tempo-synced — TIME knob is ignored. Standalone hosts typically
            // report no tempo, so fall back to 120 BPM there.
            double bpm = 120.0;
            if (auto* ph = getPlayHead())
                if (auto pos = ph->getPosition())
                    bpm = pos->getBpm().orFallback(120.0);
            delayMs = computeSyncMs(delaySync, bpm);
        }
        delay.setParameters(delayMs, delayFb, delayMix, delayTone, delayPP);
        delay.process(buffer);
    }
    if (reverbOn)
    {
        reverb.setParameters(reverbMix, reverbSz, reverbTn);
        reverb.process(buffer);
    }

    // ── Post-effects safety limiter ──
    // delay/reverb run AFTER the amp's output limiter, so their wet tails can
    // sum past full-scale. Soft-clip the summed output back into ±1 (and scrub
    // any non-finite sample). Runs ONLY when an effect is active, so the dry
    // amp path stays bit-identical when the whole module is off.
    if (delayOn || reverbOn)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* samples = buffer.getWritePointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float s = samples[i];
                samples[i] = std::isfinite(s) ? softClip(s) : 0.0f;
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

    // ── DSP-state telemetry for the editor readout (publish atomic snapshot) ──
    {
        vBPlusForGUI.store(psu.vPower(),   std::memory_order_relaxed);
        vScreenForGUI.store(psu.vScreen(), std::memory_order_relaxed);
        vPiForGUI.store(psu.vPi(),         std::memory_order_relaxed);
        vPreampForGUI.store(psu.vPreamp(), std::memory_order_relaxed);
        sagOffsetForGUI.store(sag.biasOffset(), std::memory_order_relaxed);
        el84BiasForGUI.store(el84BaseBias, std::memory_order_relaxed);
        osFactorForGUI.store(oversampleSwitcher.activeFactor(), std::memory_order_relaxed);
    }
}

SilentRiotProcessor::DSPReadout SilentRiotProcessor::getDSPReadout() const
{
    // Reads only atomic snapshots published by the audio thread — no torn reads
    // of the live (non-atomic) PSU/sag state from the message thread.
    const float biasOffset = sagOffsetForGUI.load(std::memory_order_relaxed);
    const float baseBias   = el84BiasForGUI.load(std::memory_order_relaxed);

    DSPReadout r;
    r.vBPlus   = VS::toVolts(vBPlusForGUI.load(std::memory_order_relaxed));
    r.vScreen  = VS::toVolts(vScreenForGUI.load(std::memory_order_relaxed));
    r.vPi      = VS::toVolts(vPiForGUI.load(std::memory_order_relaxed));
    r.vPreamp  = VS::toVolts(vPreampForGUI.load(std::memory_order_relaxed));
    r.sagPercent = biasOffset / 0.01f; // approximate %
    r.biasV    = VS::toVolts(baseBias + biasOffset);
    r.gridCurrentMA = VS::toAmps(gridCurrentForGUI.load(std::memory_order_relaxed)) * 1000.0f;
    r.osFactor = osFactorForGUI.load(std::memory_order_relaxed);
    r.is7W     = std::abs(baseBias - PENTODE_BIAS_7W_N) < 1e-6f;
    return r;
}

// ── Boilerplate ──
juce::AudioProcessorEditor* SilentRiotProcessor::createEditor() { return new SilentRiotEditor(*this); }
bool SilentRiotProcessor::hasEditor() const { return true; }
const juce::String SilentRiotProcessor::getName() const { return JucePlugin_Name; }
bool SilentRiotProcessor::acceptsMidi() const { return false; }
bool SilentRiotProcessor::producesMidi() const { return false; }
bool SilentRiotProcessor::isMidiEffect() const { return false; }
double SilentRiotProcessor::getTailLengthSeconds() const
{
    double tail = cabSim.getIRTailSeconds();
    if (apvts.getRawParameterValue(Params::IDs::DELAY_ON)->load() > 0.5f)
        tail = std::max(tail, static_cast<double>(delay.tailSeconds()));
    if (apvts.getRawParameterValue(Params::IDs::REVERB_ON)->load() > 0.5f)
        tail = std::max(tail, static_cast<double>(reverb.tailSeconds()));
    return tail;
}
int SilentRiotProcessor::getNumPrograms() { return 1; }
int SilentRiotProcessor::getCurrentProgram() { return 0; }
void SilentRiotProcessor::setCurrentProgram(int) {}
const juce::String SilentRiotProcessor::getProgramName(int) { return {}; }
void SilentRiotProcessor::changeProgramName(int, const juce::String&) {}

// ── Presets ──

void SilentRiotProcessor::applyPreset(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= sr::Presets::count) return;
    const auto& p = sr::Presets::table[presetIndex];

    auto setNorm = [this](const juce::String& id, float value01) {
        if (auto* param = apvts.getParameter(id))
            param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value01));
    };

    // GAIN/TONE/VOLUME ranges are 0–1, so the normalized value is the value itself.
    setNorm(Params::IDs::GAIN,   p.gain);
    setNorm(Params::IDs::TONE,   p.tone);
    setNorm(Params::IDs::VOLUME, p.volume);
    setNorm(Params::IDs::HALF_POWER, p.halfPower ? 1.0f : 0.0f);
    setNorm(Params::IDs::CAB_SIM,    p.cabSim    ? 1.0f : 0.0f);

    if (auto* pi = apvts.getParameter(Params::IDs::PRESET_IDX))
        pi->setValueNotifyingHost(pi->convertTo0to1(static_cast<float>(presetIndex)));
}

// ── User IR Management ──

bool SilentRiotProcessor::loadUserIR(const juce::File& file)
{
    // Called from message thread (editor).
    // Cap the slot count at the IR_INDEX parameter's max — past it, the
    // clamped parameter would select a different IR than the one appended.
    if (static_cast<int>(userIRs.size()) >= sr::MAX_USER_IRS)
        return false;

    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(file));
    if (!reader) return false;

    int numSamples = static_cast<int>(
        std::min(reader->lengthInSamples, static_cast<juce::int64>(sr::MAX_USER_IR_SAMPLES)));
    if (numSamples <= 0) return false;

    // Convolution uses at most 2 channels (Stereo::yes); reading more would
    // only bloat the saved state — and the restore path rejects >2 channels.
    int numChannels = std::min(static_cast<int>(reader->numChannels), 2);
    if (numChannels <= 0) return false;

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
    if (auto* param = apvts.getParameter(sr::Params::IDs::IR_INDEX))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(newIndex)));

    return true;
}

void SilentRiotProcessor::selectUserIR(int userIndex)
{
    // 0-based index into userIRs
    if (userIndex < 0 || userIndex >= static_cast<int>(userIRs.size())) return;

    auto& ir = userIRs[static_cast<size_t>(userIndex)];
    juce::AudioBuffer<float> bufCopy(ir.audioData);
    cabSim.loadIRFromBuffer(std::move(bufCopy), ir.sampleRate);

    int paramIndex = userIndex + 1; // 1-based
    cachedIRIndex = paramIndex;
    if (auto* param = apvts.getParameter(sr::Params::IDs::IR_INDEX))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(paramIndex)));
}

void SilentRiotProcessor::saveUserIRsToState()
{
    // Remove any existing UserIRs node
    auto existing = apvts.state.getChildWithName("UserIRs");
    if (existing.isValid())
        apvts.state.removeChild(existing, nullptr);

    if (userIRs.empty()) return;

    apvts.state.addChild(sr::userIRsToValueTree(userIRs), -1, nullptr);
}

void SilentRiotProcessor::loadUserIRsFromState()
{
    // userIRsFromValueTree() validates every entry — state comes from DAW
    // project files and must not be trusted (see UserIRState.h).
    userIRs = sr::userIRsFromValueTree(apvts.state.getChildWithName("UserIRs"));

    // Reload active IR if saved
    int irIndex = static_cast<int>(apvts.getRawParameterValue(sr::Params::IDs::IR_INDEX)->load());
    if (irIndex > 0 && irIndex <= static_cast<int>(userIRs.size()))
    {
        auto& ir = userIRs[static_cast<size_t>(irIndex - 1)];
        juce::AudioBuffer<float> bufCopy(ir.audioData);
        cabSim.loadIRFromBuffer(std::move(bufCopy), ir.sampleRate);
        cachedIRIndex = irIndex;
    }
}

// ── State Persistence ──

void SilentRiotProcessor::getStateInformation(juce::MemoryBlock& destData) {
    saveUserIRsToState();
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SilentRiotProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml && xml->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        loadUserIRsFromState();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SilentRiotProcessor(); }
