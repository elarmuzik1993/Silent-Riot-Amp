#pragma once
// Multi-stage PSU droop (Spec §3.11)
#include "../../utils/VoltageScale.h"
#include "../../utils/Constants.h"
#include <cmath>
#include <algorithm>

namespace sr {

struct SupplyNode {
    float voltage, nominalVoltage, decay, tauMs;
    void init(float nominal_n, float tau_ms) {
        nominalVoltage = nominal_n; voltage = nominal_n; tauMs = tau_ms; decay = 0.999f;
    }
    void recalcDecay(float effectiveSR) {
        decay = std::exp(-1.0f / (tauMs * 0.001f * effectiveSR));
    }
    void track(float target_n) {
        voltage = target_n + (voltage - target_n) * decay;
        if (std::abs(voltage - target_n) < 1e-10f) voltage = target_n;
    }
};

struct PowerSupplyModel {
    SupplyNode powerNode, screenNode, piNode, preampNode;
    float preampCurrentAccum = 0.0f;
    float piCurrentAccum     = 0.0f;
    float powerCurrent       = 0.0f;
    float screenCurrent      = 0.0f;

    float sourceImpedance_n  = 0.0f;
    float rScreen_n          = 0.0f;
    float rDrop1_n           = 0.0f;
    float rDrop2_n           = 0.0f;

    void init();
    void recalcDecayCoeffs(float effectiveSR);
    void reset();   // restore node voltages to nominal, clear current draw
    void update();
    void resetCurrentDraw();
    void reportPreampLevel(float plateSwing);
    void reportPiLevel(float plateSwing);
    void reportPowerDirect(float totalPlate, float totalScreen);

    float vPower()  const { return powerNode.voltage; }
    float vScreen() const { return screenNode.voltage; }
    float vPi()     const { return piNode.voltage; }
    float vPreamp() const { return preampNode.voltage; }
};

} // namespace sr
