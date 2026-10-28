// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
// ═══════════════════════════════════════════════════════════════
// PitchDetector — monophonic pitch tracking for the live tuner
//
// Uses the McLeod Pitch Method (MPM): a Normalized Square Difference
// Function (NSDF) with peak-picking + parabolic interpolation. Robust on
// guitar DI signals and reports a clarity metric (0–1) so the tuner can
// reject noise / silence instead of chasing garbage.
//
// Pure C++ (no JUCE) so it lives in the SilentRiotTests target and can be
// verified against synthetic sines at known frequencies.
// ═══════════════════════════════════════════════════════════════

#include <vector>

namespace sr {

// Chromatic note resolved from a frequency, referenced to A4 (default 440 Hz).
struct NoteInfo {
    bool        valid   = false;   // false when frequency is non-positive
    int         midiNote = 0;      // nearest MIDI note number
    int         octave   = 0;      // scientific pitch notation octave (A4 = octave 4)
    float       cents    = 0.0f;   // signed offset from the nearest note, [-50, +50]
    const char* name     = "-";    // "C", "C#", … "B"
};

// Resolve a frequency (Hz) to its nearest chromatic note + cents offset.
NoteInfo frequencyToNote(float frequencyHz, float a4Hz = 440.0f);

class PitchDetector {
public:
    struct Result {
        float frequencyHz = 0.0f;  // detected fundamental, 0 when no confident pitch
        float clarity     = 0.0f;  // NSDF peak height, [0, 1]; higher = more tonal
    };

    // Size internal buffers for the given sample rate. minFrequencyHz sets the
    // lowest detectable pitch (bounds the correlation lag and window length).
    void prepare(double sampleRate, float minFrequencyHz = 55.0f);

    // Preferred analysis window length in samples (callers should hand process()
    // this many of the most recent input samples).
    int getWindowSize() const { return windowSize; }

    // Detect the fundamental over the supplied samples. Returns {0, clarity} when
    // no peak clears the clarity gate. `n` may be smaller than getWindowSize()
    // (e.g. right after start-up); detection just uses fewer lags.
    Result process(const float* samples, int n);

    void reset();

private:
    double sampleRate = 44100.0;
    int    windowSize = 2048;
    int    maxLag     = 735;      // ~sampleRate / minFrequency
    std::vector<float> nsdf;      // scratch, sized to maxLag + 1

    static constexpr float kClarityThreshold = 0.5f;  // reject below this
    static constexpr float kPeakPickK        = 0.9f;  // MPM key-maximum ratio
    static constexpr float kRmsGate          = 3.0e-4f; // ignore near-silence
};

} // namespace sr
