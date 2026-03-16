#include "TriodeWaveshaper.h"

namespace tt {

float triodeWaveshaper(float vGrid_n, float vBias_n, float vSupply_n, float rPlate_n) {
    float vgk = vGrid_n + vBias_n;
    float ip = triodeCurrentModel(vgk, TRIODE_MU, TRIODE_RP_N);
    float vPlate = vSupply_n - ip * rPlate_n;
    return std::clamp(vPlate, 0.0f, vSupply_n);
}

float triodeCurrentModel(float vgk_n, float, float) {
    // Child-Langmuir 3/2 power law with cutoff offset
    // vgk_n is grid-to-cathode voltage (normalized, typically negative)
    // Tube conducts when vgk > cutoff (-0.01 normalized = -3V real)
    // At operating point: vgk = -0.005, distance from cutoff = 0.005
    float vAboveCutoff = vgk_n - V_CUTOFF_12AX7_N;  // offset from cutoff
    float v2 = vAboveCutoff * vAboveCutoff + VS::V_THERMAL * VS::V_THERMAL;
    float softVgk = 0.5f * (vAboveCutoff + std::sqrt(v2));  // smooth rectifier
    float ip = K_12AX7 * softVgk * std::sqrt(std::abs(softVgk));  // 3/2 power law
    return std::max(0.0f, ip);
}

} // namespace tt
