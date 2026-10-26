#include "Reverb.h"
#include <algorithm>
#include <cmath>

namespace sr {

void Reverb::prepare(double sampleRate, int /*samplesPerBlock*/, int numChannels)
{
    channels = std::max(1, numChannels);

    const float sr = static_cast<float>(sampleRate);
    coreL.prepare(sr, 1.0f);
    coreR.prepare(sr, 1.031f);   // detune R for stereo decorrelation

    mixSm.reset(sampleRate, 0.05);
    mixSm.setCurrentAndTargetValue(0.0f);

    lastSize = -1.0f;
    lastTone = -1.0f;
    sizePending = false;
    tailEstimate = 0.0f;
}

void Reverb::reset()
{
    if (sizePending)   // never ramped: land it, there is nothing to sweep
    {
        coreL.setSize(lastSize);
        coreR.setSize(lastSize);
        sizePending = false;
    }
    coreL.reset();   // also lands a ramp or glide in progress
    coreR.reset();
}

void Reverb::restart()
{
    reset();
    const float mixTarget = mixSm.getTargetValue();
    mixSm.setCurrentAndTargetValue(0.0f);
    mixSm.setTargetValue(mixTarget);
}

void Reverb::setParameters(float mix, float size, float tone)
{
    mix  = juce::jlimit(0.0f, 1.0f, mix);
    size = juce::jlimit(0.0f, 1.0f, size);
    tone = juce::jlimit(0.0f, 1.0f, tone);

    mixSm.setTargetValue(mix);

    // SIZE/TONE change loop length & filter cutoffs — recompute only when they
    // actually move. A new SIZE is ramped across the next process() block and
    // glided per sample (SpringReverb.h, review S5); restart() lands both.
    if (std::abs(size - lastSize) > 1e-4f)
    {
        lastSize = size;
        sizePending = true;
    }
    if (std::abs(tone - lastTone) > 1e-4f)
    {
        coreL.setTone(tone);
        coreR.setTone(tone);
        lastTone = tone;
    }

    // RT60 estimate for getTailLengthSeconds(): loopLen · ln(0.001)/ln(feedback).
    // From the SIZE asked for, not the one the ramp has reached.
    const float fb = juce::jlimit(1e-4f, 0.999f, SpringReverbCore::feedbackFor(lastSize));
    tailEstimate = (mix > 0.0f)
        ? coreL.loopSecondsFor(lastSize) * (std::log(0.001f) / std::log(fb))
        : 0.0f;
}

void Reverb::process(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh = std::min(buffer.getNumChannels(), channels);

    // Equal-power wet/dry blend: constant perceived loudness across the sweep,
    // and still bit-exact dry at mix=0 (cos(0)=1, sin(0)=0).
    constexpr float kHalfPi = 1.57079632679f;

    if (sizePending && numSamples > 0)
    {
        coreL.rampSize(lastSize, numSamples);
        coreR.rampSize(lastSize, numSamples);
        sizePending = false;
    }

    if (numCh <= 1)
    {
        float* ch0 = buffer.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = mixSm.getNextValue();
            const float dryG = std::cos(m * kHalfPi);
            const float wetG = std::sin(m * kHalfPi);
            const float dry = ch0[i];
            const float wet = coreL.process(dry);
            ch0[i] = dry * dryG + wet * wetG;
        }
        return;
    }

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mixSm.getNextValue();
        const float dryG = std::cos(m * kHalfPi);
        const float wetG = std::sin(m * kHalfPi);
        const float dryL = L[i];
        const float dryR = R[i];
        const float send = 0.5f * (dryL + dryR);   // mono send into both tanks
        const float wetL = coreL.process(send);
        const float wetR = coreR.process(send);
        L[i] = dryL * dryG + wetL * wetG;
        R[i] = dryR * dryG + wetR * wetG;
    }
}

} // namespace sr
