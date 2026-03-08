#pragma once
// Dynamic Speaker-Amp Coupling (Spec §3.9)
#include "../filters/Biquad.h"
#include "../filters/OnePole.h"
#include "../../utils/Constants.h"
#include <cmath>
#include <algorithm>

namespace tt {

struct SpeakerCoupling {
    Biquad bassExtract, midExtract;
    OnePoleLP hfEnvelope;
    float bassEnv = 0.0f, midEnv = 0.0f, hfEnv = 0.0f;
    float envDecay = 0.999f;

    void init();
    void recalcFilters(float effectiveSR);
    void reset();
    float process(float powerOut);
};

} // namespace tt
