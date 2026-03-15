#include "LTPSolver.h"

namespace tt {

LTPOutput solveLTP(float vInput_n, float vSupply_n, LTPModel& ltp) {
    float vGridA = ltp.vBiasRef + vInput_n;
    float vGridB = ltp.vBiasRef;

    // Damped iteration: the triode current model has high gain in the
    // normalized domain, so direct cathode update oscillates.
    // Use alpha=0.25 blending for stable convergence.
    constexpr float alpha = 0.25f;
    constexpr int iterations = 8;

    for (int i = 0; i < iterations; ++i) {
        float vgkA = vGridA - ltp.vCathode;
        float vgkB = vGridB - ltp.vCathode;

        float ipA = triodeCurrentModel(vgkA, ltp.mu, ltp.rp);
        float ipB = triodeCurrentModel(vgkB, ltp.mu, ltp.rp);

        float iTail = ipA + ipB;
        float vCathodeNew = iTail * ltp.tailR_n;
        ltp.vCathode = ltp.vCathode + alpha * (vCathodeNew - ltp.vCathode);
    }

    // Final currents at converged cathode
    float vgkA = vGridA - ltp.vCathode;
    float vgkB = vGridB - ltp.vCathode;
    float ipA = triodeCurrentModel(vgkA, ltp.mu, ltp.rp);
    float ipB = triodeCurrentModel(vgkB, ltp.mu, ltp.rp);

    float vPlateA = vSupply_n - ipA * ltp.rPlateA_n;
    float vPlateB = vSupply_n - ipB * ltp.rPlateB_n;
    vPlateA = std::clamp(vPlateA, ltp.vCathode, vSupply_n);
    vPlateB = std::clamp(vPlateB, ltp.vCathode, vSupply_n);

    // Return raw plate voltages — DC blocking is handled by pi_couplingHP filters
    // (the old 10%-per-sample dcPlateRef tracking was acting as an 18kHz HP,
    // stripping out fundamentals instead of just removing DC)
    return { vPlateA, vPlateB, ltp.vCathode, 0.0f };
}

} // namespace tt
