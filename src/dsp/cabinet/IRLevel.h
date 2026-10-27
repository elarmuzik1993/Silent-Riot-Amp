#pragma once
// ═══════════════════════════════════════════════════════════════
// IR level match (IR-path plan Task 2) — pure C++, header-only.
//
// A loaded cabinet IR is scaled ONCE, at its native rate, so that its band
// power equals the built-in cab's, then handed to juce::dsp::Convolution with
// Normalise::no. JUCE's own Normalise::yes scaled to 0.125/sqrt(energy) —
// 18 dB under unit energy, measured AFTER resampling to the session rate — so
// IR mode played ~11 dB under the built-in cab and +3 dB louder at 96 kHz than
// at 48 kHz. With Normalise::no JUCE instead scales the resampled IR by
// originalRate / sessionRate, which keeps its frequency response, and so the
// level set here, the same at every host rate.
//
// Metric: the power average of |H(f)|² over log-spaced points, 1/12 octave
// apart, from IR_LEVEL_BAND_LO_HZ to IR_LEVEL_BAND_HI_HZ (69 points). Log
// spacing weighs every octave equally. Matching THE BUILT-IN CAB's value means
// OUTPUT_MAKEUP_GAIN, the headroom calibration and A1/D1 mean the same in both
// cab modes (owner decision D1). The OT low-pass is common to both paths and
// left out.
// ═══════════════════════════════════════════════════════════════

#include "../../utils/Constants.h"
#include "../filters/Biquad.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace sr::irlevel {

inline constexpr double kTwoPi = 6.283185307179586476925;

struct Band
{
    double loHz;
    double hiHz;
    int    points;   // log-spaced, both edges included
};

// The band for an IR at `rate`. The top edge is clamped to 0.45 × rate, so a
// low-rate IR (8 kHz: 3.6 kHz) is measured over what it can represent.
inline Band bandFor(double rate)
{
    const double lo = IR_LEVEL_BAND_LO_HZ;
    const double hi = std::min(static_cast<double>(IR_LEVEL_BAND_HI_HZ), 0.45 * rate);
    const int points = hi > lo
        ? static_cast<int>(std::ceil(std::log2(hi / lo) * IR_LEVEL_POINTS_PER_OCT - 1e-9)) + 1
        : 1;
    return { lo, hi, points };
}

inline double bandFreq(const Band& b, int k)
{
    return b.points > 1 ? b.loHz * std::pow(b.hiHz / b.loHz, static_cast<double>(k) / (b.points - 1))
                        : b.loHz;
}

inline double powerToDb(double power) { return 10.0 * std::log10(power); }

// Channel-averaged band power of an IR: mean over channels and band points of
// |H(f)|², H from a direct DFT at each point (a double-precision phasor).
// ~points × numSamples × numChannels complex multiply-adds: about 1.1M for a
// stereo 8192-sample IR. Message thread only (never in processBlock).
//
// Returns no level for an IR that is all zeros (or has no channels/samples):
// there is nothing to scale, and the caller leaves it as it is. Non-finite
// samples give a non-finite power; callers check isSafeToNormaliseIR() first.
inline std::optional<double> bandPower(const float* const* channels, int numChannels,
                                       int numSamples, double rate)
{
    if (numChannels < 1 || numSamples < 1 || !(rate > 0.0))
        return std::nullopt;

    bool anyNonZero = false;
    for (int ch = 0; ch < numChannels && !anyNonZero; ++ch)
        anyNonZero = std::any_of(channels[ch], channels[ch] + numSamples,
                                 [](float x) { return std::abs(x) > 0.0f; });
    if (!anyNonZero)
        return std::nullopt;

    const Band band = bandFor(rate);
    double sum = 0.0;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* x = channels[ch];
        for (int k = 0; k < band.points; ++k)
        {
            const double w = kTwoPi * bandFreq(band, k) / rate;
            const double stepRe = std::cos(w), stepIm = -std::sin(w);
            double pRe = 1.0, pIm = 0.0;   // e^{-jwn}
            double re = 0.0, im = 0.0;
            for (int n = 0; n < numSamples; ++n)
            {
                re += x[n] * pRe;
                im += x[n] * pIm;
                const double nRe = pRe * stepRe - pIm * stepIm;
                pIm = pRe * stepIm + pIm * stepRe;
                pRe = nRe;
            }
            sum += re * re + im * im;
        }
    }
    return sum / (static_cast<double>(band.points) * numChannels);
}

// |H(f)|² of one biquad (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2).
inline double biquadPowerAt(const Biquad& q, double f, double rate)
{
    const double w = kTwoPi * f / rate;
    const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2.0 * w), s2 = std::sin(2.0 * w);
    const double nRe = q.b0 + q.b1 * c1 + q.b2 * c2, nIm = -(q.b1 * s1 + q.b2 * s2);
    const double dRe = 1.0 + q.a1 * c1 + q.a2 * c2, dIm = -(q.a1 * s1 + q.a2 * s2);
    return (nRe * nRe + nIm * nIm) / (dRe * dRe + dIm * dIm);
}

// The built-in cab's band power over `band` — the reference every IR is
// scaled to. The three biquads are designed by the very calls
// FilterBank::recalcAll() makes (Biquad::makePeakEQ / makeLowPass / makeNotch
// on the CAB_* constants, in float as there), so a CAB_* change moves the
// reference too; the pinned test in ir_level_tests.cpp catches it.
//
// Design rate: a FIXED one (IR_LEVEL_REF_DESIGN_SR), not the IR's native rate.
// The processor never runs these biquads at an IR's rate: it designs them at
// the oversampled rate (base × 2/4/8 — 88.2 to 384 kHz), where they are within
// 0.01 dB of each other over the band. Designing them at a 44.1 kHz IR's rate
// would add bilinear warping the built-in cab doesn't have, and tying the
// reference to the session rate would change every IR's gain on an oversample
// switch.
inline double builtInCabBandPower(const Band& band, double designRate = IR_LEVEL_REF_DESIGN_SR)
{
    Biquad presence, rolloff, notch;
    const float fs = static_cast<float>(designRate);
    presence.makePeakEQ(CAB_PRESENCE_FREQ, CAB_PRESENCE_Q, CAB_PRESENCE_GAIN_DB, fs);
    rolloff.makeLowPass(CAB_ROLLOFF_FREQ, CAB_ROLLOFF_Q, fs);
    notch.makeNotch(CAB_NOTCH_FREQ, CAB_NOTCH_Q, fs);

    double sum = 0.0;
    for (int k = 0; k < band.points; ++k)
    {
        const double f = bandFreq(band, k);
        sum += biquadPowerAt(presence, f, designRate)
             * biquadPowerAt(rolloff, f, designRate)
             * biquadPowerAt(notch, f, designRate);
    }
    return sum / band.points;
}

// The full, unclamped band (IR_LEVEL_BAND_LO_HZ..HI_HZ, 69 points): what an IR
// at 11.1 kHz or above is measured over, and what the -0.90 dB pin refers to.
inline Band fullBand()
{
    const double lo = IR_LEVEL_BAND_LO_HZ, hi = IR_LEVEL_BAND_HI_HZ;
    return { lo, hi,
             static_cast<int>(std::ceil(std::log2(hi / lo) * IR_LEVEL_POINTS_PER_OCT - 1e-9)) + 1 };
}

inline double builtInCabBandPower() { return builtInCabBandPower(fullBand()); }

// The reference for an IR at `irRate`: the built-in cab over the SAME band
// the IR is measured over (bandFor(irRate)), so a low-rate IR whose band is
// clamped is compared like for like. Equal to builtInCabBandPower() from
// 11.1 kHz up; -0.72 dB instead of -0.89 dB at 8 kHz (100 Hz–3.6 kHz).
inline double referenceBandPower(double irRate) { return builtInCabBandPower(bandFor(irRate)); }

// The one amplitude gain that brings an IR at `irRate` with band power
// `power` to the built-in cab's. No level → 1 (left unscaled). Capped at
// IR_LEVEL_MAX_GAIN_DB, so a wrong or sub-only file with next to nothing in
// band is not raised without bound into the output tanh.
inline double levelMatchGain(std::optional<double> power, double irRate)
{
    if (!power.has_value() || !(*power > 0.0) || !std::isfinite(*power))
        return 1.0;
    const double maxGain = std::pow(10.0, IR_LEVEL_MAX_GAIN_DB / 20.0);
    return std::min(std::sqrt(referenceBandPower(irRate) / *power), maxGain);
}

} // namespace sr::irlevel
