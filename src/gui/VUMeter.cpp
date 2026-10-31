// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "VUMeter.h"
#include "ColourPalette.h"
#include "FontData.h"

#include <cmath>

namespace sr {

VUMeter::VUMeter(const juce::String& l) : label(l)
{
    startTimerHz(60);
}

float VUMeter::barPositionFromAmplitude(float amplitude)
{
    // Not finite or at/below silence reads as an empty bar, never NaN into the lerp.
    if (!(amplitude > 0.0f) || !std::isfinite(amplitude)) return 0.0f;
    const float db = 20.0f * std::log10(amplitude);
    return juce::jlimit(0.0f, 1.0f, (db - kFloorDb) / -kFloorDb);
}

void VUMeter::setLevel(float amplitude)
{
    targetLevel = barPositionFromAmplitude(amplitude);
}

void VUMeter::setBallistics(int mode)
{
    switch (mode)
    {
        case 0:  lerpFactor = 0.30f; break;  // Fast
        case 2:  lerpFactor = 0.05f; break;  // Slow
        default: lerpFactor = 0.12f; break;  // Medium
    }
}

void VUMeter::setPeakHold(bool shouldHold)
{
    peakHoldEnabled = shouldHold;
    if (!shouldHold) peakLevel = 0.0f;
}

void VUMeter::timerCallback()
{
    float jitter = (rng.nextFloat() - 0.5f) * 2.0f * jitterAmount;
    float target = juce::jlimit(0.0f, 1.0f, targetLevel + jitter);
    displayLevel += (target - displayLevel) * lerpFactor;

    if (peakHoldEnabled)
    {
        if (displayLevel > peakLevel) peakLevel = displayLevel;
        else                          peakLevel *= peakDecay;
    }
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

        // Peak-hold marker: light the single segment at the held peak position
        bool isPeak = peakHoldEnabled && peakLevel > 0.001f
                   && drawIdx == juce::jlimit(0, numSegments - 1,
                          static_cast<int>(peakLevel * numSegments) - 1);

        if (isPeak)
            g.setColour(segColour);
        else if (active)
            g.setColour(segColour.withAlpha(0.9f));
        else
            g.setColour(Colours::track.withAlpha(0.45f));

        g.fillRect(meterX, y, meterW, segH);
    }

    // Label below
    g.setFont(Fonts::ui(Fonts::Sizes::small));
    g.setColour(Colours::dim);
    juce::Rectangle<float> labelBox(0.0f, meterH + 2.0f, bounds.getWidth(), labelH);
    g.drawText(label, labelBox, juce::Justification::centredTop, false);
}

} // namespace sr
