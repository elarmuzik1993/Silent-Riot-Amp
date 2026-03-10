#include "SegmentedSelector.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace tt {

SegmentedSelector::SegmentedSelector(const juce::StringArray& opts, int defaultIdx)
    : options(opts), selectedIndex(defaultIdx) {}

void SegmentedSelector::setSelectedIndex(int idx)
{
    selectedIndex = juce::jlimit(0, options.size() - 1, idx);
    repaint();
}

void SegmentedSelector::mouseDown(const juce::MouseEvent& e)
{
    int n = options.size();
    if (n == 0) return;
    float cellW = getWidth() / static_cast<float>(n);
    int clicked = static_cast<int>(e.position.x / cellW);
    clicked = juce::jlimit(0, n - 1, clicked);
    if (clicked != selectedIndex)
    {
        selectedIndex = clicked;
        repaint();
        if (onChange) onChange(selectedIndex);
    }
}

void SegmentedSelector::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    int n = options.size();
    if (n == 0) return;

    float cellW = bounds.getWidth() / static_cast<float>(n);

    // Background
    g.setColour(Colours::surface);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(Colours::border);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    g.setFont(Fonts::spaceMono(8.0f, true));

    for (int i = 0; i < n; ++i)
    {
        auto cell = juce::Rectangle<float>(bounds.getX() + i * cellW, bounds.getY(), cellW, bounds.getHeight());
        bool active = (i == selectedIndex);

        if (active)
        {
            g.setColour(Colours::accent);
            g.fillRoundedRectangle(cell.reduced(1.0f), 3.0f);
            g.setColour(Colours::white);
        }
        else
        {
            g.setColour(Colours::label);
        }

        g.drawText(options[i], cell, juce::Justification::centred, false);
    }
}

} // namespace tt
