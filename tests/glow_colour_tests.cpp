// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Reactive panel glow: the bead colour maths (glowBeadColour, a pure
// function of level, heat and the palette) and the faceplate's drive state
// and plate cache. Nothing here compares against a reference image
// (docs/plans/2026-10-29-reactive-panel-glow.md, Review Focus 7): the colour
// is checked as numbers, the painting by single sampled pixels that must
// differ (or match) between two states.
//
// In SilentRiotProcessorTests, not SilentRiotTests: glowBeadColour returns a
// juce::Colour, and the pure C++ target has no JUCE.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gui/AmpFaceplate.h"
#include "gui/ColourPalette.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <limits>

namespace {

// Every test here touches the palette globals; put them back for the rest.
struct ThemeGuard
{
    explicit ThemeGuard(int theme) { sr::Colours::setTheme(theme); }
    ~ThemeGuard() { sr::Colours::setTheme(sr::Colours::Dark); }
};

constexpr int kThemes[] = { sr::Colours::Dark, sr::Colours::Light };

float hueDistance(float a, float b)
{
    const float d = std::abs(a - b);
    return std::min(d, 1.0f - d);
}

// WCAG 2 relative luminance of an opaque sRGB colour.
double luminance(double r8, double g8, double b8)
{
    const auto lin = [](double c8) {
        const double c = c8 / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(r8) + 0.7152 * lin(g8) + 0.0722 * lin(b8);
}

double luminance(juce::Colour c)
{
    return luminance(c.getRed(), c.getGreen(), c.getBlue());
}

// fg at `alpha` composited over opaque bg, in 8-bit sRGB as JUCE blends it.
double compositeLuminance(juce::Colour fg, float alpha, juce::Colour bg)
{
    const auto mix = [alpha](int f, int b) { return alpha * f + (1.0 - alpha) * b; };
    return luminance(mix(fg.getRed(), bg.getRed()), mix(fg.getGreen(), bg.getGreen()),
                     mix(fg.getBlue(), bg.getBlue()));
}

double contrast(double la, double lb)
{
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// The alpha the glow actually puts on the brightest pixel of the piping line:
// the halo's strokes all cover it, so their coverages stack.
float litAlpha(juce::Colour bead)
{
    if (!sr::glowUsesHalo())
        return bead.getFloatAlpha();
    float clear = 1.0f;
    for (const auto& s : sr::kGlowHaloStrokes)
        clear *= 1.0f - bead.getFloatAlpha() * s.alphaScale;
    return 1.0f - clear;
}

} // namespace

TEST_CASE("Glow colour: a silent amp adds nothing, at every heat and in both themes", "[gui][glow]")
{
    for (int theme : kThemes)
    {
        ThemeGuard guard(theme);
        for (float heat = 0.0f; heat <= 1.0f; heat += 0.125f)
            CHECK(sr::glowBeadColour(0.0f, heat).getAlpha() == 0);
    }
}

TEST_CASE("Glow colour: alpha rises monotonically with level, up to the ceiling", "[gui][glow]")
{
    for (int theme : kThemes)
    {
        ThemeGuard guard(theme);
        const float ceiling = sr::glowUsesHalo() ? sr::kGlowAlphaMaxHalo : sr::kGlowAlphaMaxBead;
        for (float heat : { 0.0f, 0.5f, 1.0f })
        {
            // Fine sweep: never falls (juce::Colour holds alpha in 8 bits, so
            // neighbouring 1% steps can share a value)...
            float previous = -1.0f;
            for (int i = 0; i <= 100; ++i)
            {
                const float a = sr::glowBeadColour(static_cast<float>(i) / 100.0f, heat).getFloatAlpha();
                CHECK(a >= previous);
                CHECK(a <= ceiling + 1.0f / 255.0f);
                previous = a;
            }
            CHECK_THAT(previous, Catch::Matchers::WithinAbs(ceiling, 1.0f / 255.0f));

            // ...and coarse 5% steps, wider than one 8-bit alpha step, always rise
            previous = -1.0f;
            for (int i = 0; i <= 20; ++i)
            {
                const float a = sr::glowBeadColour(static_cast<float>(i) / 20.0f, heat).getFloatAlpha();
                CHECK(a > previous);
                previous = a;
            }
        }
    }
}

TEST_CASE("Glow colour: heat moves the hue from accent to red, not only the alpha", "[gui][glow]")
{
    for (int theme : kThemes)
    {
        ThemeGuard guard(theme);
        const auto clean = sr::glowBeadColour(1.0f, 0.0f);
        const auto hot = sr::glowBeadColour(1.0f, 1.0f);
        const auto mid = sr::glowBeadColour(1.0f, 0.5f);
        const float tol = 1.5f / 360.0f;   // 8-bit rounding of the HSB round trip

        CHECK(hueDistance(clean.getHue(), sr::Colours::accent.getHue()) < tol);
        CHECK(hueDistance(hot.getHue(), sr::Colours::red.getHue()) < tol);
        CHECK(hueDistance(clean.getHue(), hot.getHue()) > 10.0f / 360.0f);

        // Mid-ramp sits between the ends in hue, clear of both
        CHECK(hueDistance(mid.getHue(), clean.getHue()) > 5.0f / 360.0f);
        CHECK(hueDistance(mid.getHue(), hot.getHue()) > 5.0f / 360.0f);
        CHECK(clean.getAlpha() == hot.getAlpha());
    }
}

TEST_CASE("Glow colour: out-of-range and non-finite inputs clamp", "[gui][glow]")
{
    ThemeGuard guard(sr::Colours::Dark);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    CHECK(sr::glowBeadColour(2.0f, 0.0f) == sr::glowBeadColour(1.0f, 0.0f));
    CHECK(sr::glowBeadColour(-1.0f, 0.5f) == sr::glowBeadColour(0.0f, 0.5f));
    CHECK(sr::glowBeadColour(1.0f, 7.0f) == sr::glowBeadColour(1.0f, 1.0f));
    CHECK(sr::glowBeadColour(1.0f, -3.0f) == sr::glowBeadColour(1.0f, 0.0f));

    // Non-finite reads as 0: a NaN level is silent, a NaN heat is clean
    CHECK(sr::glowBeadColour(nan, 0.5f).getAlpha() == 0);
    CHECK(sr::glowBeadColour(inf, 0.5f).getAlpha() == 0);
    CHECK(sr::glowBeadColour(-inf, 0.5f).getAlpha() == 0);
    CHECK(sr::glowBeadColour(1.0f, nan) == sr::glowBeadColour(1.0f, 0.0f));
    CHECK(sr::glowBeadColour(1.0f, inf) == sr::glowBeadColour(1.0f, 0.0f));
}

TEST_CASE("Glow colour: the theme picks halo or tinted bead, both visible at full drive", "[gui][glow]")
{
    {
        ThemeGuard guard(sr::Colours::Dark);
        CHECK(sr::glowUsesHalo());
        CHECK(sr::glowBeadColour(1.0f, 0.5f).getFloatAlpha() > 0.3f);
    }
    {
        ThemeGuard guard(sr::Colours::Light);
        CHECK_FALSE(sr::glowUsesHalo());
        // The bead colours up: more opaque than the halo's core
        CHECK(sr::glowBeadColour(1.0f, 0.5f).getFloatAlpha() > sr::kGlowAlphaMaxHalo);
    }
    CHECK(sr::glowUsesHalo());   // the guard restored the dark palette
}

TEST_CASE("Glow colour: text stays above 4.5:1 against the lit bead in both themes", "[gui][glow]")
{
    for (int theme : kThemes)
    {
        ThemeGuard guard(theme);
        const double text = luminance(sr::Colours::text);
        for (float heat = 0.0f; heat <= 1.0f; heat += 0.125f)
        {
            const auto bead = sr::glowBeadColour(1.0f, heat);
            const double lit = compositeLuminance(bead.withAlpha(1.0f), litAlpha(bead), sr::Colours::panel);
            INFO("theme " << theme << ", heat " << heat << ", contrast " << contrast(text, lit));
            CHECK(contrast(text, lit) >= 4.5);
        }
    }
}

// ── AmpFaceplate ──

namespace {

struct Plate
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // first: outlives the faceplate
    sr::AmpFaceplate plate;

    Plate()
    {
        plate.setSize(300, 120);
        plate.setLampCentre({ 250.0f, 60.0f });
    }

    juce::Colour pixel(int x, int y, float scale = 1.0f)
    {
        const auto img = plate.createComponentSnapshot(plate.getLocalBounds(), true, scale);
        return img.getPixelAt(juce::roundToInt(static_cast<float>(x) * scale),
                              juce::roundToInt(static_cast<float>(y) * scale));
    }
};

} // namespace

TEST_CASE("Faceplate: setDrive stores sanitised values and ignores invisible moves", "[gui][glow]")
{
    Plate p;
    p.plate.setDrive(0.5f, 0.25f);
    CHECK(p.plate.getDriveLevel() == 0.5f);
    CHECK(p.plate.getDriveHeat() == 0.25f);

    p.plate.setDrive(0.501f, 0.251f);   // under the epsilon: no change, no repaint
    CHECK(p.plate.getDriveLevel() == 0.5f);
    CHECK(p.plate.getDriveHeat() == 0.25f);

    p.plate.setDrive(0.002f, 0.25f);
    CHECK(p.plate.getDriveLevel() == 0.002f);
    p.plate.setDrive(0.0f, 0.25f);       // to exactly 0 always lands, so silence goes fully dark
    CHECK(p.plate.getDriveLevel() == 0.0f);

    p.plate.setDrive(std::numeric_limits<float>::quiet_NaN(), 5.0f);
    CHECK(p.plate.getDriveLevel() == 0.0f);
    CHECK(p.plate.getDriveHeat() == 1.0f);
}

TEST_CASE("Faceplate: a theme switch rebuilds the cached plate", "[gui][glow]")
{
    Plate p;
    const int x = 75, y = 62;   // open plate: clear of piping, screws and lamp
    juce::Colour dark, light;
    {
        ThemeGuard guard(sr::Colours::Dark);
        dark = p.pixel(x, y);
    }
    {
        ThemeGuard guard(sr::Colours::Light);
        light = p.pixel(x, y);   // same size, same component: only the theme moved
    }
    CHECK(dark != light);
    CHECK(light.getPerceivedBrightness() > dark.getPerceivedBrightness());
    CHECK(p.pixel(x, y) == dark);   // and back again

    // A different physical scale rebuilds too, rather than upscaling the cache
    CHECK(p.pixel(x, y, 2.0f).getPerceivedBrightness() < 0.5f);
}

TEST_CASE("Faceplate: the bead lights only while powered and driven", "[gui][glow]")
{
    for (int theme : kThemes)
    {
        ThemeGuard guard(theme);
        Plate p;
        const int x = 150, y = 1;   // on the top piping, mid-span

        p.plate.setDrive(0.0f, 0.0f);
        const auto idle = p.pixel(x, y);

        p.plate.setDrive(1.0f, 0.0f);
        const auto lit = p.pixel(x, y);
        CHECK(lit != idle);
        CHECK(lit.getRed() > idle.getRed());   // warmer: the glow is orange to red

        p.plate.setPowered(false);
        CHECK(p.pixel(x, y) == idle);           // power off: no residual bead colour
    }
}
