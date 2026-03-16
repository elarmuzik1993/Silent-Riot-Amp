#include "TriodeWaveshaper.h"

namespace tt {

static const KorenTriodeParams ecc83 = makeECC83Params();

float korenTriode(float vgk_n, float vPlate_n, const KorenTriodeParams& p) {
    float vpSafe = std::max(vPlate_n, 1e-6f);
    float arg = p.kp * (1.0f / p.mu + vgk_n
                        / std::sqrt(p.kvb_n + vpSafe * vpSafe));
    // Numerically stable softplus: log(1 + exp(x)) ≈ x for x >> 1
    float logTerm = arg > 20.0f ? arg : std::log(1.0f + std::exp(arg));
    float e1 = (vpSafe / p.kp) * logTerm;
    return std::pow(std::max(e1, 0.0f), p.ex) / p.kg_n;
}

float triodeWaveshaper(float vGrid_n, float vBias_n, float vSupply_n, float rPlate_n, TriodeState& state) {
    float vgk = vGrid_n + vBias_n;

    // Iterative load-line solve: Vp = Vsupply - Ip(Vgk, Vp) * Rplate
    // Use cached plate voltage from previous sample as initial guess
    // (plate voltage changes slowly → typically <0.1% off, converges in 3-4 iters)
    float vPlate = (state.vPlate >= 0.0f) ? state.vPlate : vSupply_n * 0.6f;

    for (int i = 0; i < 4; ++i) {
        float ip = korenTriode(vgk, vPlate, ecc83);
        float vPlateNew = vSupply_n - ip * rPlate_n;
        vPlateNew = std::clamp(vPlateNew, 0.0f, vSupply_n);
        vPlate += 0.5f * (vPlateNew - vPlate);
    }

    state.vPlate = vPlate;
    return vPlate;
}

} // namespace tt
