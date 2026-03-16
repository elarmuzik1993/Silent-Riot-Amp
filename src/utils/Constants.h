#pragma once
// ═══════════════════════════════════════════════════════════════
// DSP Constants — Tiny Terror (Spec Section 10)
//
// Convention: _N = normalized, _REAL = real units, no suffix = dimensionless
// ═══════════════════════════════════════════════════════════════

#include "VoltageScale.h"

namespace tt {

// ── Stage gains (dimensionless) ─────────────────────────────
namespace StageGains {
    constexpr float V1A_GAIN        = 60.0f;
    constexpr float V1B_GAIN        = 60.0f;
    constexpr float EL84_GAIN_BASE  = 17.0f;
    constexpr float EL84_THRESH_BASE = 0.5f;
}

// ── Supply node voltages ────────────────────────────────────
constexpr float V_POWER_NOMINAL_N   = VS::v(300.0f);
constexpr float V_SCREEN_NOMINAL_N  = VS::v(280.0f);
constexpr float V_PI_NOMINAL_N      = VS::v(280.0f);
constexpr float V_PREAMP_NOMINAL_N  = VS::v(250.0f);

// ── Supply droop time constants (ms) ────────────────────────
constexpr float TAU_POWER_MS        = 10.0f;
constexpr float TAU_SCREEN_MS       = 60.0f;
constexpr float TAU_PI_MS           = 35.0f;
constexpr float TAU_PREAMP_MS       = 80.0f;

// ── Supply droop sensitivity ────────────────────────────────
constexpr float DROOP_POWER         = 0.06f;
constexpr float DROOP_SCREEN        = 0.12f;
constexpr float DROOP_PI            = 0.04f;
constexpr float DROOP_PREAMP        = 0.02f;

// ── Supply impedances ───────────────────────────────────────
constexpr float SOURCE_IMPEDANCE_N  = VS::r(200.0f);
constexpr float R_DROP1_N           = VS::r(4700.0f);
constexpr float R_DROP2_N           = VS::r(22000.0f);

// ── Filter frequencies (Hz) ─────────────────────────────────
constexpr float INPUT_HP_FREQ       = 80.0f;
constexpr float CATHODE_BYPASS_FREQ = 4.2f;
constexpr float STAGE1_COUPLING_HP  = 72.0f;
constexpr float STAGE2_COUPLING_HP  = 80.0f;
constexpr float PI_COUPLING_HP      = 50.0f;
constexpr float OT_LP_FREQ          = 7000.0f;
constexpr float GAIN_LOADING_LP_FREQ = 8000.0f;

// ── Math constant ────────────────────────────────────────────
constexpr float PI_F = 3.14159265358979323846f;

// ── Anti-denormal ───────────────────────────────────────────
constexpr float ANTI_DENORMAL       = 1e-18f;

// ── Tone control ────────────────────────────────────────────
constexpr float TONE_MIN_FREQ       = 800.0f;
constexpr float TONE_MAX_FREQ       = 5000.0f;

// ── Triode model ────────────────────────────────────────────
constexpr float TRIODE_BIAS_N       = VS::v(-1.5f);
// Note: triode waveshaper uses stageGain (V1A_GAIN=70, V1B_GAIN=70) as
// the drive parameter directly — there is no separate TRIODE_DRIVE constant.
// See spec Section 4.1 Option A: x = vGrid_n * drive + vBias_n
constexpr float TRIODE_ASYM_FACTOR  = 1.5f;
constexpr float HEADROOM_FACTOR     = 0.85f;
constexpr float TRIODE_MU           = 100.0f;
constexpr float TRIODE_RP_N         = VS::r(62500.0f);

// ── Pentode model (EL84) ───────────────────────────────────
// EL84 cathode bias: shared 100ohm resistor, Ik ~ 2×48mA = 96mA
// Vk = 96mA × 100ohm = 9.6V → Vgk = 0 - 9.6 = -9.6V (15W mode)
// In 7W mode: hotter bias, Vgk ~ -7V
// NOTE: The old value VS::v(-270) was WRONG — that's the plate supply,
// not the cathode voltage. The cathode is a few volts above ground.
constexpr float PENTODE_BIAS_15W_N  = VS::v(-9.6f);    // -0.032 normalized
constexpr float PENTODE_BIAS_7W_N   = VS::v(-7.0f);    // -0.023 normalized
constexpr float MU_SCREEN           = 12.0f;
constexpr float EL84_EXPONENT       = 1.35f;
// EL84_KG calibrated: at idle, driveScaled ~ 0.046, driveScaled^1.35 ~ 0.016
// Target: Ip = 48mA = 48.0 normalized → KG = 0.016 / 48 = 0.000324
constexpr float EL84_KG             = 0.000324f;
constexpr float V_PLATE_KNEE_N      = VS::v(40.0f);
constexpr float SCREEN_CURRENT_RATIO = 0.25f;
constexpr float SCREEN_REDIST_FACTOR = 0.15f;
constexpr float R_SCREEN_N          = VS::r(470.0f);
constexpr float EL84_CUTOFF_N       = VS::v(-10.0f);
// EL84 max plate current: absolute max rating ~65mA continuous
// The power law is unbounded, so we clamp ip before plate saturation
constexpr float EL84_IP_MAX_N       = VS::i(0.065f);   // 65.0 normalized

// ── Grid conduction ─────────────────────────────────────────
constexpr float GRID_CONDUCT_THRESH_N       = VS::v(0.5f);
constexpr float GRID_CONDUCT_THRESH_EL84_N  = VS::v(0.0f);
constexpr float GRID_R_TRIODE_N             = VS::R_300;
constexpr float GRID_R_PENTODE_N            = VS::R_500;
constexpr float GRID_SOURCE_R_MUSICAL_N      = VS::r(1000.0f);  // reduced source R for musical loading
constexpr float GRID_CHARGE_RATE            = 0.01f;
constexpr float GRID_VT_N                   = VS::v(0.026f);
constexpr float GRID_IS                     = 1e-6f;
constexpr float BLOCKING_CHARGE_SCALE       = 0.001f;
constexpr float MAX_BIAS_SHIFT_N            = VS::v(-5.0f);
constexpr float COND_LP_MIN_FREQ            = 3000.0f;
constexpr float GRID_I_SATURATION           = 0.01f;

// ── Sag envelope ────────────────────────────────────────────
constexpr float SAG_ATTACK_MS       = 10.0f;
constexpr float SAG_RELEASE_MS      = 100.0f;
constexpr float SAG_DEPTH           = 0.003f;

// ── Phase inverter ──────────────────────────────────────────
constexpr float PI_TAIL_R_N         = VS::R_1K2;
constexpr float PI_RPLATE_N         = VS::R_100K;
constexpr float PI_BIAS_REF_N       = VS::v(-1.5f);
constexpr float PI_RP_N             = VS::r(62500.0f);
constexpr float PI_MU               = 100.0f;
constexpr float PI_CATHODE_INIT_N   = VS::v(1.5f);

// ── Triode current model (Child-Langmuir) ───────────────────
// 12AX7 cutoff: Vgk ~ -3V (plate current drops to zero)
// Operating point: Vgk = -1.5V, Ip ~ 1.2mA
// The model computes: ip = K * softRect(vgk_n - V_CUTOFF_12AX7_N)^1.5
// The offset from cutoff is critical — without it, vgk_n = -0.005
// looks like "deeply negative" to softRect and produces zero current.
// With offset: softRect(-0.005 - (-0.01)) = softRect(0.005) = 0.005
// K calibrated so that K * 0.005^1.5 = 1.2 (1.2mA normalized)
constexpr float V_CUTOFF_12AX7_N    = VS::v(-3.0f);    // -0.01 normalized
constexpr float K_12AX7             = 3394.0f;          // calibrated: K * softRect(0.005)^1.5 = 1.2

// ── Dynamic Miller ──────────────────────────────────────────
constexpr float CGP_12AX7           = 1.7e-12f;
constexpr float CGK_12AX7           = 1.6e-12f;
constexpr float V1A_RSOURCE_REAL    = 10000.0f;
constexpr float V1B_RSOURCE_BASE_REAL = 38500.0f;
constexpr float GAIN_POT_R_REAL     = 500000.0f;
constexpr float V1B_RSOURCE_MAX_REAL = 150000.0f;  // cap Miller rSource to keep fc ≥ ~7kHz
constexpr float MILLER_FC_SMOOTH_MS = 1.0f;

// ── Speaker coupling ────────────────────────────────────────
constexpr float SPK_RE_N            = VS::r(6.0f);
constexpr float SPK_ZPEAK_N         = VS::r(35.0f);
constexpr float SPK_ZMIN_N          = VS::r(8.0f);
constexpr float SPK_FS              = 75.0f;
constexpr float SPK_Q               = 3.5f;
constexpr float OT_TURNS_RATIO_SQ   = 1000.0f;
constexpr float OT_NOMINAL_LOAD_N   = VS::r(8000.0f);
constexpr float SPK_VOICECOIL_LP    = 2000.0f;
constexpr float Z_REFLECTED_BASS_N  = VS::r(35000.0f);
constexpr float Z_REFLECTED_MID_N   = VS::r(8000.0f);
constexpr float Z_REFLECTED_HF_N    = VS::r(20000.0f);
constexpr float SPK_ENV_TAU_MS      = 2.0f;
constexpr float SPK_GAINMOD_DEPTH   = 2.0f;
constexpr float SPK_GAINMOD_RANGE   = 0.5f;

// ── OT core saturation ─────────────────────────────────────
constexpr float OT_CURRENT_SCALE    = 0.015f;  // plate current → OT primary voltage (~4.5 kΩ eff.)
constexpr float OT_SAT_DRIVE        = 12.0f;   // saturation onset (higher = earlier)
constexpr float OT_SAT_DC_FREQ      = 1.5f;    // integrator DC-block frequency (Hz)

// ── Gain taper ──────────────────────────────────────────────
constexpr float GAIN_TAPER_EXPONENT = 2.0f;


// ── LUT waveshaper ──────────────────────────────────────────
constexpr int TABLE_SIZE            = 2048;

// ── Debug assertions ────────────────────────────────────────
#ifdef DEBUG
#define VS_ASSERT_RANGE(val, lo, hi, stage) \
    jassert((val) >= (lo) && (val) <= (hi) && "Signal out of expected range at " stage)
#else
#define VS_ASSERT_RANGE(val, lo, hi, stage) ((void)0)
#endif

} // namespace tt
