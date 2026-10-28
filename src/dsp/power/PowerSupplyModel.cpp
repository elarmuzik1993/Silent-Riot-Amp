// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "PowerSupplyModel.h"

namespace sr {

void PowerSupplyModel::init() {
    powerNode.init(V_POWER_NOMINAL_N,  TAU_POWER_MS);
    screenNode.init(V_SCREEN_NOMINAL_N, TAU_SCREEN_MS);
    piNode.init(V_PI_NOMINAL_N,        TAU_PI_MS);
    preampNode.init(V_PREAMP_NOMINAL_N, TAU_PREAMP_MS);

    sourceImpedance_n = SOURCE_IMPEDANCE_N;
    rScreen_n         = R_SCREEN_N;
    rDrop1_n          = R_DROP1_N;
    rDrop2_n          = R_DROP2_N;
}

void PowerSupplyModel::reset() {
    // Restore each node to its nominal voltage (keeps tau/decay coeffs intact)
    powerNode.voltage  = powerNode.nominalVoltage;
    screenNode.voltage = screenNode.nominalVoltage;
    piNode.voltage     = piNode.nominalVoltage;
    preampNode.voltage = preampNode.nominalVoltage;
    resetCurrentDraw();
}

void PowerSupplyModel::settle(float totalPlate, float totalScreen) {
    resetCurrentDraw();
    reportPowerDirect(totalPlate, totalScreen);
    // update()'s targets with each node already at its target
    powerNode.voltage  = std::max(0.0f, V_POWER_NOMINAL_N - (totalPlate + totalScreen) * sourceImpedance_n);
    const float powerDroop = powerNode.voltage - V_POWER_NOMINAL_N;
    screenNode.voltage = std::max(0.0f, V_SCREEN_NOMINAL_N + powerDroop - totalScreen * rScreen_n);
    piNode.voltage     = std::max(0.0f, V_PI_NOMINAL_N + powerDroop);
    const float piDroop = piNode.voltage - V_PI_NOMINAL_N;
    preampNode.voltage = std::max(0.0f, V_PREAMP_NOMINAL_N + piDroop);
}

void PowerSupplyModel::recalcDecayCoeffs(float effectiveSR) {
    powerNode.recalcDecay(effectiveSR);
    screenNode.recalcDecay(effectiveSR);
    piNode.recalcDecay(effectiveSR);
    preampNode.recalcDecay(effectiveSR);
}

void PowerSupplyModel::update() {
    // §3.11 Cascaded droop — impedance-based voltage drop
    float totalDraw = powerCurrent + screenCurrent + piCurrentAccum + preampCurrentAccum;

    // Rectifier sag: all current flows through source impedance
    float vRect = std::max(0.0f, V_POWER_NOMINAL_N - totalDraw * sourceImpedance_n);
    powerNode.track(vRect);

    // Power node deviation from nominal propagates to downstream nodes
    float powerDroop = powerNode.voltage - V_POWER_NOMINAL_N;

    // Screen: nominal + power droop share - own I×R drop
    screenNode.track(std::max(0.0f, V_SCREEN_NOMINAL_N + powerDroop - screenCurrent * rScreen_n));

    // PI: nominal + power droop share - own I×R drop
    piNode.track(std::max(0.0f, V_PI_NOMINAL_N + powerDroop - piCurrentAccum * rDrop1_n));

    // Preamp: nominal + PI droop share - own I×R drop
    float piDroop = piNode.voltage - V_PI_NOMINAL_N;
    preampNode.track(std::max(0.0f, V_PREAMP_NOMINAL_N + piDroop - preampCurrentAccum * rDrop2_n));
}

void PowerSupplyModel::resetCurrentDraw() {
    preampCurrentAccum = 0.0f;
    piCurrentAccum     = 0.0f;
    powerCurrent       = 0.0f;
    screenCurrent      = 0.0f;
}

void PowerSupplyModel::reportPreampLevel(float plateSwing) {
    preampCurrentAccum += std::abs(plateSwing) / VS::R_100K;
}

void PowerSupplyModel::reportPiLevel(float plateSwing) {
    piCurrentAccum += std::abs(plateSwing) / VS::R_100K;
}

void PowerSupplyModel::reportPowerDirect(float totalPlate, float totalScreen) {
    powerCurrent = totalPlate;
    screenCurrent = totalScreen;
}

} // namespace sr
