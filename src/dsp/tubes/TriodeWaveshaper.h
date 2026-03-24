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

} // namespace sr
