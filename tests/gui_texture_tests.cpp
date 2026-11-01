// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Surface textures painted in code: the noise helpers they are built from
// (ProceduralTexture.h), the cab front's tolex, wear and cache (CabinetView),
// and the pointer knobs' handling wear (SilentRiotLookAndFeel). As in
// glow_colour_tests.cpp, nothing is compared against a reference image: the
// noise is checked as numbers, the painting by sampled pixels that must match
// (determinism) or differ (theme, wear seed) between two renders.
//
// In SilentRiotProcessorTests: the helpers return juce::Colour and the views
// are juce::Components, and the pure C++ target has no JUCE.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include "gui/AmpFaceplate.h"
#include "gui/CabinetView.h"
#include "gui/ColourPalette.h"
#include "gui/IRLoaderPanel.h"
#include "gui/LookAndFeel.h"
#include "gui/ProceduralTexture.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

struct ThemeGuard
{
    explicit ThemeGuard(int theme) { sr::Colours::setTheme(theme); }
    ~ThemeGuard() { sr::Colours::setTheme(sr::Colours::Dark); }
};

// A 600 x 242 view puts the cab at (100, 4, 400, 200): the height minus the
// name strip sets 200, and 400 wide fits between the arrows
// (CabinetView::cabBounds()).
struct Cab
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // first: outlives the view
    sr::CabinetView view;

    Cab() { view.setSize(600, 242); }

    juce::Image snapshot(float scale = 1.0f)
    {
        return view.createComponentSnapshot(view.getLocalBounds(), true, scale);
    }
    juce::Colour pixel(int x, int y) { return snapshot().getPixelAt(x, y); }
};

// In the frame band above the grille, clear of the tears, dents and caps
constexpr int kTolexX = 300, kTolexY = 12;
// The middle of the tear ripped up from the bottom edge: x = 0.64 of the cab's
// width, 0.22 of the frame band (0.095 x 200) up from its bottom edge
constexpr int kTearX = 100 + 256, kTearY = 4 + 200 - 4;

bool sameImage(const juce::Image& a, const juce::Image& b)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

} // namespace

// ── Noise helpers ──────────────────────────────────────────────────

TEST_CASE("Texture noise: deterministic, in range, and not flat", "[gui][texture]")
{
    using namespace sr::texture;
    float lo = 1.0f, hi = 0.0f, sum = 0.0f;
    int n = 0;
    for (int y = 0; y < 60; ++y)
        for (int x = 0; x < 60; ++x)
        {
            const float u = static_cast<float>(x) * 0.37f, v = static_cast<float>(y) * 0.41f;
            const float f = fbm(u, v, 11, 4);
            REQUIRE(f == fbm(u, v, 11, 4));          // same input, same value
            REQUIRE(f >= 0.0f);
            REQUIRE(f <= 1.0f);
            lo = std::min(lo, f);
            hi = std::max(hi, f);
            sum += f;
            ++n;

            float f1 = 0.0f, f2 = 0.0f;
            cellular(u, v, 211, f1, f2);
            REQUIRE(f1 <= f2);                       // nearest, then second-nearest
            REQUIRE(f1 < 1.5f);                      // a feature point in a neighbouring cell
        }
    CHECK(hi - lo > 0.3f);                           // it varies...
    CHECK(sum / static_cast<float>(n) > 0.4f);       // ...about the middle
    CHECK(sum / static_cast<float>(n) < 0.6f);
    CHECK(fbm(3.3f, 4.4f, 1, 3) != fbm(3.3f, 4.4f, 2, 3)); // the seed matters
}

TEST_CASE("Texture noise: smoothstep and the over-compositor", "[gui][texture]")
{
    using namespace sr::texture;
    CHECK(smoothstep(0.2f, 0.8f, 0.0f) == 0.0f);
    CHECK(smoothstep(0.2f, 0.8f, 1.0f) == 1.0f);
    CHECK(smoothstep(0.2f, 0.8f, 0.5f) == 0.5f);
    CHECK(smoothstep(0.8f, 0.2f, 0.0f) == 1.0f);     // reversed edges fall instead

    Px p;
    CHECK(p.colour().getAlpha() == 0);               // nothing composited: transparent
    p.over(juce::Colours::white, 0.5f);
    p.over(juce::Colours::black, 0.5f);
    const auto c = p.colour();
    CHECK(c.getAlpha() == 191);                      // 1 - 0.5 x 0.5 = 0.75
    CHECK(c.getRed() == 85);                         // 0.25 white over 0.75 alpha = 1/3 grey
}

// ── Cab front ──────────────────────────────────────────────────────

TEST_CASE("Cab: the front paints the same on every render", "[gui][texture]")
{
    Cab a, b;
    CHECK(sameImage(a.snapshot(), b.snapshot()));    // fixed seeds: no per-instance wear
    CHECK(sameImage(a.snapshot(), a.snapshot()));    // and the cache serves the same front
}

TEST_CASE("Cab: wood shows through the torn tolex in both themes", "[gui][texture]")
{
    Cab cab;
    for (int theme : { sr::Colours::Dark, sr::Colours::Light })
    {
        ThemeGuard guard(theme);
        const auto wood = cab.pixel(kTearX, kTearY);
        INFO("theme " << theme << " pixel " << wood.toDisplayString(true));
        CHECK(wood.getRed() > wood.getBlue() + 15);  // warm brown, not the cool tolex
        const auto tolex = cab.pixel(kTolexX, kTolexY);
        CHECK(std::abs(tolex.getRed() - tolex.getBlue()) < 15);
    }
}

TEST_CASE("Cab: a theme switch rebuilds the cached front", "[gui][texture]")
{
    Cab cab;
    juce::Colour dark, light;
    {
        ThemeGuard guard(sr::Colours::Dark);
        dark = cab.pixel(kTolexX, kTolexY);
    }
    {
        ThemeGuard guard(sr::Colours::Light);
        light = cab.pixel(kTolexX, kTolexY);         // same size: only the theme moved
    }
    CHECK(light.getPerceivedBrightness() > dark.getPerceivedBrightness() + 0.3f);
    CHECK(cab.pixel(kTolexX, kTolexY) == dark);      // and back again

    // A different physical scale re-renders rather than upscaling the cache
    const auto hi = cab.snapshot(2.0f);
    CHECK(hi.getWidth() == 1200);
    CHECK(hi.getPixelAt(kTolexX * 2, kTolexY * 2).getPerceivedBrightness() < 0.5f);
}

// ── Brick wall behind the CAB view ─────────────────────────────────

namespace {

// Left of the cab and its arrow, clear of the header and the name lines
const juce::Rectangle<int> kWallPatch { 20, 80, 60, 60 };

struct Loader
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // first: outlives the panel
    sr::IRLoaderPanel panel;

    Loader() { panel.setSize(700, sr::IRLoaderPanel::panelHeight); }

    juce::Image patch()
    {
        return panel.createComponentSnapshot(panel.getLocalBounds(), true, 1.0f).getClippedImage(kWallPatch);
    }
};

struct Mean { float r = 0, g = 0, b = 0, spread = 0; };

Mean meanOf(const juce::Image& img)
{
    Mean m;
    float lo = 1.0f, hi = 0.0f;
    const float n = static_cast<float>(img.getWidth() * img.getHeight());
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
        {
            const auto c = img.getPixelAt(x, y);
            m.r += c.getFloatRed() / n;
            m.g += c.getFloatGreen() / n;
            m.b += c.getFloatBlue() / n;
            lo = std::min(lo, c.getPerceivedBrightness());
            hi = std::max(hi, c.getPerceivedBrightness());
        }
    m.spread = hi - lo;
    return m;
}

} // namespace

TEST_CASE("Brick wall: CAB view only, dimmed in dark, washed pale in light", "[gui][texture]")
{
    Loader l;
    REQUIRE(l.panel.getView() == sr::IRLoaderPanel::View::Cab);

    const auto dark = meanOf(l.patch());
    INFO("dark mean " << dark.r << " " << dark.g << " " << dark.b << " spread " << dark.spread);
    CHECK(dark.r > dark.b + 0.03f);                 // warm clay, not the cool panel
    CHECK(dark.spread > 0.08f);                     // bricks and joints, not a flat fill
    CHECK((dark.r + dark.g + dark.b) / 3.0f < 0.3f); // dimmed: the UI stays the bright part

    {
        ThemeGuard guard(sr::Colours::Light);
        const auto light = meanOf(l.patch());
        INFO("light mean " << light.r << " " << light.g << " " << light.b);
        CHECK((light.r + light.g + light.b) / 3.0f > 0.6f); // washed pale
        CHECK(light.spread > 0.08f);
    }

    // LIBRARY keeps the plain panel behind its list
    l.panel.setView(sr::IRLoaderPanel::View::Library);
    const auto lib = meanOf(l.patch());
    CHECK(lib.spread < 0.02f);
}

TEST_CASE("Brick wall: same on every render, and while the section animates open", "[gui][texture]")
{
    Loader a, b;
    const auto full = a.patch();
    CHECK(sameImage(full, b.patch()));     // fixed seeds
    CHECK(sameImage(full, a.patch()));     // and the cache serves the same wall

    // The open/close animation resizes the panel every frame: the wall is
    // rendered at full height and drawn from the top, so it neither moves nor rescales
    a.panel.setSize(700, 150);
    CHECK(sameImage(full, a.patch()));
}

// ── Steel layer on the amp plate ───────────────────────────────────

TEST_CASE("Steel layer: the photo's rust streak shows on the amp plate", "[gui][texture]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sr::AmpFaceplate plate;
    plate.setSize(604, 180);                      // the plate's own proportions
    const auto img = plate.createComponentSnapshot(plate.getLocalBounds(), true, 1.0f);

    // Warmth (red minus blue) of a vertical strip, away from the rim
    const auto warmth = [&img](float fx0, float fx1) {
        const int x0 = juce::roundToInt(fx0 * 604.0f), x1 = juce::roundToInt(fx1 * 604.0f);
        double sum = 0.0;
        int n = 0;
        for (int y = 36; y < 144; ++y)
            for (int x = x0; x < x1; ++x, ++n)
                sum += img.getPixelAt(x, y).getFloatRed() - img.getPixelAt(x, y).getFloatBlue();
        return sum / n;
    };
    const double rust = warmth(0.49f, 0.53f);   // the streak sits just left of centre in the photo
    const double plain = warmth(0.12f, 0.16f);
    INFO("rust " << rust << " plain " << plain);
    // Almost transparent by design: +0.0098 with the layer, -0.0001 without (measured)
    CHECK(rust > plain + 0.005);
}

TEST_CASE("Corner screws: polished heads stand out, two are rusted", "[gui][texture]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto render = [](int theme) {
        ThemeGuard guard(theme);
        sr::AmpFaceplate plate;
        plate.setSize(604, 180);
        plate.setLampCentre({ 300.0f, 90.0f }); // its bloom off the corners (the editor sets it)
        return plate.createComponentSnapshot(plate.getLocalBounds(), true, 1.0f);
    };

    for (const int theme : { sr::Colours::Dark, sr::Colours::Light })
    {
        const auto img = render(theme);
        // Mean over a 5 x 5 box: brightness, and warmth (red minus blue)
        const auto mean = [&img](int cx, int cy, bool warm) {
            double sum = 0.0;
            for (int y = cy - 2; y <= cy + 2; ++y)
                for (int x = cx - 2; x <= cx + 2; ++x)
                {
                    const auto c = img.getPixelAt(x, y);
                    sum += warm ? c.getFloatRed() - c.getFloatBlue() : c.getPerceivedBrightness();
                }
            return sum / 25.0;
        };
        // Screw centres sit 10 px in from the plate outline (0.5 px in from the edge)
        const int left = 11, right = 593, top = 11, bottom = 169;
        INFO("theme " << theme);

        // Brightest pixel in the dome's upper-left quarter, where its highlight sits
        // (the cross recess darkens the centre, so a mean there reads the cut)
        const auto highlight = [&img](int cx, int cy) {
            float best = 0.0f;
            for (int y = cy - 3; y <= cy; ++y)
                for (int x = cx - 3; x <= cx; ++x)
                    best = juce::jmax(best, img.getPixelAt(x, y).getPerceivedBrightness());
            return best;
        };

        // Polished (top left, bottom right): a bright, neutral head, plainly
        // lighter than the coat beside it in the dark theme
        INFO("highlight TL " << highlight(left, top) << " BR " << highlight(right, bottom));
        CHECK(highlight(left, top) > 0.75f);
        CHECK(highlight(right, bottom) > 0.75f);
        CHECK(std::abs(mean(left, top, true)) < 0.05);
        if (theme == sr::Colours::Dark)
            CHECK(mean(left, top, false) > mean(left + 30, top + 20, false) + 0.1); // gunmetal: +0.16 measured

        // Rust (top right more, bottom left only flecks): warmer than the polished ones
        const double polishedWarmth = mean(left, top, true);
        INFO("rust TR " << mean(right, top, true) << " BL " << mean(left, bottom, true)
                        << " polished " << polishedWarmth);
        CHECK(mean(right, top, true) > polishedWarmth + 0.05);
        CHECK(mean(left, bottom, true) > polishedWarmth + 0.025);
        CHECK(mean(right, top, true) > mean(left, bottom, true));

        // The stain below the top-right screw, where the drip starts. Thin, so
        // look for the warmest pixel rather than averaging it away
        const auto warmest = [&img](int cx, int cy) {
            float best = -1.0f;
            for (int y = cy - 2; y <= cy + 2; ++y)
                for (int x = cx - 3; x <= cx + 3; ++x)
                    best = juce::jmax(best, img.getPixelAt(x, y).getFloatRed()
                                                - img.getPixelAt(x, y).getFloatBlue());
            return best;
        };
        INFO("drip " << warmest(right, top + 7) << " coat " << warmest(right - 40, top + 7));
        CHECK(warmest(right, top + 7) > warmest(right - 40, top + 7) + 0.04);
    }

    // Deterministic: the rust is seeded noise, so two renders match exactly
    CHECK(sameImage(render(sr::Colours::Dark), render(sr::Colours::Dark)));
}

// ── Knob wear ──────────────────────────────────────────────────────

TEST_CASE("Knob wear: a seed changes the knob, and repeats exactly", "[gui][texture]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sr::SilentRiotLookAndFeel lnf;
    juce::Slider slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
    slider.setLookAndFeel(&lnf);
    slider.setRotaryParameters(sr::kRotaryStartAngle, sr::kRotaryEndAngle, true);
    slider.setRange(0.0, 1.0);
    slider.setValue(0.6, juce::dontSendNotification);
    slider.setSize(120, 120);
    slider.getProperties().set(sr::SilentRiotLookAndFeel::kPointerKnobProperty, true);

    const auto render = [&slider](int seed) {
        slider.getProperties().set(sr::SilentRiotLookAndFeel::kKnobWearProperty, seed);
        return slider.createComponentSnapshot(slider.getLocalBounds(), true, 1.0f);
    };
    const auto clean = render(0), worn1 = render(1), worn2 = render(2);
    CHECK_FALSE(sameImage(clean, worn1));            // wear paints something
    CHECK_FALSE(sameImage(worn1, worn2));            // each knob wears its own way
    CHECK(sameImage(worn1, render(1)));              // and the same way every repaint

    slider.setLookAndFeel(nullptr);
}
