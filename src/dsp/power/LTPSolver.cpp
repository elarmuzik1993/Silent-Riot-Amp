#include "LTPSolver.h"
#include "../tubes/TriodeWaveshaper.h"

namespace sr {

static const KorenTriodeParams ecc83 = makeECC83Params();

LTPOutput solveLTP(float vInput_n, float vSupply_n, LTPModel& ltp) {
    float vGridA = ltp.vBiasRef + vInput_n;
    float vGridB = ltp.vBiasRef;

    // Damped iteration: solve for cathode voltage and plate voltages
    // simultaneously using the Koren triode model.
    // Cached plate voltages from previous sample give excellent initial guess.
    constexpr float alpha = 0.25f;
    constexpr int iterations = 4;
    float defaultVp = vSupply_n * 0.6f;

    float vPlateA = (ltp.vPlateA >= 0.0f) ? ltp.vPlateA : defaultVp;
    float vPlateB = (ltp.vPlateB >= 0.0f) ? ltp.vPlateB : defaultVp;

    for (int i = 0; i < iterations; ++i) {
        float vgkA = vGridA - ltp.vCathode;
        float vgkB = vGridB - ltp.vCathode;

        float ipA = korenTriode(vgkA, vPlateA, ecc83);
        float ipB = korenTriode(vgkB, vPlateB, ecc83);

        vPlateA = std::clamp(vSupply_n - ipA * ltp.rPlateA_n, 0.0f, vSupply_n);
        vPlateB = std::clamp(vSupply_n - ipB * ltp.rPlateB_n, 0.0f, vSupply_n);

        float iTail = ipA + ipB;
        float vCathodeNew = iTail * ltp.tailR_n;
        ltp.vCathode = ltp.vCathode + alpha * (vCathodeNew - ltp.vCathode);
    }

    // Final currents at converged state
    float vgkA = vGridA - ltp.vCathode;
    float vgkB = vGridB - ltp.vCathode;
    float ipA = korenTriode(vgkA, vPlateA, ecc83);
    float ipB = korenTriode(vgkB, vPlateB, ecc83);

    vPlateA = std::clamp(vSupply_n - ipA * ltp.rPlateA_n, ltp.vCathode, vSupply_n);
    vPlateB = std::clamp(vSupply_n - ipB * ltp.rPlateB_n, ltp.vCathode, vSupply_n);

    ltp.vPlateA = vPlateA;
    ltp.vPlateB = vPlateB;

    return { vPlateA, vPlateB, ltp.vCathode };
}

} // namespace sr
