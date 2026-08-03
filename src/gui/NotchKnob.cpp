#include "NotchKnob.h"
#include "ColourPalette.h"
#include "FontData.h"
#include "LookAndFeel.h"

namespace sr {

NotchKnob::NotchKnob(const juce::String& l, float defaultValue)
    : label(l), defaultVal(defaultValue)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    // JUCE's default rotary sweep is not 270° — set ours explicitly so the
    // ticks in drawRotarySlider land where the value actually does.
    slider.setRotaryParameters(kRotaryStartAngle, kRotaryEndAngle, true);
    slider.setRange(0.0, 1.0, 0.001);
    slider.setMouseDragSensitivity(160);
    slider.setDoubleClickReturnValue(true, defaultVal);
    // Repaints this component's readout; the slider repaints itself.
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible(slider);
}

void NotchKnob::setKnobMode(int mode)
{
    // 0 = Linear: straight vertical drag. 1 = Rotary: circular drag around the knob.
    slider.setSliderStyle(mode == 1 ? juce::Slider::Rotary
                                    : juce::Slider::RotaryVerticalDrag);
}

void NotchKnob::resized()
{
    // Reserve the readout + label strip, then centre a square knob in what's
    // left — drawRotarySlider takes jmin(w,h), so a non-square slider would
    // just pad one axis.
    auto area = getLocalBounds();
    area.removeFromBottom(kStripH);
    const int side = juce::jmin(area.getWidth(), area.getHeight());
    slider.setBounds(area.withSizeKeepingCentre(side, side));
}

void NotchKnob::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    auto labelBox   = bounds.removeFromBottom(kLabelH).toFloat();
    bounds.removeFromBottom(kReadoutGap);
    auto readoutRow = bounds.removeFromBottom(kReadoutH).toFloat();

    // Value readout — knobs are 0..1 internally, displayed ×10.
    const float pos = static_cast<float>(slider.getValue());
    const juce::String valText(pos * 10.0f, 1);
    auto valFont = Fonts::spaceMono(11.0f);
    g.setFont(valFont);

    const float valW = juce::jmax(kReadoutMinW, valFont.getStringWidthFloat(valText) + 20.0f);
    auto valBox = readoutRow.withSizeKeepingCentre(valW, readoutRow.getHeight());

    g.setColour(Colours::surface);
    g.fillRoundedRectangle(valBox, 3.0f);
    g.setColour(Colours::border);
    g.drawRoundedRectangle(valBox, 3.0f, 1.0f);
    g.setColour(Colours::bright);
    g.drawText(valText, valBox, juce::Justification::centred, false);

    // Parameter name
    g.setFont(Fonts::spaceMono(9.0f, true));
    g.setColour(Colours::label);
    g.drawText(label.toUpperCase(), labelBox, juce::Justification::centred, false);
}

void NotchKnob::KnobSlider::paint(juce::Graphics& g)
{
    juce::Slider::paint(g);

    // Keyboard focus ring. The visible parameter name is a sibling of the
    // slider rather than its label, so focus has to be indicated on the knob.
    if (hasKeyboardFocus(false))
    {
        g.setColour(Colours::accent);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 2.0f);
    }
}

void NotchKnob::KnobSlider::focusGained(FocusChangeType c)
{
    juce::Slider::focusGained(c);
    repaint();
}

void NotchKnob::KnobSlider::focusLost(FocusChangeType c)
{
    juce::Slider::focusLost(c);
    repaint();
}

} // namespace sr
