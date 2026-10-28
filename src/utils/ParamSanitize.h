// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
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

// Most params in this project are "0 = neutral/bypassed" (gate threshold 0 =
// always-open, boost/delay/reverb knobs at 0 = no effect), so 0.0f is the right
// fallback for them. Centre-detented trims are NOT: for those, 0.0f means full
// cut — and on the output trim, silence — so a single NaN from a host would mute
// the plugin. Those callers pass their own neutral value via the overload.
inline float sanitizeUnitParamOr(float v, float fallback)
{
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : fallback;
}

inline float sanitizeUnitParam(float v)
{
    return sanitizeUnitParamOr(v, 0.0f);
}

} // namespace sr
