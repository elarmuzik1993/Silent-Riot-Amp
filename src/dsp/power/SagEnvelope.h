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
    // Reference idle current. MUST match what the pentode model actually draws
    // at idle for the ACTIVE bias — see pentodeQuiescentTotal(). If it sits above
    // the real idle current, excessI goes negative, biasOffset() goes POSITIVE and
    // pins at the warm clamp, so the amp runs permanently hot and sag stops being
    // a dynamic at all. The constant is only a fallback; the processor overrides
    // it via setQuiescent() on prepare and whenever 15W/7W changes the bias.
    float quiescentI   = SAG_QUIESCENT_I_N;
    float envelope     = SAG_QUIESCENT_I_N;  // init at idle cathode current
    float attackDecay  = 0.999f;
    float releaseDecay = 0.999f;

    // Retarget the idle reference (bias-dependent). Leaves the envelope tracking
    // the real current — only the reference point moves, so a 15W/7W switch
    // doesn't punch a discontinuity into the envelope itself.
    void setQuiescent(float idleTotalCurrent) { quiescentI = idleTotalCurrent; }

    void recalcCoeffs(float effectiveSR) {
        attackDecay  = std::exp(-1.0f / (SAG_ATTACK_MS  * 0.001f * effectiveSR));
        releaseDecay = std::exp(-1.0f / (SAG_RELEASE_MS * 0.001f * effectiveSR));
    }

    // Restore the envelope to idle cathode current (no sag offset)
    void reset() { envelope = quiescentI; }

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
        float excessI = envelope - quiescentI;
        float shift = -excessI * SAG_R_CATHODE_N;
        return std::clamp(shift, SAG_BIAS_CLAMP_COLD_N, SAG_BIAS_CLAMP_WARM_N);  // -3V cold, +1.5V warm
    }
};

} // namespace sr
