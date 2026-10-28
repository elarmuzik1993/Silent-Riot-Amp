// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "FilterBank.h"

namespace sr {

void FilterBank::recalcAll(float baseSampleRate, int oversampleFactor) {
    effectiveSR = baseSampleRate * oversampleFactor;

    inputHP.setFreq(INPUT_HP_FREQ, effectiveSR);
    v1a_cathodeBypass.setFreq(CATHODE_BYPASS_FREQ, effectiveSR);
    v1a_couplingHP.setFreq(STAGE1_COUPLING_HP, effectiveSR);
    v1b_cathodeBypass.setFreq(CATHODE_BYPASS_FREQ, effectiveSR);
    v1b_couplingHP.setFreq(STAGE2_COUPLING_HP, effectiveSR);
    pi_couplingHP_A.setFreq(PI_COUPLING_HP, effectiveSR);
    pi_couplingHP_B.setFreq(PI_COUPLING_HP, effectiveSR);

    ot_leakageLP.makeLowPass(OT_LP_FREQ, OT_LP_Q, effectiveSR);
    cab_presence.makePeakEQ(CAB_PRESENCE_FREQ, CAB_PRESENCE_Q, CAB_PRESENCE_GAIN_DB, effectiveSR);
    cab_rolloff.makeLowPass(CAB_ROLLOFF_FREQ, CAB_ROLLOFF_Q, effectiveSR);
    cab_notch.makeNotch(CAB_NOTCH_FREQ, CAB_NOTCH_Q, effectiveSR);

    // Initialize shape EQ at noon (shape=0.5)
    updateShape(0.5f);
    gainLoadingLP.setFreq(GAIN_LOADING_LP_FREQ, effectiveSR);
}

void FilterBank::updateShape(float shapeVal) {
    float lowGain  = SHAPE_LOW_GAIN_MIN  + (SHAPE_LOW_GAIN_MAX  - SHAPE_LOW_GAIN_MIN)  * shapeVal;
    float midGain  = SHAPE_MID_GAIN_MIN  + (SHAPE_MID_GAIN_MAX  - SHAPE_MID_GAIN_MIN)  * shapeVal;
    float highGain = SHAPE_HIGH_GAIN_MIN + (SHAPE_HIGH_GAIN_MAX - SHAPE_HIGH_GAIN_MIN) * shapeVal;

    shapeLowShelf.makeLowShelf(SHAPE_LOW_SHELF_FREQ, SHAPE_LOW_SHELF_Q, lowGain, effectiveSR);
    shapeMidBell.makePeakEQ(SHAPE_MID_FREQ, SHAPE_MID_Q, midGain, effectiveSR);
    shapeHighShelf.makeHighShelf(SHAPE_HIGH_SHELF_FREQ, SHAPE_HIGH_SHELF_Q, highGain, effectiveSR);
}

void FilterBank::resetAll() {
    inputHP.reset();
    v1a_cathodeBypass.reset(); v1a_couplingHP.reset();
    v1b_cathodeBypass.reset(); v1b_couplingHP.reset();
    pi_couplingHP_A.reset(); pi_couplingHP_B.reset();
    shapeLowShelf.reset(); shapeMidBell.reset(); shapeHighShelf.reset();
    gainLoadingLP.reset();
    ot_leakageLP.reset();
    cab_presence.reset(); cab_rolloff.reset(); cab_notch.reset();
}

} // namespace sr
