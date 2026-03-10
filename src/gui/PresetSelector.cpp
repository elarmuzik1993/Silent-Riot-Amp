#include "PresetSelector.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace tt {

PresetSelector::PresetSelector() {}

void PresetSelector::setPresetIndex(int idx)
{
    currentIndex = ((idx % numPresets) + numPresets) % numPresets;
    repaint();
}

void PresetSelector::mouseDown(const juce::MouseEvent& e)
{
    auto bounds = getLocalBounds().toFloat();
    float x = e.position.x;

    if (x < arrowW)
    {
        // Left arrow — previous
        setPresetIndex(currentIndex - 1);
        if (onPresetChanged) onPresetChanged(currentIndex);
    }
    else if (x > bounds.getWidth() - arrowW)
    {
        // Right arrow — next
        setPresetIndex(currentIndex + 1);
        if (onPresetChanged) onPresetChanged(currentIndex);
    }
}

void PresetSelector::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float h = bounds.getHeight();

    // Background
    g.setColour(Colours::surface);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(Colours::border);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    // Left arrow cell
    g.setColour(Colours::border);
    g.fillRect(arrowW, 0.0f, 1.0f, h);
    g.setFont(Fonts::spaceMono(11.0f));
    g.setColour(Colours::label);
    g.drawText(juce::CharPointer_UTF8("\xe2\x97\x82"),
               juce::Rectangle<float>(0.0f, 0.0f, arrowW, h),
               juce::Justification::centred, false);

    // Right arrow cell
    float rightX = bounds.getWidth() - arrowW;
    g.setColour(Colours::border);
    g.fillRect(rightX, 0.0f, 1.0f, h);
    g.setColour(Colours::label);
    g.drawText(juce::CharPointer_UTF8("\xe2\x96\xb8"),
               juce::Rectangle<float>(rightX, 0.0f, arrowW, h),
               juce::Justification::centred, false);

    // Center: preset name
    float centerW = bounds.getWidth() - arrowW * 2.0f;
    g.setFont(Fonts::spaceMono(10.0f));
    g.setColour(Colours::bright);
    g.drawText(presetNames[currentIndex],
               juce::Rectangle<float>(arrowW, 0.0f, centerW, h),
               juce::Justification::centred, false);
}

} // namespace tt
