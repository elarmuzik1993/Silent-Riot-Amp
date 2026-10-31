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
#include "gui/CabinetView.h"
#include "gui/ColourPalette.h"
#include "gui/LookAndFeel.h"
#include "gui/ProceduralTexture.h"

#include <juce_events/juce_events.h>

#include <algorithm>
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
