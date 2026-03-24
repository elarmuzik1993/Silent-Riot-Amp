#pragma once
// Long-Tail Pair Phase Inverter — Nodal Solver (Spec §3.5)
#include "../../utils/VoltageScale.h"
#include "../../utils/Constants.h"
#include <algorithm>

namespace sr {

struct LTPModel {
    float tailR_n, rPlateA_n, rPlateB_n, vBiasRef, vCathode;
    float vPlateA = -1.0f, vPlateB = -1.0f;  // cached plate voltages (<0 = uninitialized)
    void init() {
        tailR_n = PI_TAIL_R_N; rPlateA_n = PI_RPLATE_A_N; rPlateB_n = PI_RPLATE_B_N;
        vBiasRef = PI_BIAS_REF_N; vCathode = PI_CATHODE_INIT_N;
        vPlateA = vPlateB = -1.0f;
    }
};

struct LTPOutput {
    float piA, piB, vCathode;
};

LTPOutput solveLTP(float vInput_n, float vSupply_n, LTPModel& ltp);

} // namespace sr
