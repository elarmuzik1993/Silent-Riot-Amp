#include "Delay.h"
#include <algorithm>
#include <cmath>

namespace sr {

void Delay::prepare(double sampleRate, int samplesPerBlock, int numChannels)
{
    sr       = sampleRate;
    channels = std::max(1, numChannels);

    // Room for the longest delay plus the wow+flutter sway and a few samples
    // of Lagrange3rd interpolation headroom.
    const double maxMs = DELAY_TIME_MAX_MS + DELAY_WOW_DEPTH_MS + DELAY_FLUTTER_DEPTH_MS + 2.0;
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

    wowDepthSamps     = static_cast<float>(DELAY_WOW_DEPTH_MS * 0.001 * sr);
    wowInc            = static_cast<float>(2.0 * PI_F * DELAY_WOW_RATE_HZ / sr);
    flutterDepthSamps = static_cast<float>(DELAY_FLUTTER_DEPTH_MS * 0.001 * sr);
    flutterInc        = static_cast<float>(2.0 * PI_F * DELAY_FLUTTER_RATE_HZ / sr);

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
    wowPhase     = 0.0f;
    flutterPhase = 0.0f;
}

void Delay::setParameters(float timeMs, float feedback, float mix, float tone, bool pingPong)
{
    currentTimeMs   = juce::jlimit(DELAY_TIME_MIN_MS, DELAY_TIME_MAX_MS, timeMs);
    currentFeedback = juce::jlimit(0.0f, DELAY_MAX_FEEDBACK, feedback);
    pingPongOn      = pingPong;

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

    // Equal-power wet/dry blend: constant perceived loudness across the sweep,
    // and still bit-exact dry at mix=0 (cos(0)=1, sin(0)=0). Matches Reverb.cpp.
    constexpr float kHalfPi = 1.57079632679f;

    // Feedback-loop saturation — unity gain at low level, soft-knee above.
    const float satK    = DELAY_FB_SAT_DRIVE;
    const float satNorm = 1.0f / std::tanh(satK);

    const bool doPingPong = pingPongOn && numCh >= 2;

    for (int n = 0; n < numSamples; ++n)
    {
        const float base = delaySamples.getNextValue();
        const float fb   = feedbackSm.getNextValue();
        const float mx   = mixSm.getNextValue();
        const float dryG = std::cos(mx * kHalfPi);
        const float wetG = std::sin(mx * kHalfPi);

        // Wow (slow) + flutter (fast) — sway the read tap for a living tape feel.
        const float mod = wowDepthSamps * std::sin(wowPhase)
                        + flutterDepthSamps * std::sin(flutterPhase);
        wowPhase += wowInc;
        if (wowPhase >= 2.0f * PI_F) wowPhase -= 2.0f * PI_F;
        flutterPhase += flutterInc;
        if (flutterPhase >= 2.0f * PI_F) flutterPhase -= 2.0f * PI_F;

        line.setDelay(juce::jmax(1.0f, base + mod));

        if (doPingPong)
        {
            // Cross-feed topology: a mono send triggers the L tap, which feeds
            // forward into the R tap (repeat #2), which feeds back into the L
            // tap (repeat #3), etc. — a genuine bouncing stereo echo rather
            // than two independent dual-mono lines.
            auto* L = buffer.getWritePointer(0);
            auto* R = buffer.getWritePointer(1);
            const float inL    = L[n];
            const float inR    = R[n];
            const float monoIn = 0.5f * (inL + inR);

            const float wetA = line.popSample(0);
            const float wetB = line.popSample(1);
            const float shapedA = loopHP[0].process(loopLP[0].process(wetA));
            const float shapedB = loopHP[1].process(loopLP[1].process(wetB));
            const float satA = std::tanh(shapedA * satK) * satNorm;
            const float satB = std::tanh(shapedB * satK) * satNorm;

            line.pushSample(0, monoIn + satB * fb);
            line.pushSample(1, satA * fb);

            L[n] = inL * dryG + satA * wetG;
            R[n] = inR * dryG + satB * wetG;
        }
        else
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* data = buffer.getWritePointer(ch);
                const float in = data[n];

                const float wet = line.popSample(ch);
                // Tone-shape the feedback path so each repeat darkens (analog/tape),
                // then soft-saturate it (what a tape head/BBD actually plays back).
                const float shaped = loopHP[static_cast<size_t>(ch)].process(
                                         loopLP[static_cast<size_t>(ch)].process(wet));
                const float sat = std::tanh(shaped * satK) * satNorm;
                line.pushSample(ch, in + sat * fb);

                data[n] = in * dryG + sat * wetG;
            }
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
