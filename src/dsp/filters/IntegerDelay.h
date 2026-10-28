// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
// Fixed whole-sample delay, pre-allocated (no allocation in processBlock).
//
// Used to delay the IIR cab path by the IR-path limiter oversampler's latency,
// so the latency the processor reports holds in both cab modes (review B4).
// Without it the IIR path — the default — ran ahead of the reported latency.
// Also delays the dry signal by the reported latency under host bypass.
#include <algorithm>
#include <array>
#include <cstddef>

namespace sr {

template <int MaxChannels, int MaxDelay>
struct IntegerDelay {
    static_assert(MaxChannels > 0 && MaxDelay > 0, "IntegerDelay needs room");

    // Clamped to [0, MaxDelay]. Clears the line, so call it from prepare only.
    void setDelay(int samples) {
        delay = std::clamp(samples, 0, MaxDelay);
        reset();
    }

    int getDelay() const { return delay; }

    void reset() {
        for (auto& ch : lines) ch.fill(0.0f);
        writePos = 0;
    }

    // In place. Channels beyond MaxChannels pass through undelayed.
    void process(float* const* channels, int numChannels, int numSamples) {
        if (delay == 0) return;
        const int n = std::min(numChannels, MaxChannels);
        int pos = writePos;
        for (int ch = 0; ch < n; ++ch) {
            auto& line = lines[static_cast<size_t>(ch)];
            float* x = channels[ch];
            pos = writePos;
            for (int i = 0; i < numSamples; ++i) {
                // The slot about to be overwritten was written `delay` samples ago.
                const float out = line[static_cast<size_t>(pos)];
                line[static_cast<size_t>(pos)] = x[i];
                x[i] = out;
                if (++pos == delay) pos = 0;
            }
        }
        writePos = pos;
    }

    // Feeds the line without reading from it, so a passthrough that is only
    // switched in now and then (the host-bypass path) is always primed with the
    // last `delay` samples and continues seamlessly when process() takes over.
    void write(const float* const* channels, int numChannels, int numSamples) {
        if (delay == 0) return;
        const int n = std::min(numChannels, MaxChannels);
        int pos = writePos;
        for (int ch = 0; ch < n; ++ch) {
            auto& line = lines[static_cast<size_t>(ch)];
            const float* x = channels[ch];
            pos = writePos;
            for (int i = 0; i < numSamples; ++i) {
                line[static_cast<size_t>(pos)] = x[i];
                if (++pos == delay) pos = 0;
            }
        }
        writePos = pos;
    }

private:
    std::array<std::array<float, static_cast<size_t>(MaxDelay)>, static_cast<size_t>(MaxChannels)> lines {};
    int delay    = 0;
    int writePos = 0;
};

} // namespace sr
