#include "SpeakerCoupling.h"

namespace tt {

void SpeakerCoupling::init() {}

void SpeakerCoupling::recalcFilters(float effectiveSR) {
    bassExtract.makePeakEQ(SPK_FS, 2.0f, 0.0f, effectiveSR);
    midExtract.makePeakEQ(500.0f, 1.0f, 0.0f, effectiveSR);
    hfEnvelope.setFreq(3000.0f, effectiveSR);
    envDecay = std::exp(-1.0f / (SPK_ENV_TAU_MS * 0.001f * effectiveSR));
}

void SpeakerCoupling::reset() {
    bassExtract.reset(); midExtract.reset(); hfEnvelope.reset();
    bassEnv = midEnv = hfEnv = 0.0f;
}

float SpeakerCoupling::process(float powerOut) {
    float bass = bassExtract.process(powerOut);
    float mid  = midExtract.process(powerOut);
    float hf   = powerOut - hfEnvelope.process(powerOut);

    bassEnv = std::max(std::abs(bass), bassEnv * envDecay);
    midEnv  = std::max(std::abs(mid),  midEnv  * envDecay);
    hfEnv   = std::max(std::abs(hf),   hfEnv   * envDecay);

    float total = bassEnv + midEnv + hfEnv + 1e-10f;
    float zReflected = (bassEnv / total) * Z_REFLECTED_BASS_N
                     + (midEnv  / total) * Z_REFLECTED_MID_N
                     + (hfEnv   / total) * Z_REFLECTED_HF_N;

    float gainMod = 1.0f + std::tanh((zReflected / OT_NOMINAL_LOAD_N - 1.0f) * SPK_GAINMOD_DEPTH) * SPK_GAINMOD_RANGE;
    return powerOut * gainMod;
}

} // namespace tt
