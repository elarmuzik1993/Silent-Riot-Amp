// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Tone lock, part 1: the fingerprint and its comparison. A lock that never
// fails is worse than none, so these pin what it must catch (a 0.2 dB level
// change, a gentle low-pass) and what it must shrug off (noise far below the
// floor), plus the file format's round trip and its refusal of a truncated
// file. tone_lock_tests.cpp applies it to the real amp.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include "tone_lock.h"

#include <cmath>
#include <vector>

using namespace sr::test::tone;

namespace {

constexpr double kRate = 48000.0;

// 2 s of a bright, harmonically rich tone: a band-limited sawtooth at 110 Hz.
std::vector<float> saw(float gain = 0.25f, double seconds = 2.0)
{
    const int n = static_cast<int>(seconds * kRate);
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        double v = 0.0;
        for (int k = 1; k * 110.0 < 18000.0; ++k)
            v += std::sin(2.0 * juce::MathConstants<double>::pi * 110.0 * k * i / kRate) / k;
        x[static_cast<size_t>(i)] = static_cast<float>(gain * v);
    }
    return x;
}

std::vector<float> scaled(std::vector<float> x, float dB)
{
    const float g = std::pow(10.0f, dB / 20.0f);
    for (auto& s : x) s *= g;
    return x;
}

std::vector<float> lowPassed(std::vector<float> x, double cutoffHz)
{
    const float a = static_cast<float>(std::exp(-2.0 * juce::MathConstants<double>::pi * cutoffHz / kRate));
    float y = 0.0f;
    for (auto& s : x) { y = (1.0f - a) * s + a * y; s = y; }
    return x;
}

} // namespace

TEST_CASE("tone lock: identical audio matches", "[tone-lock-core]")
{
    const auto x = saw();
    const auto a = fingerprint(x, x, kRate);
    const auto b = fingerprint(x, x, kRate);
    REQUIRE(a.frames > 50);
    REQUIRE(a.bands == kBands);
    const auto c = compare(a, b);
    CHECK(c.matches);
    CHECK(c.worstBandDeltaDb == 0.0f);
}

TEST_CASE("tone lock: a 0.2 dB level change fails", "[tone-lock-core]")
{
    const auto x = saw();
    const auto y = scaled(x, 0.2f);
    const auto c = compare(fingerprint(x, x, kRate), fingerprint(y, y, kRate));
    CHECK_FALSE(c.matches);
    CHECK(c.rmsDeltaDb > 0.15f);
}

TEST_CASE("tone lock: one channel changing fails", "[tone-lock-core]")
{
    const auto x = saw();
    const auto y = scaled(x, 0.2f);
    CHECK_FALSE(compare(fingerprint(x, x, kRate), fingerprint(x, y, kRate)).matches);
}

TEST_CASE("tone lock: a gentle low-pass fails", "[tone-lock-core]")
{
    const auto x = saw();
    const auto y = lowPassed(x, 8000.0);
    const auto c = compare(fingerprint(x, x, kRate), fingerprint(y, y, kRate));
    CHECK_FALSE(c.matches);
    CHECK(c.worstBandDeltaDb > 1.0f);
}

TEST_CASE("tone lock: a different length fails", "[tone-lock-core]")
{
    const auto x = saw(0.25f, 2.0);
    const auto y = saw(0.25f, 2.5);
    CHECK_FALSE(compare(fingerprint(x, x, kRate), fingerprint(y, y, kRate)).matches);
}

TEST_CASE("tone lock: noise far below the floor does not matter", "[tone-lock-core]")
{
    // Two signals that are silent at the floor's scale but differ by 20 dB.
    auto quiet = scaled(saw(), -140.0f);
    auto quieter = scaled(saw(), -160.0f);
    CHECK(compare(fingerprint(quiet, quiet, kRate), fingerprint(quieter, quieter, kRate)).matches);
}

TEST_CASE("tone lock: signal appearing above the floor fails", "[tone-lock-core]")
{
    auto silent = scaled(saw(), -160.0f);
    auto audible = saw();
    CHECK_FALSE(compare(fingerprint(silent, silent, kRate), fingerprint(audible, audible, kRate)).matches);
}

TEST_CASE("tone lock: the skip window ignores the start", "[tone-lock-core]")
{
    auto x = saw();
    auto y = x;
    for (int i = 0; i < static_cast<int>(0.4 * kRate); ++i)   // a startup glitch
        y[static_cast<size_t>(i)] = (i % 2 == 0) ? 0.9f : -0.9f;
    CHECK(compare(fingerprint(x, x, kRate), fingerprint(y, y, kRate)).matches);
}

TEST_CASE("tone lock: file round trip and truncation", "[tone-lock-core]")
{
    const auto x = saw();
    const auto fp = fingerprint(x, x, kRate);

    juce::TemporaryFile tmp(".srtl");
    REQUIRE(writeFingerprint(tmp.getFile(), fp));
    const auto back = readFingerprint(tmp.getFile());
    REQUIRE(back.has_value());
    CHECK(back->frames == fp.frames);
    CHECK(back->bands == fp.bands);
    CHECK(back->rmsDb == fp.rmsDb);
    CHECK(back->levelsDb == fp.levelsDb);

    juce::MemoryBlock data;
    REQUIRE(tmp.getFile().loadFileAsData(data));
    data.setSize(data.getSize() - 4);
    REQUIRE(tmp.getFile().replaceWithData(data.getData(), data.getSize()));
    CHECK_FALSE(readFingerprint(tmp.getFile()).has_value());

    CHECK_FALSE(readFingerprint(tmp.getFile().getSiblingFile("does-not-exist.srtl")).has_value());
}
