// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
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
        19.5f,                                                    // mu
        1.35f,                                                    // ex
        EL84_KG1 * std::pow(VS::V_TO_N, 1.35f) / VS::I_TO_N,     // kg1_n
        84.0f,                                                    // kp
        300.0f * VS::V_TO_N * VS::V_TO_N,                        // kvb_n
    };
}

struct PentodeOutput {
    float plateCurrent  = 0.0f;
    float screenCurrent = 0.0f;
};

PentodeOutput pentodeWaveshaper(float vGrid_n, float vBias_n, float vPlate_n, float vScreen_n);
float softConductionCurve(float vGrid_n, float vBias_n);

// Total idle cathode current for the EL84 PAIR at a given bias (both grids at
// 0 V signal, supplies at nominal). This is the reference the sag envelope must
// subtract — hardcoding it desynchronises sag from the tube model: if the
// constant sits above the model's real idle current, biasOffset() goes POSITIVE
// and pins at the warm clamp, biasing the amp permanently hot. A cathode-biased
// stage can only ever go colder than idle, so biasOffset() must be <= 0.
// Depends on bias, so 15W and 7W have different quiescent points.
float pentodeQuiescentTotal(float vBias_n, float vPlate_n, float vScreen_n);

} // namespace sr
