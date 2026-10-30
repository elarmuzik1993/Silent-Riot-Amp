// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "PluginProcessor.h"
#include "dsp/IdleOperatingPoint.h"
#include "PluginEditor.h"

#include <cmath>
#include <algorithm>

using namespace sr;

namespace {

// RMS of one channel, for the GUI meters. 0 for an empty block.
float channelRms(const float* x, int n)
{
    if (n <= 0) return 0.0f;
    float sumSq = 0.0f;
    for (int i = 0; i < n; ++i)
        sumSq += x[i] * x[i];
    return std::sqrt(sumSq / static_cast<float>(n));
}

// The audio input is a system boundary, like the parameters (review S1): an
// upstream plugin, a failing interface or an uninitialised first host block
// can hand over NaN or Inf. One such sample latches in every stateful stage
// of the amp (filters, plate and cathode caches, grid charge, PSU, sag) and
// silences it until the next prepareToPlay — and the output scrubs hide it,
// since the host only ever hears 0 (RT-1). Replace them with 0 on the way in.
// A finite sample is never touched, so the normal path stays bit-identical.
void scrubNonFiniteInput(juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* s = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite(s[i])) s[i] = 0.0f;
    }
}

// A host may hand over a block larger than prepareToPlay's maximum (VST3 and
// AU forbid it, but hosts do it, most often on an offline bounce or a freeze).
// The oversamplers, convolution, delay and reverb are sized for that maximum
// and a bigger block writes past their buffers (RT-5). Run `fn` on views of at
// most `maxBlock` samples: non-owning, so no copy and no allocation (JUCE keeps
// up to 32 channel pointers inline).
template <typename Fn>
void forEachPreparedChunk(juce::AudioBuffer<float>& buffer, int maxBlock, Fn&& fn)
{
    const int total = buffer.getNumSamples();
    for (int offset = 0; offset < total; offset += maxBlock)
    {
        juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                      offset, std::min(maxBlock, total - offset));
        fn(view);
    }
}

} // namespace

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

    // Latency reports and IR_INDEX changes reach the host and the convolution
    // from here, on the message thread (RT-2, RT-3). The APVTS already needs
    // a MessageManager, so this adds no dependency.
    startTimerHz(20);
}

SilentRiotProcessor::~SilentRiotProcessor()
{
    stopTimer();   // first: no callback into a half-destroyed processor
}

void SilentRiotProcessor::timerCallback()
{
    // Message thread. The audio thread never calls the host or posts to the
    // message queue: it leaves a new latency in pendingLatency (oversample
    // switch) and IR_INDEX in the parameter, and this picks both up within
    // one tick (50 ms).
    //
    // RT-2: setLatencySamples() calls every host listener synchronously, and
    // hosts recompile their graph in it. The host's delay compensation is up
    // to one tick behind the engine; the switch is crossfading through
    // silence then, so nothing is heard.
    const int latency = pendingLatency.load(std::memory_order_acquire);
    if (latency != getLatencySamples())
        setLatencySamples(latency);

    // RT-3: an IR_INDEX change (host automation) is applied here, not by a
    // triggerAsyncUpdate() from processBlock (a lock, a write() syscall and a
    // possible allocation on the audio thread). CabModeSwitcher waits for
    // the real IR before entering IR mode, and JUCE builds that engine in
    // longer than a tick, so the extra delay is not heard.
    const juce::ScopedLock irLock(irStateLock);
    const int irIdx = static_cast<int>(
        apvts.getRawParameterValue(sr::Params::IDs::IR_INDEX)->load());
    if (irIdx != cachedIRIndex.load())
        applyIRIndexParam(sr::IRLoad::Async);
}

void SilentRiotProcessor::applyIRIndexParam(sr::IRLoad load)
{
    // Message thread (state restore, the timer, the GUI) or prepareToPlay.
    // Syncs the convolution to IR_INDEX now — including disabling it when the
    // index is past the IR list, which the timer can't catch when the index
    // equals cachedIRIndex (review S2).
    //
    // `load` is IRLoad::Now only from prepareToPlay, where processBlock can't
    // run: the IR then plays from the first block instead of JUCE's
    // pass-through. Everything else can run alongside processBlock, so it
    // stays Async (CabinetSim::installIRNow must not race the audio thread).
    const int irIdx = static_cast<int>(
        apvts.getRawParameterValue(sr::Params::IDs::IR_INDEX)->load());
    cachedIRIndex = irIdx;
    sr::applyUserIRIndex(cabSim, userIRs, irIdx, load);
}

juce::AudioProcessorValueTreeState::ParameterLayout SilentRiotProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Knobs: internal 0–1, UI displays 0–10 (see UI spec §7.1)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::GAIN, 1 }, "Gain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), Params::Defaults::GAIN));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::TONE, 1 }, "Shape",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), Params::Defaults::TONE));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::VOLUME, 1 }, "Volume",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), Params::Defaults::VOLUME));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ Params::IDs::HALF_POWER, 1 }, "7W Mode", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ Params::IDs::CAB_SIM, 1 }, "Cab Sim", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ Params::IDs::POWER, 1 }, "Power", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ Params::IDs::TUNER_MUTE, 1 }, "Tuner Mute", false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ Params::IDs::OVERSAMPLE, 1 }, "Oversample",
        juce::StringArray{ "2x", "4x", "8x" }, 1));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ Params::IDs::PRESET_IDX, 1 }, "Preset", 0, 4, 0));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ Params::IDs::IR_INDEX, 1 }, "IR Selection",
        0, sr::MAX_USER_IRS, 0));

    // ── Effects module — all default OFF / dry (tone-neutral when bypassed) ──
    // Noise gate has no on/off toggle — it self-gates. Threshold is a 0–1 knob
    // (maps 0..1 → GATE_THRESH_MIN..MAX dB in processBlock, so NotchKnob's 0–1
    // arc works). Default 0 = gate OFF (true bypass — see NoiseGate::process).
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
    // Time is a 0–1 knob (maps 0..1 → DELAY_TIME_MIN..KNOB_MAX ms in processBlock).
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

    // Stereo widener — autonomous (no ON toggle): 0 = off (dry/mono), 1 = wide.
    // Defaults off, so it's tone-transparent until turned up.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::WIDTH, 1 }, "Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    // ── Gain staging (utility trims) ──
    // APPEND-ONLY BELOW THIS POINT. Hosts that index automation positionally
    // bind lanes by order, so inserting a parameter above this line shifts every
    // later lane and breaks automation in sessions saved with an earlier build.
    // Add new parameters at the end, never in the middle.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::INPUT_LEVEL, 1 }, "Input Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::OUTPUT_LEVEL, 1 }, "Output Level",
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
    iirPathAlign.reset();

    // Effects module
    noiseGate.reset();
    boost.reset();
    delay.reset();
    reverb.reset();
    stereoWidth.reset();
    widenerWasActive = false;
    delayWasOn  = false;
    reverbWasOn = false;
    delayFading = reverbFading = false;
    tailsStale  = false;
    bypassDelay.reset();

    lastShapeCoeffVal = -1.0f;   // force shape coeff recompute on first sample
    psuFrozen = false;

    // Start on the idle operating point, not on zeroed filters and a nominal
    // supply: those replayed ~0.5 s of settling as a burst on silence after
    // every prepare (review B1; see dsp/IdleOperatingPoint.h). Needs the rate-
    // dependent coefficients, which prepareToPlay sets before calling this.
    setEl84Bias(apvts.getRawParameterValue(Params::IDs::HALF_POWER)->load() > 0.5f
                    ? PENTODE_BIAS_7W_N : PENTODE_BIAS_15W_N);
    sr::settlePowerStageAtIdle(el84BaseBias, psu, sag);
    sr::seedPreampAtIdle(filterBank, v1aTriode, v1bTriode, ltpModel, psu);

    // Snap the gain-staging trims to their current targets so a re-prepare or
    // sample-rate change doesn't audibly ramp from a stale value.
    inputTrimSm.setCurrentAndTargetValue(inputTrimSm.getTargetValue());
    outputTrimSm.setCurrentAndTargetValue(outputTrimSm.getTargetValue());
}

float SilentRiotProcessor::readAmpKnob(const juce::String& id, float fallback) const
{
    return sanitizeUnitParamOr(apvts.getRawParameterValue(id)->load(), fallback);
}

void SilentRiotProcessor::setEl84Bias(float bias)
{
    el84BaseBias = bias;
    lastSagQuiescentBias = bias;
    sag.setQuiescent(pentodeQuiescentTotal(bias, V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N));
}

void SilentRiotProcessor::recalcRateDependentCoeffs(LatencyPublish publish)
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
    // a host renegotiation every time irMode flips; iirPathAlign delays the
    // IIR path by the same amount so the report holds there too (review B4).
    // Both oversamplers use integer latency, so these are exact, not truncated.
    int totalLatency = static_cast<int>(std::lround(oversampleSwitcher.active()->getLatencyInSamples()));
    if (limiterOversampler != nullptr)
        totalLatency += static_cast<int>(std::lround(limiterOversampler->getLatencyInSamples()));
    engineLatency = totalLatency;
    pendingLatency.store(totalLatency, std::memory_order_release);
    // Deferred (audio thread): timerCallback() reports it (RT-2).
    if (publish == LatencyPublish::Now)
        setLatencySamples(totalLatency);
}

void SilentRiotProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Prepares the cab (cabSim.prepare, installIRNow) and reads userIRs; a
    // host may call this off the message thread, beside a timer tick.
    const juce::ScopedLock irLock(irStateLock);
    baseSampleRate = sampleRate;
    preparedBlockSize = samplesPerBlock;

    // Start in the saved oversample mode — otherwise every prepare starts at 4x
    // and a 2x/8x session fades out, goes silent and fades back in on its first
    // blocks (review B5). A choice parameter's raw value is its index.
    const float osRaw = apvts.getRawParameterValue(Params::IDs::OVERSAMPLE)->load();
    const int osMode = std::isfinite(osRaw) ? static_cast<int>(std::lround(osRaw)) : 1;
    oversampleSwitcher.init(getTotalNumOutputChannels(), samplesPerBlock, osMode);

    // 4x polyphase-IIR oversampler dedicated to the IR-path output limiter
    // (see docs/records/handoffs/handoff-IR-limiter-aliasing.md). Runs at base SR -> 4x base SR,
    // independent of the main engine's oversample factor.
    limiterOversampler = sr::makeIntegerLatencyOversampler(getTotalNumOutputChannels(), 2,
                                                           samplesPerBlock);
    const int limiterLatency = static_cast<int>(std::lround(limiterOversampler->getLatencyInSamples()));
    jassert(limiterLatency <= 16);   // iirPathAlign's capacity
    iirPathAlign.setDelay(limiterLatency);

    recalcRateDependentCoeffs(LatencyPublish::Now);
    syncBypassDelay();

    // Seed the amp-knob smoothers at the knobs' values. They were never seeded,
    // so a fresh instance ramped GAIN/VOLUME in from 0 and a smoother that once
    // held NaN stayed NaN through every later prepare (review S1).
    gainSmoother.setImmediate(readAmpKnob(Params::IDs::GAIN, Params::Defaults::GAIN));
    toneSmoother.setImmediate(readAmpKnob(Params::IDs::TONE, Params::Defaults::TONE));
    volumeSmoother.setImmediate(readAmpKnob(Params::IDs::VOLUME, Params::Defaults::VOLUME));

    // Cabinet IR convolution engine (runs at base SR, after downsample)
    cabSim.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    // ── Effects module (base SR, outside the oversampled amp loop) ──
    const float baseSR = static_cast<float>(sampleRate);
    noiseGate.recalcCoeffs(baseSR);
    boost.prepare(baseSR);
    delay.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    reverb.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    stereoWidth.prepare(baseSR);

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

    inputTrimSm.reset(sampleRate, fxRampSec);
    outputTrimSm.reset(sampleRate, fxRampSec);
    // 0.5 (unity), not 0.0 — see ParamSanitize.h. A 0.0 fallback here would
    // mean -12 dB in and silence out on the first block after a NaN.
    inputTrimSm.setCurrentAndTargetValue(sanitizeUnitParamOr(
        apvts.getRawParameterValue(Params::IDs::INPUT_LEVEL)->load(), 0.5f));
    outputTrimSm.setCurrentAndTargetValue(sanitizeUnitParamOr(
        apvts.getRawParameterValue(Params::IDs::OUTPUT_LEVEL)->load(), 0.5f));

    // Install the active user IR synchronously, so the first block already
    // plays it — a session restored before prepare would otherwise start on
    // JUCE's pass-through engine (no cab) and hand over at a wall-clock time.
    // Or disable it if IR_INDEX no longer names one (review S2).
    applyIRIndexParam(sr::IRLoad::Now);

    // Clear any state carried over from a previous configuration
    resetDSPState();

    // Start in the mode the knobs ask for, with no fade pending. With an IR
    // selected it was just installed (IRLoad::Now), so IR mode is real from
    // the first block; should it not be (nothing installed it), start on the
    // built-in cab and let the switcher wait for it rather than play JUCE's
    // pass-through.
    {
        const bool cabOn = apvts.getRawParameterValue(Params::IDs::CAB_SIM)->load() > 0.5f;
        sr::CabMode mode = sr::requestedCabMode(cabOn, cabSim.useIR.load());
        if (mode == sr::CabMode::IR && !cabSim.hasRealIR())
            mode = sr::CabMode::BuiltIn;
        cabSwitcher.reset(mode);
        cabWaitLogged = false;
        cabStartFresh = true;
    }
}

int SilentRiotProcessor::cabFadeInHold(sr::CabMode entered, sr::CabMode left) const
{
    // How long the path just entered needs, after enterCabMode()'s resets,
    // before its output is the steady signal; the fade-in waits that long at
    // gain 0, so the ramp starts on it instead of on a reset's onset.
    //  - IR mode: the convolution reaches full output at its IR's peak, then
    //    the limiter oversampler (also reset) adds its latency. Capped, so a
    //    strange IR with a late peak can't hold the dip open for long.
    //  - Leaving IR mode: iirPathAlign was cleared and plays zeros until it
    //    refills, then the signal mid-cycle, i.e. a step.
    //  - Off <-> built-in: nothing was reset, no hold.
    constexpr int kMaxIRHold = 1024;   // 23 ms at 44.1 kHz
    if (entered == sr::CabMode::IR)
        return std::min(cabSim.engineOnsetSamples() + kCabOnsetMarginSamples, kMaxIRHold)
               + static_cast<int>(std::lround(limiterOversampler->getLatencyInSamples()));
    if (left == sr::CabMode::IR)
        return iirPathAlign.getDelay();
    return 0;
}

void SilentRiotProcessor::enterCabMode(sr::CabMode entered, sr::CabMode left)
{
    // The switcher's commit: a block start with the output at gain 0. Each
    // path entered is cleared of whatever it froze with while skipped, so it
    // starts clean under the fade-in (review B6). All allocation-free.
    if (entered == sr::CabMode::IR)
    {
        // Also drops a half-finished JUCE crossfade, e.g. from the
        // pass-through to the first IR of a session (Convolution::reset()).
        cabSim.reset();
        limiterOversampler->reset();
    }
    // The built-in cab's biquads need no reset: they run in every mode (see
    // the amp loop), so their state is live when the built-in cab takes over.
    // Resetting them instead would start them from zero mid-note, and the
    // 400 Hz notch's ring-in would be heard through the fade-in.

    // Leaving IR mode: the IIR path's alignment delay holds the last samples
    // it saw before IR mode, not this signal.
    if (left == sr::CabMode::IR)
        iirPathAlign.reset();
}

void SilentRiotProcessor::releaseResources() {}

void SilentRiotProcessor::reset()
{
    // Host stop / transport reset (VST3 setProcessing(false)): the delay, reverb,
    // IR convolution and widener tails would otherwise play on into the next
    // start. Only flags it — the next processBlock clears them on the audio
    // thread, so this is safe from any thread. The amp stays on its operating
    // point: zeroing its filters here would replay the startup burst (B1).
    tailsStale = true;
}

void SilentRiotProcessor::syncBypassDelay()
{
    // setDelay clears the line, so only on a change. Allocation-free. The
    // engine's own latency, not getLatencySamples(): after an oversample
    // switch the host's copy lags it by up to one timer tick (RT-2).
    const int latency = engineLatency;
    jassert(latency <= 32);   // bypassDelay's capacity
    if (latency != bypassDelay.getDelay()) bypassDelay.setDelay(latency);
}

void SilentRiotProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Same guard as processBlock. bypassDelay is a ring sized by its maximum
    // delay, not by the block, so this path was already safe — kept symmetrical.
    if (preparedBlockSize <= 0 || buffer.getNumSamples() <= preparedBlockSize)
    {
        processChunkBypassed(buffer, midi);
        return;
    }
    jassertfalse;   // the host broke its promised maximum block size (RT-5)
    oversizedBlocks.fetch_add(1, std::memory_order_relaxed);
    forEachPreparedChunk(buffer, preparedBlockSize,
                         [&](juce::AudioBuffer<float>& view) { processChunkBypassed(view, midi); });
}

void SilentRiotProcessor::processChunkBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Host bypass. processBlock doesn't run, so delay, reverb, IR convolution
    // and widener freeze holding their tails, exactly as under power-off: flag
    // them so the first processed block after un-bypass starts them clean
    // (review B6). The tuner keeps getting the DI, as it does under power-off
    // and tuner mute — tuning with the plugin bypassed is a normal workflow.
    tailsStale = true;

    // Scrubbed here too: bypass would otherwise pass a NaN straight to the
    // host, and poison the tuner ring and the bypass line on the way (RT-1).
    scrubNonFiniteInput(buffer);

    const int n = buffer.getNumSamples();
    const bool hasCh0 = buffer.getNumChannels() > 0;
    if (hasCh0)
        pushTunerSamples(buffer.getReadPointer(0), n);

    // Meters show what the plugin passes: the raw DI in (no INPUT trim runs
    // here) and, below, that DI out. processBlock's metering doesn't run, so
    // without this both meters froze at their last level (review S6). No amp
    // is running, so no grid current either.
    inputLevelForGUI.store(hasCh0 ? channelRms(buffer.getReadPointer(0), n) : 0.0f,
                           std::memory_order_relaxed);
    gridCurrentForGUI.store(0.0f, std::memory_order_relaxed);

    // Dry input, delayed by the latency the host is compensating for (JUCE's
    // default passes it through undelayed and asserts the latency is 0).
    // processBlock primes the line, so entering bypass is seamless.
    syncBypassDelay();
    bypassDelay.process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), n);
    outputLevelForGUI.store(hasCh0 ? channelRms(buffer.getReadPointer(0), n) : 0.0f,
                            std::memory_order_relaxed);
}

bool SilentRiotProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    auto mainIn  = layouts.getMainInputChannelSet();
    auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    return mainIn == mainOut;
}

void SilentRiotProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // At or below the prepared maximum (every well-behaved host): one direct
    // pass, bit-identical. Above it, split into prepared-size views and run
    // each as its own block (RT-5) — the same as the host sending them one by
    // one, so per-block work (meters, cab-mode commit, oversample fade) just
    // runs per view. Not prepared yet (0): left to the body, as before.
    if (preparedBlockSize <= 0 || buffer.getNumSamples() <= preparedBlockSize)
    {
        processChunk(buffer, midi);
        return;
    }
    jassertfalse;   // the host broke its promised maximum block size (RT-5)
    oversizedBlocks.fetch_add(1, std::memory_order_relaxed);
    forEachPreparedChunk(buffer, preparedBlockSize,
                         [&](juce::AudioBuffer<float>& view) { processChunk(view, midi); });
}

void SilentRiotProcessor::processChunk(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // First, before the tuner capture and the bypass-delay write below, so
    // neither the tuner ring nor the bypass line ever holds a NaN (RT-1).
    scrubNonFiniteInput(buffer);

    // Read parameters. The amp knobs are sanitized like the effects below: a
    // single host NaN on GAIN or VOLUME used to lock its ParamSmoother at NaN
    // and silence the plugin for the life of the instance (review S1).
    float gainParam = readAmpKnob(Params::IDs::GAIN,   Params::Defaults::GAIN);
    float toneParam = readAmpKnob(Params::IDs::TONE,   Params::Defaults::TONE);
    float volParam  = readAmpKnob(Params::IDs::VOLUME, Params::Defaults::VOLUME);
    bool  is7W      = apvts.getRawParameterValue(Params::IDs::HALF_POWER)->load() > 0.5f;
    bool  cabOn     = apvts.getRawParameterValue(Params::IDs::CAB_SIM)->load() > 0.5f;
    bool  powerOn   = apvts.getRawParameterValue(Params::IDs::POWER)->load() > 0.5f;
    bool  tunerMute = apvts.getRawParameterValue(Params::IDs::TUNER_MUTE)->load() > 0.5f;

    // Effects module parameters (run at base SR, outside the amp loop).
    // Float reads are sanitized at this boundary — a misbehaving host could
    // report NaN via automation, which would otherwise silently poison the
    // gate/boost/delay/reverb filters' IIR state forever (see ParamSanitize.h).
    float gateThr   = sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::GATE_THRESHOLD)->load());
    // No on/off toggle: the gate is active whenever its threshold is off 0
    // (knob at 0 = off). NoiseGate::process bypasses at the min threshold too.
    // Keyed off the SMOOTHED value as well as the raw one, so turning the knob
    // down to 0 lets the threshold ramp out instead of cutting the gate dead
    // mid-attenuation — see gateShouldRun() in NoiseGate.h.
    bool  gateOn    = gateShouldRun(gateThr, gateThreshSm.getCurrentValue());
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
    float widthParam= sanitizeUnitParam(apvts.getRawParameterValue(Params::IDs::WIDTH)->load());
    float inTrimParam  = sanitizeUnitParamOr(
        apvts.getRawParameterValue(Params::IDs::INPUT_LEVEL)->load(), 0.5f);
    float outTrimParam = sanitizeUnitParamOr(
        apvts.getRawParameterValue(Params::IDs::OUTPUT_LEVEL)->load(), 0.5f);

    // ── Live tuner capture ──
    // Tap the raw DI input BEFORE any processing or the power-off return, so
    // the tuner tracks pitch even when the amp is silenced (classic workflow).
    if (buffer.getNumChannels() > 0)
        pushTunerSamples(buffer.getReadPointer(0), buffer.getNumSamples());

    // Keep the host-bypass passthrough primed with the raw input, so a bypass
    // engaged after this block continues from it without a gap.
    syncBypassDelay();
    bypassDelay.write(buffer.getArrayOfReadPointers(), buffer.getNumChannels(),
                      buffer.getNumSamples());

    // Master power off, or tuner mute engaged — silence output. Capture above
    // still ran, so the tuner keeps tracking pitch while the amp is silent.
    // The effects below stop running here and freeze holding their tails, so
    // flag them to start clean when the amp comes back (review B6).
    //
    // The metering at the end of the block is skipped too, so publish the
    // silence here, or both meters hold their last level and VUMeter's jitter
    // makes them look live (review S6). Under tuner mute the input meter stays
    // live (post-trim, as when playing): the tuner is listening to that signal.
    // Power off reads all zero, matching the editor's dark meters.
    if (!powerOn || tunerMute)
    {
        const bool meterInput = powerOn && buffer.getNumChannels() > 0;
        inputLevelForGUI.store(meterInput ? channelRms(buffer.getReadPointer(0), buffer.getNumSamples())
                                                * inputTrimGain(inTrimParam)
                                          : 0.0f,
                               std::memory_order_relaxed);
        outputLevelForGUI.store(0.0f, std::memory_order_relaxed);
        gridCurrentForGUI.store(0.0f, std::memory_order_relaxed);
        buffer.clear();
        tailsStale = true;
        return;
    }

    // Leaving power-off / tuner mute, or after a host reset(): drop every frozen
    // effect tail. Clearing the was-on flags sends delay, reverb and widener
    // through their off→on restart below; the IR convolution and its limiter
    // oversampler are cleared if IR mode is the active cab mode (any other
    // mode clears them when it hands over to IR mode); the IIR path's
    // alignment delay is cleared here.
    bool cabSnap = cabStartFresh;   // first block since prepare: nothing to fade from
    cabStartFresh = false;
    if (tailsStale.exchange(false))
    {
        cabSnap = true;   // the last output was silence (or host bypass's dry signal)
        delayWasOn = reverbWasOn = widenerWasActive = false;
        delayFading = reverbFading = false;
        if (cabSwitcher.activeMode() == sr::CabMode::IR)
        {
            cabSim.reset();
            limiterOversampler->reset();
        }
        iirPathAlign.reset();
    }

    // Keep the sag reference locked to what the tube model actually idles at for
    // this bias. 7W runs a hotter bias → higher idle current → different
    // reference, so this has to follow the power-mode switch, not be a constant.
    {
        const float bias = is7W ? PENTODE_BIAS_7W_N : PENTODE_BIAS_15W_N;
        if (bias != lastSagQuiescentBias) setEl84Bias(bias);
    }

    // IR_INDEX changes are applied by timerCallback() on the message thread
    // (RT-3); this block only sees their result, through useIR.

    // ── Cab mode (IR-path plan Task 3) ──
    // Requested: CAB off, else IR when one is selected (useIR is atomic, set
    // by the message thread after the IR is handed to bufferTransfer), else
    // the built-in cab. The switcher decides the ACTIVE mode, which is what
    // the rest of the block runs: a change fades out, commits at gain 0 at a
    // block start, and fades in; IR mode is only entered once the
    // convolution holds the real IR. Until then prime() lets JUCE install it
    // without playing it, so the first IR of a session never plays JUCE's
    // pass-through (the amp with no cab).
    {
        const sr::CabMode requested = sr::requestedCabMode(cabOn, cabSim.useIR.load());
        const sr::CabMode before = cabSwitcher.activeMode();
        if (requested == sr::CabMode::IR && before != sr::CabMode::IR)
            cabSim.prime(buffer.getNumSamples());
        if (cabSnap)
        {
            // Nothing audible to fade out from: the first block since
            // prepareToPlay (a host sets its knobs after prepare), or the first
            // after power-off / tuner mute / host bypass / reset(). Switch at
            // once, still never into an IR that isn't installed.
            const bool canEnter = requested != sr::CabMode::IR || cabSim.hasRealIR();
            const sr::CabMode mode = canEnter ? requested : before;
            if (mode != before) enterCabMode(mode, before);
            cabSwitcher.reset(mode);
        }
        if (cabSwitcher.beginBlock(requested, cabSim.hasRealIR()))
        {
            enterCabMode(cabSwitcher.activeMode(), before);
            cabSwitcher.holdSilence(cabFadeInHold(cabSwitcher.activeMode(), before));
        }

        if (cabSwitcher.isWaitingForIR() && !cabWaitLogged
            && cabSwitcher.waitedSamples() > static_cast<std::int64_t>(baseSampleRate))
        {
            cabWaitLogged = true;
            DBG("Silent Riot: waiting over 1 s for the IR engine; staying on the active cab mode");
        }
        if (!cabSwitcher.isWaitingForIR()) cabWaitLogged = false;
    }
    const bool irMode     = cabSwitcher.activeMode() == sr::CabMode::IR;
    const bool builtInCab = cabSwitcher.activeMode() == sr::CabMode::BuiltIn;
    cabSim.setEnabled(cabSwitcher.activeMode() != sr::CabMode::Off);

    // Set smoother targets (per-sample tick happens inside the loop)
    gainSmoother.setTarget(gainParam);
    toneSmoother.setTarget(toneParam);
    volumeSmoother.setTarget(volParam);

    // ── INPUT trim (gain staging) ──
    // Tone-neutral ±12 dB trim so the GAIN knob means roughly the same thing
    // across pickups and interfaces. Sits AFTER the power/tuner-mute return
    // (before it would be wasted work on a cleared buffer) and BEFORE the input
    // meter, so the meter shows what the amp is actually being fed. The amp
    // reads channel 0 only, so only channel 0 is trimmed — same convention as
    // the gate/boost block below. Skipped entirely at unity to keep the dry
    // path bit-identical.
    inputTrimSm.setTargetValue(inTrimParam);
    if (inputTrimSm.isSmoothing() || inputTrimGain(inputTrimSm.getCurrentValue()) != 1.0f)
    {
        const int n = buffer.getNumSamples();
        float* ch0 = buffer.getWritePointer(0);
        for (int i = 0; i < n; ++i)
            ch0[i] *= inputTrimGain(inputTrimSm.getNextValue());
    }

    // ── Input level metering (RMS, post-trim) ──
    // Post-trim on purpose: the meter has to show what the amp sees, or the
    // INPUT knob can't be set by eye.
    if (buffer.getNumSamples() > 0)
        inputLevelForGUI.store(channelRms(buffer.getReadPointer(0), buffer.getNumSamples()),
                               std::memory_order_relaxed);

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
            recalcRateDependentCoeffs(LatencyPublish::Deferred);   // audio thread (RT-2)
            // recalcAll() rebuilds the shape EQ at noon (FilterBank.cpp), so the
            // cached value must be invalidated or the per-sample guard below sees
            // "shape hasn't moved" and never restores the knob's actual position —
            // leaving Shape silently stuck at noon until the user touches it.
            lastShapeCoeffVal = -1.0f;
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
    float peakEl84GridDrop = 0.0f;   // GRID I readout: the block's peak, not its last sample

    for (size_t i = 0; i < osBlock.getNumSamples(); ++i)
    {
        // Per-sample parameter smoothing
        float gainVal = gainSmoother.tick();
        float volVal  = volumeSmoother.tick();
        float toneVal = toneSmoother.tick();
        float gainDrive = gainDriveTaper(gainVal);

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
        float gainA = audioTaper(gainDrive);
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

        // ── Gain Pot Gang B (LINEAR — taper already applied on Gang A) ──
        // The signal passes through both gangs, but the user knob is first
        // remapped to a virtual pot position. After the EL84 current
        // calibration, the raw x^3 law made knob 3 behave like edge/crank.
        // gainDriveTaper() keeps knob 3 clean, knob 5 edge, and knob 10 full.
        x *= gainDrive;
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
        float vol = volumeDriveTaper(volVal);
        piA *= vol;
        piB *= vol;

        // ── Stage 4: Power Amp (EL84 push-pull) ──
        float effectiveBias = el84BaseBias + sag.biasOffset();
        piA = applyGridConduction(piA, el84GridA);
        piB = applyGridConduction(piB, el84GridB);
        peakEl84GridDrop = std::max({ peakEl84GridDrop, el84GridA.lastGridDrop, el84GridB.lastGridDrop });
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
        // The built-in cab's biquads run in every cab mode, so they are live
        // when the built-in cab takes over from CAB off or IR mode (Task 3);
        // only the built-in cab hears them.
        float cabOut = otOut;
        {
            float builtIn = filterBank.cab_presence.process(otOut);
            builtIn = filterBank.cab_rolloff.process(builtIn);
            builtIn = filterBank.cab_notch.process(builtIn);
            if (builtInCab) cabOut = builtIn;
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
        // Entering IR mode cleared the convolution and its limiter
        // oversampler (enterCabMode, and the tailsStale block above), so
        // neither replays a tail from before (review B6).
        cabSim.processBlock(outputBlock);

        // Apply output scaling + tanh limiter oversampled (4x) so the
        // harmonics tanh generates don't fold back into the band — running
        // it at base SR (as before) aliased audibly on bright/cranked IR
        // tones (see docs/records/handoffs/handoff-IR-limiter-aliasing.md).
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
    else
    {
        // Match the IR path's limiter-oversampler latency (review B4). Cleared
        // on an IR -> IIR switch (enterCabMode) so it never replays a stale
        // few samples.
        iirPathAlign.process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                             buffer.getNumSamples());
    }

    // ── Cab-mode fade (IR-path plan Task 3) ──
    // Here, on the finished cab-path output at base rate, because it is the
    // one point both paths pass: after the IR path's convolution and limiter,
    // after the IIR path's alignment delay, so neither can leak un-faded
    // audio around a switch. Before delay/reverb, so their tails carry on
    // through the dip instead of being cut by it. After the limiters, so the
    // dip only lowers the level (≤ ±1 still holds). Untouched while idle: the
    // steady state is bit-identical. An oversample switch fading at the same
    // time multiplies with it (its fade ran on the downsampled amp above);
    // both ramps move 1/128 per sample, so the product moves at most 2/128 —
    // a deeper or longer dip, never a step.
    cabSwitcher.applyGain(buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                          buffer.getNumSamples());

    // ── Post-cab effects (delay → reverb) at base SR, after the amp/cab ──
    // Operate per channel: a stereo bus picks up reverb/delay width; mono
    // stays mono. Skipped entirely (bit-exact dry) when toggled off. On each
    // off→on edge restart() clears the tail the effect froze with while skipped,
    // snaps its smoothers and ramps its mix in from 0; on each on→off edge the
    // effect keeps running for one mix ramp down to 0 before it is skipped, so
    // switching it off doesn't cut the wet and step the dry (review B6).
    // Switched back on mid-fade, it never stopped running, so its tail is live
    // and the mix ramp simply reverses — no restart.
    if (delayOn)
    {
        float delayMs;
        if (delaySync <= 0)
        {
            // MANUAL — map time knob 0..1 → ms
            delayMs = DELAY_TIME_MIN_MS
                + delayTime * (DELAY_TIME_KNOB_MAX_MS - DELAY_TIME_MIN_MS);
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
        if (!delayWasOn && !delayFading) delay.restart();
        delayFading = false;
    }
    else if (delayWasOn)
    {
        delay.fadeOut();
        delayFading = !delay.fadedOut();   // mix already at 0: nothing to fade
    }
    const bool delayRan = delayOn || delayFading;
    if (delayRan) delay.process(buffer);
    if (delayFading && delay.fadedOut()) delayFading = false;
    delayWasOn = delayOn;

    if (reverbOn)
    {
        reverb.setParameters(reverbMix, reverbSz, reverbTn);
        if (!reverbWasOn && !reverbFading) reverb.restart();
        reverbFading = false;
    }
    else if (reverbWasOn)
    {
        reverb.fadeOut();
        reverbFading = !reverb.fadedOut();   // mix already at 0: nothing to fade
    }
    const bool reverbRan = reverbOn || reverbFading;
    if (reverbRan) reverb.process(buffer);
    if (reverbFading && reverb.fadedOut()) reverbFading = false;
    reverbWasOn = reverbOn;

    // ── MONO/STEREO widener (stereo bus only) ──
    // Skipped entirely (bit-exact dry) while the knob sits parked at 0 and the
    // internal smoother has settled — see StereoWidth::isActive(). Meaningless
    // on a mono bus, so guarded to two channels. On the OFF→on edge we clear the
    // Haas tail: while skipped, the delay line stops being primed, so without
    // this a re-engage could bleed ~12 ms of stale audio into the side voice.
    const bool widenerActive =
        buffer.getNumChannels() == 2 && (widthParam != 0.0f || stereoWidth.isActive());
    if (widenerActive)
    {
        if (!widenerWasActive) stereoWidth.clearTail();
        stereoWidth.process(buffer.getWritePointer(0), buffer.getWritePointer(1),
                            buffer.getNumSamples(), widthParam);
    }
    widenerWasActive = widenerActive;

    // ── OUTPUT trim (gain staging) ──
    // Post-limiter master: it rides on top of OUTPUT_MAKEUP_GAIN and cannot add
    // amp compression, because the amp's own tanh already ran. Runs on the full
    // stereo bus (delay/reverb/widener are stereo), advancing the smoother once
    // per SAMPLE rather than once per channel — per-channel advance would
    // desynchronise L and R during a ramp. Skipped entirely at unity.
    outputTrimSm.setTargetValue(outTrimParam);
    const bool outputTrimActive =
        outputTrimSm.isSmoothing() || outputTrimGain(outputTrimSm.getCurrentValue()) != 1.0f;
    // Only boosting can push past full scale; at or below unity the amp's own
    // tanh already guarantees ±1, so the dry path stays untouched. Tracked
    // per-sample (not sampled once after the loop) because a 50ms ramp can
    // cross down through unity mid-block on a large host buffer — sampling
    // the smoother's value only after the loop would miss samples that were
    // boosted earlier in the same block.
    bool outputBoosting = false;
    if (outputTrimActive)
    {
        const int n     = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            const float g = outputTrimGain(outputTrimSm.getNextValue());
            outputBoosting = outputBoosting || (g > 1.0f);
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer(ch)[i] *= g;
        }
    }

    // ── Post-effects safety limiter ──
    // delay/reverb run AFTER the amp's output limiter (their wet tails can sum
    // past full-scale), and the widener boosts one channel's peak above the
    // input (it can exceed ±1 even when the mono sum is in range). Both land
    // after the amp's own limiter, so soft-clip the finished bus back into ±1
    // (and scrub any non-finite sample) as a last line of defence. Runs ONLY
    // while an effect runs (fading out included), so the dry amp path stays
    // bit-identical when the whole module is off. NOTE: this per-channel clip
    // is nonlinear, so on peaks that actually hit the knee it marginally
    // perturbs the widener's exact mono-sum invariance — an accepted trade
    // against hard DAC clipping.
    if (delayRan || reverbRan || widenerActive || outputBoosting)
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

    // ── GUI level metering (RMS of channel 0, post-trim) ──
    if (buffer.getNumSamples() > 0)
        outputLevelForGUI.store(channelRms(buffer.getReadPointer(0), buffer.getNumSamples()),
                                std::memory_order_relaxed);

    // ── Grid current readout (block peak across both EL84 grids) ──
    // The tracked drop is a voltage; the current is drop / source resistance
    // (review S3). Both grids share GRID_SOURCE_R_EL84_N, so one division.
    gridCurrentForGUI.store(gridCurrentN(peakEl84GridDrop, el84GridA), std::memory_order_relaxed);

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

// ── Live tuner ring buffer ──
void SilentRiotProcessor::pushTunerSamples(const float* data, int n)
{
    if (data == nullptr || n <= 0) return;

    uint64_t pos = tunerWritePos.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        tunerRing[static_cast<size_t>((pos + static_cast<uint64_t>(i)) & kTunerRingMask)]
            .store(data[i], std::memory_order_relaxed);

    // Publish the advanced write position last (release) so a concurrent reader
    // that observes the new count also sees the samples written above.
    tunerWritePos.store(pos + static_cast<uint64_t>(n), std::memory_order_release);
}

int SilentRiotProcessor::readLatestTunerSamples(float* dest, int maxN) const
{
    if (dest == nullptr || maxN <= 0) return 0;

    const uint64_t pos = tunerWritePos.load(std::memory_order_acquire);
    const int available = static_cast<int>(std::min<uint64_t>(pos, kTunerRingSize));
    const int count = std::min(maxN, available);

    // Copy the last `count` samples ending at the write position (oldest first).
    const uint64_t start = pos - static_cast<uint64_t>(count);
    for (int i = 0; i < count; ++i)
        dest[i] = tunerRing[static_cast<size_t>((start + static_cast<uint64_t>(i)) & kTunerRingMask)]
                      .load(std::memory_order_relaxed);
    return count;
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

    // Reads, format-checks and rejects NaN/Inf samples (review S4) — the same
    // criteria the restore path applies, see UserIRState.h.
    UserIREntry entry;
    if (!sr::readUserIRFromFile(file, entry))
        return false;

    // The file is read outside the lock; the list and the cab change inside
    // it (irStateLock), so the cap is checked again there.
    const juce::ScopedLock irLock(irStateLock);
    if (static_cast<int>(userIRs.size()) >= sr::MAX_USER_IRS)
        return false;

    // Load into convolution immediately
    juce::AudioBuffer<float> bufCopy(entry.audioData);
    cabSim.loadIRFromBuffer(std::move(bufCopy), entry.sampleRate);

    userIRs.push_back(std::move(entry));

    // Set IR_INDEX to the new IR (1-based)
    int newIndex = static_cast<int>(userIRs.size());
    cachedIRIndex = newIndex;  // already applied: the timer must not reload it
    if (auto* param = apvts.getParameter(sr::Params::IDs::IR_INDEX))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(newIndex)));

    return true;
}

void SilentRiotProcessor::selectUserIR(int userIndex)
{
    // 0-based index into userIRs
    const juce::ScopedLock irLock(irStateLock);
    if (userIndex < 0 || userIndex >= static_cast<int>(userIRs.size())) return;

    auto& ir = userIRs[static_cast<size_t>(userIndex)];
    juce::AudioBuffer<float> bufCopy(ir.audioData);
    cabSim.loadIRFromBuffer(std::move(bufCopy), ir.sampleRate);

    int paramIndex = userIndex + 1; // 1-based
    cachedIRIndex = paramIndex;
    if (auto* param = apvts.getParameter(sr::Params::IDs::IR_INDEX))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(paramIndex)));
}

void SilentRiotProcessor::selectBuiltInCab()
{
    // Message thread. Applied here rather than left to the timer, like
    // selectUserIR: the convolution turns off now. The cache is set first, as
    // selectUserIR does, so no timer tick sees the new parameter value before
    // it is applied.
    const juce::ScopedLock irLock(irStateLock);
    cachedIRIndex = 0;
    if (auto* param = apvts.getParameter(sr::Params::IDs::IR_INDEX))
        param->setValueNotifyingHost(param->convertTo0to1(0.0f));
    applyIRIndexParam(sr::IRLoad::Async);
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

    // Reload the saved active IR — or disable the live one when the saved
    // index is past the restored list (review S2). Async: a restore can run
    // while processBlock does; the next prepareToPlay installs it at once.
    applyIRIndexParam(sr::IRLoad::Async);
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
        // Hosts may restore off the message thread: keep a timer tick from
        // applying IR_INDEX against a half-replaced userIRs (irStateLock).
        const juce::ScopedLock irLock(irStateLock);
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        loadUserIRsFromState();

        // Tuner mute is a transient performance control, not a session setting:
        // it silences the whole plugin, and its only affordance is a small pill
        // in the tuner strip. Restoring it would reopen a saved session dead
        // silent with no obvious cause, so it always loads OFF.
        if (auto* p = apvts.getParameter(Params::IDs::TUNER_MUTE))
            p->setValueNotifyingHost(0.0f);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SilentRiotProcessor(); }
