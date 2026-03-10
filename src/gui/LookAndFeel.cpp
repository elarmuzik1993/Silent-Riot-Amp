#include "LookAndFeel.h"
#include "ColourPalette.h"

namespace tt {

SilentRiotLookAndFeel::SilentRiotLookAndFeel()
{
    // Slider colours
    setColour(juce::Slider::rotarySliderFillColourId,    Colours::accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, Colours::track);
    setColour(juce::Slider::textBoxTextColourId,         Colours::bright);
    setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId,   Colours::surface);

    // Toggle button
    setColour(juce::ToggleButton::textColourId, Colours::label);
    setColour(juce::ToggleButton::tickColourId, Colours::accent);

    // ComboBox
    setColour(juce::ComboBox::backgroundColourId, Colours::surface);
    setColour(juce::ComboBox::textColourId,       Colours::text);
    setColour(juce::ComboBox::outlineColourId,    Colours::border);
    setColour(juce::ComboBox::arrowColourId,      Colours::label);

    // PopupMenu
    setColour(juce::PopupMenu::backgroundColourId,            Colours::panel);
    setColour(juce::PopupMenu::textColourId,                  Colours::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Colours::accent);
    setColour(juce::PopupMenu::highlightedTextColourId,       Colours::white);

    // Label
    setColour(juce::Label::textColourId, Colours::text);

    // ScrollBar
    setColour(juce::ScrollBar::thumbColourId, Colours::dim);
}

void SilentRiotLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                              float pos, float startAngle, float endAngle,
                                              juce::Slider&)
{
    // Basic rotary — NotchKnob replaces this with custom painting in Stage 2
    auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(6.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto cx = bounds.getCentreX(), cy = bounds.getCentreY();
    auto angle = startAngle + pos * (endAngle - startAngle);

    g.setColour(Colours::surface);
    g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    juce::Path arc;
    arc.addCentredArc(cx, cy, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, angle, true);
    g.setColour(Colours::accent);
    g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

    juce::Path ptr;
    ptr.addRectangle(-1.5f, -radius + 4.0f, 3.0f, radius * 0.5f);
    ptr.applyTransform(juce::AffineTransform::rotation(angle).translated(cx, cy));
    g.setColour(Colours::white);
    g.fillPath(ptr);
}

void SilentRiotLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& btn,
                                              bool /*highlighted*/, bool /*down*/)
{
    auto bounds = btn.getLocalBounds().toFloat();
    g.setColour(Colours::label);
    g.setFont(12.0f);
    g.drawText(btn.getButtonText(), bounds, juce::Justification::centred);
}

} // namespace tt
