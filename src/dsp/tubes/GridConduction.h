// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
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
    // Last sample's drop across the source impedance (vGrid − vLoaded), a
    // normalized VOLTAGE — what the charge path integrates. For the grid
    // current divide by sourceResistance: gridCurrentN() (review S3).
    float lastGridDrop      = 0.0f;
    // Per-sample charge increment; rate-scaled with the decay by
    // recalcGridChargeDecay. The default is the GRID_CHARGE_REF_SR calibration.
    float chargeRate        = GRID_CHARGE_RATE;

    // Clear dynamic state (keeps configured resistances + charge decay/rate)
    void reset() { couplingCapCharge = 0.0f; lastGridDrop = 0.0f; }
};

// Grid current (normalized, I_n = V_n / R_n) for a drop across `m`'s source
// impedance. Readout only; the charge path stays on the voltage.
inline float gridCurrentN(float drop, const GridConductionModel& m) {
    return drop / m.sourceResistance;
}

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
