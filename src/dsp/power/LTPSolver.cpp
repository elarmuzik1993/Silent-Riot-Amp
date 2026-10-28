// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "LTPSolver.h"
#include "../tubes/TriodeWaveshaper.h"
#include <cmath>

namespace sr {

static const KorenTriodeParams ecc83 = makeECC83Params();

// Plate voltage can't fall below the cathode — but a hard std::clamp there is a
// derivative discontinuity, and at high drive it engaged on well over half of all
// samples, generating harsh (and alias-prone) harmonics. A real triode compresses
// smoothly as the plate approaches the cathode, so bound it with a softplus knee:
// identity well above the bound, asymptotic to it below, C-infinity throughout.
// Used identically inside the iteration and for the final result — previously the
// loop bounded at 0.0 while the final result bounded at vCathode, so the solver
// converged against one constraint and the output was clipped to a tighter one.
static inline float softLowerBound(float x, float lo, float knee) {
    const float d = x - lo;
    if (d > 8.0f * knee) return x;   // untouched in normal operation
    const float z = d / knee;
    return lo + knee * ((z > 20.0f) ? z : std::log1p(std::exp(z)));
}

static inline float boundPlate(float raw, float vCathode, float vSupply_n) {
    return std::min(softLowerBound(raw, vCathode, PI_PLATE_KNEE_N), vSupply_n);
}

// Plate current for ONE triode at a fixed grid-cathode voltage, solving its own
// load line Vp = Vsupply - Ip(Vgk, Vp) * Rp. Damped at 0.5 — the same scheme
// triodeWaveshaper() uses, which is stable because an undamped step on a 100K
// plate load has |dVp/dVp| > 1 and oscillates. Warm-started from `vPlateSeed`,
// which must be the SAME seed for every evaluation within one sample: the outer
// root-find needs f() to be a pure function of vCathode, and threading a mutating
// plate estimate through it would make f() path-dependent and stall convergence.
static inline float plateCurrentAt(float vgk, float vPlateSeed, float rPlate_n,
                                   float vSupply_n, float vCathode, float& vPlateOut) {
    constexpr int innerIterations = 4;
    float vP = vPlateSeed;
    for (int i = 0; i < innerIterations; ++i) {
        const float ip = korenTriode(vgk, vP, ecc83);
        vP += 0.5f * (boundPlate(vSupply_n - ip * rPlate_n, vCathode, vSupply_n) - vP);
    }
    vPlateOut = vP;
    return korenTriode(vgk, vP, ecc83);
}

LTPOutput solveLTP(float vInput_n, float vSupply_n, LTPModel& ltp) {
    const float vGridA = ltp.vBiasRef + vInput_n;
    const float vGridB = ltp.vBiasRef;

    const float defaultVp = vSupply_n * 0.6f;
    const float seedA = (ltp.vPlateA >= 0.0f) ? ltp.vPlateA : defaultVp;
    const float seedB = (ltp.vPlateB >= 0.0f) ? ltp.vPlateB : defaultVp;

    // ── Cathode node: bracketed root-find, NOT relaxation ────────────────────
    //
    // The shared cathode sits at vC = (ipA + ipB) * tailR. Raising vC lowers both
    // grid-cathode voltages, so both plate currents fall: the residual
    //
    //     f(vC) = (ipA(vC) + ipB(vC)) * tailR - vC
    //
    // is strictly decreasing and has exactly one root, with f(0) > 0 and
    // f(vSupply) < 0. So [0, vSupply] always brackets it and a bracketed method
    // cannot diverge.
    //
    // This replaces a damped fixed-point relaxation that did NOT converge: it
    // settled into a period-2 limit cycle instead, making piA alternate between
    // 0.933 and 0.292 every sample at a constant 1.5 V input — a full-scale
    // Nyquist-rate square wave synthesised from DC, which the pentode, OT
    // saturation and output tanh then intermodulated back into the audio band.
    // No damping constant fixed it (measured across plate-damping and alpha
    // sweeps); undamped made vCathode run away past the supply rail instead.
    //
    // vC moves slowly between samples, so seeding the secant with the previous
    // value converges in ~2.6 evaluations on average. Bisection is the fallback
    // whenever the secant leaves the physical range or stalls.
    float vPlateA = seedA, vPlateB = seedB;
    auto residual = [&](float vC) {
        float a, b;
        const float ipA = plateCurrentAt(vGridA - vC, seedA, ltp.rPlateA_n, vSupply_n, vC, a);
        const float ipB = plateCurrentAt(vGridB - vC, seedB, ltp.rPlateB_n, vSupply_n, vC, b);
        vPlateA = a; vPlateB = b;
        return (ipA + ipB) * ltp.tailR_n - vC;
    };

    // 1e-5 normalised = 3 mV on the cathode node. Measured identical to a 1e-7
    // solve (same operating point, same swing, zero residual oscillation) while
    // needing 1.4 residual evaluations per sample instead of 2.6 — the solver is
    // the most expensive thing in the amp loop, so that halving matters. Do not
    // loosen further: at 1e-4 the operating point visibly shifts.
    constexpr float tolerance = 1.0e-5f;
    constexpr int   secantSteps = 6;
    constexpr int   bisectSteps = 24;

    float x0 = std::clamp(ltp.vCathode, 0.0f, vSupply_n);
    float f0 = residual(x0);
    float vCathode = x0;

    if (std::abs(f0) > tolerance) {
        float x1 = std::clamp(x0 + ((f0 > 0.0f) ? 1.0f : -1.0f)
                                   * std::max(1.0e-4f, std::abs(f0)),
                              0.0f, vSupply_n);
        float f1 = residual(x1);

        bool converged = false;
        for (int i = 0; i < secantSteps; ++i) {
            const float den = f1 - f0;
            if (std::abs(den) < 1.0e-20f) break;
            const float x2 = x1 - f1 * (x1 - x0) / den;
            if (!(x2 >= 0.0f && x2 <= vSupply_n)) break;   // left the physical range
            const float f2 = residual(x2);
            x0 = x1; f0 = f1; x1 = x2; f1 = f2;
            if (std::abs(f2) < tolerance) { converged = true; break; }
        }

        if (converged) {
            vCathode = x1;
        } else {
            float lo = 0.0f, hi = vSupply_n;
            float fLo = residual(lo);
            for (int i = 0; i < bisectSteps && (hi - lo) > 1.0e-8f; ++i) {
                const float mid = 0.5f * (lo + hi);
                const float fMid = residual(mid);
                if ((fMid > 0.0f) == (fLo > 0.0f)) { lo = mid; fLo = fMid; }
                else                               { hi = mid; }
            }
            vCathode = 0.5f * (lo + hi);
            residual(vCathode);   // leave vPlateA/B consistent with the accepted root
        }
    }

    ltp.vCathode = vCathode;
    ltp.vPlateA  = vPlateA;
    ltp.vPlateB  = vPlateB;

    return { vPlateA, vPlateB, vCathode };
}

} // namespace sr
