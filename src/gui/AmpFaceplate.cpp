// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "AmpFaceplate.h"
#include "BinaryData.h"
#include "ColourPalette.h"
#include "FontData.h"
#include "ProceduralTexture.h"
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <vector>

namespace sr {

namespace {

constexpr float kPlateRadius = 6.0f;      // the faceplate's corner radius (and its piping's)
constexpr float kDriveEpsilon = 0.004f;   // ~1/255: smaller moves are not visible
constexpr float kLampBloomBase = 3.2f;    // paintLamp's default bloom
constexpr float kLampBloomGain = 0.35f;   // full drive widens the bloom by 35%
constexpr float kLampRadius = 8.0f;       // the pilot lamp's lens radius

float sanitiseUnit(float v)
{
    return std::isfinite(v) ? juce::jlimit(0.0f, 1.0f, v) : 0.0f;
}

// The plate's outline in local coordinates; the piping is stroked on it.
juce::Rectangle<float> plateBounds(const juce::Component& c)
{
    return c.getLocalBounds().toFloat().reduced(0.5f);
}

float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace

bool glowUsesHalo()
{
    return Colours::panel.getPerceivedBrightness() <= 0.5f;
}

juce::Colour glowBeadColour(float level, float heat)
{
    const float lv = sanitiseUnit(level), ht = sanitiseUnit(heat);

    // Hue along the shorter arc from accent to red, so the ramp never swings
    // through green or blue whatever the palette holds.
    const juce::Colour from = Colours::accent, to = Colours::red;
    float dh = to.getHue() - from.getHue();
    if (dh > 0.5f)  dh -= 1.0f;
    if (dh < -0.5f) dh += 1.0f;
    float hue = from.getHue() + dh * ht;
    hue -= std::floor(hue);
    const float sat = from.getSaturation() + (to.getSaturation() - from.getSaturation()) * ht;
    const float bri = from.getBrightness() + (to.getBrightness() - from.getBrightness()) * ht;

    const float ceiling = glowUsesHalo() ? kGlowAlphaMaxHalo : kGlowAlphaMaxBead;
    return juce::Colour::fromHSV(hue, sat, bri, ceiling * lv);
}

GlowDriveFollower::Drive GlowDriveFollower::update(float outPeak, float inRms, float outRms,
                                                   double dtSeconds, uint32_t paramsSignature)
{
    const auto clean = [](float v) { return std::isfinite(v) && v > 0.0f ? v : 0.0f; };
    outPeak = clean(outPeak);
    inRms = clean(inRms);
    outRms = clean(outRms);
    const float dt = std::isfinite(dtSeconds)
        ? static_cast<float>(juce::jlimit(0.0, kGlowMaxFrameSeconds, dtSeconds)) : 0.0f;

    if (!haveSignature || paramsSignature != signature)
    {
        signature = paramsSignature;
        haveSignature = true;
        ref = 0.0f;
    }

    float levelTarget = 0.0f;
    if (outPeak > 0.0f)
        levelTarget = sanitiseUnit((20.0f * std::log10(outPeak) - kGlowLevelFloorDb)
                                   / (kGlowLevelCeilDb - kGlowLevelFloorDb));
    const float levelTau = levelTarget > level ? kGlowLevelAttackSec : kGlowLevelReleaseSec;
    level += (levelTarget - level) * (1.0f - std::exp(-dt / levelTau));
    level = sanitiseUnit(level);
    if (levelTarget == 0.0f && level < kGlowLevelOffBelow)
        level = 0.0f;

    float target = 0.0f;
    if (inRms >= dbToGain(kGlowInputGateDb))
    {
        const float ratio = outRms / inRms;
        ref *= dbToGain(-kGlowRefReleaseDbPerSec * dt);
        if (ratio > ref)
            ref = ratio;
        // A silent output under a live input (tuner mute, OUTPUT trim at 0)
        // is not compression: there is nothing to glow, so it stays cool.
        if (ratio > 0.0f && ref > 0.0f)
        {
            const float compDb = 20.0f * std::log10(ref / ratio);
            target = sanitiseUnit((compDb - kGlowHeatKneeDb) / kGlowHeatSpanDb);
        }
    }

    const float tau = target > heat ? kGlowHeatAttackSec : kGlowHeatReleaseSec;
    heat += (target - heat) * (1.0f - std::exp(-dt / tau));
    heat = sanitiseUnit(heat);
    return { level, heat };
}

void GlowDriveFollower::reset()
{
    ref = 0.0f;
    level = 0.0f;
    heat = 0.0f;
    haveSignature = false;
}

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

void AmpFaceplate::setDrive(float level, float heat)
{
    const float lv = sanitiseUnit(level), ht = sanitiseUnit(heat);
    const auto moved = [](float now, float was) {
        return std::abs(now - was) > kDriveEpsilon || ((now == 0.0f) != (was == 0.0f));
    };
    if (!moved(lv, driveLevel) && !moved(ht, driveHeat))
        return;
    driveLevel = lv;
    driveHeat = ht;
    if (powered)
        repaintGlow();
}

void AmpFaceplate::repaintGlow()
{
    // Only the lamp's bloom changes with drive. A whole-component repaint would
    // also repaint the knobs and toggles laid over the plate on every 30 Hz frame.
    const float bloomR = kLampRadius * kLampBloomBase * (1.0f + kLampBloomGain) + 2.0f;
    repaint(juce::Rectangle<float>(lampCentre.x - bloomR, lampCentre.y - bloomR,
                                   bloomR * 2.0f, bloomR * 2.0f).getSmallestIntegerContainer());
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
                                                 0.65f, 0.65f, caustic.withAlpha(juce::jmin(1.0f, mixf(0.3f, 0.9f) * 1.15f))));
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

// The plate itself: raised gradient, optional grille-cloth weave, piping. The
// EFFECTS cards take it with the weave (paintPlate); the amp plate takes it
// without, because its worn powder coat (paintGrunge) is its surface and a
// weave under it read as a second, competing pattern.
static void paintPlateFace(juce::Graphics& g, juce::Rectangle<float> b, float radius, bool weave)
{
    // Raised panel
    g.setGradientFill(juce::ColourGradient(Colours::panelLight, 0.0f, b.getY(),
                                           Colours::surface, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle(b, radius);

    // Grille-cloth weave: two sets of fine diagonals, clipped to the panel
    if (weave)
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

void AmpFaceplate::paintPlate(juce::Graphics& g, juce::Rectangle<float> b, float radius)
{
    paintPlateFace(g, b, radius, true);
}

// Worn powder-coat finish over the amp plate.
//
// The surface is a height field, shaded with the same top light as the plate's
// gradient, so the fine orange-peel stipple reads as relief rather than as
// printed noise. Wear is layered on that physically:
//   - grime settles into the stipple's low spots and in soft, uneven patches,
//   - handled areas are polished: flatter relief with a faint sheen,
//   - dirt collects toward the rim, where the coat is chipped to bare steel,
//   - a few fine hairline scratches catch the light.
// Every layer blends through smoothstep, never a hard threshold, and the noise
// (ProceduralTexture.h) is rotated gradient noise, so nothing lines up with
// the pixel grid. Rendered once into the plate cache at the physical pixel scale
// from fixed seeds, so the finish is identical on every repaint and launch.
static void paintGrunge(juce::Graphics& g, juce::Rectangle<float> b, float radius, float scale)
{
    using namespace texture;
    const bool darkPanel = Colours::panel.getPerceivedBrightness() < 0.5f;
    // Dark marks on a white plate read far heavier than the same marks on a
    // black one, so a light palette gets the shading, grime and scratches
    // at under half strength
    const float ink = darkPanel ? 1.0f : 0.4f;
    const juce::Colour light = juce::Colour(0xffe6e9f0);
    const juce::Colour shade = juce::Colour(0xff000000);
    const juce::Colour dirt  = darkPanel ? juce::Colour(0xff050403) : juce::Colour(0xff3b2f1e);
    const juce::Colour steel = juce::Colour(0xff8b8e95);

    const int pw = juce::jmax(1, juce::roundToInt(b.getWidth() * scale));
    const int ph = juce::jmax(1, juce::roundToInt(b.getHeight() * scale));
    const float inv = 1.0f / scale; // physical px -> logical px
    const auto idx = [pw](int x, int y) {
        return static_cast<size_t>(y) * static_cast<size_t>(pw) + static_cast<size_t>(x);
    };

    // ── Height field: fine orange-peel stipple over a slow undulation ──
    std::vector<float> height(static_cast<size_t>(pw) * static_cast<size_t>(ph));
    std::vector<float> stipple(height.size());
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x)
        {
            const float u = static_cast<float>(x) * inv, v = static_cast<float>(y) * inv;
            const float fine = fbm(u / 3.2f, v / 3.2f, 7, 3);
            const float slow = fbm(u / 26.0f, v / 26.0f, 19, 2);
            stipple[idx(x, y)] = fine;
            height[idx(x, y)] = fine * 0.75f + slow * 0.6f;
        }

    // Light from above, slightly left: matches the plate's top-lit gradient
    const float lx = -0.28f, ly = -0.62f, lz = 0.73f;

    juce::Image tex(juce::Image::ARGB, pw, ph, true);
    {
        juce::Image::BitmapData out(tex, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < ph; ++y)
        {
            const int y0 = juce::jmax(0, y - 1), y1 = juce::jmin(ph - 1, y + 1);
            for (int x = 0; x < pw; ++x)
            {
                const int x0 = juce::jmax(0, x - 1), x1 = juce::jmin(pw - 1, x + 1);
                const float u = static_cast<float>(x) * inv, v = static_cast<float>(y) * inv;

                // Distance in from the visible (clipped) edge, logical px
                const float edge = juce::jmin(juce::jmin(u, b.getWidth() - u),
                                              juce::jmin(v, b.getHeight() - v)) - 3.0f;

                // Polish: soft patches where the coat has been rubbed smooth
                const float polish = smoothstep(0.56f, 0.74f, fbm(u / 110.0f, v / 80.0f, 53, 3));

                // Slope in logical units; relief flattens where polished
                const float relief = 0.55f * (1.0f - 0.65f * polish);
                const float dhdx = (height[idx(x1, y)] - height[idx(x0, y)])
                                 / (static_cast<float>(x1 - x0) * inv) * relief;
                const float dhdy = (height[idx(x, y1)] - height[idx(x, y0)])
                                 / (static_cast<float>(y1 - y0) * inv) * relief;
                const float nlen = std::sqrt(dhdx * dhdx + dhdy * dhdy + 1.0f);
                const float lit = (-dhdx * lx - dhdy * ly + lz) / nlen - lz; // vs a flat surface

                Px p;
                if (lit > 0.0f)
                    p.over(light, lit * 0.55f);
                else
                    p.over(shade, -lit * 0.95f * ink);

                // Polished sheen
                p.over(light, polish * 0.035f);

                // Grime: uneven patches, deepest in the stipple's pores
                const float cavity = smoothstep(0.62f, 0.30f, stipple[idx(x, y)]);
                const float patches = smoothstep(0.40f, 0.78f, fbm(u / 64.0f, v / 46.0f, 31, 4));
                const float rimDirt = std::exp(-juce::jmax(0.0f, edge) / 14.0f)
                                    * (0.55f + 0.9f * fbm(u / 18.0f, v / 18.0f, 67, 3));
                const float grime = (patches * 0.20f + rimDirt * 0.30f) * (0.55f + 0.9f * cavity)
                                  * (1.0f - 0.5f * polish);
                p.over(dirt, grime * ink);

                // Chipped coat at the rim: soft-edged islands of bare steel, only
                // along the stretches that got knocked, and most at the corners
                const float cornerDist = std::hypot(juce::jmin(u, b.getWidth() - u),
                                                    juce::jmin(v, b.getHeight() - v));
                const float knocked = juce::jmax(smoothstep(0.52f, 0.70f, fbm(u / 70.0f, v / 70.0f, 97, 2)),
                                                 smoothstep(40.0f, 12.0f, cornerDist));
                const float rimBias = 0.20f * smoothstep(7.0f, 0.0f, edge) * knocked - 0.06f * (1.0f - knocked);
                const float chipField = fbm(u / 5.5f, v / 5.5f, 89, 3) + rimBias;
                const float chip = smoothstep(0.69f, 0.73f, chipField) * smoothstep(8.0f, 2.0f, edge);
                if (chip > 0.0f)
                {
                    p.over(steel, chip * 0.62f);
                    // The chip's lower lip catches the light; its upper wall is in shadow
                    const float wall = chipField - (fbm(u / 5.5f, (v - 1.0f) / 5.5f, 89, 3) + rimBias);
                    p.over(wall > 0.0f ? light : shade,
                           chip * juce::jmin(1.0f, std::abs(wall) * 10.0f) * 0.25f);
                }

                out.setPixelColour(x, y, p.colour());
            }
        }
    }

    juce::Graphics::ScopedSaveState save(g);
    juce::Path clip;
    clip.addRoundedRectangle(b.reduced(3.0f), radius - 2.0f);
    g.reduceClipRegion(clip);
    g.drawImage(tex, b);

    // Hairline scratches: a few clusters of fine, slightly curved, near-parallel
    // lines: a groove in shadow with a lit lip beneath it, fading at both ends
    juce::Random rng(0x5112107);
    const auto rx = [&](float lo, float hi) { return lo + rng.nextFloat() * (hi - lo); };
    for (int cluster = 0; cluster < 4; ++cluster)
    {
        const float cx = rx(b.getX() + 40.0f, b.getRight() - 40.0f);
        const float cy = rx(b.getY() + 30.0f, b.getBottom() - 30.0f);
        const float dir = rx(-0.3f, 0.3f);
        const int n = 2 + rng.nextInt(4);
        for (int i = 0; i < n; ++i)
        {
            const float sx = cx + rx(-30.0f, 30.0f), sy = cy + rx(-10.0f, 10.0f);
            const float len = rx(25.0f, 110.0f), ang = dir + rx(-0.04f, 0.04f);
            const float ex = sx + std::cos(ang) * len, ey = sy + std::sin(ang) * len;
            const float bend = rx(-3.0f, 3.0f);
            const float strength = rx(0.4f, 1.0f);
            const auto pt = [&](float t) {
                const float bx = sx + (ex - sx) * t, by = sy + (ey - sy) * t;
                const float off = bend * 4.0f * t * (1.0f - t);
                return juce::Point<float>(bx - std::sin(ang) * off, by + std::cos(ang) * off);
            };
            constexpr int segs = 12; // drawn in segments so the alpha can taper
            for (int k = 0; k < segs; ++k)
            {
                const float t0 = static_cast<float>(k) / segs, t1 = static_cast<float>(k + 1) / segs;
                const float taper = std::sin(juce::MathConstants<float>::pi * (t0 + t1) * 0.5f);
                const auto p0 = pt(t0), p1 = pt(t1);
                g.setColour(shade.withAlpha(0.22f * taper * strength * ink));
                g.drawLine({ p0, p1 }, 0.6f);
                g.setColour(light.withAlpha(0.10f * taper * strength));
                g.drawLine({ p0.translated(0.0f, 0.55f), p1.translated(0.0f, 0.55f) }, 0.45f);
            }
        }
    }
}

// A photo of scratched, rust-streaked steel supplied by the owner
// (`resources/textures/faceplate-steel.jpg`; source to confirm and credit in
// attribution/CREDITS.md before the repo goes public), laid over the powder coat as a
// layer. Not pasted opaque, which would turn the plate silver: each pixel's
// opacity follows how far it stands out from its surroundings, so the flat
// steel only tints the coat a little while the scratches, the rust streak and
// the dark specks come through. Built once from the photo, theme-independent;
// the light palette takes it at reduced opacity, like the grunge.
static const juce::Image& steelLayer()
{
    static const juce::Image layer = [] {
        const juce::Image photo = juce::ImageCache::getFromMemory(BinaryData::faceplatesteel_jpg,
                                                                  BinaryData::faceplatesteel_jpgSize);
        const int w = photo.getWidth(), h = photo.getHeight();
        juce::Image out(juce::Image::ARGB, juce::jmax(1, w), juce::jmax(1, h), true);
        if (w <= 0 || h <= 0)
            return out;

        // Luminance, and its local mean over a ~29 px box (summed-area table)
        std::vector<float> lum(static_cast<size_t>(w) * static_cast<size_t>(h));
        const juce::Image::BitmapData src(photo, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                lum[static_cast<size_t>(y * w + x)] = src.getPixelColour(x, y).getPerceivedBrightness();
        std::vector<double> sat(static_cast<size_t>(w + 1) * static_cast<size_t>(h + 1), 0.0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                sat[static_cast<size_t>((y + 1) * (w + 1) + x + 1)] = lum[static_cast<size_t>(y * w + x)]
                    + sat[static_cast<size_t>(y * (w + 1) + x + 1)] + sat[static_cast<size_t>((y + 1) * (w + 1) + x)]
                    - sat[static_cast<size_t>(y * (w + 1) + x)];
        const int r = 14;

        juce::Image::BitmapData dst(out, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const int x0 = juce::jmax(0, x - r), x1 = juce::jmin(w, x + r + 1);
                const int y0 = juce::jmax(0, y - r), y1 = juce::jmin(h, y + r + 1);
                const double area = static_cast<double>((x1 - x0) * (y1 - y0));
                const double sum = sat[static_cast<size_t>(y1 * (w + 1) + x1)] - sat[static_cast<size_t>(y0 * (w + 1) + x1)]
                                 - sat[static_cast<size_t>(y1 * (w + 1) + x0)] + sat[static_cast<size_t>(y0 * (w + 1) + x0)];
                const float dev = lum[static_cast<size_t>(y * w + x)] - static_cast<float>(sum / area);

                const juce::Colour c = src.getPixelColour(x, y);
                const float chroma = juce::jmax(c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue())
                                   - juce::jmin(c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue());
                // Scratches and highlights in the steel's own colour; specks and
                // stains darken, so they read as dark on a dark plate too
                const float alpha = juce::jlimit(0.0f, 0.75f, 0.10f + 2.2f * std::abs(dev) + 1.1f * chroma);
                const juce::Colour ink = dev >= 0.0f ? c : c.withMultipliedBrightness(0.35f);
                dst.setPixelColour(x, y, ink.withAlpha(alpha));
            }
        return out;
    }();
    return layer;
}

// How much of the steel layer shows: almost transparent (owner's call), so it
// reads as a faint memory of bare metal under the coat, not as a photo
constexpr float kSteelOpacityDark  = 0.2f;
constexpr float kSteelOpacityLight = 0.14f;

static void paintSteelLayer(juce::Graphics& g, juce::Rectangle<float> b, float radius)
{
    const bool darkPanel = Colours::panel.getPerceivedBrightness() < 0.5f;
    juce::Graphics::ScopedSaveState save(g);
    juce::Path clip;
    clip.addRoundedRectangle(b.reduced(2.0f), radius - 1.5f); // inside the piping
    g.reduceClipRegion(clip);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.setOpacity(darkPanel ? kSteelOpacityDark : kSteelOpacityLight);
    g.drawImage(steelLayer(), b,
                juce::RectanglePlacement(juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination));
}

void AmpFaceplate::renderPlateCache(float scale)
{
    const int w = juce::jmax(1, juce::roundToInt(static_cast<float>(getWidth()) * scale));
    const int h = juce::jmax(1, juce::roundToInt(static_cast<float>(getHeight()) * scale));
    plateCache = juce::Image(juce::Image::ARGB, w, h, true);
    plateCacheTheme = Colours::panel.getARGB();
    plateCacheScale = scale;

    juce::Graphics g(plateCache);
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(w) / static_cast<float>(getWidth()),
                                                static_cast<float>(h) / static_cast<float>(getHeight())));
    const auto b = plateBounds(*this);
    paintPlateFace(g, b, kPlateRadius, false); // no weave: the powder coat is the surface
    paintGrunge(g, b, kPlateRadius, scale);    // amp plate only: the EFFECTS cards share paintPlate, not this
    paintSteelLayer(g, b, kPlateRadius);        // the steel photo over the coat, amp plate only

    // Corner screws
    const float inset = 10.0f;
    drawScrew(g, b.getX() + inset, b.getY() + inset);
    drawScrew(g, b.getRight() - inset, b.getY() + inset);
    drawScrew(g, b.getX() + inset, b.getBottom() - inset);
    drawScrew(g, b.getRight() - inset, b.getBottom() - inset);
}

void AmpFaceplate::paint(juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    // ── Static plate, from the cache ──
    // There is no theme-change callback (applySettings() swaps the Colours
    // globals and repaints from the top), so the key carries a theme token as
    // well as the size. It is rendered at the physical pixel scale, so the UI
    // scale's transform and HiDPI screens get a sharp plate, not an upscale.
    const float scale = juce::jmax(0.01f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (plateCache.isNull()
        || plateCacheTheme != Colours::panel.getARGB()
        || plateCacheScale != scale
        || plateCache.getWidth() != juce::jmax(1, juce::roundToInt(static_cast<float>(getWidth()) * scale))
        || plateCache.getHeight() != juce::jmax(1, juce::roundToInt(static_cast<float>(getHeight()) * scale)))
        renderPlateCache(scale);
    g.drawImage(plateCache, getLocalBounds().toFloat());

    // The piping glow now lives on the cab (CabinetView::setDrive); the amp
    // plate keeps only the lamp bloom below.
    const float level = powered ? driveLevel : 0.0f;

    // ── Glass pilot lamp, placed by the editor in the knob row ──
    // Lit means powered, so brightness stays binary; playing only widens the bloom.
    const float midY = lampCentre.y, lx = lampCentre.x, r = kLampRadius;
    paintLamp(g, { lx, midY }, r, Colours::red, powered ? 1.0f : 0.0f,
              kLampBloomBase * (1.0f + kLampBloomGain * level));

    g.setColour(Colours::dim);
    g.setFont(Fonts::ui(Fonts::Sizes::micro, true));
    g.drawText("POWER", juce::Rectangle<float>(lx - 40.0f, midY + r + 8.0f, 80.0f, 14.0f),
               juce::Justification::centred, false);
}

} // namespace sr
