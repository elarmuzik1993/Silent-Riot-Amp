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

// The cab takes all the height above the name strip at kCabAspect, centred,
// narrowed if the arrows either side would not fit.
juce::Rectangle<float> CabinetView::cabBounds() const
{
    const float h = static_cast<float>(getHeight()) - kNameH - 6.0f;
    const float maxW = static_cast<float>(getWidth()) - 2.0f * (kArrowGap + kArrowR + 8.0f);
    const float w = juce::jmin(h * kCabAspect, maxW);
    return { (static_cast<float>(getWidth()) - w) * 0.5f, 4.0f, w, w / kCabAspect };
}

// Either side of the cab, level with its middle
juce::Point<float> CabinetView::arrowCentre(int direction) const
{
    const auto cab = cabBounds();
    return { cab.getCentreX() + static_cast<float>(direction) * (cab.getWidth() * 0.5f + kArrowGap),
             cab.getCentreY() };
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
    // The dark-theme ramp, fixed: speakers are black in both themes and sit
    // behind the black grille on a black baffle
    const juce::Colour body { 0xFF15171E }, bodyEdge { 0xFF272B36 }, faceEdge { 0xFF20242E },
                       well { 0xFF08080C }, bolt { 0xFF6A6A80 }, roll { 0xFF7E7E96 };
    const auto circle = [c](float rad) {
        return juce::Rectangle<float>(c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
    };

    // Basket rim and bolts
    g.setGradientFill(juce::ColourGradient(bodyEdge, c.x - r, c.y - r,
                                           body, c.x + r, c.y + r, false));
    g.fillEllipse(circle(r));
    for (int i = 0; i < 4; ++i)
    {
        const float a = juce::MathConstants<float>::pi * (0.25f + 0.5f * static_cast<float>(i));
        const float br = r * 0.05f;
        const juce::Point<float> b { c.x + std::cos(a) * r * 0.93f, c.y + std::sin(a) * r * 0.93f };
        g.setColour(bolt);
        g.fillEllipse(b.x - br, b.y - br, br * 2.0f, br * 2.0f);
    }

    // Surround: a roll lit from the upper left
    g.setColour(well);
    g.fillEllipse(circle(r * 0.87f));
    g.setGradientFill(juce::ColourGradient(roll.withAlpha(0.7f), c.x - r, c.y - r,
                                           black.withAlpha(0.0f), c.x + r * 0.5f, c.y + r * 0.5f, false));
    g.drawEllipse(circle(r * 0.83f), r * 0.07f);

    // Cone: deep at the centre, lit across the upper left, with pressed ribs
    g.setGradientFill(juce::ColourGradient(well, c.x, c.y, faceEdge,
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
    g.setGradientFill(juce::ColourGradient(bodyEdge.brighter(0.4f), c.x - cap * 0.35f, c.y - cap * 0.35f,
                                           body, c.x + cap, c.y + cap, true));
    g.fillEllipse(circle(cap));
    g.setColour(black.withAlpha(0.45f));
    g.drawEllipse(circle(cap), 1.0f);
    g.setColour(white.withAlpha(0.22f));
    g.fillEllipse(c.x - cap * 0.6f, c.y - cap * 0.62f, cap * 0.55f, cap * 0.32f);
}

// A plain black plastic corner cap. Black in both themes, like the real part.
static void paintCorner(juce::Graphics& g, juce::Rectangle<float> r)
{
    const float cs = r.getWidth() * 0.32f;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xFF2A2D36), r.getX(), r.getY(),
                                           juce::Colour(0xFF050507), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle(r, cs);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRoundedRectangle(r.reduced(0.5f), cs, 0.8f);
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

    // Baffle, and the two speakers on it. The baffle is black in both themes:
    // it is what shows through the black steel grille's openings.
    const auto grille = b.reduced(side * 0.095f);
    const float gr = rad * 0.5f;
    g.setColour(juce::Colour(0xFF060608));
    g.fillRoundedRectangle(grille, gr);
    const float spkR = juce::jmin(grille.getWidth() * 0.235f, grille.getHeight() * 0.45f);
    // Side by side in the middle, as in a real 2x12, however wide the box
    for (float dir : { -1.0f, 1.0f })
        paintSpeaker(g, { grille.getCentreX() + dir * spkR * 1.12f, grille.getCentreY() }, spkR);

    // Expanded-metal grille over them, as on old-school metal cabs: a black
    // steel plate with staggered diamond openings (filled even-odd), so the
    // speakers show through every opening. Black in both themes, like the
    // real part; the upper edges of each opening catch the light.
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(grille, gr);
        g.reduceClipRegion(clip);
        juce::Path plate, edges;
        plate.setUsingNonZeroWinding(false);
        plate.addRectangle(grille);
        const float dw = 9.0f, dh = 4.4f;   // diamond opening
        int row = 0;
        for (float y = grille.getY(); y < grille.getBottom() + dh; y += dh * 0.5f + 0.9f, ++row)
            for (float x = grille.getX() + ((row & 1) != 0 ? dw * 0.5f : 0.0f) - dw;
                 x < grille.getRight() + dw; x += dw + 1.6f)
            {
                juce::Path d;
                d.startNewSubPath(x, y);
                d.lineTo(x + dw * 0.5f, y - dh * 0.5f);
                d.lineTo(x + dw, y);
                d.lineTo(x + dw * 0.5f, y + dh * 0.5f);
                d.closeSubPath();
                plate.addPath(d);
                edges.startNewSubPath(x, y);
                edges.lineTo(x + dw * 0.5f, y - dh * 0.5f);
                edges.lineTo(x + dw, y);
            }
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xFF24262C), grille.getX(), grille.getY(),
                                               juce::Colour(0xFF0C0D10), grille.getRight(), grille.getBottom(), false));
        g.fillPath(plate);
        g.setColour(white.withAlpha(0.13f));
        g.strokePath(edges, juce::PathStrokeType(0.6f));
        // The frame's shadow falls across the top of the cloth
        g.setGradientFill(juce::ColourGradient(black.withAlpha(0.4f), 0.0f, grille.getY(),
                                               black.withAlpha(0.0f), 0.0f, grille.getY() + 10.0f, false));
        g.fillRect(grille.withHeight(10.0f));
    }

    // Piping: an orange bead around the grille, the faceplate's one accent
    // colour, with a dark seam outside it
    g.setColour(Colours::bg.withAlpha(0.8f));
    g.drawRoundedRectangle(grille.expanded(3.0f), gr + 3.0f, 1.0f);
    g.setColour(Colours::accent.withAlpha(0.9f));
    g.drawRoundedRectangle(grille.expanded(1.2f), gr + 1.2f, 1.5f);

    // Corner protectors
    const float s = side * 0.11f;
    for (int sy : { -1, 1 })
        for (int sx : { -1, 1 })
            paintCorner(g, { sx < 0 ? b.getX() - 1.0f : b.getRight() - s + 1.0f,
                             sy < 0 ? b.getY() - 1.0f : b.getBottom() - s + 1.0f, s, s });
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
        g.setFont(Fonts::ui(Fonts::Sizes::small, true));
        g.setColour(Colours::accent);
        g.drawText("DROP TO LOAD", pill, juce::Justification::centred, false);
    }

    for (int dir : { -1, 1 })
        paintArrowButton(g, arrowCentre(dir), dir, canStep, hoverArrow == dir);

    const float textW = static_cast<float>(getWidth()) - 32.0f, textX = 16.0f;
    g.setFont(Fonts::ui(Fonts::Sizes::value, true));
    g.setColour(Colours::bright);
    g.drawText(name, juce::Rectangle<float>(textX, cab.getBottom() + 6.0f, textW, 16.0f),
               juce::Justification::centred, true);
    g.setFont(Fonts::ui(Fonts::Sizes::micro));
    g.setColour(Colours::dim);
    g.drawText(detail, juce::Rectangle<float>(textX, cab.getBottom() + 23.0f, textW, 12.0f),
               juce::Justification::centred, true);
}

} // namespace sr
