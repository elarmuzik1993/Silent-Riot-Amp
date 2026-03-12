#pragma once
// Grid Conduction Model (Spec §4.1.1) — MVP model
// Dynamic input impedance + coupling cap bias shift
#include "../../utils/VoltageScale.h"
#include "../../utils/Constants.h"
#include <cmath>

namespace tt {

struct GridConductionModel {
    float conductionThreshold;
    float gridResistance;
    float sourceResistance;
    float couplingCapCharge = 0.0f;
    float chargeDecay       = 0.999f;
    float lastGridCurrent   = 0.0f;  // for GUI readout
};

float applyGridConduction(float vGrid, GridConductionModel& model);

// Factory functions for each stage (Spec §4.1.1 per-stage instances)
GridConductionModel makeV1aGrid();
GridConductionModel makeV1bGrid();
GridConductionModel makePIGrid();
GridConductionModel makeEL84Grid();

void recalcGridChargeDecay(GridConductionModel& m, float tauSec, float effectiveSR);

} // namespace tt
