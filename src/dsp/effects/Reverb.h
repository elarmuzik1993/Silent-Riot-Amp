#pragma once
// ═══════════════════════════════════════════════════════════════
// Reverb — post-cab reverb (thin wrapper over juce::dsp::Reverb,
// Freeverb). Baseline room/plate voicing; a spring-style physical
// model is a future tuning item, not built here. Runs at base SR,
// after the amp+cab (and after delay). Defaults bypassed.
// ═══════════════════════════════════════════════════════════════

#include <juce_dsp/juce_dsp.h>

namespace sr {

class Reverb {
public:
    void prepare(double sampleRate, int samplesPerBlock, int numChannels);
    void reset();

    // mix / size are the raw 0..1 parameter values.
    void setParameters(float mix, float size);

    // Processes the buffer in place. No allocation.
    void process(juce::AudioBuffer<float>& buffer);

    float tailSeconds() const { return tailEstimate; }

private:
    juce::dsp::Reverb reverb;
    juce::dsp::Reverb::Parameters params;
    float tailEstimate = 0.0f;
};

} // namespace sr
