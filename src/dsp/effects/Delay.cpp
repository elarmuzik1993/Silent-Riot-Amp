#include "Delay.h"
#include <algorithm>
#include <cmath>

namespace sr {

void Delay::prepare(double sampleRate, int samplesPerBlock, int numChannels)
{
    sr       = sampleRate;
    channels = std::max(1, numChannels);

    const int maxDelay = static_cast<int>(std::ceil(DELAY_TIME_MAX_MS * 0.001 * sr)) + 1;
    line.setMaximumDelayInSamples(maxDelay);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels      = static_cast<juce::uint32>(channels);
    line.prepare(spec);

    const double rampSec = 0.05;
    delaySamples.reset(sampleRate, rampSec);
    feedbackSm.reset(sampleRate, rampSec);
    mixSm.reset(sampleRate, rampSec);

    delaySamples.setCurrentAndTargetValue(static_cast<float>(currentTimeMs * 0.001 * sr));
    feedbackSm.setCurrentAndTargetValue(0.0f);
    mixSm.setCurrentAndTargetValue(0.0f);

    reset();
}

void Delay::reset()
{
    line.reset();
}

void Delay::setParameters(float timeMs, float feedback, float mix)
{
    currentTimeMs   = juce::jlimit(DELAY_TIME_MIN_MS, DELAY_TIME_MAX_MS, timeMs);
    currentFeedback = juce::jlimit(0.0f, DELAY_MAX_FEEDBACK, feedback);

    delaySamples.setTargetValue(static_cast<float>(currentTimeMs * 0.001 * sr));
    feedbackSm.setTargetValue(currentFeedback);
    mixSm.setTargetValue(juce::jlimit(0.0f, 1.0f, mix));
}

void Delay::process(juce::AudioBuffer<float>& buffer)
{
    const int numCh = std::min(buffer.getNumChannels(), channels);
    const int numSamples = buffer.getNumSamples();

    for (int n = 0; n < numSamples; ++n)
    {
        const float d  = delaySamples.getNextValue();
        const float fb = feedbackSm.getNextValue();
        const float mx = mixSm.getNextValue();

        line.setDelay(d);

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            const float in = data[n];

            const float wet = line.popSample(ch);
            line.pushSample(ch, in + wet * fb);

            data[n] = in * (1.0f - mx) + wet * mx;
        }
    }
}

float Delay::tailSeconds() const
{
    if (currentFeedback <= 0.0f)
        return static_cast<float>(currentTimeMs * 0.001);

    // Number of repeats for feedback^n to fall below -60 dB (0.001).
    const float repeats = std::log(0.001f) / std::log(std::max(currentFeedback, 1e-4f));
    return static_cast<float>(currentTimeMs * 0.001) * std::max(1.0f, repeats);
}

} // namespace sr
