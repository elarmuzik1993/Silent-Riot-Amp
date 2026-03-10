#include "VUMeter.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace tt {

VUMeter::VUMeter(const juce::String& l) : label(l)
{
    startTimerHz(60);
}

void VUMeter::setLevel(float newLevel)
{
    targetLevel = juce::jlimit(0.0f, 1.0f, newLevel);
}

void VUMeter::timerCallback()
{
    float jitter = (rng.nextFloat() - 0.5f) * 2.0f * jitterAmount;
    float target = juce::jlimit(0.0f, 1.0f, targetLevel + jitter);
    displayLevel += (target - displayLevel) * lerpFactor;
    repaint();
}

void VUMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float meterW = 10.0f;
    float labelH = 16.0f;
    float meterH = bounds.getHeight() - labelH;
    float meterX = (bounds.getWidth() - meterW) * 0.5f;

    float segH = (meterH - (numSegments - 1) * 1.0f) / numSegments;
    float activeSegs = displayLevel * numSegments;

    for (int i = 0; i < numSegments; ++i)
    {
        // Bottom-up: segment 0 is bottom
        int drawIdx = numSegments - 1 - i;
        float y = meterH - (drawIdx + 1) * (segH + 1.0f);

        // Colour zones by position
        float posNorm = static_cast<float>(drawIdx) / static_cast<float>(numSegments);
        juce::Colour segColour;
        if (posNorm > 0.85f)      segColour = Colours::red;
        else if (posNorm > 0.70f) segColour = Colours::yellow;
        else                      segColour = Colours::green;

        bool active = (static_cast<float>(drawIdx) < activeSegs);
        if (active)
            g.setColour(segColour.withAlpha(0.9f));
        else
            g.setColour(Colours::track.withAlpha(0.3f));

        g.fillRect(meterX, y, meterW, segH);
    }

    // Label below
    g.setFont(Fonts::spaceMono(8.0f));
    g.setColour(Colours::dim);
    juce::Rectangle<float> labelBox(0.0f, meterH + 2.0f, bounds.getWidth(), labelH);
    g.drawText(label, labelBox, juce::Justification::centredTop, false);
}

} // namespace tt
