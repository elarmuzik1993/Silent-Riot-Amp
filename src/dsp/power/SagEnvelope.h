#pragma once
// Cathode Bias Sag (Spec §3.8)
//
// Models the shared cathode resistor/capacitor on the EL84 pair.
// Under drive, total cathode current rises → cathode voltage rises →
// Vgk becomes more negative → bias shifts colder → less gain.
// When signal drops, cap discharges → bias recovers → "bloom."
#include "../../utils/Constants.h"
#include <cmath>
#include <algorithm>

namespace sr {

struct SagEnvelope {
    float envelope     = SAG_QUIESCENT_I_N;  // init at idle cathode current
    float attackDecay  = 0.999f;
    float releaseDecay = 0.999f;

    void recalcCoeffs(float effectiveSR) {
        attackDecay  = std::exp(-1.0f / (SAG_ATTACK_MS  * 0.001f * effectiveSR));
        releaseDecay = std::exp(-1.0f / (SAG_RELEASE_MS * 0.001f * effectiveSR));
    }

    // Feed with TOTAL plate current: |ipA| + |ipB| (both tubes through shared cathode R)
    void update(float totalPlateCurrent) {
        float level = std::abs(totalPlateCurrent);
        float d = (level > envelope) ? attackDecay : releaseDecay;
        envelope = level + (envelope - level) * d;
        if (envelope < 1e-10f) envelope = 0.0f;
    }

    // Bias offset from cathode voltage change: ΔVk = (Itotal - Iquiescent) × R_cathode
    // Positive excess → cathode rises → Vgk more negative → NEGATIVE offset
    // At idle (envelope ≈ quiescent): offset ≈ 0
    // Under drive (envelope > quiescent): offset < 0 (colder)
    float biasOffset() const {
        float excessI = envelope - SAG_QUIESCENT_I_N;
        float shift = -excessI * SAG_R_CATHODE_N;
        return std::clamp(shift, -0.01f, 0.005f);  // safety: -3V cold, +1.5V warm
    }
};

} // namespace sr
