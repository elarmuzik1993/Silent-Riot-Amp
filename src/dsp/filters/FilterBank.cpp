#include "FilterBank.h"

namespace tt {

void FilterBank::recalcAll(float baseSampleRate, int oversampleFactor) {
    effectiveSR = baseSampleRate * oversampleFactor;

    inputHP.setFreq(INPUT_HP_FREQ, effectiveSR);
    v1a_cathodeBypass.setFreq(CATHODE_BYPASS_FREQ, effectiveSR);
    v1a_couplingHP.setFreq(STAGE1_COUPLING_HP, effectiveSR);
    v1b_cathodeBypass.setFreq(CATHODE_BYPASS_FREQ, effectiveSR);
    v1b_couplingHP.setFreq(STAGE2_COUPLING_HP, effectiveSR);
    pi_couplingHP_A.setFreq(PI_COUPLING_HP, effectiveSR);
    pi_couplingHP_B.setFreq(PI_COUPLING_HP, effectiveSR);

    ot_leakageLP.makeLowPass(OT_LP_FREQ, 0.707f, effectiveSR);
    cab_presence.makePeakEQ(3500.0f, 1.5f, 4.0f, effectiveSR);
    cab_rolloff.makeLowPass(8000.0f, 0.707f, effectiveSR);
    cab_notch.makeNotch(350.0f, 1.5f, effectiveSR);

    // Initialize modulated filters with sensible defaults
    toneCutFilter.setFreq(TONE_MAX_FREQ, effectiveSR);      // fully bright
    gainLoadingLP.setFreq(GAIN_LOADING_LP_FREQ, effectiveSR);
}

void FilterBank::updateModulated(float toneFreq, float gainLoadFreq) {
    toneCutFilter.setFreqFast(toneFreq, effectiveSR);
    gainLoadingLP.setFreqFast(gainLoadFreq, effectiveSR);
}

void FilterBank::resetAll() {
    inputHP.reset();
    v1a_cathodeBypass.reset(); v1a_couplingHP.reset();
    v1b_cathodeBypass.reset(); v1b_couplingHP.reset();
    pi_couplingHP_A.reset(); pi_couplingHP_B.reset();
    toneCutFilter.reset(); gainLoadingLP.reset();
    ot_leakageLP.reset();
    cab_presence.reset(); cab_rolloff.reset(); cab_notch.reset();
}

} // namespace tt
