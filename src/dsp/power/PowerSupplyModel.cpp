#include "PowerSupplyModel.h"

namespace tt {

void PowerSupplyModel::init() {
    powerNode.init(V_POWER_NOMINAL_N,  TAU_POWER_MS);
    screenNode.init(V_SCREEN_NOMINAL_N, TAU_SCREEN_MS);
    piNode.init(V_PI_NOMINAL_N,        TAU_PI_MS);
    preampNode.init(V_PREAMP_NOMINAL_N, TAU_PREAMP_MS);
}

void PowerSupplyModel::recalcDecayCoeffs(float effectiveSR) {
    powerNode.recalcDecay(effectiveSR);
    screenNode.recalcDecay(effectiveSR);
    piNode.recalcDecay(effectiveSR);
    preampNode.recalcDecay(effectiveSR);
}

void PowerSupplyModel::update() {
    float powerTarget  = V_POWER_NOMINAL_N;
    float screenTarget = V_SCREEN_NOMINAL_N;
    float piTarget     = V_PI_NOMINAL_N;
    float preampTarget = V_PREAMP_NOMINAL_N;
    // TODO: compute droop from current reports
    powerNode.track(powerTarget);
    screenNode.track(screenTarget);
    piNode.track(piTarget);
    preampNode.track(preampTarget);
}

void PowerSupplyModel::resetCurrentDraw() {
    preampCurrentAccum = 0.0f;
    piCurrentAccum = 0.0f;
}

void PowerSupplyModel::reportPreampLevel(float plateSwing) {
    preampCurrentAccum += std::abs(plateSwing) / VS::R_100K;
}

void PowerSupplyModel::reportPiLevel(float plateSwing) {
    piCurrentAccum += std::abs(plateSwing) / VS::R_100K;
}

void PowerSupplyModel::reportPowerDirect(float totalPlate, float totalScreen) {
    (void)totalPlate; (void)totalScreen; // TODO: use for droop calculation
}

} // namespace tt
