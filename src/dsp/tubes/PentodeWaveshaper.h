#pragma once
// EL84 Pentode Model (Spec §4.2) — screen-aware, push-pull
// Uses Koren pentode equations: screen grid as virtual anode
#include "../../utils/VoltageScale.h"
#include "../../utils/Constants.h"
#include <cmath>
#include <algorithm>

namespace sr {

struct KorenPentodeParams {
    float mu;      // g1-to-g2 amplification factor (dimensionless)
    float ex;      // exponent (dimensionless)
    float kg1_n;   // cathode current scale (normalized)
    float kp;      // perveance constant (dimensionless)
    float kvb_n;   // knee voltage squared (normalized: Kvb_real * V_TO_N^2)
};

// Pre-computed EL84 Koren parameters (normalized domain)
// kg1_n derivation: Ik_n = E1_n^ex / kg1_n, where kg1_n = Kg1_real * V_TO_N^ex / I_TO_N
// kvb_n: Kvb has units V^2, so kvb_n = Kvb_real * V_TO_N^2
inline KorenPentodeParams makeEL84Params() {
    return {
        19.5f,                                                 // mu
        1.35f,                                                 // ex
        820.0f * std::pow(VS::V_TO_N, 1.35f) / VS::I_TO_N,   // kg1_n
        84.0f,                                                 // kp
        300.0f * VS::V_TO_N * VS::V_TO_N,                     // kvb_n
    };
}

struct PentodeOutput {
    float plateCurrent  = 0.0f;
    float screenCurrent = 0.0f;
};

PentodeOutput pentodeWaveshaper(float vGrid_n, float vBias_n, float vPlate_n, float vScreen_n);
float softConductionCurve(float vGrid_n, float vBias_n);

} // namespace sr
