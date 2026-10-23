#include "GridConduction.h"

namespace sr {

float applyGridConduction(float vGrid, GridConductionModel& model) {
    float vAbove = vGrid - model.conductionThreshold;

    if (vAbove <= 0.0f) {
        model.couplingCapCharge *= model.chargeDecay;
        if (std::abs(model.couplingCapCharge) < 1e-10f)
            model.couplingCapCharge = 0.0f;
        model.lastGridDrop = 0.0f;
        return vGrid - model.couplingCapCharge;
    }

    // ── Grid conducts: DIODE clamp, not a resistive divider ──────────────────
    //
    // The grid-cathode junction is a diode. Once the grid swings positive its
    // current rises exponentially, so the driving stage's source impedance pins
    // the grid a couple of volts above the conduction threshold no matter how
    // hard it is driven. Inverting an exponential I-V gives a LOGARITHMIC law.
    //
    // The old model was a fixed resistive divider,
    //     vLoaded = threshold + vAbove * Rg/(Rg+Rs)
    // which can only ever ATTENUATE — it never clamps. At the PI that ratio is
    // 0.23, so a ~520 V swing still delivered a 121 V grid on a 12AX7 biased at
    // -1.5 V. That is what left the whole power section overdriven at every knob
    // position and flattened the amp's dynamics.
    //
    // log1p is transparent below the knee (slope 1 at vAbove = 0, so this is C1
    // continuous with the non-conducting branch above) and grows only as log()
    // above it. The knee scales with the stage's static loading ratio, so a
    // low-impedance source (V1a: 100R) clamps loosely and a high-impedance one
    // (PI: 47K) clamps hard — the same per-stage character the divider had.
    const float loadingFactor = model.gridResistance
                              / (model.gridResistance + model.sourceResistance);
    const float knee = std::max(GRID_CLAMP_KNEE_N * loadingFactor, 1.0e-9f);

    float vLoaded = model.conductionThreshold + knee * std::log1p(vAbove / knee);

    // Coupling cap charge (bias shift). Driven by what the source impedance has
    // to absorb — the same quantity the divider fed it, so GRID_CHARGE_RATE and
    // MAX_BIAS_SHIFT_N keep their calibrated meaning. chargeRate is
    // GRID_CHARGE_RATE scaled to the running sample rate, so the charge per
    // second — not per sample — is fixed. The drop is a voltage, not a current
    // (that is drop / sourceResistance; see gridCurrentN()).
    float vDrop = vGrid - vLoaded;
    model.lastGridDrop = vDrop;
    model.couplingCapCharge += vDrop * model.chargeRate;
    float maxCharge = std::abs(MAX_BIAS_SHIFT_N);
    if (model.couplingCapCharge > maxCharge)
        model.couplingCapCharge = maxCharge;
    model.couplingCapCharge *= model.chargeDecay;
    if (std::abs(model.couplingCapCharge) < 1e-10f)
        model.couplingCapCharge = 0.0f;

    return vLoaded - model.couplingCapCharge;
}

GridConductionModel makeV1aGrid()  { return { GRID_CONDUCT_THRESH_N,      GRID_R_V1A_N,   VS::r(100.0f),           0.0f, 0.999f }; }
GridConductionModel makeV1bGrid()  { return { GRID_CONDUCT_THRESH_N,      GRID_R_V1B_N,   GRID_SOURCE_R_V1B_N,     0.0f, 0.999f }; }
GridConductionModel makePIGrid()   { return { GRID_CONDUCT_THRESH_N,      GRID_R_PI_N,    GRID_SOURCE_R_PI_N,      0.0f, 0.999f }; }
GridConductionModel makeEL84Grid() { return { GRID_CONDUCT_THRESH_EL84_N, GRID_R_EL84_N,  GRID_SOURCE_R_EL84_N,    0.0f, 0.999f }; }

void recalcGridChargeDecay(GridConductionModel& m, float tauSec, float effectiveSR) {
    m.chargeDecay = std::exp(-1.0f / (tauSec * effectiveSR));
    // Ratio first: at the reference rate it is exactly 1.0f, so 44.1 kHz / 4×
    // keeps GRID_CHARGE_RATE bit for bit.
    m.chargeRate  = GRID_CHARGE_RATE * (GRID_CHARGE_REF_SR / effectiveSR);
}

} // namespace sr
