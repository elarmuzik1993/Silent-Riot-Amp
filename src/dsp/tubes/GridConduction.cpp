#include "GridConduction.h"

namespace tt {

float applyGridConduction(float vGrid, GridConductionModel& model) {
    float vAbove = vGrid - model.conductionThreshold;

    if (vAbove <= 0.0f) {
        model.couplingCapCharge *= model.chargeDecay;
        if (std::abs(model.couplingCapCharge) < 1e-10f)
            model.couplingCapCharge = 0.0f;
        model.lastGridCurrent = 0.0f;
        return vGrid - model.couplingCapCharge;
    }

    // Grid conducts — impedance loading
    float conductionDepth = std::tanh(vAbove * 3.0f);
    float effectiveGridR  = model.gridResistance / (conductionDepth + 1e-10f);
    float loadingFactor   = effectiveGridR / (effectiveGridR + model.sourceResistance);

    float vLoaded = model.conductionThreshold + vAbove * loadingFactor;

    // Coupling cap charge (bias shift)
    float gridCurrent = vAbove * (1.0f - loadingFactor);
    model.lastGridCurrent = gridCurrent;
    model.couplingCapCharge += gridCurrent * GRID_CHARGE_RATE;
    float maxCharge = std::abs(MAX_BIAS_SHIFT_N);
    if (model.couplingCapCharge > maxCharge)
        model.couplingCapCharge = maxCharge;
    model.couplingCapCharge *= model.chargeDecay;
    if (std::abs(model.couplingCapCharge) < 1e-10f)
        model.couplingCapCharge = 0.0f;

    return vLoaded - model.couplingCapCharge;
}

GridConductionModel makeV1aGrid()  { return { GRID_CONDUCT_THRESH_N,      GRID_R_TRIODE_N,  VS::r(100.0f),  0.0f, 0.999f }; }
GridConductionModel makeV1bGrid()  { return { GRID_CONDUCT_THRESH_N,      GRID_R_TRIODE_N,  VS::Z_OUT_V1A,  0.0f, 0.999f }; }
GridConductionModel makePIGrid()   { return { GRID_CONDUCT_THRESH_N,      GRID_R_TRIODE_N,  VS::Z_OUT_V1B,  0.0f, 0.999f }; }
GridConductionModel makeEL84Grid() { return { GRID_CONDUCT_THRESH_EL84_N, GRID_R_PENTODE_N, VS::Z_OUT_PI,   0.0f, 0.999f }; }

void recalcGridChargeDecay(GridConductionModel& m, float tauSec, float effectiveSR) {
    m.chargeDecay = std::exp(-1.0f / (tauSec * effectiveSR));
}

} // namespace tt
