#pragma once
// ═══════════════════════════════════════════════════════════════
// Test Helpers — shared utilities for Silent Riot test suite
// ═══════════════════════════════════════════════════════════════

#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>
#include "utils/VoltageScale.h"
#include "utils/Constants.h"

namespace sr::test {

constexpr float TEST_SR = 44100.0f;
constexpr float TEST_SR_4X = 44100.0f * 4.0f;

// Generate a sine wave buffer in normalized voltage domain
inline std::vector<float> makeSine(float freqHz, float amplitude_n, int numSamples, float sr = TEST_SR) {
    std::vector<float> buf(numSamples);
    for (int i = 0; i < numSamples; ++i)
        buf[i] = amplitude_n * std::sin(2.0f * PI_F * freqHz * i / sr);
    return buf;
}

// Generate a DC offset buffer
inline std::vector<float> makeDC(float level_n, int numSamples) {
    return std::vector<float>(numSamples, level_n);
}

// Generate a step function (0 for first half, level for second half)
inline std::vector<float> makeStep(float level_n, int numSamples) {
    std::vector<float> buf(numSamples, 0.0f);
    for (int i = numSamples / 2; i < numSamples; ++i)
        buf[i] = level_n;
    return buf;
}

// Measure peak absolute value in a buffer
inline float peakAbs(const std::vector<float>& buf) {
    float peak = 0.0f;
    for (float v : buf) peak = std::max(peak, std::abs(v));
    return peak;
}

// Measure peak absolute value in a subrange (skip initial transient)
inline float peakAbsRange(const std::vector<float>& buf, int from, int to) {
    float peak = 0.0f;
    to = std::min(to, static_cast<int>(buf.size()));
    for (int i = from; i < to; ++i)
        peak = std::max(peak, std::abs(buf[i]));
    return peak;
}

// Measure RMS of a buffer
inline float rms(const std::vector<float>& buf) {
    float sum = 0.0f;
    for (float v : buf) sum += v * v;
    return std::sqrt(sum / static_cast<float>(buf.size()));
}

// Measure RMS in a subrange
inline float rmsRange(const std::vector<float>& buf, int from, int to) {
    float sum = 0.0f;
    to = std::min(to, static_cast<int>(buf.size()));
    for (int i = from; i < to; ++i) sum += buf[i] * buf[i];
    return std::sqrt(sum / static_cast<float>(to - from));
}

// Audio taper (matches processBlock usage)
inline float audioTaper(float x) {
    return std::pow(x, GAIN_TAPER_EXPONENT);
}

// Measure magnitude response of a filter at a given frequency
// Processes a full-amplitude sine through the filter and returns output peak / input peak
template<typename FilterT>
float measureMagnitude(FilterT& filter, float freqHz, float sr, int cycles = 20) {
    int samplesPerCycle = static_cast<int>(sr / freqHz);
    int totalSamples = samplesPerCycle * cycles;
    int measureFrom = samplesPerCycle * (cycles / 2); // skip transient
    
    float inPeak = 1.0f;
    float outPeak = 0.0f;
    
    for (int i = 0; i < totalSamples; ++i) {
        float in = inPeak * std::sin(2.0f * PI_F * freqHz * i / sr);
        float out = filter.process(in);
        if (i >= measureFrom)
            outPeak = std::max(outPeak, std::abs(out));
    }
    
    return outPeak / inPeak;
}

// Measure magnitude using a callable (lambda) instead of a filter object
template<typename ProcessFn>
float measureMagnitudeCustom(ProcessFn processFn, float freqHz, float sr, int cycles = 20) {
    int samplesPerCycle = static_cast<int>(sr / freqHz);
    int totalSamples = samplesPerCycle * cycles;
    int measureFrom = samplesPerCycle * (cycles / 2); // skip transient

    float inPeak = 1.0f;
    float outPeak = 0.0f;

    for (int i = 0; i < totalSamples; ++i) {
        float in = inPeak * std::sin(2.0f * PI_F * freqHz * i / sr);
        float out = processFn(in);
        if (i >= measureFrom)
            outPeak = std::max(outPeak, std::abs(out));
    }

    return outPeak / inPeak;
}

} // namespace sr::test
