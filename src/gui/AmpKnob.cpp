#include "AmpKnob.h"
namespace tt {
AmpKnob::AmpKnob(const juce::String& l) : label(l) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(slider);
}
void AmpKnob::resized() {
    auto a = getLocalBounds(); a.removeFromTop(18); slider.setBounds(a);
}
void AmpKnob::paint(juce::Graphics& g) {
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(12.0f));
    g.drawText(label, getLocalBounds().removeFromTop(18), juce::Justification::centred);
}
} // namespace tt
