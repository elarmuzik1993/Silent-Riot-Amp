#include "PentodeWaveshaper.h"

namespace sr {

static const KorenPentodeParams el84 = makeEL84Params();

// Koren pentode cathode current — screen grid as virtual anode
// Mirrors korenTriode() from TriodeWaveshaper.cpp but with Vg2 replacing Vp
static float korenPentodeCurrent(float vg1_n, float vScreen_n, const KorenPentodeParams& p) {
    float vg2Safe = std::max(vScreen_n, 1e-6f);
    float arg = p.kp * (1.0f / p.mu + vg1_n
                        / std::sqrt(p.kvb_n + vg2Safe * vg2Safe));
    // Numerically stable softplus: log(1 + exp(x)) ≈ x for x >> 1
    float logTerm = arg > 20.0f ? arg : std::log(1.0f + std::exp(arg));
    float e1 = (vg2Safe / p.kp) * logTerm;
    return std::pow(std::max(e1, 0.0f), p.ex) / p.kg1_n;
}

PentodeOutput pentodeWaveshaper(float vGrid_n, float vBias_n, float vPlate_n, float vScreen_n) {
    // Spec §4.2: vg1 = vGrid + vBias (bias is negative, so this subtracts correctly)
    float vg1 = vGrid_n + vBias_n;

    // Core nonlinearity — Koren pentode with screen as virtual anode
    float ip = korenPentodeCurrent(vg1, vScreen_n, el84);

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

float pentodeQuiescentTotal(float vBias_n, float vPlate_n, float vScreen_n) {
    // Both tubes idle at the same operating point, so the shared cathode
    // resistor sees 2x a single tube's plate current.
    return 2.0f * pentodeWaveshaper(0.0f, vBias_n, vPlate_n, vScreen_n).plateCurrent;
}

float softConductionCurve(float vGrid_n, float vBias_n) {
    // Spec §3.8: smooth Class A/AB crossover per EL84
    //
    // cutoffMargin = how far above cutoff the grid sits. vGrid_n is the grid
    // SIGNAL, before bias, so the gate sits at signal -10 V (real grid ≈ -19.6 V
    // at 15W, -17 V at 7W), not at the Koren cutoff.
    //   At idle (vGrid=0): margin = 0 - (-0.033) = +0.033 (conducting)
    //   At deep negative swing: margin goes negative (approaching cutoff)
    //
    // overlapWidth controls the transition steepness:
    //   15W (bias=-0.032, -9.6 V): narrower overlap → more AB (some crossover)
    //   7W  (bias=-0.023, -7.0 V): wider overlap → more Class A (no crossover)
    //
    // The width is |EL84_CUTOFF_N| × 0.5 (5 V), scaled by the bias ratio so
    // 7W is wider than 15W: biasFactor = -9.6/-7.0 ≈ 1.37 for 7W, 1.0 for 15W,
    // giving 5 V (15W) / 6.9 V (7W). Idle value: ≈ 0.98 (15W) / 0.95 (7W).
    // vBias_n is the effective bias, so sag shifts biasFactor slightly.
    float cutoffMargin = vGrid_n - EL84_CUTOFF_N;
    float biasFactor = PENTODE_BIAS_15W_N / vBias_n;  // >1.0 for 7W (hotter)
    float overlapWidth = std::abs(EL84_CUTOFF_N) * 0.5f * biasFactor;
    if (overlapWidth < 1e-10f) overlapWidth = 1e-10f;
    return 0.5f * (1.0f + std::tanh(cutoffMargin / overlapWidth));
}

} // namespace sr
