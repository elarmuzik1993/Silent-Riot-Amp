// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "AmpFaceplate.h"
#include "ColourPalette.h"
#include "FontData.h"
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace sr {

AmpFaceplate::AmpFaceplate()
{
    setInterceptsMouseClicks(false, false);
}

void AmpFaceplate::setLampCentre(juce::Point<float> centre)
{
    if (centre == lampCentre)
        return;
    lampCentre = centre;
    repaint();
}

void AmpFaceplate::setPowered(bool on)
{
    if (on == powered)
        return;
    powered = on;
    repaint();
}

static void drawScrew(juce::Graphics& g, float cx, float cy)
{
    const float r = 3.5f;
    g.setGradientFill(juce::ColourGradient(Colours::knobBodyEdge, cx - r, cy - r,
                                           Colours::knobBody, cx + r, cy + r, false));
    g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
    g.setColour(Colours::bg);
    g.drawLine(cx - r * 0.6f, cy + r * 0.6f, cx + r * 0.6f, cy - r * 0.6f, 1.0f);  // slot
}

void AmpFaceplate::paintLamp(juce::Graphics& g, juce::Point<float> c, float r,
                             juce::Colour colour, float brightness, float bloom)
{
    using juce::Colours::black;
    using juce::Colours::white;
    const float twoPi = juce::MathConstants<float>::twoPi;
    const float lit = juce::jlimit(0.0f, 1.0f, brightness);
    const auto circle = [c](float rad) {
        return juce::Rectangle<float>(c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
    };

    // Bloom spilling onto the panel
    if (lit > 0.0f)
    {
        g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.38f * lit), c.x, c.y,
                                               colour.withAlpha(0.0f), c.x + r * bloom, c.y, true));
        g.fillEllipse(circle(r * bloom));
    }

    // Drop shadow, then a knurled chrome bezel
    const float bezelR = r + 4.0f;
    g.setColour(black.withAlpha(0.45f));
    g.fillEllipse(circle(bezelR).translated(0.0f, 1.5f));
    g.setGradientFill(juce::ColourGradient(Colours::bright, c.x - bezelR, c.y - bezelR,
                                           Colours::knobBody, c.x + bezelR, c.y + bezelR, false));
    g.fillEllipse(circle(bezelR));
    g.setColour(black.withAlpha(0.3f));
    for (int i = 0; i < 28; ++i)
    {
        const float a = twoPi * static_cast<float>(i) / 28.0f;
        g.drawLine(c.x + std::cos(a) * (bezelR - 1.6f), c.y + std::sin(a) * (bezelR - 1.6f),
                   c.x + std::cos(a) * bezelR, c.y + std::sin(a) * bezelR, 0.7f);
    }
    g.setColour(black.withAlpha(0.6f));          // seat the lens in the bezel
    g.drawEllipse(circle(r + 0.6f), 1.2f);

    // Deep-ruby glass dome. Each layer is painted as the off glass, then the lit
    // glass on top at alpha `lit`, so brightness crossfades between the two.
    const auto lighten = [](juce::Colour col, float t) { return col.interpolatedWith(white, t); };
    const auto darken = [](juce::Colour col, float t) { return col.interpolatedWith(black, t); };
    const auto mixf = [lit](float off, float on) { return off + (on - off) * lit; };
    const juce::Colour off = darken(colour, 0.72f);
    const auto radial = [c, r](float cx, float cy, float rad,
                               std::initializer_list<std::pair<float, juce::Colour>> stops) {
        juce::ColourGradient grad(stops.begin()->second, c.x + cx * r, c.y + cy * r,
                                  std::prev(stops.end())->second, c.x + (cx + rad) * r, c.y + cy * r, true);
        for (const auto& [pos, col] : stops)
            if (pos > 0.0f && pos < 1.0f)
                grad.addColour(pos, col);
        return grad;
    };
    const auto linear = [c, r](float x1, float y1, juce::Colour c1, float x2, float y2, juce::Colour c2) {
        return juce::ColourGradient(c1, c.x + x1 * r, c.y + y1 * r, c2, c.x + x2 * r, c.y + y2 * r, false);
    };
    // A crescent: inside the circle of radius r1 (lens radii) and outside the same
    // circle of radius r2 offset by (dx, dy). Clipped to the first, filled even-odd.
    const auto fillLune = [&](float r1, float dx, float dy, float r2, const juce::ColourGradient& grad) {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addEllipse(circle(r * r1));
        g.reduceClipRegion(clip);
        juce::Path lune;
        lune.setUsingNonZeroWinding(false);
        lune.addEllipse(circle(r * r1));
        lune.addEllipse(circle(r * r2).translated(dx * r, dy * r));
        g.setGradientFill(grad);
        g.fillPath(lune);
    };
    const auto fillTiltedEllipse = [&](float cx, float cy, float rx, float ry, float degrees) {
        juce::Path e;
        e.addEllipse(c.x + (cx - rx) * r, c.y + (cy - ry) * r, rx * 2.0f * r, ry * 2.0f * r);
        e.applyTransform(juce::AffineTransform::rotation(juce::degreesToRadians(degrees),
                                                         c.x + cx * r, c.y + cy * r));
        g.fillPath(e);
    };

    // Lens body: lit from the bulb a little below centre, deep at the edge
    g.setGradientFill(radial(0.06f, 0.2f, 1.12f, { { 0.0f, lighten(off, 0.1f) }, { 0.5f, off },
                                                   { 0.85f, darken(off, 0.45f) }, { 1.0f, darken(off, 0.7f) } }));
    g.fillEllipse(circle(r));
    if (lit > 0.0f)
    {
        g.setGradientFill(radial(0.06f, 0.2f, 1.12f, { { 0.0f, lighten(colour, 0.35f).withAlpha(lit) },
                                                       { 0.35f, lighten(colour, 0.1f).withAlpha(lit) },
                                                       { 0.68f, colour.withAlpha(lit) },
                                                       { 0.9f, darken(colour, 0.5f).withAlpha(lit) },
                                                       { 1.0f, darken(colour, 0.75f).withAlpha(lit) } }));
        g.fillEllipse(circle(r));

        // Hot core: the bulb seen through the glass
        g.setGradientFill(radial(0.04f, 0.18f, 0.55f, { { 0.0f, white.withAlpha(0.75f * lit) },
                                                        { 0.4f, lighten(colour, 0.55f).withAlpha(0.35f * lit) },
                                                        { 1.0f, lighten(colour, 0.5f).withAlpha(0.0f) } }));
        g.fillEllipse(circle(r * 0.55f).translated(0.04f * r, 0.18f * r));
    }

    // Depth: the glass is thickest under the top rim, so the top edge darkens
    fillLune(1.0f, 0.0f, 0.16f, 1.0f, linear(0.0f, -1.0f, black.withAlpha(mixf(0.5f, 0.4f)),
                                             0.0f, -0.6f, black.withAlpha(0.0f)));

    // Caustic: light leaving the lower right of the dome, with a brighter meniscus line
    const juce::Colour caustic = lighten(colour, 0.75f), meniscus = lighten(colour, 0.9f);
    fillLune(0.93f, -0.12f, -0.2f, 0.93f, linear(-0.2f, -0.2f, caustic.withAlpha(0.0f),
                                                 0.65f, 0.65f, caustic.withAlpha(mixf(0.3f, 0.9f) * 1.15f)));
    fillLune(0.95f, -0.05f, -0.08f, 0.95f, linear(-0.2f, -0.2f, meniscus.withAlpha(0.0f),
                                                  0.65f, 0.65f, meniscus.withAlpha(mixf(0.22f, 0.7f) * 1.15f)));

    // Rim falloff
    g.setGradientFill(radial(0.0f, 0.0f, 1.0f, { { 0.0f, black.withAlpha(0.0f) }, { 0.82f, black.withAlpha(0.0f) },
                                                 { 1.0f, black.withAlpha(0.5f) } }));
    g.fillEllipse(circle(r));

    // Catch-light on the upper-left edge (JUCE arc angles run clockwise from 12 o'clock)
    {
        juce::Path edge;
        edge.addCentredArc(c.x, c.y, r * 0.93f, r * 0.93f, 0.0f,
                           juce::degreesToRadians(285.0f), juce::degreesToRadians(345.0f), true);
        g.setColour(white.withAlpha(mixf(0.3f, 0.35f)));
        g.strokePath(edge, juce::PathStrokeType(r * 0.032f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    // Reflection band hugging the top rim, a faint bounce low left, and the glint
    fillLune(0.86f, 0.0f, 0.3f, 0.9f, linear(0.0f, -0.86f, white.withAlpha(mixf(0.42f, 0.55f) * 0.9f),
                                             0.0f, -0.5f, white.withAlpha(0.0f)));
    g.setColour(white.withAlpha(mixf(0.18f, 0.14f)));
    fillTiltedEllipse(-0.52f, 0.5f, 0.15f, 0.06f, 45.0f);
    g.setColour(white.withAlpha(mixf(0.75f, 0.95f)));
    fillTiltedEllipse(-0.4f, -0.56f, 0.11f, 0.0605f, -30.0f);
}

void AmpFaceplate::paintPlate(juce::Graphics& g, juce::Rectangle<float> b, float radius)
{
    // Raised panel
    g.setGradientFill(juce::ColourGradient(Colours::panelLight, 0.0f, b.getY(),
                                           Colours::surface, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle(b, radius);

    // Grille-cloth weave: two sets of fine diagonals, clipped to the panel
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(b.reduced(3.0f), radius - 2.0f);
        g.reduceClipRegion(clip);
        g.setColour(Colours::border.withAlpha(0.55f));
        const float step = 5.0f, h = b.getHeight();
        for (float x = b.getX() - h; x < b.getRight(); x += step)
        {
            g.drawLine(x, b.getBottom(), x + h, b.getY(), 0.6f);
            g.drawLine(x, b.getY(), x + h, b.getBottom(), 0.6f);
        }
    }

    // Piping: a light outer bead and a dark inner shadow line
    g.setColour(Colours::borderHi);
    g.drawRoundedRectangle(b, radius, 1.0f);
    g.setColour(Colours::bg.withAlpha(0.8f));
    g.drawRoundedRectangle(b.reduced(2.0f), radius - 1.5f, 1.0f);
}

void AmpFaceplate::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced(0.5f);
    paintPlate(g, b, 6.0f);

    // Corner screws
    const float inset = 10.0f;
    drawScrew(g, b.getX() + inset, b.getY() + inset);
    drawScrew(g, b.getRight() - inset, b.getY() + inset);
    drawScrew(g, b.getX() + inset, b.getBottom() - inset);
    drawScrew(g, b.getRight() - inset, b.getBottom() - inset);

    // ── Glass pilot lamp, placed by the editor in the knob row ──
    const float midY = lampCentre.y, lx = lampCentre.x, r = 8.0f;
    paintLamp(g, { lx, midY }, r, Colours::red, powered ? 1.0f : 0.0f);

    g.setColour(Colours::dim);
    g.setFont(Fonts::ui(Fonts::Sizes::micro, true));
    g.drawText("POWER", juce::Rectangle<float>(lx - 40.0f, midY + r + 8.0f, 80.0f, 14.0f),
               juce::Justification::centred, false);
}

} // namespace sr
