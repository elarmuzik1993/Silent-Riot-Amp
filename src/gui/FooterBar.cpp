#include "FooterBar.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace tt {

FooterBar::FooterBar() {}

void FooterBar::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Top border
    g.setColour(Colours::border);
    g.fillRect(0.0f, 0.0f, bounds.getWidth(), 1.0f);

    auto font = Fonts::spaceMono(9.0f);
    g.setFont(font);

    float y = 0.0f;
    float h = bounds.getHeight();
    float padding = 16.0f;

    // Left side
    g.setColour(Colours::dim);
    juce::String osText = juce::String(osFactor) + "x OS";
    g.drawText(osText, juce::Rectangle<float>(padding, y, 50.0f, h),
               juce::Justification::centredLeft, false);

    g.setColour(Colours::dim);
    g.drawText("LATENCY", juce::Rectangle<float>(padding + 56.0f, y, 60.0f, h),
               juce::Justification::centredLeft, false);
    g.setColour(Colours::text);
    juce::String latText = juce::String(latencyMs, 1) + "ms";
    g.drawText(latText, juce::Rectangle<float>(padding + 118.0f, y, 50.0f, h),
               juce::Justification::centredLeft, false);

    // Right side
    float rx = bounds.getWidth() - padding;

    juce::String srText = juce::String(static_cast<int>(sampleRate / 1000.0)) + "kHz / "
                         + juce::String(bitDepth) + "bit";
    float srW = font.getStringWidthFloat(srText) + 4.0f;
    g.setColour(Colours::text);
    g.drawText(srText, juce::Rectangle<float>(rx - srW, y, srW, h),
               juce::Justification::centredRight, false);
}

void FooterBar::setStats(int os, float lat, double sr, int bd)
{
    osFactor = os;
    latencyMs = lat;
    sampleRate = sr;
    bitDepth = bd;
    repaint();
}

} // namespace tt
