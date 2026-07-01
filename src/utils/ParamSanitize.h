#pragma once
// ═══════════════════════════════════════════════════════════════
// ParamSanitize — system-boundary guard for host-supplied 0..1
// parameter values. A misbehaving host can (rarely but really) report
// NaN via automation; NaN silently poisons IIR/biquad filter state
// forever (comparisons with NaN are always false, and z1 = x*b1 - ...
// never clears once contaminated). Clamp/sanitize once here, at the
// point raw APVTS values enter the effects module, rather than
// scattering NaN checks inside every DSP class.
// ═══════════════════════════════════════════════════════════════

#include <algorithm>
#include <cmath>

namespace sr {

// 0.0f is the safe fallback for every one of these params — it's already
// this project's "neutral/bypassed" value (gate threshold 0 = always-open,
// boost/delay/reverb knobs at 0 = no effect).
inline float sanitizeUnitParam(float v)
{
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

} // namespace sr
