// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "AmpFaceplate.h"
#include "ColourPalette.h"
#include "FontData.h"
#include <cmath>

namespace sr {

AmpFaceplate::AmpFaceplate()
{
    setInterceptsMouseClicks(false, false);
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
                             juce::Colour colour, float brightness)
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
                                               colour.withAlpha(0.0f), c.x + r * 3.2f, c.y, true));
        g.fillEllipse(circle(r * 3.2f));
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

    // Lens body: dark glass when off, the lamp colour when lit
    const juce::Colour glass = colour.darker(2.6f).interpolatedWith(colour, lit);
    g.setColour(glass);
    g.fillEllipse(circle(r));

    // Facets: an octagonal table, a ring of 8 kites and a ring of 16 triangles,
    // each shaded by how squarely it faces a light from the upper left
    const float lightA = -2.35f;
    const auto shade = [&](float faceA, float tilt) {
        const float facing = 0.5f + 0.5f * std::cos(faceA - lightA);
        const juce::Colour f = facing > 0.5f ? glass.brighter(tilt * (facing - 0.5f) * 1.6f)
                                             : glass.darker(tilt * (0.5f - facing) * 1.6f);
        g.setColour(f);
    };
    const auto at = [c](float a, float rad) {
        return juce::Point<float>(c.x + std::cos(a) * rad, c.y + std::sin(a) * rad);
    };
    const float r1 = r * 0.34f, r2 = r * 0.68f, step = twoPi / 8.0f;
    for (int i = 0; i < 8; ++i)
    {
        const float a = step * static_cast<float>(i);
        juce::Path kite;
        kite.startNewSubPath(at(a, r1));
        kite.lineTo(at(a + step * 0.5f, r2));
        kite.lineTo(at(a + step, r1));
        kite.lineTo(at(a + step * 0.5f, r1 * 0.55f));
        kite.closeSubPath();
        shade(a + step * 0.5f, 0.55f);
        g.fillPath(kite);
        for (int half = 0; half < 2; ++half)
        {
            const float a0 = a + step * 0.5f * static_cast<float>(half);
            juce::Path tri;
            tri.startNewSubPath(at(a0, half == 0 ? r1 : r2));
            tri.lineTo(at(a0 + step * 0.5f, half == 0 ? r2 : r1));
            tri.lineTo(at(a0 + step * 0.25f, r * 0.97f));
            tri.closeSubPath();
            shade(a0 + step * 0.25f, 0.9f);
            g.fillPath(tri);
        }
    }
    juce::Path table;
    for (int i = 0; i < 8; ++i)
    {
        const auto p = at(step * static_cast<float>(i) + step * 0.5f, r1 * 0.55f);
        if (i == 0) table.startNewSubPath(p); else table.lineTo(p);
    }
    table.closeSubPath();
    g.setColour(glass.brighter(0.25f));
    g.fillPath(table);

    // Hot core: the bulb seen through the glass
    if (lit > 0.0f)
    {
        g.setGradientFill(juce::ColourGradient(white.withAlpha(0.75f * lit), c.x, c.y,
                                               colour.brighter(0.5f).withAlpha(0.0f), c.x + r * 0.75f, c.y, true));
        g.fillEllipse(circle(r * 0.75f));
    }

    // Rim falloff and a specular glint
    g.setGradientFill(juce::ColourGradient(black.withAlpha(0.0f), c.x, c.y,
                                           black.withAlpha(0.45f), c.x + r, c.y, true));
    g.fillEllipse(circle(r));
    g.setColour(white.withAlpha(0.35f + 0.3f * lit));
    g.fillEllipse(c.x - r * 0.62f, c.y - r * 0.66f, r * 0.42f, r * 0.26f);
}

void AmpFaceplate::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced(0.5f);
    const float radius = 6.0f;

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

    // Corner screws
    const float inset = 10.0f;
    drawScrew(g, b.getX() + inset, b.getY() + inset);
    drawScrew(g, b.getRight() - inset, b.getY() + inset);
    drawScrew(g, b.getX() + inset, b.getBottom() - inset);
    drawScrew(g, b.getRight() - inset, b.getBottom() - inset);

    // ── Jewel pilot lamp, right end ──
    const float midY = b.getY() + 62.0f;  // level with the knob dials
    const float lx = b.getRight() - sideW * 0.5f, r = 11.0f;
    paintLamp(g, { lx, midY }, r, Colours::red, powered ? 1.0f : 0.0f);

    g.setColour(Colours::dim);
    g.setFont(Fonts::spaceMono(Fonts::Sizes::micro, true));
    g.drawText("POWER", juce::Rectangle<float>(lx - 40.0f, midY + r + 8.0f, 80.0f, 14.0f),
               juce::Justification::centred, false);
}

} // namespace sr
