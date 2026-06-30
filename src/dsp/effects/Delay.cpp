#include "Delay.h"
#include <algorithm>
#include <cmath>

namespace sr {

void Delay::prepare(double sampleRate, int samplesPerBlock, int numChannels)
{
    sr       = sampleRate;
    channels = std::max(1, numChannels);

    // Room for the longest delay plus the wow/flutter sway and a few samples
    // of Lagrange3rd interpolation headroom.
    const double maxMs = DELAY_TIME_MAX_MS + DELAY_MOD_DEPTH_MS + 2.0;
    const int maxDelay = static_cast<int>(std::ceil(maxMs * 0.001 * sr)) + 4;
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

    modDepthSamps = static_cast<float>(DELAY_MOD_DEPTH_MS * 0.001 * sr);
    modInc        = static_cast<float>(2.0 * PI_F * DELAY_MOD_RATE_HZ / sr);

    const float fsr = static_cast<float>(sr);
    for (auto& f : loopLP) f.setFreq(DELAY_TONE_LP_MAX, fsr);
    for (auto& f : loopHP) f.setFreq(DELAY_LOOP_HP_HZ, fsr);

    reset();
}

void Delay::reset()
{
    line.reset();
    for (auto& f : loopLP) f.reset();
    for (auto& f : loopHP) f.reset();
    modPhase = 0.0f;
}

void Delay::setParameters(float timeMs, float feedback, float mix, float tone)
{
    currentTimeMs   = juce::jlimit(DELAY_TIME_MIN_MS, DELAY_TIME_MAX_MS, timeMs);
    currentFeedback = juce::jlimit(0.0f, DELAY_MAX_FEEDBACK, feedback);

    delaySamples.setTargetValue(static_cast<float>(currentTimeMs * 0.001 * sr));
    feedbackSm.setTargetValue(currentFeedback);
    mixSm.setTargetValue(juce::jlimit(0.0f, 1.0f, mix));

    // TONE 0..1 → feedback high-cut cutoff (dark → bright).
    const float t      = juce::jlimit(0.0f, 1.0f, tone);
    const float cutoff = DELAY_TONE_LP_MIN + t * (DELAY_TONE_LP_MAX - DELAY_TONE_LP_MIN);
    const float fsr    = static_cast<float>(sr);
    for (auto& f : loopLP) f.setFreqFast(cutoff, fsr);
}

void Delay::process(juce::AudioBuffer<float>& buffer)
{
    const int numCh = std::min({ buffer.getNumChannels(), channels, kMaxCh });
    const int numSamples = buffer.getNumSamples();

    for (int n = 0; n < numSamples; ++n)
    {
        const float base = delaySamples.getNextValue();
        const float fb   = feedbackSm.getNextValue();
        const float mx   = mixSm.getNextValue();

        // Wow/flutter — sway the read tap a fraction of a millisecond.
        const float mod = modDepthSamps * std::sin(modPhase);
        modPhase += modInc;
        if (modPhase >= 2.0f * PI_F) modPhase -= 2.0f * PI_F;

        line.setDelay(juce::jmax(1.0f, base + mod));

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            const float in = data[n];

            const float wet = line.popSample(ch);
            // Tone-shape the feedback path so each repeat darkens (analog/tape).
            const float shaped = loopHP[static_cast<size_t>(ch)].process(
                                     loopLP[static_cast<size_t>(ch)].process(wet));
            line.pushSample(ch, in + shaped * fb);

            data[n] = in * (1.0f - mx) + shaped * mx;
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
