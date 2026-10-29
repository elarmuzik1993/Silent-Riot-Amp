// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
// ═══════════════════════════════════════════════════════════════
// Tone lock: reduces a stereo render to a fingerprint and compares two.
//
// The fingerprint is the level of 32 log-spaced bands (40 Hz to 16 kHz) in
// every 2048-sample Hann frame (hop 1024), per channel, plus the overall
// RMS — after skipping the start of the render, which is a transition (the
// amp settling from idle) and may still be fixed. It is phase-blind on
// purpose: it pins what is heard (level, tone, distortion, dynamics over
// time) and tolerates float rounding between compilers, at ~10 KB a render
// instead of the raw audio. See docs/dev/tone-lock.md.
// ═══════════════════════════════════════════════════════════════

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

namespace sr::test::tone {

inline constexpr int    kFftOrder = 11;                 // 2048-point frames
inline constexpr int    kFrameSize = 1 << kFftOrder;
inline constexpr int    kHop = kFrameSize / 2;
inline constexpr int    kBands = 32;
inline constexpr double kLowHz = 40.0;
inline constexpr double kHighHz = 16000.0;
inline constexpr int    kChannels = 2;

struct Fingerprint
{
    int frames = 0;
    int bands = 0;
    float rmsDb = -200.0f;
    std::vector<float> levelsDb;   // [frame][channel][band]
};

inline float toDb(double power) { return static_cast<float>(10.0 * std::log10(power + 1e-20)); }

inline Fingerprint fingerprint(const std::vector<float>& left, const std::vector<float>& right,
                               double sampleRate, double skipSeconds = 0.5)
{
    Fingerprint fp;
    fp.bands = kBands;

    const int skip = static_cast<int>(std::lround(skipSeconds * sampleRate));
    const int n = static_cast<int>(std::min(left.size(), right.size()));
    const int usable = n - skip;
    fp.frames = usable >= kFrameSize ? (usable - kFrameSize) / kHop + 1 : 0;

    // Band edges in FFT bins. Low bands narrower than a bin share it with
    // their neighbour; that is fine, the layout only has to be repeatable.
    const double binHz = sampleRate / kFrameSize;
    const int nyquistBin = kFrameSize / 2;
    int loBin[kBands], hiBin[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        const double lo = kLowHz * std::pow(kHighHz / kLowHz, static_cast<double>(b) / kBands);
        const double hi = kLowHz * std::pow(kHighHz / kLowHz, static_cast<double>(b + 1) / kBands);
        loBin[b] = std::clamp(static_cast<int>(lo / binHz), 1, nyquistBin - 1);
        hiBin[b] = std::clamp(std::max(loBin[b] + 1, static_cast<int>(hi / binHz)), loBin[b] + 1, nyquistBin);
    }

    // A full-scale sine through the Hann window peaks at N/4: normalise so it
    // reads ~0 dB in its band.
    const double norm = 1.0 / (static_cast<double>(kFrameSize) * kFrameSize / 16.0);

    juce::dsp::FFT fft(kFftOrder);
    juce::dsp::WindowingFunction<float> window(static_cast<size_t>(kFrameSize),
                                               juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> work(static_cast<size_t>(2 * kFrameSize));
    fp.levelsDb.resize(static_cast<size_t>(fp.frames) * kChannels * kBands);

    const std::vector<float>* channels[kChannels] = { &left, &right };
    for (int f = 0; f < fp.frames; ++f)
    {
        const int start = skip + f * kHop;
        for (int ch = 0; ch < kChannels; ++ch)
        {
            std::fill(work.begin(), work.end(), 0.0f);
            std::copy_n(channels[ch]->begin() + start, kFrameSize, work.begin());
            window.multiplyWithWindowingTable(work.data(), static_cast<size_t>(kFrameSize));
            fft.performFrequencyOnlyForwardTransform(work.data(), true);

            for (int b = 0; b < kBands; ++b)
            {
                double power = 0.0;
                for (int k = loBin[b]; k < hiBin[b]; ++k)
                    power += static_cast<double>(work[static_cast<size_t>(k)]) * work[static_cast<size_t>(k)];
                fp.levelsDb[static_cast<size_t>((f * kChannels + ch) * kBands + b)] = toDb(power * norm);
            }
        }
    }

    double sum = 0.0;
    for (int i = std::max(0, skip); i < n; ++i)
        sum += static_cast<double>(left[static_cast<size_t>(i)]) * left[static_cast<size_t>(i)]
             + static_cast<double>(right[static_cast<size_t>(i)]) * right[static_cast<size_t>(i)];
    fp.rmsDb = usable > 0 ? toDb(sum / (2.0 * usable)) : -200.0f;
    return fp;
}

// ── File format: "SRTL", version, frames, bands, rmsDb, then the levels ──
inline constexpr int kFileVersion = 1;

inline bool writeFingerprint(const juce::File& file, const Fingerprint& fp)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream out(file);
    if (!out.openedOk())
        return false;
    out.write("SRTL", 4);
    out.writeInt(kFileVersion);
    out.writeInt(fp.frames);
    out.writeInt(fp.bands);
    out.writeFloat(fp.rmsDb);
    for (float v : fp.levelsDb)
        out.writeFloat(v);
    out.flush();
    return out.getStatus().wasOk();
}

inline std::optional<Fingerprint> readFingerprint(const juce::File& file)
{
    juce::MemoryBlock data;
    if (!file.existsAsFile() || !file.loadFileAsData(data))
        return std::nullopt;

    juce::MemoryInputStream in(data, false);
    char magic[4] = {};
    if (in.read(magic, 4) != 4 || std::memcmp(magic, "SRTL", 4) != 0)
        return std::nullopt;
    if (in.readInt() != kFileVersion)
        return std::nullopt;

    Fingerprint fp;
    fp.frames = in.readInt();
    fp.bands = in.readInt();
    fp.rmsDb = in.readFloat();
    if (fp.frames < 0 || fp.bands != kBands)
        return std::nullopt;

    const auto count = static_cast<size_t>(fp.frames) * kChannels * static_cast<size_t>(fp.bands);
    if (in.getNumBytesRemaining() != static_cast<juce::int64>(count * sizeof(float)))
        return std::nullopt;
    fp.levelsDb.resize(count);
    for (auto& v : fp.levelsDb)
        v = in.readFloat();
    return fp;
}

// ── Comparison ──
struct Comparison
{
    bool matches = false;
    float worstBandDeltaDb = 0.0f;
    float rmsDeltaDb = 0.0f;
    juce::String where;   // the worst band, for the failure message
};

// Levels below floorDb count as "silent": two silent values match whatever
// their exact level, so noise far under the music cannot make the lock flaky.
inline Comparison compare(const Fingerprint& ref, const Fingerprint& got,
                          float bandTolDb = 0.1f, float rmsTolDb = 0.02f, float floorDb = -100.0f)
{
    Comparison c;
    if (ref.frames != got.frames || ref.bands != got.bands
        || ref.levelsDb.size() != got.levelsDb.size())
    {
        c.where = "shape differs: " + juce::String(ref.frames) + "x" + juce::String(ref.bands)
                + " vs " + juce::String(got.frames) + "x" + juce::String(got.bands);
        c.worstBandDeltaDb = 1.0e9f;
        return c;
    }

    for (size_t i = 0; i < ref.levelsDb.size(); ++i)
    {
        const float r = std::max(ref.levelsDb[i], floorDb);
        const float g = std::max(got.levelsDb[i], floorDb);
        const float d = std::abs(r - g);
        if (d > c.worstBandDeltaDb)
        {
            c.worstBandDeltaDb = d;
            const auto frame = static_cast<int>(i / (kChannels * static_cast<size_t>(ref.bands)));
            const auto ch = static_cast<int>((i / static_cast<size_t>(ref.bands)) % kChannels);
            const auto band = static_cast<int>(i % static_cast<size_t>(ref.bands));
            c.where = "frame " + juce::String(frame) + ", channel " + juce::String(ch) + ", band "
                    + juce::String(band) + ": " + juce::String(ref.levelsDb[i], 2) + " dB -> "
                    + juce::String(got.levelsDb[i], 2) + " dB";
        }
    }

    c.rmsDeltaDb = std::abs(std::max(ref.rmsDb, floorDb) - std::max(got.rmsDb, floorDb));
    c.matches = c.worstBandDeltaDb <= bandTolDb && c.rmsDeltaDb <= rmsTolDb;
    return c;
}

} // namespace sr::test::tone
