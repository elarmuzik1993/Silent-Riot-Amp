#include "SegmentedSelector.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace sr {

SegmentedSelector::SegmentedSelector(const juce::StringArray& opts, int defaultIdx)
    : options(opts), selectedIndex(defaultIdx)
{
    setWantsKeyboardFocus(true);
}

void SegmentedSelector::setTitleText(const juce::String& t)
{
    titlePrefix = t;
    setTitle(titlePrefix + ": " + options[juce::jlimit(0, options.size() - 1, selectedIndex)]);
}

void SegmentedSelector::setSelectedIndex(int idx)
{
    selectedIndex = juce::jlimit(0, options.size() - 1, idx);
    if (titlePrefix.isNotEmpty())
        setTitle(titlePrefix + ": " + options[selectedIndex]);
    repaint();
}

void SegmentedSelector::selectIndex(int idx)
{
    idx = juce::jlimit(0, options.size() - 1, idx);
    if (idx != selectedIndex)
    {
        setSelectedIndex(idx);
        if (onChange) onChange(selectedIndex);
    }
}

void SegmentedSelector::mouseDown(const juce::MouseEvent& e)
{
    int n = options.size();
    if (n == 0 || getWidth() <= 0) return;
    float cellW = getWidth() / static_cast<float>(n);
    selectIndex(static_cast<int>(e.position.x / cellW));
}

bool SegmentedSelector::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey)  { selectIndex(selectedIndex - 1); return true; }
    if (key == juce::KeyPress::rightKey) { selectIndex(selectedIndex + 1); return true; }
    return false;
}

void SegmentedSelector::focusGained(juce::Component::FocusChangeType) { repaint(); }
void SegmentedSelector::focusLost(juce::Component::FocusChangeType)   { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> SegmentedSelector::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::comboBox,
        juce::AccessibilityActions()
            .addAction(juce::AccessibilityActionType::showMenu,
                       [this] { selectIndex(selectedIndex + 1); }));
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

    // Keyboard focus indicator
    if (hasKeyboardFocus(false))
    {
        g.setColour(Colours::accent.withAlpha(0.7f));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 4.0f, 1.5f);
    }
}

} // namespace sr
