#pragma once
// Long-Tail Pair Phase Inverter — Nodal Solver (Spec §3.5)
#include "../../utils/VoltageScale.h"
#include "../../utils/Constants.h"
#include "../tubes/TriodeWaveshaper.h"
#include <algorithm>

namespace tt {

struct LTPModel {
    float tailR_n, rPlateA_n, rPlateB_n, vBiasRef, vCathode, mu, rp;
    void init() {
        tailR_n = PI_TAIL_R_N; rPlateA_n = PI_RPLATE_N; rPlateB_n = PI_RPLATE_N;
        vBiasRef = PI_BIAS_REF_N; vCathode = PI_CATHODE_INIT_N;
        mu = PI_MU; rp = PI_RP_N;
    }
};

struct LTPOutput {
    float piA, piB, vCathode;
};

LTPOutput solveLTP(float vInput_n, float vSupply_n, LTPModel& ltp);

} // namespace tt
