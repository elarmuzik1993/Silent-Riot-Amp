#include "LookAndFeel.h"
namespace tt {
OrangeLookAndFeel::OrangeLookAndFeel() {
    setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFFFF6600));
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF2A2A2A));
}
void OrangeLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                          float pos, float startAngle, float endAngle, juce::Slider&) {
    auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(6.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto cx = bounds.getCentreX(), cy = bounds.getCentreY();
    auto angle = startAngle + pos * (endAngle - startAngle);
    // Background
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    // Arc
    juce::Path arc;
    arc.addCentredArc(cx, cy, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, angle, true);
    g.setColour(juce::Colour(0xFFFF6600));
    g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // Pointer
    juce::Path ptr;
    ptr.addRectangle(-1.5f, -radius + 4.0f, 3.0f, radius * 0.5f);
    ptr.applyTransform(juce::AffineTransform::rotation(angle).translated(cx, cy));
    g.setColour(juce::Colours::white);
    g.fillPath(ptr);
}
} // namespace tt
