// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "CabinetView.h"
#include "AmpFaceplate.h"
#include "ColourPalette.h"
#include "FontData.h"
#include "ProceduralTexture.h"
#include <cmath>
#include <vector>

namespace sr {

namespace {

constexpr float kDriveEpsilon = 0.004f;   // ~1/255: smaller moves are not visible

// The cab's glow is bolder than the amp plate's was: no value readouts sit on
// the cab, so the plate's text-contrast ceiling (kGlowAlphaMaxHalo) does not
// apply. Wide, faint strokes bloom onto the grille and the tolex around it.
constexpr float kCabGlowAlphaMax = 0.95f;
constexpr GlowStroke kCabHaloStrokes[] = { { 2.0f, 1.0f }, { 5.0f, 0.55f }, { 10.0f, 0.3f }, { 18.0f, 0.14f } };
constexpr float kCabBeadWidth = 3.0f;   // light theme: no halo, a thicker bead

float sanitiseUnit(float v)
{
    return std::isfinite(v) ? juce::jlimit(0.0f, 1.0f, v) : 0.0f;
}

// The orange piping bead around the grille, as paintCabinet() lays it out, and
// the stretch of its bottom run that has torn out of the channel (no glow there).
struct Piping { juce::Rectangle<float> bead; float radius; juce::Rectangle<float> gap; };

Piping pipingFor(juce::Rectangle<float> cab)
{
    const float side = juce::jmin(cab.getWidth(), cab.getHeight());
    const float gr = side * 0.035f * 0.5f;
    const auto grille = cab.reduced(side * 0.095f);
    const auto bead = grille.expanded(1.2f);
    const float gapX = grille.getX() + grille.getWidth() * 0.38f, gapW = side * 0.07f;
    return { bead, gr + 1.2f, { gapX, bead.getBottom() - 3.0f, gapW, 6.0f } };
}

} // namespace

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

void CabinetView::setDrive(float level, float heat)
{
    const float lv = sanitiseUnit(level), ht = sanitiseUnit(heat);
    const auto moved = [](float now, float was) {
        return std::abs(now - was) > kDriveEpsilon || ((now == 0.0f) != (was == 0.0f));
    };
    if (!moved(lv, driveLevel) && !moved(ht, driveHeat))
        return;
    driveLevel = lv;
    driveHeat = ht;
    const auto p = pipingFor(cabBounds());
    repaint(p.bead.expanded(kCabHaloStrokes[std::size(kCabHaloStrokes) - 1].width).getSmallestIntegerContainer());
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
// (sx, sy) is the corner it sits on (-1/+1 each); its outer tip, the part that
// takes the knocks, is rubbed to a dull grey sheen and scratched.
static void paintCorner(juce::Graphics& g, juce::Rectangle<float> r, int sx, int sy)
{
    const float cs = r.getWidth() * 0.32f;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xFF2A2D36), r.getX(), r.getY(),
                                           juce::Colour(0xFF050507), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle(r, cs);

    juce::Graphics::ScopedSaveState save(g);
    juce::Path cap;
    cap.addRoundedRectangle(r, cs);
    g.reduceClipRegion(cap);
    const juce::Point<float> tip { sx < 0 ? r.getX() : r.getRight(), sy < 0 ? r.getY() : r.getBottom() };
    const float w = r.getWidth();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xFF9A9CA4).withAlpha(0.16f), tip.x, tip.y,
                                           juce::Colour(0xFF9A9CA4).withAlpha(0.0f),
                                           tip.x - static_cast<float>(sx) * w * 0.6f, tip.y, true));
    g.fillRect(r);
    juce::Random rng(0x3C0 + (sx + 1) * 3 + (sy + 1));
    for (int i = 0; i < 4; ++i)
    {
        const float x0 = tip.x - static_cast<float>(sx) * rng.nextFloat() * w * 0.55f;
        const float y0 = tip.y - static_cast<float>(sy) * rng.nextFloat() * w * 0.55f;
        const float a = rng.nextFloat() * juce::MathConstants<float>::pi, len = w * (0.12f + 0.2f * rng.nextFloat());
        g.setColour(juce::Colours::white.withAlpha(0.06f + 0.08f * rng.nextFloat()));
        g.drawLine(x0, y0, x0 + std::cos(a) * len, y0 + std::sin(a) * len, 0.4f);
    }
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRoundedRectangle(r.reduced(0.5f), cs, 0.8f);
}

// Pebbled (levant) tolex over the whole box, as a texture image: a cellular
// grain, domain-warped so the pebbles are irregular, read as a height field
// and lit from the upper left like the rest of the UI, with the semi-gloss
// highlight vinyl has. Grime sits in the creases; the edges, where the box
// gets dragged and handled, are rubbed flat and shiny, and a few long scuffs
// run along the front. The grille's area is skipped: the baffle covers it.
static juce::Image renderTolex(juce::Rectangle<float> b, juce::Rectangle<float> covered, float scale)
{
    using namespace texture;
    const bool dark = Colours::surface.getPerceivedBrightness() < 0.5f;
    const juce::Colour light { 0xFFE8EAF0 }, shade { 0xFF000000 };
    const juce::Colour dirt  = dark ? juce::Colour(0xFF030304) : juce::Colour(0xFF3E3324);
    const juce::Colour worn  = dark ? juce::Colour(0xFF8E9098) : juce::Colour(0xFF5E5240);

    const int pw = juce::jmax(1, juce::roundToInt(b.getWidth() * scale));
    const int ph = juce::jmax(1, juce::roundToInt(b.getHeight() * scale));
    const float inv = 1.0f / scale;
    const auto idx = [pw](int x, int y) { return static_cast<size_t>(y) * static_cast<size_t>(pw) + static_cast<size_t>(x); };
    const auto skip = [&](float u, float v) { // well inside the grille: never seen
        return covered.reduced(3.0f).contains(b.getX() + u, b.getY() + v);
    };

    // Damage, placed in the frame band around the grille (and clear of the
    // corner caps), in units of that band so it holds at any size.
    const float W = b.getWidth(), Hgt = b.getHeight();
    const float band = juce::jmin(W, Hgt) * 0.095f;
    struct Tear { float cx, cy, rx, ry; int seed; };
    const Tear tears[] = {
        { W * 0.64f, Hgt - band * 0.22f, band * 0.80f, band * 0.46f, 401 },  // ripped up from the bottom edge
        { band * 0.28f, Hgt * 0.40f, band * 0.34f, band * 0.85f, 411 },      // down the left side
        { W * 0.29f, band * 0.52f, band * 0.30f, band * 0.17f, 421 },        // a gouge in the top
    };
    // Ragged outline: an ellipse whose radius wanders with the noise. < 1 inside.
    const auto tearDist = [](const Tear& t, float u, float v) {
        const float dx = (u - t.cx) / t.rx, dy = (v - t.cy) / t.ry;
        return std::sqrt(dx * dx + dy * dy)
             + 0.45f * (fbm(u / 4.5f, v / 4.5f, t.seed, 3) - 0.5f)
             + 0.16f * (fbm(u / 1.3f, v / 1.3f, t.seed + 7, 2) - 0.5f);
    };
    struct Dent { float cx, cy, sigma; };
    const Dent dents[] = {
        { W * 0.86f, band * 0.5f, band * 0.3f },
        { W - band * 0.5f, Hgt * 0.74f, band * 0.26f },
    };

    std::vector<float> height(static_cast<size_t>(pw) * static_cast<size_t>(ph), 0.0f);
    std::vector<float> crease(height.size(), 1.0f);
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x)
        {
            const float u = static_cast<float>(x) * inv, v = static_cast<float>(y) * inv;
            if (skip(u, v))
                continue;
            const float wu = u + 2.6f * (fbm(u / 10.0f, v / 10.0f, 201, 2) - 0.5f);
            const float wv = v + 2.6f * (fbm(u / 10.0f, v / 10.0f, 202, 2) - 0.5f);
            float f1 = 0.0f, f2 = 0.0f;
            cellular(wu / 3.3f, wv / 3.3f, 211, f1, f2);
            const float c = smoothstep(0.0f, 0.3f, f2 - f1);           // 0 in the creases
            const float dome = 1.0f - juce::jmin(1.0f, f1 * f1 * 1.3f);  // pebbles bulge
            float hgt = c * (0.55f + 0.45f * dome) + 0.12f * (fbm(u / 1.5f, v / 1.5f, 223, 2) - 0.5f);
            for (const auto& d : dents) // knocks pushed into the box
            {
                const float rr = ((u - d.cx) * (u - d.cx) + (v - d.cy) * (v - d.cy)) / (2.0f * d.sigma * d.sigma);
                hgt -= 8.0f * std::exp(-rr);
            }
            height[idx(x, y)] = hgt;
            crease[idx(x, y)] = c;
        }

    // Light from the upper left; the half-vector toward a viewer straight on
    const float lx = -0.28f, ly = -0.62f, lz = 0.73f;
    const float hn = std::sqrt(lx * lx + ly * ly + (lz + 1.0f) * (lz + 1.0f));
    const float hx = lx / hn, hy = ly / hn, hz = (lz + 1.0f) / hn;
    const float specFlat = std::pow(hz, 18.0f);

    juce::Image img(juce::Image::ARGB, pw, ph, true);
    {   // the pixel access is released before the image is returned
        juce::Image::BitmapData out(img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < ph; ++y)
        {
            const int y0 = juce::jmax(0, y - 1), y1 = juce::jmin(ph - 1, y + 1);
            for (int x = 0; x < pw; ++x)
            {
                const float u = static_cast<float>(x) * inv, v = static_cast<float>(y) * inv;
                if (skip(u, v))
                    continue;
                const int x0 = juce::jmax(0, x - 1), x1 = juce::jmin(pw - 1, x + 1);
                const float edge = juce::jmin(juce::jmin(u, b.getWidth() - u), juce::jmin(v, b.getHeight() - v));

                // Rubbed: the box's edges, and long scuffs along the front
                const float edgeRub = std::exp(-edge / 6.0f) * (0.3f + 0.9f * fbm(u / 14.0f, v / 14.0f, 231, 3));
                const float scuff = 0.8f * smoothstep(0.66f, 0.84f, fbm(u / 60.0f, v / 11.0f, 241, 3));
                const float wear = juce::jlimit(0.0f, 1.0f, edgeRub + scuff);

                const float relief = 0.7f * (1.0f - 0.7f * wear);
                const float dhdx = (height[idx(x1, y)] - height[idx(x0, y)]) / (static_cast<float>(x1 - x0) * inv) * relief;
                const float dhdy = (height[idx(x, y1)] - height[idx(x, y0)]) / (static_cast<float>(y1 - y0) * inv) * relief;
                const float nlen = std::sqrt(dhdx * dhdx + dhdy * dhdy + 1.0f);
                const float nx = -dhdx / nlen, ny = -dhdy / nlen, nz = 1.0f / nlen;
                const float lit = nx * lx + ny * ly + nz * lz - lz;
                const float spec = juce::jmax(0.0f, std::pow(juce::jmax(0.0f, nx * hx + ny * hy + nz * hz), 18.0f) - specFlat);

                Px p;
                if (lit > 0.0f)
                    p.over(light, lit * 0.28f);
                else
                    p.over(shade, -lit * 0.6f);
                p.over(light, spec * 0.16f);

                const float cavity = 1.0f - crease[idx(x, y)];
                const float soil = 0.10f + 0.16f * smoothstep(0.40f, 0.80f, fbm(u / 70.0f, v / 40.0f, 251, 3));
                p.over(dirt, soil * cavity * (1.0f - 0.6f * wear));
                p.over(worn, wear * 0.11f);

                // Rubbed through: stretches of the very edge worn down to the
                // tolex's fabric backing
                const float rubThrough = smoothstep(3.0f, 0.8f, edge)
                                       * smoothstep(0.60f, 0.70f, fbm(u / 26.0f, v / 26.0f, 261, 2));
                p.over(juce::Colour(0xFF6B5F50), rubThrough * 0.75f);

                // Tears: plywood where the tolex has ripped away, a shadow cast
                // into the hole by the cut edge, and that edge, curled up,
                // showing its pale backing and catching the light on the far side
                for (const auto& t : tears)
                {
                    if (std::abs(u - t.cx) > t.rx * 1.6f || std::abs(v - t.cy) > t.ry * 1.6f)
                        continue;
                    const float d = tearDist(t, u, v);
                    const float hole = smoothstep(1.03f, 0.97f, d);
                    const float lip = smoothstep(1.32f, 1.03f, d) * smoothstep(0.97f, 1.03f, d);
                    if (hole <= 0.0f && lip <= 0.0f)
                        continue;
                    float dx = (u - t.cx) / t.rx, dy = (v - t.cy) / t.ry;
                    const float dl = std::sqrt(dx * dx + dy * dy) + 1.0e-4f;
                    dx /= dl; dy /= dl;
                    const float towardLight = (dx * lx + dy * ly) / 0.68f; // +1: this side faces the light

                    if (hole > 0.0f)
                    {
                        const float grain = fbm(u / 45.0f, v / 2.0f, 271, 3);
                        const float fibre = fbm(u / 8.0f, v / 0.9f, 281, 2);
                        const juce::Colour wood = juce::Colour(0xFF35261A)
                            .interpolatedWith(juce::Colour(0xFF6A5036), juce::jlimit(0.0f, 1.0f, grain * 0.8f + fibre * 0.35f));
                        p.over(wood, hole);
                        p.over(shade, hole * 0.3f); // old wood, glue-stained and dirty
                        p.over(shade, hole * 0.6f * smoothstep(0.55f, 1.0f, d) * juce::jmax(0.0f, towardLight));
                    }
                    if (lip > 0.0f)
                    {
                        p.over(juce::Colour(0xFFB8AD98), lip * 0.26f * smoothstep(0.3f, 0.6f, fbm(u / 2.2f, v / 2.2f, t.seed + 13, 2)));
                        p.over(light, lip * 0.35f * juce::jmax(0.0f, -towardLight));
                    }
                }

                out.setPixelColour(x, y, p.colour());
            }
        }
    }
    return img;
}

// Dust on the grille's steel and the piping: light grey, uneven, thickest
// where it settles against the frame. Drawn clipped to the steel (and the
// piping), so the openings stay dark.
static juce::Image renderGrilleDust(juce::Rectangle<float> area, juce::Rectangle<float> grille, float scale)
{
    using namespace texture;
    const juce::Colour dust { 0xFF948F84 }, rustColour { 0xFF9A5428 };
    const int pw = juce::jmax(1, juce::roundToInt(area.getWidth() * scale));
    const int ph = juce::jmax(1, juce::roundToInt(area.getHeight() * scale));
    const float inv = 1.0f / scale;
    juce::Image img(juce::Image::ARGB, pw, ph, true);
    {   // the pixel access is released before the image is returned
        juce::Image::BitmapData out(img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < ph; ++y)
            for (int x = 0; x < pw; ++x)
            {
                const float u = static_cast<float>(x) * inv, v = static_cast<float>(y) * inv;
                const float gx = area.getX() + u, gy = area.getY() + v;
                const float edge = juce::jmax(0.0f, juce::jmin(juce::jmin(gx - grille.getX(), grille.getRight() - gx),
                                                               juce::jmin(gy - grille.getY(), grille.getBottom() - gy)));
                const float a = 0.03f
                              + 0.13f * smoothstep(0.42f, 0.80f, fbm(u / 38.0f, v / 22.0f, 301, 4))
                              + 0.12f * std::exp(-edge / 9.0f) * (0.4f + fbm(u / 12.0f, v / 12.0f, 311, 2))
                              + 0.04f * (fbm(u / 2.0f, v / 2.0f, 321, 2) - 0.5f);
                // Rust on the steel, in spots, worst toward the bottom where
                // moisture sits; kept off the piping (outside the grille)
                const float rust = smoothstep(0.56f, 0.68f, fbm(u / 16.0f, v / 12.0f, 331, 3))
                                 * smoothstep(1.0f, 5.0f, edge)
                                 * (0.35f + 0.65f * smoothstep(0.3f, 1.0f, v / area.getHeight()));
                out.setPixelColour(x, y, dust.interpolatedWith(rustColour, rust)
                                             .withAlpha(juce::jlimit(0.0f, 1.0f, juce::jmax(a, rust * 0.75f))));
            }
    }
    return img;
}

void CabinetView::paintCabinet(juce::Graphics& g, juce::Rectangle<float> b, float scale)
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
    const auto grille = b.reduced(side * 0.095f);
    {
        // Pebbled tolex, worn at the edges
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(b, rad);
        g.reduceClipRegion(clip);
        g.drawImage(renderTolex(b, grille, scale), b);
    }
    g.setColour(Colours::borderHi);
    g.drawRoundedRectangle(b, rad, 1.0f);

    // Baffle, and the two speakers on it. The baffle is black in both themes:
    // it is what shows through the black steel grille's openings.
    const float gr = rad * 0.5f;
    g.setColour(juce::Colour(0xFF060608));
    g.fillRoundedRectangle(grille, gr);
    const float spkR = juce::jmin(grille.getWidth() * 0.235f, grille.getHeight() * 0.45f);
    // Side by side in the middle, as in a real 2x12, however wide the box
    for (float dir : { -1.0f, 1.0f })
        paintSpeaker(g, { grille.getCentreX() + dir * spkR * 1.12f, grille.getCentreY() }, spkR);

    const auto dustArea = grille.expanded(4.0f);
    const juce::Image dust = renderGrilleDust(dustArea, grille, scale);

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
        {
            // Dust on the steel only; the openings stay dark
            juce::Graphics::ScopedSaveState dustSave(g);
            g.reduceClipRegion(plate);
            g.drawImage(dust, dustArea);
        }
        // The frame's shadow falls across the top of the cloth
        g.setGradientFill(juce::ColourGradient(black.withAlpha(0.4f), 0.0f, grille.getY(),
                                               black.withAlpha(0.0f), 0.0f, grille.getY() + 10.0f, false));
        g.fillRect(grille.withHeight(10.0f));
    }

    // Piping: an orange bead around the grille, the faceplate's one accent
    // colour, with a dark seam outside it
    g.setColour(Colours::bg.withAlpha(0.8f));
    g.drawRoundedRectangle(grille.expanded(3.0f), gr + 3.0f, 1.0f);
    // A stretch of the bottom bead has torn out of its channel
    const auto beadRect = grille.expanded(1.2f);
    const float gapX = grille.getX() + grille.getWidth() * 0.38f, gapW = side * 0.07f;
    const float beadY = beadRect.getBottom();
    {
        juce::Graphics::ScopedSaveState save(g);
        g.excludeClipRegion(juce::Rectangle<float>(gapX, beadY - 3.0f, gapW, 6.0f).toNearestInt());
        g.setColour(Colours::accent.withAlpha(0.9f));
        g.drawRoundedRectangle(beadRect, gr + 1.2f, 1.5f);

        // The same dust dulls the piping in patches
        juce::Path bead, beadStroke;
        bead.addRoundedRectangle(beadRect, gr + 1.2f);
        juce::PathStrokeType(1.5f).createStrokedPath(beadStroke, bead);
        g.reduceClipRegion(beadStroke);
        g.setOpacity(0.9f);
        g.drawImage(dust, dustArea);
    }
    {
        const float x0 = static_cast<float>(juce::roundToInt(gapX));
        const float x1 = static_cast<float>(juce::roundToInt(gapX + gapW));
        // The empty channel the bead sat in
        g.setColour(black.withAlpha(0.6f));
        g.drawLine(x0, beadY, x1, beadY, 1.2f);
        // The loose end, curling out of the channel, over its own shadow
        juce::Path stub;
        stub.startNewSubPath(x0, beadY);
        stub.quadraticTo(x0 + (x1 - x0) * 0.3f, beadY + 0.8f, x0 + (x1 - x0) * 0.42f, beadY + 3.4f);
        const juce::PathStrokeType stubStroke(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(black.withAlpha(0.4f));
        g.strokePath(stub, stubStroke, juce::AffineTransform::translation(0.7f, 1.1f));
        g.setColour(Colours::accent.interpolatedWith(black, 0.25f).withAlpha(0.9f));
        g.strokePath(stub, stubStroke);
        // Its frayed tip: the cord's core, paler than the vinyl
        g.setColour(juce::Colour(0xFFD8CBB0).withAlpha(0.7f));
        g.fillEllipse(x0 + (x1 - x0) * 0.42f - 0.7f, beadY + 3.4f - 0.7f, 1.4f, 1.4f);
    }

    // Corner protectors
    const float s = side * 0.11f;
    for (int sy : { -1, 1 })
        for (int sx : { -1, 1 })
            paintCorner(g, { sx < 0 ? b.getX() - 1.0f : b.getRight() - s + 1.0f,
                             sy < 0 ? b.getY() - 1.0f : b.getBottom() - s + 1.0f, s, s }, sx, sy);
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
    {
        const float scale = juce::jmax(0.01f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto area = cab.expanded(kCacheMargin);
        const int w = juce::jmax(1, juce::roundToInt(area.getWidth() * scale));
        const int h = juce::jmax(1, juce::roundToInt(area.getHeight() * scale));
        const juce::uint32 theme = Colours::surface.getARGB() ^ (Colours::bg.getARGB() * 31u)
                                 ^ (Colours::borderHi.getARGB() * 131u) ^ (Colours::accent.getARGB() * 1313u);
        if (cabCache.isNull() || cabCacheScale != scale || cabCacheTheme != theme
            || cabCache.getWidth() != w || cabCache.getHeight() != h)
        {
            cabCache = juce::Image(juce::Image::ARGB, w, h, true);
            cabCacheScale = scale;
            cabCacheTheme = theme;
            juce::Graphics cg(cabCache);
            cg.addTransform(juce::AffineTransform::scale(static_cast<float>(w) / area.getWidth(),
                                                         static_cast<float>(h) / area.getHeight()));
            paintCabinet(cg, cab.withPosition(kCacheMargin, kCacheMargin), scale);
        }
        g.drawImage(cabCache, area);
    }

    if (driveLevel > 0.0f)
    {
        // Piping glow over the cached cab front; the torn-out stretch stays dark
        const auto p = pipingFor(cab);
        // Hue from the shared ramp (heat); alpha from level, lifted so a quiet
        // signal still shows
        const juce::Colour bead = glowBeadColour(1.0f, driveHeat)
                                      .withAlpha(kCabGlowAlphaMax * std::sqrt(driveLevel));
        juce::Graphics::ScopedSaveState save(g);
        g.excludeClipRegion(p.gap.toNearestInt());
        if (glowUsesHalo())
        {
            // Widest and faintest first, so the bright core sits on top
            for (int i = static_cast<int>(std::size(kCabHaloStrokes)) - 1; i >= 0; --i)
            {
                const auto& s = kCabHaloStrokes[i];
                g.setColour(bead.withMultipliedAlpha(s.alphaScale));
                g.drawRoundedRectangle(p.bead, p.radius, s.width);
            }
        }
        else
        {
            g.setColour(bead);
            g.drawRoundedRectangle(p.bead, p.radius, kCabBeadWidth);
        }
    }

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
