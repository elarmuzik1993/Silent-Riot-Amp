// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "ToggleSwitch.h"
#include "ColourPalette.h"
#include "FontData.h"
#include "AmpFaceplate.h"
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace sr {

ToggleSwitch::ToggleSwitch(const juce::String& l, juce::Colour color)
    : label(l), activeColor(color)
{
    button.setClickingTogglesState(true);
    button.onStateChange = [this] { startTimerHz(60); };
    addChildComponent(button); // hidden — we handle clicks

    setWantsKeyboardFocus(true);
    setTitle(l);
}

void ToggleSwitch::toggle()
{
    button.setToggleState(!button.getToggleState(), juce::sendNotification);
}

void ToggleSwitch::setOnSilently(bool on)
{
    button.setToggleState(on, juce::dontSendNotification);
    startTimerHz(60);
}

void ToggleSwitch::mouseDown(const juce::MouseEvent&)
{
    toggle();
}

bool ToggleSwitch::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
    {
        toggle();
        return true;
    }
    return false;
}

void ToggleSwitch::focusGained(juce::Component::FocusChangeType) { repaint(); }
void ToggleSwitch::focusLost(juce::Component::FocusChangeType)   { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> ToggleSwitch::createAccessibilityHandler()
{
    class Handler : public juce::AccessibilityHandler
    {
    public:
        explicit Handler(ToggleSwitch& s)
            : juce::AccessibilityHandler(
                  s, juce::AccessibilityRole::toggleButton,
                  juce::AccessibilityActions().addAction(
                      juce::AccessibilityActionType::toggle, [&s] { s.toggle(); })),
              owner(s) {}

        juce::AccessibleState getCurrentState() const override
        {
            auto state = juce::AccessibilityHandler::getCurrentState().withCheckable();
            return owner.getButton().getToggleState() ? state.withChecked() : state;
        }

    private:
        ToggleSwitch& owner;
    };
    return std::make_unique<Handler>(*this);
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

void ToggleSwitch::setStyle(Style s)
{
    if (style == s)
        return;
    style = s;
    repaint();
}

void ToggleSwitch::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (style == Style::Jewel)
    {
        paintJewel(g, bounds);
        return;
    }

    // Label above the lever
    g.setFont(Fonts::ui(Fonts::Sizes::small));
    g.setColour(Colours::label);
    g.drawText(label.toUpperCase(), juce::Rectangle<float>(0, 0, bounds.getWidth(), kLabelH),
               juce::Justification::centred, false);

    paintSwitch(g, bounds);
}

void ToggleSwitch::paintJewel(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // The lamp hugs the right edge with exactly its glow's room, so a row of
    // jewel toggles right-aligns on their lamps whatever the label length.
    const juce::Point<float> c(bounds.getRight() - kJewelGlowR, bounds.getCentreY());

    if (showLabel)
    {
        g.setFont(Fonts::ui(Fonts::Sizes::small, true));
        g.setColour(Colours::label.interpolatedWith(Colours::bright, thumbPos));
        g.drawText(label.toUpperCase(),
                   bounds.withRight(c.x - kJewelBezelR - kJewelLabelGap),
                   juce::Justification::centredRight, false);
    }

    AmpFaceplate::paintLamp(g, c, kJewelR, activeColor, thumbPos, kJewelGlowR / kJewelR);

    if (hasKeyboardFocus(false))
    {
        g.setColour(Colours::accent.withAlpha(0.7f));
        g.drawEllipse(juce::Rectangle<float>(2.0f * (kJewelGlowR - 1.0f), 2.0f * (kJewelGlowR - 1.0f))
                          .withCentre(c), 1.0f);
    }
}

void ToggleSwitch::paintSwitch(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    using juce::Colours::black;
    using juce::Colours::white;
    using Stops = std::initializer_list<std::pair<float, juce::Colour>>;
    const float pos = juce::jlimit(0.0f, 1.0f, thumbPos);
    const float cx = bounds.getWidth() * 0.5f, cy = kLabelH + kLabelGap + kBatPivot;
    const auto rgb = [](juce::uint32 argb) { return juce::Colour(argb); };
    const auto lighten = [](juce::Colour c, float t) { return c.interpolatedWith(juce::Colours::white, t); };
    const auto darken = [](juce::Colour c, float t) { return c.interpolatedWith(juce::Colours::black, t); };
    const auto circle = [](float x, float y, float r) { return juce::Rectangle<float>(x - r, y - r, r * 2.0f, r * 2.0f); };
    // Every dimension below is at scale 1; the whole switch grows about the nut.
    juce::Graphics::ScopedSaveState scaled(g);
    g.addTransform(juce::AffineTransform::scale(kBatScale, kBatScale, cx, cy));
    const auto withStops = [](juce::ColourGradient grad, Stops stops) {
        for (const auto& [p, col] : stops)
            if (p > 0.0f && p < 1.0f)
                grad.addColour(p, col);
        return grad;
    };
    const auto radial = [&](float x, float y, float r, Stops stops) {
        return withStops(juce::ColourGradient(stops.begin()->second, x, y,
                                              std::prev(stops.end())->second, x + r, y, true), stops);
    };
    const auto linear = [&](float x1, float y1, float x2, float y2, Stops stops) {
        return withStops(juce::ColourGradient(stops.begin()->second, x1, y1,
                                              std::prev(stops.end())->second, x2, y2, false), stops);
    };
    const auto hexagon = [](float x, float y, float r) {
        juce::Path p;
        for (int i = 0; i < 6; ++i)
        {
            const float a = juce::degreesToRadians(30.0f + 60.0f * static_cast<float>(i));
            const juce::Point<float> v(x + r * std::cos(a), y + r * std::sin(a));
            if (i == 0) p.startNewSubPath(v); else p.lineTo(v);
        }
        p.closeSubPath();
        return p;
    };

    // Soft contact shadow on the panel, then a faint glow from the lit ring
    g.setGradientFill(radial(cx + 0.8f, cy + 1.6f, 9.6f, { { 0.0f, black.withAlpha(0.55f) },
                                                            { 0.75f, black.withAlpha(0.35f) },
                                                            { 1.0f, black.withAlpha(0.0f) } }));
    g.fillEllipse(circle(cx + 0.8f, cy + 1.6f, 9.6f));
    if (pos > 0.0f)
    {
        g.setGradientFill(radial(cx, cy, 11.0f, { { 0.0f, activeColor.withAlpha(0.2f * pos) },
                                                  { 0.65f, activeColor.withAlpha(0.08f * pos) },
                                                  { 1.0f, activeColor.withAlpha(0.0f) } }));
        g.fillEllipse(circle(cx, cy, 11.0f));
    }

    // Narrow glass ring under the nut: a dark lip, dark tinted glass, and
    // the lit glass crossfaded over it on the toggle animation. Only the band
    // outside the nut shows, so the light stays a thin, quiet rim.
    g.setGradientFill(linear(cx - 8.5f, cy - 8.5f, cx + 8.5f, cy + 8.5f,
                             { { 0.0f, rgb(0xFF55575F) }, { 1.0f, rgb(0xFF08080A) } }));
    g.fillEllipse(circle(cx, cy, 8.5f));
    const juce::Colour off = darken(activeColor, 0.8f);
    g.setGradientFill(radial(cx, cy + 1.0f, 7.8f, { { 0.0f, darken(off, 0.4f) }, { 0.7f, off },
                                                    { 1.0f, lighten(off, 0.08f) } }));
    g.fillEllipse(circle(cx, cy, 7.8f));
    if (pos > 0.0f)
    {
        const float a = 0.85f * pos;
        g.setGradientFill(radial(cx, cy + 1.0f, 7.8f, { { 0.0f, darken(activeColor, 0.3f).withAlpha(a) },
                                                        { 0.8f, darken(activeColor, 0.15f).withAlpha(a) },
                                                        { 0.92f, activeColor.withAlpha(a) },
                                                        { 1.0f, lighten(activeColor, 0.15f).withAlpha(a) } }));
        g.fillEllipse(circle(cx, cy, 7.8f));
    }
    {
        juce::Path edge;  // catch-light on the ring's upper-left edge
        edge.addCentredArc(cx, cy, 7.3f, 7.3f, 0.0f,
                           juce::degreesToRadians(288.2f), juce::degreesToRadians(341.8f), true);
        g.setColour(white.withAlpha(0.35f));
        g.strokePath(edge, juce::PathStrokeType(0.45f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    // Hex nut: chamfered edge facets shaded by how they face a light from the
    // upper left, a flat top, and a hairline outline
    g.setColour(black.withAlpha(0.45f));
    g.fillPath(hexagon(cx + 0.7f, cy + 1.0f, 6.7f));
    for (int i = 0; i < 6; ++i)
    {
        const auto vertex = [&](int k, float r) {
            const float a = juce::degreesToRadians(30.0f + 60.0f * static_cast<float>(k % 6));
            return juce::Point<float>(cx + r * std::cos(a), cy + r * std::sin(a));
        };
        juce::Path facet;
        facet.startNewSubPath(vertex(i, 6.7f));
        facet.lineTo(vertex(i + 1, 6.7f));
        facet.lineTo(vertex(i + 1, 5.5f));
        facet.lineTo(vertex(i, 5.5f));
        facet.closeSubPath();
        const float facing = std::cos(juce::degreesToRadians(60.0f * static_cast<float>(i) + 60.0f - 225.0f));
        g.setColour(rgb(0xFF050506).interpolatedWith(rgb(0xFF6A6C76), (facing + 1.0f) * 0.5f));
        g.fillPath(facet);
    }
    g.setGradientFill(linear(cx - 5.5f, cy - 5.5f, cx + 5.5f, cy + 5.5f,
                             { { 0.0f, rgb(0xFF34353C) }, { 0.55f, rgb(0xFF17181C) }, { 1.0f, rgb(0xFF08080A) } }));
    g.fillPath(hexagon(cx, cy, 5.5f));
    g.setColour(black.withAlpha(0.4f));
    g.strokePath(hexagon(cx, cy, 6.7f), juce::PathStrokeType(0.35f, juce::PathStrokeType::curved));

    // Threaded bushing seen end on, and the hole the lever leaves through
    g.setGradientFill(linear(cx, cy - 4.2f, cx, cy + 4.2f, { { 0.0f, rgb(0xFF08080A) }, { 1.0f, rgb(0xFF44464E) } }));
    g.fillEllipse(circle(cx, cy, 4.2f));
    for (const float r : { 3.85f, 3.45f })
    {
        g.setColour(black.withAlpha(0.45f));
        g.drawEllipse(circle(cx, cy, r), 0.28f);
        g.setColour(white.withAlpha(0.2f));
        g.drawEllipse(circle(cx, cy + 0.25f, r), 0.22f);
    }
    g.setGradientFill(radial(cx, cy - 0.6f, 3.0f, { { 0.0f, black }, { 0.8f, rgb(0xFF0A0A0E) }, { 1.0f, rgb(0xFF2A2C34) } }));
    g.fillEllipse(circle(cx, cy, 3.0f));

    // The bat, foreshortened: the tip sits below the pivot when off, above it
    // when on, and passes through the pivot mid-flip
    const float k = 1.0f - 2.0f * pos, tipY = cy + 8.5f * k;
    const float ballR = 2.5f + 0.45f * std::abs(k), w0 = 1.75f, w1 = 1.25f;
    const bool hasShaft = std::abs(tipY - cy) > 0.4f;
    const auto shaft = [&](float dx, float dy, float grow) {
        juce::Path p;
        p.startNewSubPath(cx - w0 - grow + dx, cy + dy);
        p.lineTo(cx - w1 - grow + dx, tipY + dy);
        p.lineTo(cx + w1 + grow + dx, tipY + dy);
        p.lineTo(cx + w0 + grow + dx, cy + dy);
        p.closeSubPath();
        return p;
    };
    if (hasShaft)
    {
        g.setColour(black.withAlpha(0.14f));
        g.fillPath(shaft(2.6f, 3.0f, 0.9f));
        g.setColour(black.withAlpha(0.3f));
        g.fillPath(shaft(1.5f, 1.8f, 0.2f));
    }
    g.setColour(black.withAlpha(0.14f));
    g.fillEllipse(circle(cx + 2.4f, tipY + 2.8f, ballR + 0.9f));
    g.setColour(black.withAlpha(0.32f));
    g.fillEllipse(circle(cx + 1.4f, tipY + 1.7f, ballR));

    g.setGradientFill(radial(cx - 0.7f, cy - 0.8f, 2.8f, { { 0.0f, rgb(0xFF70727C) }, { 0.5f, rgb(0xFF24252B) }, { 1.0f, rgb(0xFF050506) } }));
    g.fillEllipse(circle(cx, cy, 2.3f));  // collar
    if (hasShaft)
    {
        g.setGradientFill(linear(cx - w0, cy, cx + w0, cy,
                                 { { 0.0f, rgb(0xFF050506) }, { 0.28f, rgb(0xFF2C2D33) }, { 0.4f, rgb(0xFF70727C) },
                                   { 0.55f, rgb(0xFF24252B) }, { 0.8f, rgb(0xFF0C0C0F) }, { 1.0f, rgb(0xFF050506) } }));
        g.fillPath(shaft(0.0f, 0.0f, 0.0f));
        // the shaft darkens toward the tip, where it reflects the panel
        g.setGradientFill(linear(cx, cy, cx, tipY, { { 0.0f, black.withAlpha(0.0f) }, { 0.75f, black.withAlpha(0.0f) },
                                                     { 1.0f, black.withAlpha(0.25f) } }));
        g.fillPath(shaft(0.0f, 0.0f, 0.0f));
    }

    // Black phenolic ball tip with a soft highlight
    g.setGradientFill(radial(cx - ballR * 0.35f, tipY - ballR * 0.4f, ballR * 1.35f,
                             { { 0.0f, rgb(0xFF5E616C) }, { 0.35f, rgb(0xFF1E1F26) },
                               { 0.85f, rgb(0xFF060608) }, { 1.0f, rgb(0xFF2A2C33) } }));
    g.fillEllipse(circle(cx, tipY, ballR));
    {
        const float hx = cx - ballR * 0.35f, hy = tipY - ballR * 0.42f;
        juce::Path spec;
        spec.addEllipse(hx - ballR * 0.32f, hy - ballR * 0.18f, ballR * 0.64f, ballR * 0.36f);
        spec.applyTransform(juce::AffineTransform::rotation(juce::degreesToRadians(-30.0f), hx, hy));
        g.setColour(white.withAlpha(0.7f));
        g.fillPath(spec);
    }
    g.setColour(black.withAlpha(0.35f));
    g.drawEllipse(circle(cx, tipY, ballR), 0.3f);

    // Keyboard focus ring around the glass ring
    if (hasKeyboardFocus(false))
    {
        g.setColour(Colours::accent.withAlpha(0.7f));
        g.drawEllipse(circle(cx, cy, 10.5f), 1.5f);
    }
}

} // namespace sr
