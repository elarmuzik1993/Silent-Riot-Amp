#include "ToggleSwitch.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace sr {

ToggleSwitch::ToggleSwitch(const juce::String& l, juce::Colour color)
    : label(l), activeColor(color)
{
    button.setClickingTogglesState(true);
    button.onStateChange = [this] { startTimerHz(60); };
    addChildComponent(button); // hidden — we handle clicks
}

void ToggleSwitch::mouseDown(const juce::MouseEvent&)
{
    button.setToggleState(!button.getToggleState(), juce::sendNotification);
}

void ToggleSwitch::timerCallback()
{
    float target = button.getToggleState() ? 1.0f : 0.0f;
    // Overshoot easing approximation
    float diff = target - thumbPos;
    thumbPos += diff * 0.2f;
    if (std::abs(diff) < 0.005f)
    {
        thumbPos = target;
        stopTimer();
    }
    repaint();
}

void ToggleSwitch::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float labelH = 14.0f;

    // Label above
    g.setFont(Fonts::spaceMono(8.0f));
    g.setColour(Colours::label);
    g.drawText(label.toUpperCase(), juce::Rectangle<float>(0, 0, bounds.getWidth(), labelH),
               juce::Justification::centred, false);

    // Track
    float trackX = (bounds.getWidth() - trackW) * 0.5f;
    float trackY = labelH + 4.0f;
    float trackRadius = trackH * 0.5f;

    juce::Colour trackColour = button.getToggleState()
        ? activeColor
        : Colours::track;
    g.setColour(trackColour);
    g.fillRoundedRectangle(trackX, trackY, trackW, trackH, trackRadius);

    // Thumb
    float thumbTravel = trackW - thumbD - 4.0f;
    float thumbX = trackX + 2.0f + thumbPos * thumbTravel;
    float thumbY = trackY + (trackH - thumbD) * 0.5f;

    if (button.getToggleState())
    {
        // Glow
        g.setColour(activeColor.withAlpha(0.25f));
        g.fillEllipse(thumbX - 5.0f, thumbY - 5.0f, thumbD + 10.0f, thumbD + 10.0f);
    }

    juce::Colour thumbCol = button.getToggleState() ? Colours::white : Colours::dim;
    g.setColour(thumbCol);
    g.fillEllipse(thumbX, thumbY, thumbD, thumbD);
}

} // namespace sr
