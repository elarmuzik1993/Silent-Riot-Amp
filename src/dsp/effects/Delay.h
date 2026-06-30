#pragma once
// ═══════════════════════════════════════════════════════════════
// Delay — post-cab stereo delay/echo (thin wrapper over
// juce::dsp::DelayLine). Runs at base SR, after the amp+cab output.
//
// Analog/tape voicing: the feedback path is tone-shaped (high-cut +
// fixed low-cut) so each repeat darkens cumulatively, and a slow wow/
// flutter LFO sways the read tap a fraction of a millisecond for a
// "living" tape character. Per-channel feedback with wet/dry mix.
// Defaults bypassed; bit-exact dry when mix=0.
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

    // Per-block parameter update (called before processBlock).
    // timeMs / feedback / mix / tone are the raw parameter values.
    // tone is 0..1 (0 = dark/tape, 1 = bright/digital) → feedback high-cut.
    void setParameters(float timeMs, float feedback, float mix, float tone);

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

    // Wow/flutter LFO modulating the read tap (mono — shared across channels).
    float modDepthSamps = 0.0f;
    float modInc        = 0.0f;
    float modPhase      = 0.0f;
};

} // namespace sr
