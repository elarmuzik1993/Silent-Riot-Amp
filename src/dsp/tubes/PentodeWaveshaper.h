#pragma once
// EL84 Pentode Model (Spec §4.2) — screen-aware, push-pull
#include "../../utils/VoltageScale.h"
#include "../../utils/Constants.h"
#include <cmath>
#include <algorithm>

namespace tt {

struct PentodeOutput {
    float plateCurrent  = 0.0f;
    float screenCurrent = 0.0f;
};

PentodeOutput pentodeWaveshaper(float vGrid_n, float vBias_n, float vPlate_n, float vScreen_n);
float softConductionCurve(float vGrid_n, float vBias_n);

} // namespace tt
