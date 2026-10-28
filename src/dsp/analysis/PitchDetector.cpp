// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "PitchDetector.h"
#include <cmath>
#include <algorithm>

namespace sr {

static const char* const kNoteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

NoteInfo frequencyToNote(float frequencyHz, float a4Hz)
{
    NoteInfo info;
    if (frequencyHz <= 0.0f || a4Hz <= 0.0f)
        return info;

    // MIDI 69 = A4. Round to the nearest semitone, then measure the residual.
    const float fractional = 69.0f + 12.0f * std::log2(frequencyHz / a4Hz);
    const int   midi       = static_cast<int>(std::lround(fractional));
    const float nearestHz   = a4Hz * std::pow(2.0f, (static_cast<float>(midi) - 69.0f) / 12.0f);

    info.valid    = true;
    info.midiNote = midi;
    info.cents    = 1200.0f * std::log2(frequencyHz / nearestHz);
    // Scientific pitch notation: C-1 is MIDI 0, so C4 (middle C) is MIDI 60.
    info.octave   = midi / 12 - 1;
    info.name     = kNoteNames[((midi % 12) + 12) % 12];
    return info;
}

void PitchDetector::prepare(double sr, float minFrequencyHz)
{
    sampleRate = sr;
    if (minFrequencyHz < 1.0f) minFrequencyHz = 1.0f;

    // Correlation must reach at least one full period of the lowest note.
    maxLag = static_cast<int>(std::ceil(sampleRate / minFrequencyHz)) + 1;

    // Two periods of the lowest note give a stable NSDF; round up to a sensible
    // floor so higher notes still get a well-averaged estimate.
    windowSize = std::max(2048, 2 * maxLag);

    nsdf.assign(static_cast<size_t>(maxLag) + 1, 0.0f);
    reset();
}

void PitchDetector::reset()
{
    std::fill(nsdf.begin(), nsdf.end(), 0.0f);
}

PitchDetector::Result PitchDetector::process(const float* samples, int n)
{
    Result result;
    if (samples == nullptr || n < 32)
        return result;

    // Gate on RMS — silence and hum floors produce meaningless NSDF peaks.
    double sumSq = 0.0;
    for (int i = 0; i < n; ++i)
        sumSq += static_cast<double>(samples[i]) * samples[i];
    const float rms = static_cast<float>(std::sqrt(sumSq / n));
    if (rms < kRmsGate)
        return result;

    const int lags = std::min(maxLag, n - 1);
    if (static_cast<int>(nsdf.size()) < lags + 1)
        nsdf.assign(static_cast<size_t>(lags) + 1, 0.0f);

    // NSDF(tau) = 2·Σ x[j]x[j+tau] / Σ (x[j]² + x[j+tau]²)
    for (int tau = 0; tau <= lags; ++tau)
    {
        double acf = 0.0;   // autocorrelation
        double m   = 0.0;   // squared-sum normaliser
        const int limit = n - tau;
        for (int j = 0; j < limit; ++j)
        {
            const double a = samples[j];
            const double b = samples[j + tau];
            acf += a * b;
            m   += a * a + b * b;
        }
        nsdf[static_cast<size_t>(tau)] = (m > 0.0) ? static_cast<float>(2.0 * acf / m) : 0.0f;
    }

    // MPM peak picking: walk to the first positive zero crossing, then collect
    // the maximum of each positive hump (a "key maximum") up to the next
    // negative crossing. The chosen peak is the first key maximum that clears
    // k · (global key-maximum height).
    std::vector<int> keyMaxima;
    int   bestIdxInHump = -1;
    float bestValInHump = -1.0f;
    bool  aboveZero     = false;
    float globalMax     = 0.0f;

    for (int tau = 1; tau <= lags; ++tau)
    {
        const float prev = nsdf[static_cast<size_t>(tau - 1)];
        const float cur  = nsdf[static_cast<size_t>(tau)];

        if (!aboveZero)
        {
            // Wait for an upward zero crossing before tracking a hump.
            if (prev <= 0.0f && cur > 0.0f)
            {
                aboveZero = true;
                bestIdxInHump = tau;
                bestValInHump = cur;
            }
        }
        else
        {
            if (cur > bestValInHump)
            {
                bestValInHump = cur;
                bestIdxInHump = tau;
            }
            // Downward zero crossing closes the hump.
            if (cur <= 0.0f)
            {
                keyMaxima.push_back(bestIdxInHump);
                globalMax = std::max(globalMax, bestValInHump);
                aboveZero = false;
                bestIdxInHump = -1;
                bestValInHump = -1.0f;
            }
        }
    }
    // Close a hump still open at the end of the buffer.
    if (aboveZero && bestIdxInHump > 0)
    {
        keyMaxima.push_back(bestIdxInHump);
        globalMax = std::max(globalMax, bestValInHump);
    }

    if (keyMaxima.empty() || globalMax <= 0.0f)
        return result;

    const float threshold = kPeakPickK * globalMax;
    int chosen = -1;
    for (int idx : keyMaxima)
    {
        if (nsdf[static_cast<size_t>(idx)] >= threshold)
        {
            chosen = idx;
            break;
        }
    }
    if (chosen < 1)
        return result;

    // Parabolic interpolation around the chosen peak for sub-sample precision.
    float tauEst = static_cast<float>(chosen);
    float peakVal = nsdf[static_cast<size_t>(chosen)];
    if (chosen > 0 && chosen < lags)
    {
        const float y0 = nsdf[static_cast<size_t>(chosen - 1)];
        const float y1 = nsdf[static_cast<size_t>(chosen)];
        const float y2 = nsdf[static_cast<size_t>(chosen + 1)];
        const float denom = y0 - 2.0f * y1 + y2;
        if (std::abs(denom) > 1.0e-9f)
        {
            const float delta = 0.5f * (y0 - y2) / denom;
            tauEst  = static_cast<float>(chosen) + delta;
            peakVal = y1 - 0.25f * (y0 - y2) * delta;
        }
    }

    result.clarity = std::min(1.0f, std::max(0.0f, peakVal));
    if (result.clarity < kClarityThreshold || tauEst <= 0.0f)
    {
        result.frequencyHz = 0.0f;
        return result;
    }

    result.frequencyHz = static_cast<float>(sampleRate) / tauEst;
    return result;
}

} // namespace sr
