#include "Reverb.h"
#include <algorithm>

namespace sr {

void Reverb::prepare(double sampleRate, int samplesPerBlock, int numChannels)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels      = static_cast<juce::uint32>(std::max(1, numChannels));
    reverb.prepare(spec);

    params.roomSize  = 0.5f;
    params.damping   = 0.5f;
    params.width     = 1.0f;
    params.wetLevel  = 0.0f;
    params.dryLevel  = 1.0f;
    params.freezeMode = 0.0f;
    reverb.setParameters(params);
    tailEstimate = 0.0f;
}

void Reverb::reset()
{
    reverb.reset();
}

void Reverb::setParameters(float mix, float size)
{
    mix  = juce::jlimit(0.0f, 1.0f, mix);
    size = juce::jlimit(0.0f, 1.0f, size);

    // Equal-power-ish wet/dry from a single mix control.
    params.wetLevel = mix;
    params.dryLevel = 1.0f - mix;
    params.roomSize = size;
    // Larger rooms damp a little less so big settings ring longer.
    params.damping  = juce::jlimit(0.0f, 1.0f, 0.7f - 0.4f * size);
    reverb.setParameters(params);

    // Freeverb decay scales roughly with room size; ~0.5..8 s is a sane range.
    tailEstimate = (mix > 0.0f) ? (0.5f + 7.5f * size) : 0.0f;
}

void Reverb::process(juce::AudioBuffer<float>& buffer)
{
    // juce::dsp::Reverb dispatches to processMono / processStereo based on the
    // block's channel count (asserts 1 or 2 — our bus is mono or stereo).
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    reverb.process(ctx);
}

} // namespace sr
