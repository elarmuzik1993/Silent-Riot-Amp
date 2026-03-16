#include "PentodeWaveshaper.h"

namespace tt {

PentodeOutput pentodeWaveshaper(float vGrid_n, float vBias_n, float vPlate_n, float vScreen_n) {
    // Spec §4.2: vg1 = vGrid + vBias (bias is negative, so this subtracts correctly)
    float vg1 = vGrid_n + vBias_n;

    // Screen contribution to cathode current (mu_g2 ≈ 12 for EL84)
    float screenInfluence = vScreen_n / MU_SCREEN;

    // Effective drive: grid voltage boosted by screen contribution
    float vEffective = vg1 + screenInfluence;

    // Screen voltage scales the gain coefficient
    float screenGainFactor = vScreen_n / V_SCREEN_NOMINAL_N;
    float driveScaled = vEffective * screenGainFactor;

    // Core nonlinearity — power law with cutoff
    float ip;
    if (driveScaled > 0.0f)
        ip = std::pow(driveScaled, EL84_EXPONENT) / EL84_KG;
    else
        ip = 0.0f;

    // Soft-clamp to EL84 max plate current (tanh knee avoids harsh flat-top)
    ip = EL84_IP_MAX_N * std::tanh(ip / EL84_IP_MAX_N);

    // Plate saturation: spec uses (1 - exp(-Vp/Vp_knee)), not tanh
    float plateLimit = 1.0f - std::exp(-vPlate_n / V_PLATE_KNEE_N);
    ip *= plateLimit;

    // Screen current redistribution
    float screenRatio = SCREEN_CURRENT_RATIO + (1.0f - plateLimit) * SCREEN_REDIST_FACTOR;
    float iScreen = ip * screenRatio;

    return { ip, iScreen };
}

float softConductionCurve(float vGrid_n, float vBias_n) {
    // Spec §3.8: smooth Class A/AB crossover per EL84
    //
    // cutoffMargin = how far above cutoff the grid sits
    //   At idle (vGrid=0): margin = 0 - (-0.033) = +0.033 (conducting)
    //   At deep negative swing: margin goes negative (approaching cutoff)
    //
    // overlapWidth controls the transition steepness:
    //   15W (bias=-0.9): narrower overlap → more AB (some crossover)
    //   7W  (bias=-0.96): wider overlap → more Class A (no crossover)
    //
    // The width is scaled by |EL84_CUTOFF_N| (the cutoff distance)
    // and modulated by the bias ratio so 7W is wider than 15W.
    // bias_factor: -0.9/(-0.9) = 1.0 for 15W, -0.96/(-0.9) = 1.067 for 7W
    float cutoffMargin = vGrid_n - EL84_CUTOFF_N;
    float biasFactor = PENTODE_BIAS_15W_N / vBias_n;  // >1.0 for 7W (hotter)
    float overlapWidth = std::abs(EL84_CUTOFF_N) * 0.5f * biasFactor;
    if (overlapWidth < 1e-10f) overlapWidth = 1e-10f;
    return 0.5f * (1.0f + std::tanh(cutoffMargin / overlapWidth));
}

} // namespace tt
