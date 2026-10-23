#pragma once
// ═══════════════════════════════════════════════════════════════
// Delay — post-cab stereo delay/echo (thin wrapper over
// juce::dsp::DelayLine). Runs at base SR, after the amp+cab output.
//
// Analog/tape voicing: the feedback path is tone-shaped (high-cut +
// fixed low-cut) and soft-saturated (tanh) so each repeat darkens and
// compresses cumulatively, and a dual-rate wow+flutter LFO sways the
// read tap a fraction of a millisecond for a "living" tape character.
// Optional ping-pong mode cross-feeds the two channels for a bouncing
// stereo repeat instead of independent dual-mono. Equal-power wet/dry
// mix. Defaults bypassed; bit-exact dry when mix=0.
// ═══════════════════════════════════════════════════════════════

#include <juce_dsp/juce_dsp.h>
#include "../../utils/Constants.h"
#include "../filters/OnePole.h"
#include <array>

namespace sr {

class Delay {
public:
    void prepare(double sampleRate, int samplesPerBlock, int numChannels);
    void reset();

    // Off→on edge (call after setParameters): reset(), snap TIME and FEEDBACK to
    // their targets so a change made while off doesn't glide, and ramp MIX in
    // from 0. While off, process() isn't called, so the line still holds the
    // audio it had when it stopped and would replay it (review B6).
    void restart();

    // On→off edge: ramp MIX down to 0 over its usual 50 ms instead of cutting
    // the wet (and stepping the dry +3 dB at mix 0.5). Keep calling process()
    // until fadedOut(); from then on the processor skips it, bit-exact dry
    // (review B6).
    void fadeOut() { mixSm.setTargetValue(0.0f); }
    bool fadedOut() const { return !mixSm.isSmoothing(); }

    // Per-block parameter update (called before processBlock).
    // timeMs / feedback / mix / tone are the raw parameter values.
    // tone is 0..1 (0 = dark/tape, 1 = bright/digital) → feedback high-cut.
    // pingPong cross-feeds the two channels (bouncing L/R repeats) instead of
    // independent dual-mono; no-op on mono buffers.
    void setParameters(float timeMs, float feedback, float mix, float tone,
                        bool pingPong = false);

    // Processes the buffer in place (wet/dry mixed). No allocation.
    void process(juce::AudioBuffer<float>& buffer);

    // Tail estimate for getTailLengthSeconds(): time it takes feedback
    // echoes to decay below ~-60 dB.
    float tailSeconds() const;

private:
    // Lagrange3rd interpolation keeps the modulated (fractional) read clean.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> line { 96000 };
    double sr = 44100.0;
    int channels = 2;

    // Smoothed at base SR to avoid zipper noise / pitch artefacts.
    juce::SmoothedValue<float> delaySamples;
    juce::SmoothedValue<float> feedbackSm;
    juce::SmoothedValue<float> mixSm;
    float currentFeedback = 0.0f;
    float currentTimeMs   = DELAY_TIME_MIN_MS;

    // Feedback-path tone shaping — one filter pair per channel so each
    // repeat is darkened again (analog/tape behaviour).
    static constexpr int kMaxCh = 2;
    std::array<OnePoleLP, kMaxCh> loopLP;
    std::array<OnePoleHP, kMaxCh> loopHP;

    bool pingPongOn = false;

    // Wow (slow) + flutter (fast) LFOs modulating the read tap (mono — shared
    // across channels; ping-pong keeps its own per-tap timing via the line).
    float wowDepthSamps     = 0.0f;
    float wowInc            = 0.0f;
    float wowPhase          = 0.0f;
    float flutterDepthSamps = 0.0f;
    float flutterInc        = 0.0f;
    float flutterPhase      = 0.0f;
};

} // namespace sr
