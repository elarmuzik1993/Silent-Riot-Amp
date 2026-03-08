#pragma once
// ═══════════════════════════════════════════════════════════════
// VoltageScale — Normalized Voltage Domain (Spec Section 2.5)
//
// Real tube voltages (1Vpp–600Vpp) mapped to DSP float domain.
// V_REF = 300V (B+ supply) → 1.0 in normalized domain.
// ALL processing code uses normalized values. No raw real-world
// values in processing code — ever.
// ═══════════════════════════════════════════════════════════════

#include <cmath>

namespace tt {

struct VoltageScale {
    // ── Primary references ──
    static constexpr float V_REF     = 300.0f;
    static constexpr float R_REF     = 300000.0f;
    static constexpr float I_REF     = V_REF / R_REF;  // 1mA

    // ── Derived conversion factors ──
    static constexpr float V_TO_N    = 1.0f / V_REF;
    static constexpr float N_TO_V    = V_REF;
    static constexpr float R_TO_N    = 1.0f / R_REF;
    static constexpr float N_TO_R    = R_REF;
    static constexpr float I_TO_N    = 1.0f / I_REF;
    static constexpr float N_TO_I    = I_REF;

    // ── Conversions ──
    static constexpr float v(float volts)   { return volts * V_TO_N; }
    static constexpr float toVolts(float n) { return n * N_TO_V; }
    static constexpr float r(float ohms)    { return ohms * R_TO_N; }
    static constexpr float toOhms(float n)  { return n * N_TO_R; }
    static constexpr float i(float amps)    { return amps * I_TO_N; }
    static constexpr float toAmps(float n)  { return n * N_TO_I; }

    // ── Common resistances (normalized) ──
    static constexpr float R_100  = r(100.0f);
    static constexpr float R_300  = r(300.0f);
    static constexpr float R_470  = r(470.0f);
    static constexpr float R_500  = r(500.0f);
    static constexpr float R_820  = r(820.0f);
    static constexpr float R_1K2  = r(1200.0f);
    static constexpr float R_1K5  = r(1500.0f);
    static constexpr float R_68K  = r(68000.0f);
    static constexpr float R_100K = r(100000.0f);
    static constexpr float R_500K = r(500000.0f);
    static constexpr float R_1M   = r(1000000.0f);

    // ── Common voltages (normalized) ──
    static constexpr float V_B_PLUS      = v(300.0f);
    static constexpr float V_SCREEN      = v(280.0f);
    static constexpr float V_PI          = v(280.0f);
    static constexpr float V_PREAMP      = v(250.0f);
    static constexpr float V_GUITAR      = v(0.5f);
    static constexpr float V_BIAS_12AX7  = v(-1.5f);
    static constexpr float V_THERMAL     = v(0.026f);

    // ── Stage output impedances Rplate||rp ──
    static constexpr float Z_OUT_V1A = r(38461.0f);   // 100K||62.5K
    static constexpr float Z_OUT_V1B = r(47170.0f);   // (100K+68K)||62.5K
    static constexpr float Z_OUT_PI  = r(38461.0f);    // 100K||62.5K per side
};

using VS = VoltageScale;

} // namespace tt
