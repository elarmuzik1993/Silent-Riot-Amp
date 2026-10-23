#pragma once
// Grid Conduction Model (Spec §4.1.1) — MVP model
// Dynamic input impedance + coupling cap bias shift
#include "../../utils/VoltageScale.h"
#include "../../utils/Constants.h"
#include <cmath>
#include <algorithm>

namespace sr {

struct GridConductionModel {
    float conductionThreshold;
    float gridResistance;
    float sourceResistance;
    float couplingCapCharge = 0.0f;
    float chargeDecay       = 0.999f;
    float lastGridCurrent   = 0.0f;  // for GUI readout
    // Per-sample charge increment; rate-scaled with the decay by
    // recalcGridChargeDecay. The default is the GRID_CHARGE_REF_SR calibration.
    float chargeRate        = GRID_CHARGE_RATE;

    // Clear dynamic state (keeps configured resistances + charge decay/rate)
    void reset() { couplingCapCharge = 0.0f; lastGridCurrent = 0.0f; }
};

float applyGridConduction(float vGrid, GridConductionModel& model);

// Factory functions for each stage (Spec §4.1.1 per-stage instances)
GridConductionModel makeV1aGrid();
GridConductionModel makeV1bGrid();
GridConductionModel makePIGrid();
GridConductionModel makeEL84Grid();

// Sets both rate-dependent terms: the charge decay (time constant tauSec) and
// the charge increment (GRID_CHARGE_RATE scaled to effectiveSR).
void recalcGridChargeDecay(GridConductionModel& m, float tauSec, float effectiveSR);

} // namespace sr
