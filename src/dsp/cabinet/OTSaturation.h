#pragma once
// OT Core Saturation — flux-based model (Spec §3.9+)
//
// Faraday's law: Φ = ∫V dt / N
// Algorithm: integrate → saturate (tanh) → differentiate
// Low frequencies produce more flux per volt, so bass saturates first.

#include "../../utils/Constants.h"
#include <cmath>
#include <algorithm>

namespace sr {

struct OTSaturation {
    float flux        = 0.0f;
    float prevFluxSat = 0.0f;
    float drive       = 0.0f;
    float dcDecay     = 0.9999f;
    float inverseSR   = 1.0f / 44100.0f;
    float sampleRate  = 44100.0f;

    void recalcCoeffs(float effectiveSR) {
        sampleRate = effectiveSR;
        inverseSR  = 1.0f / effectiveSR;
        drive      = OT_SAT_DRIVE;
        // DC-block decay: exp(-2π * f_dc / sr) — very subtle HP on integrator
        dcDecay    = std::exp(-2.0f * PI_F * OT_SAT_DC_FREQ / effectiveSR);
    }

    void reset() {
        flux        = 0.0f;
        prevFluxSat = 0.0f;
    }

    float process(float input) {
        // Integrate: accumulate flux (Faraday's law)
        flux = flux * dcDecay + input * inverseSR;

        // Safety rail: a sustained DC-biased input (asymmetric tube DC) can drift
        // the integrator before the DC-block bleeds it out. Clamp + finite-guard so
        // the state can never reach Inf/NaN and poison the rest of the chain.
        if (!std::isfinite(flux)) flux = 0.0f;
        flux = std::clamp(flux, -OT_SAT_FLUX_MAX, OT_SAT_FLUX_MAX);

        // Saturate: tanh models core permeability collapse
        // Normalize by dividing by drive so small signals pass at unity
        float fluxSat = std::tanh(flux * drive) / drive;

        // Differentiate: convert back to voltage
        float output = (fluxSat - prevFluxSat) * sampleRate;
        prevFluxSat = fluxSat;

        return output;
    }
};

} // namespace sr
