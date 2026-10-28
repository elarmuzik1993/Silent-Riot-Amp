// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
// Idle operating point (review B1).
//
// resetDSPState() used to zero every filter and put the supply at nominal,
// but on silence the amp does not sit there: each 12AX7 plate idles at ~194 V
// DC behind a coupling cap, and the supply idles ~18 V below nominal under the
// EL84 pair's idle current. The first samples after every prepare then
// replayed the settling as a burst on silence (up to −5 dBFS at crank), which
// was also the whole E1 gate failure. These functions solve the idle point
// with the real tube, supply and sag models and seed the state from it, so the
// seed follows any later change to those models (e.g. review B2).
#include "filters/FilterBank.h"
#include "power/LTPSolver.h"
#include "power/PowerSupplyModel.h"
#include "power/SagEnvelope.h"
#include "tubes/PentodeWaveshaper.h"
#include "tubes/TriodeWaveshaper.h"
#include "../utils/Constants.h"
#include "../utils/VoltageScale.h"
#include <cmath>

namespace sr {

// EL84 pair, supply and sag on silence. The pair's idle current droops the
// supply and sets the sag offset; the drooped rails and the offset in turn set
// that current. Leaves psu and sag on the fixed point. The caller sets sag's
// quiescent reference first. Returns the bisection steps used.
//
// Bracketed, not a plain substitution: the sag loop's gain per step (cathode
// resistor × the pair's transconductance) is above 1, so substituting the
// drawn current back in oscillates instead of converging, the same trap as
// the old LTP solver (docs/handoff-ltp-convergence.md). Assuming a cathode
// current P and evaluating what the pair then draws gives a residual that falls
// strictly with P (more current → colder bias, lower rails), so bisection
// always lands on the one root.
inline int settlePowerStageAtIdle(float el84Bias, PowerSupplyModel& psu, SagEnvelope& sag) {
    // Pair current drawn when P flows (P sets the sag offset and, with the
    // screen current, the supply droop). The screen current is iterated: the
    // rails depend on it only through the screen resistor, a weak loop.
    auto drawnAt = [&](float plate) {
        sag.settle(plate);
        float screen = 0.0f;
        PentodeOutput tube {};
        for (int k = 0; k < 64; ++k) {
            psu.settle(plate, screen);
            tube = pentodeWaveshaper(0.0f, el84Bias + sag.biasOffset(), psu.vPower(), psu.vScreen());
            const float next = 2.0f * tube.screenCurrent;
            const bool done = std::abs(next - screen) <= 1e-7f * next;
            screen = next;
            if (done) break;
        }
        psu.settle(plate, screen);
        return 2.0f * std::abs(tube.plateCurrent);
    };

    constexpr int kMaxSteps = 60;
    float lo = 0.0f;                          // residual drawn - P is >= 0 here...
    float hi = 4.0f * sag.quiescentI + 1.0f;  // ...and < 0 here (sag's cold clamp caps the draw)
    int steps = 0;
    while (steps < kMaxSteps && (hi - lo) > 1e-6f * hi) {
        const float mid = 0.5f * (lo + hi);
        if (drawnAt(mid) > mid) lo = mid; else hi = mid;
        ++steps;
    }
    drawnAt(0.5f * (lo + hi));                // leave psu and sag on the root
    return steps;
}

// Preamp and PI on silence at the supply's settled rails: converge each
// solver's cached state and charge every filter that sits behind a DC plate
// voltage. Downstream of the PI coupling caps the idle signal is zero, so
// nothing further needs seeding. Filter coefficients must already be set.
inline void seedPreampAtIdle(FilterBank& fb, TriodeState& v1a, TriodeState& v1b,
                             LTPModel& ltp, const PowerSupplyModel& psu) {
    constexpr int kIter = 32;   // the per-sample solvers run 4 steps from a warm cache
    float plate = 0.0f;
    for (int k = 0; k < kIter; ++k)
        plate = triodeWaveshaper(0.0f, VS::V_BIAS_12AX7, psu.vPreamp(), PREAMP_RPLATE_N, v1a);
    v1b = v1a;   // same tube, same rail, same idle

    // x -= cathodeBypass(x) * depth, then the coupling high-pass. Chain from the
    // bypass filter's realised resting output, not from plate × (1 − depth): at
    // 4.2 Hz its float DC gain is ~1.0003, and assuming exactly 1 left a 37 mV
    // step on the coupling cap.
    fb.v1a_couplingHP.seedDC(plate - fb.v1a_cathodeBypass.seedDC(plate) * CATHODE_BYPASS_DEPTH);
    fb.v1b_couplingHP.seedDC(plate - fb.v1b_cathodeBypass.seedDC(plate) * CATHODE_BYPASS_DEPTH);

    LTPOutput pi {};
    for (int k = 0; k < kIter; ++k)
        pi = solveLTP(0.0f, psu.vPi(), ltp);
    fb.pi_couplingHP_A.seedDC(pi.piA);
    fb.pi_couplingHP_B.seedDC(pi.piB);
}

} // namespace sr
