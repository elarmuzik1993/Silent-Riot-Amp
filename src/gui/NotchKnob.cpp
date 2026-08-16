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

void NotchKnob::setScale(float s)
{
    scale = juce::jmax(0.1f, s);
    // Floors, not plain products — the strip carries text. At scale 0.5 an
    // unfloored label row is 7px and the name disappears, which is the exact
    // failure preferredHeight exists to warn about.
    readoutH    = juce::jmax(13.0f, kReadoutHBase    * scale);
    readoutGap  = juce::jmax( 2.0f, kReadoutGapBase  * scale);
    labelH      = juce::jmax(12.0f, kLabelHBase      * scale);
    readoutMinW = juce::jmax(28.0f, kReadoutMinWBase * scale);
    resized();
    repaint();
}

void NotchKnob::resized()
{
    // Reserve the readout + label strip, then centre a square knob in what's
    // left — drawRotarySlider takes jmin(w,h), so a non-square slider would
    // just pad one axis.
    auto area = getLocalBounds();
    area.removeFromBottom(juce::roundToInt(stripH()));
    const int side = juce::jmin(area.getWidth(), area.getHeight());
    slider.setBounds(area.withSizeKeepingCentre(side, side));
}

void NotchKnob::paint(juce::Graphics& g)
{
    auto bounds     = getLocalBounds().toFloat();
    auto labelBox   = bounds.removeFromBottom(labelH);
    bounds.removeFromBottom(readoutGap);
    auto readoutRow = bounds.removeFromBottom(readoutH);

    // Value readout — knobs are 0..1 internally, displayed ×10 unless the owner
    // supplied a formatter (DELAY TIME's ms, the % knobs).
    const float pos = static_cast<float>(slider.getValue());
    const juce::String valText = valueFormatter ? valueFormatter(pos)
                                                : juce::String(pos * 10.0f, 1);
    auto valFont = Fonts::spaceMono(juce::jmax(Fonts::Sizes::micro,
                                               Fonts::Sizes::value * scale));
    g.setFont(valFont);

    const float valW = juce::jmax(readoutMinW,
                                  valFont.getStringWidthFloat(valText) + 20.0f * scale);
    auto valBox = readoutRow.withSizeKeepingCentre(valW, readoutRow.getHeight());

    g.setColour(Colours::surface);
    g.fillRoundedRectangle(valBox, 3.0f);
    g.setColour(Colours::border);
    g.drawRoundedRectangle(valBox, 3.0f, 1.0f);
    g.setColour(Colours::bright);
    g.drawText(valText, valBox, juce::Justification::centred, false);

    // Parameter name
    g.setFont(Fonts::spaceMono(juce::jmax(Fonts::Sizes::micro,
                                          Fonts::Sizes::body * scale), true));
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
