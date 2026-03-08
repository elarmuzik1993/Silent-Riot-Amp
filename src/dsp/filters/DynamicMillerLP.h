#pragma once
// Dynamic Miller Capacitance LP (Spec §3.3 / §2.6.2)
// Per-sample fc modulated by voltage gain estimate
// V1b Rsource modulated by gain pot position

#include "OnePole.h"
#include "../../utils/Constants.h"
#include <cmath>

namespace tt {

struct DynamicMillerLP {
    OnePoleLP filter;
    float cgp     = CGP_12AX7;
    float cgk     = CGK_12AX7;
    float rSource_real = 10000.0f;
    float nominalGain  = 70.0f;
    float fcSmooth     = 20000.0f;
    float fcSmoothDecay = 0.999f;

    void init(float _cgp, float _cgk, float _rSource, float _nomGain) {
        cgp = _cgp; cgk = _cgk; rSource_real = _rSource; nominalGain = _nomGain;
    }

    void recalc(float effectiveSR) {
        fcSmoothDecay = std::exp(-1.0f / (MILLER_FC_SMOOTH_MS * 0.001f * effectiveSR));
    }

    float process(float x, float gainEstimate, float effectiveSR) {
        float cMiller = cgk + cgp * (1.0f + gainEstimate);
        float targetFc = 1.0f / (2.0f * PI_F * rSource_real * cMiller);
        fcSmooth = targetFc + (fcSmooth - targetFc) * fcSmoothDecay;
        filter.setFreqFast(fcSmooth, effectiveSR);
        return filter.process(x);
    }

    void reset() { filter.reset(); fcSmooth = 20000.0f; }
};

} // namespace tt
