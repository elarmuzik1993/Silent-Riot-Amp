// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "AmpFaceplate.h"
#include "ColourPalette.h"
#include "FontData.h"

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
    // Chrome bezel
    g.setGradientFill(juce::ColourGradient(Colours::text, lx - r, midY - r,
                                           Colours::knobBody, lx + r, midY + r, false));
    g.fillEllipse(lx - r - 3.0f, midY - r - 3.0f, (r + 3.0f) * 2.0f, (r + 3.0f) * 2.0f);
    // Lens
    const juce::Colour lit = Colours::red;
    if (powered)
    {
        // Glow spilling onto the panel
        g.setGradientFill(juce::ColourGradient(lit.withAlpha(0.35f), lx, midY,
                                               lit.withAlpha(0.0f), lx + r * 3.0f, midY, true));
        g.fillEllipse(lx - r * 3.0f, midY - r * 3.0f, r * 6.0f, r * 6.0f);
        g.setGradientFill(juce::ColourGradient(lit.brighter(0.6f), lx - r * 0.3f, midY - r * 0.3f,
                                               lit.darker(0.6f), lx + r, midY + r, true));
    }
    else
    {
        g.setGradientFill(juce::ColourGradient(lit.darker(2.2f), lx - r * 0.3f, midY - r * 0.3f,
                                               Colours::bg, lx + r, midY + r, true));
    }
    g.fillEllipse(lx - r, midY - r, r * 2.0f, r * 2.0f);
    // Facets: a ring of short cuts across the lens
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    for (int i = 0; i < 8; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 8.0f;
        g.drawLine(lx + std::cos(a) * r * 0.35f, midY + std::sin(a) * r * 0.35f,
                   lx + std::cos(a) * r * 0.95f, midY + std::sin(a) * r * 0.95f, 0.8f);
    }
    // Highlight
    g.setColour(juce::Colours::white.withAlpha(powered ? 0.55f : 0.18f));
    g.fillEllipse(lx - r * 0.55f, midY - r * 0.6f, r * 0.5f, r * 0.35f);

    g.setColour(Colours::dim);
    g.setFont(Fonts::spaceMono(Fonts::Sizes::micro, true));
    g.drawText("POWER", juce::Rectangle<float>(lx - 40.0f, midY + r + 8.0f, 80.0f, 14.0f),
               juce::Justification::centred, false);
}

} // namespace sr
