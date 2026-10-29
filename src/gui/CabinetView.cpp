// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "CabinetView.h"
#include "ColourPalette.h"
#include "FontData.h"
#include <cmath>

namespace sr {

CabinetView::CabinetView()
{
    setTitle("Cabinet");
}

void CabinetView::setSelection(const juce::String& newName, const juce::String& newDetail, bool step)
{
    if (newName == name && newDetail == detail && step == canStep)
        return;
    name = newName;
    detail = newDetail;
    canStep = step;
    setDescription(name);   // read out by screen readers
    repaint();
}

void CabinetView::setDragOver(bool over)
{
    if (over == dragOver)
        return;
    dragOver = over;
    repaint();
}

// The cab fills the component edge to edge, above the name strip: wider
// than a real 2x12's 3:2 front.
juce::Rectangle<float> CabinetView::cabBounds() const
{
    return { kMargin, 4.0f, static_cast<float>(getWidth()) - 2.0f * kMargin,
             static_cast<float>(getHeight()) - kNameH - 6.0f };
}

// Either side of the name, centred on the strip under the cab
juce::Point<float> CabinetView::arrowCentre(int direction) const
{
    const auto cab = cabBounds();
    return { cab.getCentreX() + static_cast<float>(direction) * kArrowOffset,
             cab.getBottom() + 4.0f + kNameH * 0.5f };
}

int CabinetView::arrowAt(juce::Point<float> p) const
{
    for (int dir : { -1, 1 })
        if (p.getDistanceFrom(arrowCentre(dir)) <= kArrowR + 4.0f)
            return dir;
    return 0;
}

void CabinetView::mouseDown(const juce::MouseEvent& e)
{
    const int dir = arrowAt(e.position);
    if (dir != 0 && canStep && onStep)
        onStep(dir);
}

void CabinetView::mouseMove(const juce::MouseEvent& e)
{
    const int dir = arrowAt(e.position);
    setMouseCursor(dir != 0 && canStep ? juce::MouseCursor::PointingHandCursor
                                       : juce::MouseCursor::NormalCursor);
    if (dir != hoverArrow)
    {
        hoverArrow = dir;
        repaint();
    }
}

void CabinetView::mouseExit(const juce::MouseEvent&)
{
    if (hoverArrow != 0)
    {
        hoverArrow = 0;
        repaint();
    }
}

// One 12" speaker seen from the front, radius r about c: cast basket rim with
// its mounting bolts, the surround roll, a ribbed paper cone and the dust cap.
static void paintSpeaker(juce::Graphics& g, juce::Point<float> c, float r)
{
    using juce::Colours::black;
    using juce::Colours::white;
    const auto circle = [c](float rad) {
        return juce::Rectangle<float>(c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
    };

    // Basket rim and bolts
    g.setGradientFill(juce::ColourGradient(Colours::knobBodyEdge, c.x - r, c.y - r,
                                           Colours::knobBody, c.x + r, c.y + r, false));
    g.fillEllipse(circle(r));
    for (int i = 0; i < 4; ++i)
    {
        const float a = juce::MathConstants<float>::pi * (0.25f + 0.5f * static_cast<float>(i));
        const float br = r * 0.05f;
        const juce::Point<float> b { c.x + std::cos(a) * r * 0.93f, c.y + std::sin(a) * r * 0.93f };
        g.setColour(Colours::muted);
        g.fillEllipse(b.x - br, b.y - br, br * 2.0f, br * 2.0f);
    }

    // Surround: a roll lit from the upper left
    g.setColour(Colours::bg);
    g.fillEllipse(circle(r * 0.87f));
    g.setGradientFill(juce::ColourGradient(Colours::dim.withAlpha(0.7f), c.x - r, c.y - r,
                                           black.withAlpha(0.0f), c.x + r * 0.5f, c.y + r * 0.5f, false));
    g.drawEllipse(circle(r * 0.83f), r * 0.07f);

    // Cone: deep at the centre, lit across the upper left, with pressed ribs
    g.setGradientFill(juce::ColourGradient(Colours::bg, c.x, c.y, Colours::knobFaceEdge,
                                           c.x + r * 0.78f, c.y, true));
    g.fillEllipse(circle(r * 0.78f));
    g.setGradientFill(juce::ColourGradient(white.withAlpha(0.08f), c.x - r * 0.6f, c.y - r * 0.6f,
                                           white.withAlpha(0.0f), c.x + r * 0.2f, c.y + r * 0.2f, false));
    g.fillEllipse(circle(r * 0.78f));
    for (float rib : { 0.66f, 0.54f, 0.42f })
    {
        g.setColour(black.withAlpha(0.3f));
        g.drawEllipse(circle(r * rib), 0.8f);
        g.setColour(white.withAlpha(0.05f));
        g.drawEllipse(circle(r * rib).translated(-0.6f, -0.6f), 0.6f);
    }

    // Dust cap: a dome with its highlight up and left
    const float cap = r * 0.27f;
    g.setGradientFill(juce::ColourGradient(Colours::knobBodyEdge.brighter(0.4f), c.x - cap * 0.35f, c.y - cap * 0.35f,
                                           Colours::knobBody, c.x + cap, c.y + cap, true));
    g.fillEllipse(circle(cap));
    g.setColour(black.withAlpha(0.45f));
    g.drawEllipse(circle(cap), 1.0f);
    g.setColour(white.withAlpha(0.22f));
    g.fillEllipse(c.x - cap * 0.6f, c.y - cap * 0.62f, cap * 0.55f, cap * 0.32f);
}

static void paintCorner(juce::Graphics& g, juce::Rectangle<float> r)
{
    using juce::Colours::black;
    const float cs = r.getWidth() * 0.32f;
    g.setGradientFill(juce::ColourGradient(Colours::bright, r.getX(), r.getY(),
                                           Colours::knobBody, r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle(r, cs);
    g.setColour(black.withAlpha(0.45f));
    g.drawRoundedRectangle(r, cs, 0.8f);
    const float sr = r.getWidth() * 0.11f;
    g.setColour(Colours::bg);
    g.fillEllipse(r.getCentreX() - sr, r.getCentreY() - sr, sr * 2.0f, sr * 2.0f);
}

void CabinetView::paintCabinet(juce::Graphics& g, juce::Rectangle<float> b)
{
    using juce::Colours::black;
    using juce::Colours::white;
    // Trim scales with the cab's height, so a wide cab keeps a 2x12's frame
    const float side = juce::jmin(b.getWidth(), b.getHeight());
    const float rad = side * 0.035f;

    // Drop shadow, then the tolex-covered box
    g.setColour(black.withAlpha(0.45f));
    g.fillRoundedRectangle(b.translated(0.0f, 3.0f), rad);
    g.setGradientFill(juce::ColourGradient(Colours::surface, 0.0f, b.getY(),
                                           Colours::bg, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle(b, rad);
    {
        // Pebble grain: a fixed scatter of light and dark specks
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(b, rad);
        g.reduceClipRegion(clip);
        juce::Random rng(0x2512);
        const int specks = static_cast<int>(b.getWidth() * b.getHeight() / 30.0f);
        for (int i = 0; i < specks; ++i)
        {
            g.setColour(((i & 1) != 0 ? white : black).withAlpha(0.06f));
            g.fillRect(b.getX() + rng.nextFloat() * b.getWidth(),
                       b.getY() + rng.nextFloat() * b.getHeight(), 1.0f, 1.0f);
        }
    }
    g.setColour(Colours::borderHi);
    g.drawRoundedRectangle(b, rad, 1.0f);

    // Baffle, and the two speakers on it
    const auto grille = b.reduced(side * 0.095f);
    const float gr = rad * 0.5f;
    g.setColour(Colours::bg);
    g.fillRoundedRectangle(grille, gr);
    const float spkR = juce::jmin(grille.getWidth() * 0.235f, grille.getHeight() * 0.45f);
    // Side by side in the middle, as in a real 2x12, however wide the box
    for (float dir : { -1.0f, 1.0f })
        paintSpeaker(g, { grille.getCentreX() + dir * spkR * 1.12f, grille.getCentreY() }, spkR);

    // Grille cloth over them: thin enough for the speakers to show through
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(grille, gr);
        g.reduceClipRegion(clip);
        g.setColour(Colours::panelLight.withAlpha(0.62f));
        g.fillRect(grille);
        g.setColour(Colours::borderHi.withAlpha(0.8f));
        const float step = 3.0f, h = grille.getHeight();
        for (float x = grille.getX() - h; x < grille.getRight(); x += step)
        {
            g.drawLine(x, grille.getBottom(), x + h, grille.getY(), 0.6f);
            g.drawLine(x, grille.getY(), x + h, grille.getBottom(), 0.6f);
        }
        // The frame's shadow falls across the top of the cloth
        g.setGradientFill(juce::ColourGradient(black.withAlpha(0.4f), 0.0f, grille.getY(),
                                               black.withAlpha(0.0f), 0.0f, grille.getY() + 10.0f, false));
        g.fillRect(grille.withHeight(10.0f));
    }

    // Piping: a light bead around the cloth with a dark seam outside it
    g.setColour(Colours::bg.withAlpha(0.8f));
    g.drawRoundedRectangle(grille.expanded(3.0f), gr + 3.0f, 1.0f);
    g.setColour(Colours::label.withAlpha(0.6f));
    g.drawRoundedRectangle(grille.expanded(1.2f), gr + 1.2f, 1.4f);

    // Chrome corner protectors
    const float s = side * 0.11f;
    paintCorner(g, { b.getX() - 1.0f, b.getY() - 1.0f, s, s });
    paintCorner(g, { b.getRight() - s + 1.0f, b.getY() - 1.0f, s, s });
    paintCorner(g, { b.getX() - 1.0f, b.getBottom() - s + 1.0f, s, s });
    paintCorner(g, { b.getRight() - s + 1.0f, b.getBottom() - s + 1.0f, s, s });
}

// The arrows are drawn, not typed: the embedded fonts have no arrow glyphs.
static void paintArrowButton(juce::Graphics& g, juce::Point<float> c, int direction,
                             bool enabled, bool hover)
{
    const float r = CabinetView::kArrowR;
    g.setColour(Colours::surface);
    g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour(enabled && hover ? Colours::accent : Colours::borderHi);
    g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);

    const float halfH = 5.5f, w = 6.5f;
    const float tip = c.x + static_cast<float>(direction) * w * 0.55f;
    const float base = c.x - static_cast<float>(direction) * w * 0.45f;
    juce::Path p;
    p.addTriangle(base, c.y - halfH, base, c.y + halfH, tip, c.y);
    g.setColour(!enabled ? Colours::label.withAlpha(0.35f)
                         : hover ? Colours::accent : Colours::label);
    g.fillPath(p);
}

void CabinetView::paint(juce::Graphics& g)
{
    const auto cab = cabBounds();
    paintCabinet(g, cab);

    if (dragOver)
    {
        g.setColour(Colours::accent.withAlpha(0.8f));
        g.drawRoundedRectangle(cab.expanded(4.0f), 6.0f, 2.0f);
        const auto pill = cab.withSizeKeepingCentre(120.0f, 22.0f);
        g.setColour(Colours::surface.withAlpha(0.9f));
        g.fillRoundedRectangle(pill, 4.0f);
        g.setFont(Fonts::spaceMono(Fonts::Sizes::small, true));
        g.setColour(Colours::accent);
        g.drawText("DROP TO LOAD", pill, juce::Justification::centred, false);
    }

    for (int dir : { -1, 1 })
        paintArrowButton(g, arrowCentre(dir), dir, canStep, hoverArrow == dir);

    // Name and detail fill the gap between the arrows
    const float textW = 2.0f * (kArrowOffset - kArrowR - 8.0f);
    const float textX = cab.getCentreX() - textW * 0.5f;
    g.setFont(Fonts::spaceMono(Fonts::Sizes::value, true));
    g.setColour(Colours::bright);
    g.drawText(name, juce::Rectangle<float>(textX, cab.getBottom() + 6.0f, textW, 16.0f),
               juce::Justification::centred, true);
    g.setFont(Fonts::spaceMono(Fonts::Sizes::micro));
    g.setColour(Colours::dim);
    g.drawText(detail, juce::Rectangle<float>(textX, cab.getBottom() + 23.0f, textW, 12.0f),
               juce::Justification::centred, true);
}

} // namespace sr
