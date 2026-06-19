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

namespace sr {

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
    // Note: use direct multiplication instead of r() to avoid calling
    // a constexpr member function before the struct definition is complete.
    static constexpr float R_100  = 100.0f * R_TO_N;
    static constexpr float R_300  = 300.0f * R_TO_N;
    static constexpr float R_500  = 500.0f * R_TO_N;
    static constexpr float R_1K2  = 1200.0f * R_TO_N;
    static constexpr float R_1K5  = 1500.0f * R_TO_N;
    static constexpr float R_68K  = 68000.0f * R_TO_N;
    static constexpr float R_100K = 100000.0f * R_TO_N;
    static constexpr float R_500K = 500000.0f * R_TO_N;
    static constexpr float R_1M   = 1000000.0f * R_TO_N;

    // ── Common voltages (normalized) ──
    static constexpr float V_B_PLUS      = 300.0f * V_TO_N;
    static constexpr float V_SCREEN      = 280.0f * V_TO_N;
    static constexpr float V_PI          = 280.0f * V_TO_N;
    static constexpr float V_PREAMP      = 250.0f * V_TO_N;
    static constexpr float V_GUITAR      = 0.5f * V_TO_N;
    static constexpr float V_BIAS_12AX7  = -1.5f * V_TO_N;

    // ── Stage output impedances Rplate||rp ──
    static constexpr float Z_OUT_V1A = 38461.0f * R_TO_N;   // 100K||62.5K
    static constexpr float Z_OUT_V1B = 47170.0f * R_TO_N;   // (100K+68K)||62.5K
    static constexpr float Z_OUT_PI  = 38461.0f * R_TO_N;    // 100K||62.5K per side
};

using VS = VoltageScale;

} // namespace sr
