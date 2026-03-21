#include "SpeakerCoupling.h"

namespace tt {

void SpeakerCoupling::init() {}

void SpeakerCoupling::recalcFilters(float effectiveSR) {
    xoverLow.setFreq(SPK_XOVER_LOW, effectiveSR);
    xoverHigh.setFreq(SPK_XOVER_HIGH, effectiveSR);
    envDecay = std::exp(-1.0f / (SPK_ENV_TAU_MS * 0.001f * effectiveSR));
}

void SpeakerCoupling::reset() {
    xoverLow.reset(); xoverHigh.reset();
    bassEnv = midEnv = hfEnv = 0.0f;
}

float SpeakerCoupling::process(float powerOut) {
    // Complementary 3-way crossover: bass + mid + hf = powerOut (always)
    float lp1  = xoverLow.process(powerOut);
    float lp2  = xoverHigh.process(powerOut);
    float bass = lp1;
    float mid  = lp2 - lp1;
    float hf   = powerOut - lp2;

    bassEnv = std::max(std::abs(bass), bassEnv * envDecay);
    midEnv  = std::max(std::abs(mid),  midEnv  * envDecay);
    hfEnv   = std::max(std::abs(hf),   hfEnv   * envDecay);

    float envTotal = bassEnv + midEnv + hfEnv;
    if (envTotal < 1e-10f) return powerOut;

    float zReflected = (bassEnv / envTotal) * Z_REFLECTED_BASS_N
                     + (midEnv  / envTotal) * Z_REFLECTED_MID_N
                     + (hfEnv   / envTotal) * Z_REFLECTED_HF_N;

    float rawMod = std::tanh((zReflected / OT_NOMINAL_LOAD_N - 1.0f) * SPK_GAINMOD_DEPTH) * SPK_GAINMOD_RANGE;
    float modBlend = std::min(envTotal / SPK_MOD_THRESHOLD, 1.0f);
    float gainMod = 1.0f + rawMod * modBlend;
    return powerOut * gainMod;
}

} // namespace tt
