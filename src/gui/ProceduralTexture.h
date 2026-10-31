// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdint>

// Noise and compositing helpers for the surface textures painted in code (the
// amp plate's powder coat, the cab's tolex and grille dust). Deterministic:
// the same seed and coordinates always give the same value, so a texture
// rendered into a cache looks the same on every launch. GUI thread only;
// nothing here is meant for the audio thread.
namespace sr::texture {

inline uint32_t hash(int x, int y, int seed)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u
               + static_cast<uint32_t>(seed) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// 2D gradient (Perlin) noise, roughly -0.7..0.7, quintic fade
inline float gradient(float x, float y, int seed)
{
    static constexpr float gx[8] = { 1.0f, -1.0f, 0.0f, 0.0f, 0.7071f, -0.7071f, 0.7071f, -0.7071f };
    static constexpr float gy[8] = { 0.0f, 0.0f, 1.0f, -1.0f, 0.7071f, 0.7071f, -0.7071f, -0.7071f };
    const float fx0 = std::floor(x), fy0 = std::floor(y);
    const int xi = static_cast<int>(fx0), yi = static_cast<int>(fy0);
    const float fx = x - fx0, fy = y - fy0;
    const auto dotG = [&](int cx, int cy, float dx, float dy) {
        const uint32_t k = hash(xi + cx, yi + cy, seed) & 7u;
        return gx[k] * dx + gy[k] * dy;
    };
    const auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    const float u = fade(fx), v = fade(fy);
    const float n00 = dotG(0, 0, fx, fy), n10 = dotG(1, 0, fx - 1.0f, fy);
    const float n01 = dotG(0, 1, fx, fy - 1.0f), n11 = dotG(1, 1, fx - 1.0f, fy - 1.0f);
    const float nx0 = n00 + (n10 - n00) * u, nx1 = n01 + (n11 - n01) * u;
    return nx0 + (nx1 - nx0) * v;
}

// Fractal sum mapped to ~0..1; each octave rotated ~37 degrees and offset, so
// the lattice of one octave never lines up with the next (or with the pixels)
inline float fbm(float x, float y, int seed, int octaves)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o)
    {
        sum += amp * gradient(x, y, seed + o * 131);
        norm += amp;
        const float nx = 0.8f * x - 0.6f * y, ny = 0.6f * x + 0.8f * y;
        x = nx * 2.02f + 17.3f;
        y = ny * 2.02f + 9.1f;
        amp *= 0.5f;
    }
    return juce::jlimit(0.0f, 1.0f, 0.5f + 0.75f * sum / norm);
}

// Cellular (Worley) noise: distances to the nearest and second-nearest
// jittered feature point, in cell units. f2 - f1 is ~0 on the cell borders.
inline void cellular(float x, float y, int seed, float& f1, float& f2)
{
    const float fx0 = std::floor(x), fy0 = std::floor(y);
    const int xi = static_cast<int>(fx0), yi = static_cast<int>(fy0);
    const float fx = x - fx0, fy = y - fy0;
    float d1 = 9.0f, d2 = 9.0f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            const uint32_t h = hash(xi + dx, yi + dy, seed);
            const float px = static_cast<float>(dx) + static_cast<float>(h & 0xffffu) / 65535.0f - fx;
            const float py = static_cast<float>(dy) + static_cast<float>(h >> 16) / 65535.0f - fy;
            const float d = px * px + py * py;
            if (d < d1) { d2 = d1; d1 = d; }
            else if (d < d2) { d2 = d; }
        }
    f1 = std::sqrt(d1);
    f2 = std::sqrt(d2);
}

inline float smoothstep(float e0, float e1, float x)
{
    const float t = juce::jlimit(0.0f, 1.0f, (x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

// One pixel as premultiplied float RGBA, built up by compositing "over"
struct Px
{
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    void over(juce::Colour c, float alpha)
    {
        alpha = juce::jlimit(0.0f, 1.0f, alpha);
        const float keep = 1.0f - alpha;
        r = c.getFloatRed() * alpha + r * keep;
        g = c.getFloatGreen() * alpha + g * keep;
        b = c.getFloatBlue() * alpha + b * keep;
        a = alpha + a * keep;
    }
    juce::Colour colour() const
    {
        if (a <= 0.0001f)
            return juce::Colours::transparentBlack;
        return juce::Colour::fromFloatRGBA(r / a, g / a, b / a, a);
    }
};

} // namespace sr::texture
