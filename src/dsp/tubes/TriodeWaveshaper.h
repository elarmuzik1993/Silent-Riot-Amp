#pragma once
// Triode Waveshaper — 12AX7/ECC83 Koren Model (Spec §4.1 Option B)
#include "../../utils/VoltageScale.h"
#include "../../utils/Constants.h"
#include <cmath>
#include <algorithm>

namespace sr {

struct KorenTriodeParams {
    float mu;      // amplification factor (dimensionless)
    float ex;      // exponent (dimensionless)
    float kg_n;    // grid constant (pre-normalized to DSP domain)
    float kp;      // plate constant (dimensionless)
    float kvb_n;   // knee voltage² (normalized: Kvb_real * V_TO_N²)
};

// Pre-computed ECC83/12AX7 Koren parameters (normalized domain)
// kg_n derivation: Ip_n = E1_n^ex / kg_n, where kg_n = Kg_real * V_TO_N^ex / I_TO_N
// kvb_n: Kvb has units V², so kvb_n = Kvb_real * V_TO_N²
inline KorenTriodeParams makeECC83Params() {
    return {
        100.0f,                                                // mu
        1.4f,                                                  // ex
        1060.0f * std::pow(VS::V_TO_N, 1.4f) / VS::I_TO_N,   // kg_n
        600.0f,                                                // kp
        300.0f * VS::V_TO_N * VS::V_TO_N,                     // kvb_n
    };
}

// Koren triode plate current — takes grid-cathode and plate voltages (normalized)
float korenTriode(float vgk_n, float vPlate_n, const KorenTriodeParams& p);

// Persistent state for inter-sample plate voltage caching
struct TriodeState {
    float vPlate = -1.0f;  // <0 = uninitialized (uses vSupply*0.6 as first guess)
    void reset() { vPlate = -1.0f; }
};

// Triode waveshaper: iterative load-line solve with Koren model, returns plate voltage
// Stateful version — caches converged Vp for next sample's initial guess
float triodeWaveshaper(float vGrid_n, float vBias_n, float vSupply_n, float rPlate_n, TriodeState& state);

// Stateless convenience (for tests / single evaluations)
inline float triodeWaveshaper(float vGrid_n, float vBias_n, float vSupply_n, float rPlate_n) {
    TriodeState tmp;
    return triodeWaveshaper(vGrid_n, vBias_n, vSupply_n, rPlate_n, tmp);
}

inline float audioTaper(float x) { return x * x; }

// User gain knob -> virtual pot position used by both physical gain gangs.
// The EL84 recalibration made the old direct x^3 two-gang law hit edge/crank
// behavior by knob 2-3, so the knob is remapped through a monotonic cubic.
//
// GAIN_KNOB_HEADROOM then compresses the knob's full sweep onto the lower part
// of that curve: the old top end (knob 8-10) was past where the chain runs out
// of headroom and contributed compression rather than usable drive, so knob 10
// now lands where knob 8 used to. The full 270 deg arc covers the range that
// actually sounds good, which also buys finer resolution through it.
//
// Anchors at HEADROOM = 0.8:  knob 0.30 -> drive 0.108,
//                             knob 0.50 -> drive 0.156,
//                             knob 1.00 -> drive 0.520 (= old knob 0.80).
// The cubic is monotonic on [0,1] (its derivative 0.7167 - 3.1x + 5.5x^2 has a
// negative discriminant), and scaling the input keeps it monotonic.
inline float gainDriveTaper(float x) {
    x = std::clamp(x, 0.0f, 1.0f) * GAIN_KNOB_HEADROOM;
    return x * (0.7166667f + x * (-1.55f + x * 1.8333333f));
}

// User master-volume knob -> gain applied to the PI outputs feeding the EL84
// grids. The volume control IS the power-stage drive control, and the chain
// hands it far more drive than the tube can use, so the plain audioTaper() law
// pushed the grids into permanent conduction from about knob 4 upward. Above
// that the grid diode clamp set the level instead of the tube, which cost the
// amp its touch sensitivity and left the top half of the knob adding grit
// rather than loudness.
//
// VOLUME_KNOB_HEADROOM remaps the full sweep onto the part of the drive range
// that stays out of permanent conduction, exactly as GAIN_KNOB_HEADROOM does
// for the gain knob. Knob 10 now delivers what knob 2 used to.
//
// Anchors at HEADROOM = 0.21:  knob 0.50 -> 0.011025,
//                              knob 1.00 -> 0.044100 (= old knob 0.21).
inline float volumeDriveTaper(float x) {
    return audioTaper(std::clamp(x, 0.0f, 1.0f) * VOLUME_KNOB_HEADROOM);
}

} // namespace sr
