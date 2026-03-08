#include "TriodeWaveshaper.h"

namespace tt {

float triodeWaveshaper(float vGrid_n, float vBias_n, float vSupply_n, float stageGain) {
    // Spec §4.1 Option A: x = vGrid_n * drive + vBias_n
    // stageGain acts as the drive multiplier (replaces generic TRIODE_DRIVE)
    float x = vGrid_n * stageGain + vBias_n;

    // Asymmetric clipping: softer positive (cutoff), harder negative (conduction)
    float shaped;
    if (x >= 0.0f)
        shaped = std::tanh(x);
    else
        shaped = std::tanh(x * TRIODE_ASYM_FACTOR) / TRIODE_ASYM_FACTOR;

    // Output swing proportional to available supply
    shaped *= vSupply_n;

    // Clip against supply ceiling
    float ceiling = vSupply_n * HEADROOM_FACTOR;
    return std::clamp(shaped, -ceiling, ceiling);
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
