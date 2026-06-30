#pragma once
// ═══════════════════════════════════════════════════════════════
// Delay — post-cab stereo delay/echo (thin wrapper over
// juce::dsp::DelayLine). Runs at base SR, after the amp+cab output.
// Per-channel feedback delay with wet/dry mix. Defaults bypassed.
// ═══════════════════════════════════════════════════════════════

#include <juce_dsp/juce_dsp.h>
#include "../../utils/Constants.h"

namespace sr {

class Delay {
public:
    void prepare(double sampleRate, int samplesPerBlock, int numChannels);
    void reset();

    // Per-block parameter update (called before processBlock).
    // timeMs / feedback / mix are the raw parameter values.
    void setParameters(float timeMs, float feedback, float mix);

    // Processes the buffer in place (wet/dry mixed). No allocation.
    void process(juce::AudioBuffer<float>& buffer);

    // Tail estimate for getTailLengthSeconds(): time it takes feedback
    // echoes to decay below ~-60 dB.
    float tailSeconds() const;

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line { 96000 };
    double sr = 44100.0;
    int channels = 2;

    // Smoothed at base SR to avoid zipper noise / pitch artefacts.
    juce::SmoothedValue<float> delaySamples;
    juce::SmoothedValue<float> feedbackSm;
    juce::SmoothedValue<float> mixSm;
    float currentFeedback = 0.0f;
    float currentTimeMs   = DELAY_TIME_MIN_MS;
};

} // namespace sr
