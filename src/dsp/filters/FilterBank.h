// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
// Central Filter Management (Spec §2.6.2 Filter Registry)
// All 24 filter instances, bulk recalc/reset

#include "OnePole.h"
#include "Biquad.h"
#include "../../utils/Constants.h"

namespace sr {

struct FilterBank {
    float effectiveSR = 44100.0f;

    // ── Input / Preamp ──
    OnePoleHP inputHP;
    OnePoleLP v1a_cathodeBypass;
    OnePoleHP v1a_couplingHP;
    OnePoleLP v1b_cathodeBypass;
    OnePoleHP v1b_couplingHP;

    // ── Phase Inverter ──
    OnePoleHP pi_couplingHP_A, pi_couplingHP_B;

    // ── Shape EQ (3-band on differential, modulated per block) ──
    Biquad shapeLowShelf;
    Biquad shapeMidBell;
    Biquad shapeHighShelf;

    // ── Gain loading (modulated per block) ──
    OnePoleLP gainLoadingLP;

    // ── Output Transformer ──
    Biquad ot_leakageLP;

    // ── Cabinet Sim ──
    Biquad cab_presence, cab_rolloff, cab_notch;

    void recalcAll(float baseSampleRate, int oversampleFactor);
    void updateShape(float shapeVal);
    void resetAll();
};

} // namespace sr
