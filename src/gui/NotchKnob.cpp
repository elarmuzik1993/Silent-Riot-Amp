// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
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
    readoutH    = readoutHFor(scale);
    readoutGap  = readoutGapFor(scale);
    labelH      = labelHFor(scale);
    readoutMinW = juce::jmax(28.0f, kReadoutMinWBase * scale);
    resized();
    repaint();
}

void NotchKnob::setLabelOnTop(bool onTop)
{
    labelOnTop = onTop;
    resized();
    repaint();
}

void NotchKnob::setEngravedReadout(bool engraved)
{
    engravedReadout = engraved;
    resized();
    repaint();
}

void NotchKnob::resized()
{
    // Reserve the readout + label strip, then centre a square knob in what's
    // left — drawRotarySlider takes jmin(w,h), so a non-square slider would
    // just pad one axis.
    auto area = getLocalBounds();
    if (labelOnTop)
    {
        area.removeFromTop(juce::roundToInt(labelH + readoutGap));
        area.removeFromBottom(juce::roundToInt(readoutRowH()));
    }
    else
    {
        area.removeFromBottom(juce::roundToInt(stripH()));
    }
    if (tightDial)
    {
        // Size the square by its painted extent and hang it from the area's
        // top: its blank margins spill into the strips, which hold only text.
        const int side = juce::jmin(area.getWidth(),
                                    static_cast<int>(area.getHeight() / tightDialHeight(1.0f)));
        const int top  = area.getY() - juce::roundToInt(side * (0.5f - kDialTopReach));
        slider.setBounds(area.getCentreX() - side / 2, top, side, side);
        return;
    }
    const int side = juce::jmin(area.getWidth(), area.getHeight());
    slider.setBounds(area.withSizeKeepingCentre(side, side));
}

void NotchKnob::setTightDial(bool tight)
{
    tightDial = tight;
    resized();
}

void NotchKnob::paint(juce::Graphics& g)
{
    auto bounds     = getLocalBounds().toFloat();
    auto labelBox   = labelOnTop ? bounds.removeFromTop(labelH) : bounds.removeFromBottom(labelH);
    if (! labelOnTop)
        bounds.removeFromBottom(readoutGap);
    auto readoutRow = bounds.removeFromBottom(readoutRowH());

    // Value readout — knobs are 0..1 internally, displayed ×10 unless the owner
    // supplied a formatter (DELAY TIME's ms, the % knobs).
    const float pos = static_cast<float>(slider.getValue());
    const juce::String valText = valueFormatter ? valueFormatter(pos)
                                                : juce::String(pos * 10.0f, 1);
    auto valFont = Fonts::ui(juce::jmax(Fonts::Sizes::small,
                                               Fonts::Sizes::value * scale));
    g.setFont(valFont);

    if (engravedReadout)
    {
        // Printed on the panel: a dark copy a pixel down sets it into the surface
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawText(valText, readoutRow.translated(0.0f, 1.0f), juce::Justification::centred, false);
        g.setColour(Colours::text);
        g.drawText(valText, readoutRow, juce::Justification::centred, false);
    }
    else
    {
        const float valW = juce::jmax(readoutMinW,
                                      valFont.getStringWidthFloat(valText) + 20.0f * scale);
        auto valBox = readoutRow.withSizeKeepingCentre(valW, readoutRow.getHeight());

        g.setColour(Colours::surface);
        g.fillRoundedRectangle(valBox, 3.0f);
        g.setColour(Colours::border);
        g.drawRoundedRectangle(valBox, 3.0f, 1.0f);
        g.setColour(Colours::bright);
        g.drawText(valText, valBox, juce::Justification::centred, false);
    }

    // Parameter name — full size at any scale (see setScale)
    g.setFont(Fonts::ui(Fonts::Sizes::body, true));
    g.setColour(Colours::label);
    g.drawText(label.toUpperCase(), labelBox, juce::Justification::centred, false);
}

} // namespace sr
