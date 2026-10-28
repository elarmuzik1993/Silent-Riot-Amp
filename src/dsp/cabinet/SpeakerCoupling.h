// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
// Dynamic Speaker-Amp Coupling (Spec §3.9)
#include "../filters/OnePole.h"
#include "../../utils/Constants.h"
#include <cmath>
#include <algorithm>

namespace sr {

struct SpeakerCoupling {
    OnePoleLP xoverLow, xoverHigh;  // complementary 3-way crossover
    float bassEnv = 0.0f, midEnv = 0.0f, hfEnv = 0.0f;
    float envDecay = 0.999f;
    float gainModSmooth = 1.0f;     // smoothed gain modulation output
    float gainModDecay  = 0.999f;   // smoothing coefficient

    void init();
    void recalcFilters(float effectiveSR);
    void reset();
    float process(float powerOut);
};

} // namespace sr
