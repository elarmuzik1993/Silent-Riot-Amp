// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "IRWaveformDisplay.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace sr {

IRWaveformDisplay::IRWaveformDisplay() {}

void IRWaveformDisplay::setIRData(const juce::AudioBuffer<float>& buffer, int sampleRate)
{
    int numSamples = buffer.getNumSamples();
    if (numSamples == 0) { clear(); return; }

    // Downsample to display width
    int displayW = 300;
    waveformData.resize(static_cast<size_t>(displayW));
    const float* data = buffer.getReadPointer(0);

    for (int i = 0; i < displayW; ++i)
    {
        int startSample = i * numSamples / displayW;
        int endSample = (i + 1) * numSamples / displayW;
        float maxAbs = 0.0f;
        for (int j = startSample; j < endSample && j < numSamples; ++j)
            maxAbs = std::max(maxAbs, std::abs(data[j]));
        waveformData[static_cast<size_t>(i)] = maxAbs;
    }

    durationMs = static_cast<float>(numSamples) / static_cast<float>(sampleRate) * 1000.0f;
    hasData = true;
    repaint();
}

void IRWaveformDisplay::setIRInfo(const juce::String& name, float ms)
{
    irName = name;
    durationMs = ms;
    repaint();
}

void IRWaveformDisplay::clear()
{
    waveformData.clear();
    hasData = false;
    irName = "IR IMPULSE";
    durationMs = 0.0f;
    repaint();
}

void IRWaveformDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(Colours::surface);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(Colours::border);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    float plotX = 4.0f;
    float plotY = 4.0f;
    float plotW = bounds.getWidth() - 8.0f;
    float plotH = bounds.getHeight() - 24.0f;
    float centerY = plotY + plotH * 0.5f;

    // Center line
    g.setColour(Colours::border.withAlpha(0.5f));
    float dashLen = 4.0f;
    for (float x = plotX; x < plotX + plotW; x += dashLen * 2.0f)
        g.fillRect(x, centerY, std::min(dashLen, plotX + plotW - x), 0.5f);

    // Grid lines
    g.setColour(Colours::border.withAlpha(0.3f));
    for (int i = 1; i < 4; ++i)
    {
        float gx = plotX + plotW * i / 4.0f;
        for (float y = plotY; y < plotY + plotH; y += dashLen * 2.0f)
            g.fillRect(gx, y, 0.5f, std::min(dashLen, plotY + plotH - y));
    }

    if (hasData && !waveformData.empty())
    {
        int n = static_cast<int>(waveformData.size());
        float xStep = plotW / static_cast<float>(n);

        // Normalize
        float maxVal = 0.001f;
        for (auto v : waveformData) maxVal = std::max(maxVal, v);

        // Envelope fill
        juce::Path envPath;
        envPath.startNewSubPath(plotX, centerY);
        for (int i = 0; i < n; ++i)
        {
            float v = waveformData[static_cast<size_t>(i)] / maxVal;
            float y1 = centerY - v * plotH * 0.45f;
            envPath.lineTo(plotX + i * xStep, y1);
        }
        for (int i = n - 1; i >= 0; --i)
        {
            float v = waveformData[static_cast<size_t>(i)] / maxVal;
            float y2 = centerY + v * plotH * 0.45f;
            envPath.lineTo(plotX + i * xStep, y2);
        }
        envPath.closeSubPath();
        g.setColour(Colours::accent.withAlpha(0.06f));
        g.fillPath(envPath);

        // Waveform stroke
        juce::Path wavePath;
        wavePath.startNewSubPath(plotX, centerY);
        for (int i = 0; i < n; ++i)
        {
            float v = waveformData[static_cast<size_t>(i)] / maxVal;
            wavePath.lineTo(plotX + i * xStep, centerY - v * plotH * 0.45f);
        }
        g.setColour(Colours::accent.withAlpha(0.85f));
        g.strokePath(wavePath, juce::PathStrokeType(1.2f));
    }

    // Labels
    float labelY = bounds.getHeight() - 18.0f;
    g.setFont(Fonts::ui(Fonts::Sizes::micro));
    g.setColour(Colours::dim);
    g.drawText(irName, juce::Rectangle<float>(plotX + 4.0f, labelY, 120.0f, 14.0f),
               juce::Justification::centredLeft, false);
    if (durationMs > 0.0f)
    {
        juce::String durText = juce::String(durationMs, 1) + "ms";
        g.drawText(durText, juce::Rectangle<float>(plotW - 40.0f, labelY, 44.0f, 14.0f),
                   juce::Justification::centredRight, false);
    }
}

} // namespace sr
