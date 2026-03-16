#pragma once
// Triode Waveshaper — 12AX7 (Spec §4.1)
#include "../../utils/VoltageScale.h"
#include "../../utils/Constants.h"
#include <cmath>
#include <algorithm>

namespace tt {

float triodeWaveshaper(float vGrid_n, float vBias_n, float vSupply_n, float rPlate_n);
float triodeCurrentModel(float vgk_n, float mu, float rp_n);

inline float audioTaper(float x) { return x * x; }

} // namespace tt
