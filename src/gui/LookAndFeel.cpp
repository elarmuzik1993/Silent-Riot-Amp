// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "LookAndFeel.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace sr {

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
    const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat();
    const auto centre = bounds.getCentre();
    const float d = juce::jmin(bounds.getWidth(), bounds.getHeight());

    const float arcR    = d * 0.3200f;
    const float arcW    = d * 0.0467f;
    const float bodyR   = d * 0.2267f;
    const float faceR   = d * 0.1933f;
    const float lineW   = d * 0.0067f;
    const float tickIn  = d * 0.3533f;
    const float tickOut = d * 0.3900f;
    const float tickW   = d * 0.0100f;
    const float indTip  = d * 0.1733f;
    const float indBase = d * 0.1333f;
    const float indHalf = juce::degreesToRadians(6.4f);

    const float sweep = endAngle - startAngle;
    const float angle = startAngle + pos * sweep;

    // JUCE rotary angles: radians, clockwise, 0 = 12 o'clock. Hence sin for x
    // and -cos for y (the textbook convention would mirror the knob).
    auto polar = [centre](float a, float r)
    {
        return juce::Point<float>(centre.x + std::sin(a) * r,
                                  centre.y - std::cos(a) * r);
    };

    // 1 — ticks. Five uniform marks at 0, 0.25, 0.5, 0.75, 1.0; no major/minor
    // hierarchy. With no inactive range track these are the only thing showing
    // that the arc continues past a low value. If the scale reads faint on a
    // panel, lengthen them — don't add more.
    g.setColour(Colours::knobTick);
    for (int i = 0; i < 5; ++i)
    {
        const float a = startAngle + (static_cast<float>(i) * 0.25f) * sweep;
        g.drawLine({ polar(a, tickIn), polar(a, tickOut) }, tickW);
    }

    // 2 — value arc, drawn before the body so its round cap clips against the
    // body edge rather than floating. At exactly 0 the arc is empty — no cap is
    // stamped in its place, so a parameter parked at zero (gate, width, boost)
    // reads as genuinely off. The indicator still shows where the knob points.
    if (pos > 0.001f)
    {
        g.setColour(Colours::accent);
        juce::Path fill;
        fill.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.strokePath(fill, juce::PathStrokeType(arcW, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
    }

    // 3 — body
    const auto body = juce::Rectangle<float>(bodyR * 2.0f, bodyR * 2.0f).withCentre(centre);
    g.setColour(Colours::knobBody);      g.fillEllipse(body);
    g.setColour(Colours::knobBodyEdge);  g.drawEllipse(body, lineW);

    // 4 — face
    const auto face = juce::Rectangle<float>(faceR * 2.0f, faceR * 2.0f).withCentre(centre);
    g.setColour(Colours::knobFace);      g.fillEllipse(face);
    g.setColour(Colours::knobFaceEdge);  g.drawEllipse(face, lineW);

    // 5 — indicator, entirely inside the face and touching neither outline. The
    // ~0.15·D gap between it and the arc is intentional. The 6.4° half-angle
    // holds the base at ≈0.03·D; if indBase changes, recompute the angle to
    // preserve that width rather than carrying 6.4° over.
    juce::Path ind;
    ind.startNewSubPath(polar(angle, indTip));
    ind.lineTo         (polar(angle - indHalf, indBase));
    ind.lineTo         (polar(angle + indHalf, indBase));
    ind.closeSubPath();
    g.setColour(Colours::accent);
    g.fillPath(ind);
}

void SilentRiotLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& btn,
                                              bool /*highlighted*/, bool /*down*/)
{
    auto bounds = btn.getLocalBounds().toFloat();
    g.setColour(Colours::label);
    g.setFont(Fonts::spaceMono(Fonts::Sizes::value));
    g.drawText(btn.getButtonText(), bounds, juce::Justification::centred);
}

} // namespace sr
