#pragma once
// ═══════════════════════════════════════════════════════════════
// Reverb — post-cab spring-tank reverb. JUCE-facing wrapper around the
// pure-C++ SpringReverbCore (one core per channel; detuned L/R for genuine
// stereo width). Runs at base SR, after the amp+cab (and after delay).
// Defaults bypassed. Controls: MIX, SIZE, TONE.
// ═══════════════════════════════════════════════════════════════

#include <juce_dsp/juce_dsp.h>
#include "SpringReverb.h"

namespace sr {

class Reverb {
public:
    void prepare(double sampleRate, int samplesPerBlock, int numChannels);
    void reset();

    // Off→on edge (call after setParameters): reset() the tanks and ramp MIX in
    // from 0, so re-enabling doesn't replay the tail they froze with (review B6).
    void restart();

    // mix / size / tone are the raw 0..1 parameter values.
    void setParameters(float mix, float size, float tone);

    // Processes the buffer in place. No allocation.
    void process(juce::AudioBuffer<float>& buffer);

    float tailSeconds() const { return tailEstimate; }

private:
    SpringReverbCore coreL, coreR;
    int   channels = 2;
    float tailEstimate = 0.0f;

    juce::SmoothedValue<float> mixSm;
    float lastSize = -1.0f;
    float lastTone = -1.0f;
};

} // namespace sr
