#include "SectionBar.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace tt {

SectionBar::SectionBar(const juce::String& l) : label(l) {}

void SectionBar::setOpen(bool shouldOpen)
{
    if (open == shouldOpen) return;
    open = shouldOpen;
    startTimerHz(60); // animate chevron
    if (onToggle) onToggle(open);
}

void SectionBar::mouseDown(const juce::MouseEvent&)
{
    setOpen(!open);
}

void SectionBar::timerCallback()
{
    float target = open ? 180.0f : 0.0f;
    float diff = target - chevronAngle;
    // ~300ms at 60Hz: lerp factor 0.18
    chevronAngle += diff * 0.18f;
    if (std::abs(diff) < 0.5f)
    {
        chevronAngle = target;
        stopTimer();
    }
    repaint();
}

void SectionBar::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(open ? Colours::panelLight : Colours::panel);
    g.fillRect(bounds);

    // Label
    g.setFont(Fonts::spaceMono(8.0f, true));
    g.setColour(open ? Colours::accent : Colours::dim);
    g.drawText(label, juce::Rectangle<float>(16.0f, 0.0f, 200.0f, bounds.getHeight()),
               juce::Justification::centredLeft, false);

    // Hint text when closed
    if (!open && hintText.isNotEmpty())
    {
        g.setFont(Fonts::spaceMono(8.0f));
        g.setColour(Colours::muted);
        g.drawText(hintText, juce::Rectangle<float>(120.0f, 0.0f, bounds.getWidth() - 160.0f, bounds.getHeight()),
                   juce::Justification::centredLeft, false);
    }

    // Chevron (right side)
    float chevX = bounds.getWidth() - 28.0f;
    float chevY = bounds.getHeight() * 0.5f;

    juce::Path chevron;
    chevron.addTriangle(-4.0f, -2.5f, 4.0f, -2.5f, 0.0f, 2.5f);
    chevron.applyTransform(juce::AffineTransform::rotation(
        chevronAngle * juce::MathConstants<float>::pi / 180.0f)
        .translated(chevX, chevY));
    g.setColour(open ? Colours::accent : Colours::dim);
    g.fillPath(chevron);

    // Bottom border
    g.setColour(Colours::border);
    g.fillRect(0.0f, bounds.getHeight() - 1.0f, bounds.getWidth(), 1.0f);
}

} // namespace tt
