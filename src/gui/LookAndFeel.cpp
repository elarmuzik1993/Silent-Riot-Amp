// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "LookAndFeel.h"
#include "ColourPalette.h"
#include "FontData.h"
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <vector>

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

namespace {

// Satin pointer knob: a black skirt with a chamfered bar across the top and a
// cream line along the bar. Drawn in a 76 px design square scaled to d; the
// bar is rotated point by point so the light (upper left) stays fixed, and
// each chamfer facet is shaded by how its outward normal faces that light.
void drawPointerKnob(juce::Graphics& g, juce::Point<float> c, float d, float angle)
{
    using juce::Colours::black;
    using juce::Colours::white;
    using Stops = std::initializer_list<std::pair<float, juce::Colour>>;
    const float k = d / 76.0f;
    const float sn = std::sin(angle), cs = std::cos(angle);
    const auto rgb = [](juce::uint32 argb) { return juce::Colour(argb); };
    // design units (knob centred on 0,0) -> screen, unrotated and rotated
    const auto at = [&](float x, float y) { return juce::Point<float>(c.x + x * k, c.y + y * k); };
    const auto turned = [&](float x, float y, float dx = 0.0f, float dy = 0.0f) {
        return at(x * cs - y * sn + dx, x * sn + y * cs + dy);
    };
    const auto circle = [&](float x, float y, float r) {
        return juce::Rectangle<float>(r * 2.0f * k, r * 2.0f * k).withCentre(at(x, y));
    };
    const auto withStops = [](juce::ColourGradient grad, Stops stops) {
        for (const auto& [p, col] : stops)
            if (p > 0.0f && p < 1.0f)
                grad.addColour(p, col);
        return grad;
    };
    const auto radial = [&](float x, float y, float r, Stops stops) {
        return withStops(juce::ColourGradient(stops.begin()->second, at(x, y),
                                              std::prev(stops.end())->second, at(x + r, y), true), stops);
    };
    const auto linear = [&](float x1, float y1, float x2, float y2, Stops stops) {
        return withStops(juce::ColourGradient(stops.begin()->second, at(x1, y1),
                                              std::prev(stops.end())->second, at(x2, y2), false), stops);
    };

    // Skirt: shadow on the panel, chamfered edge, satin top, a faint rim light
    const float skirtR = 18.5f;
    g.setGradientFill(radial(1.2f, 2.4f, skirtR + 2.8f, { { 0.0f, black.withAlpha(0.65f) },
                                                         { skirtR / (skirtR + 2.8f), black.withAlpha(0.45f) },
                                                         { 1.0f, black.withAlpha(0.0f) } }));
    g.fillEllipse(circle(1.2f, 2.4f, skirtR + 2.8f));
    g.setGradientFill(linear(-skirtR, -skirtR, skirtR, skirtR, { { 0.0f, rgb(0xFF4C4E55) }, { 0.3f, rgb(0xFF1E1F24) },
                                                                 { 0.55f, rgb(0xFF08080A) }, { 1.0f, black } }));
    g.fillEllipse(circle(0.0f, 0.0f, skirtR));
    g.setGradientFill(radial(-5.0f, -6.0f, 26.0f, { { 0.0f, rgb(0xFF16171B) }, { 0.5f, rgb(0xFF08080A) },
                                                    { 1.0f, rgb(0xFF030304) } }));
    g.fillEllipse(circle(0.0f, 0.0f, skirtR - 1.6f));
    {
        juce::Path rim;
        rim.addCentredArc(c.x, c.y, (skirtR - 0.8f) * k, (skirtR - 0.8f) * k, 0.0f,
                          juce::degreesToRadians(290.0f), juce::degreesToRadians(340.0f), true);
        g.setColour(white.withAlpha(0.16f));
        g.strokePath(rim, juce::PathStrokeType(0.55f * k, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    // Bar outline in design units, pointing to -y: a square front and a
    // semicircular back. The top face is the same shape inset by the chamfer.
    const auto outline = [](float halfW, float front, float backY) {
        std::vector<juce::Point<float>> pts { { -halfW, front }, { halfW, front }, { halfW, backY } };
        for (int i = 1; i < 12; ++i)
        {
            const float a = juce::degreesToRadians(15.0f * static_cast<float>(i));
            pts.push_back({ halfW * std::cos(a), backY + halfW * std::sin(a) });
        }
        pts.push_back({ -halfW, backY });
        return pts;
    };
    const auto outer = outline(7.0f, -18.0f, 6.8f), inner = outline(5.3f, -16.4f, 6.8f);
    const auto polygon = [&](const std::vector<juce::Point<float>>& pts, float dx, float dy) {
        juce::Path p;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const auto v = turned(pts[i].x, pts[i].y, dx, dy);
            if (i == 0) p.startNewSubPath(v); else p.lineTo(v);
        }
        p.closeSubPath();
        return p;
    };

    // Cast shadow of the bar on the skirt, soft then core
    g.setColour(black.withAlpha(0.2f));
    g.fillPath(polygon(outer, 3.2f, 4.4f));
    g.setColour(black.withAlpha(0.7f));
    g.fillPath(polygon(outer, 1.7f, 2.5f));

    // Chamfer facets. Satin: highlights at 45% of gloss strength.
    const float gloss = 0.45f, lightA = juce::degreesToRadians(315.0f);
    const juce::Colour facetBase = rgb(0xFF1E1F25), facetSpec = rgb(0xFFD2D4DC);
    for (size_t i = 0; i < outer.size(); ++i)
    {
        const size_t j = (i + 1) % outer.size();
        const float normal = std::atan2(outer[j].x - outer[i].x, -(outer[j].y - outer[i].y));
        const float facing = std::cos(normal + angle - lightA);
        const juce::Colour col = facing >= 0.0f
            ? facetBase.interpolatedWith(facetSpec, facing * facing * gloss)
            : facetBase.interpolatedWith(black, -facing * 0.85f);
        juce::Path facet;
        facet.startNewSubPath(turned(outer[i].x, outer[i].y));
        facet.lineTo(turned(outer[j].x, outer[j].y));
        facet.lineTo(turned(inner[j].x, inner[j].y));
        facet.lineTo(turned(inner[i].x, inner[i].y));
        facet.closeSubPath();
        g.setColour(col);
        g.fillPath(facet);
        g.strokePath(facet, juce::PathStrokeType(0.15f * k));  // closes hairline seams between facets
    }

    // Top face, lit from the upper left, and a hairline round the whole bar
    g.setGradientFill(linear(-12.0f, -14.0f, 12.0f, 14.0f, { { 0.0f, rgb(0xFF32333A) }, { 0.45f, rgb(0xFF18191D) },
                                                             { 1.0f, rgb(0xFF0A0A0C) } }));
    g.fillPath(polygon(inner, 0.0f, 0.0f));
    g.setColour(black.withAlpha(0.6f));
    g.strokePath(polygon(outer, 0.0f, 0.0f), juce::PathStrokeType(0.3f * k, juce::PathStrokeType::curved));

    // Pointer line: down the front chamfer, then along the top
    const juce::Colour lineCol = rgb(0xFFF2F1EC);
    const juce::PathStrokeType lineStroke(1.35f * k, juce::PathStrokeType::curved, juce::PathStrokeType::butt);
    juce::Path front, top;
    front.startNewSubPath(turned(0.0f, -18.0f));
    front.lineTo(turned(0.0f, -16.6f));
    top.startNewSubPath(turned(0.0f, -16.6f));
    top.lineTo(turned(0.0f, -5.5f));
    g.setColour(lineCol.interpolatedWith(black, 0.35f));
    g.strokePath(front, lineStroke);
    g.setColour(lineCol);
    g.strokePath(top, lineStroke);
}

} // namespace

void SilentRiotLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                              float pos, float startAngle, float endAngle,
                                              juce::Slider& slider)
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

    if (slider.getProperties()[kPointerKnobProperty])
    {
        drawPointerKnob(g, centre, d, angle);
        return;
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
