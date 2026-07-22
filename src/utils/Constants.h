#pragma once
// ═══════════════════════════════════════════════════════════════
// DSP Constants — Silent Riot (Spec Section 10)
//
// Convention: _N = normalized, _REAL = real units, no suffix = dimensionless
// ═══════════════════════════════════════════════════════════════

#include "VoltageScale.h"

namespace sr {

// ── Stage gains (dimensionless) ─────────────────────────────
namespace StageGains {
    constexpr float V1A_GAIN        = 60.0f;
    constexpr float V1B_GAIN        = 60.0f;
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

// ── Supply impedances ───────────────────────────────────────
constexpr float SOURCE_IMPEDANCE_N  = VS::r(200.0f);
constexpr float R_DROP1_N           = VS::r(4700.0f);
constexpr float R_DROP2_N           = VS::r(22000.0f);

// ── Filter frequencies (Hz) ─────────────────────────────────
constexpr float INPUT_HP_FREQ       = 7.0f;
constexpr float CATHODE_BYPASS_FREQ = 4.2f;
constexpr float CATHODE_GM_12AX7     = 0.0016f;    // transconductance 1.6 mA/V
constexpr float CATHODE_RK_12AX7     = 1500.0f;    // cathode resistor 1.5kΩ
constexpr float CATHODE_BYPASS_DEPTH = 1.0f - 1.0f / (1.0f + CATHODE_GM_12AX7 * CATHODE_RK_12AX7);  // ≈ 0.706
constexpr float STAGE1_COUPLING_HP  = 72.0f;
constexpr float STAGE2_COUPLING_HP  = 7.0f;      // 0.047µF × 500KΩ gain pot
constexpr float PI_COUPLING_HP      = 16.0f;      // 47nF × ~220K grid leak
constexpr float OT_LP_FREQ          = 7000.0f;
constexpr float OT_LP_Q             = 0.707f;
constexpr float GAIN_LOADING_LP_FREQ = 5000.0f;

// ── Cabinet IIR sim (presence peak / HF rolloff / low-mid notch) ────
constexpr float CAB_PRESENCE_FREQ   = 2500.0f;  // Hz
constexpr float CAB_PRESENCE_Q      = 1.5f;
constexpr float CAB_PRESENCE_GAIN_DB = 3.0f;
constexpr float CAB_ROLLOFF_FREQ    = 5500.0f;  // Hz
constexpr float CAB_ROLLOFF_Q       = 0.5f;
constexpr float CAB_NOTCH_FREQ      = 400.0f;   // Hz
constexpr float CAB_NOTCH_Q         = 2.0f;

// ── Math constant ────────────────────────────────────────────
constexpr float PI_F = 3.14159265358979323846f;

// ── Anti-denormal ───────────────────────────────────────────
constexpr float ANTI_DENORMAL       = 1e-18f;

// ── Shape control (Orange-inspired mid scoop/boost) ─────────
constexpr float SHAPE_LOW_SHELF_FREQ  = 250.0f;    // Hz
constexpr float SHAPE_LOW_SHELF_Q     = 0.7f;
constexpr float SHAPE_MID_FREQ        = 800.0f;    // Hz — center of mid bell
constexpr float SHAPE_MID_Q           = 1.2f;
constexpr float SHAPE_HIGH_SHELF_FREQ = 3500.0f;   // Hz
constexpr float SHAPE_HIGH_SHELF_Q    = 0.7f;
// Gain ranges (dB) — interpolated linearly with shape knob (0→1)
constexpr float SHAPE_LOW_GAIN_MIN    =  0.0f;     // dB at shape=0 (warm)
constexpr float SHAPE_LOW_GAIN_MAX    =  4.0f;     // dB at shape=1 (scooped)
constexpr float SHAPE_MID_GAIN_MIN    =  5.0f;     // dB at shape=0 (mid-forward)
constexpr float SHAPE_MID_GAIN_MAX    = -9.0f;     // dB at shape=1 (deep scoop)
constexpr float SHAPE_HIGH_GAIN_MIN   = -3.0f;     // dB at shape=0 (warm treble)
constexpr float SHAPE_HIGH_GAIN_MAX   =  4.0f;     // dB at shape=1 (bright)

// ── Preamp plate load ────────────────────────────────────────
constexpr float PREAMP_RPLATE_N     = VS::R_100K;  // 100kΩ plate load for V1a/V1b

// ── Pentode model (EL84) ───────────────────────────────────
constexpr float PENTODE_BIAS_15W_N  = VS::v(-9.6f);    // -0.032 normalized
constexpr float PENTODE_BIAS_7W_N   = VS::v(-7.0f);    // -0.023 normalized
constexpr float V_PLATE_KNEE_N      = VS::v(40.0f);
constexpr float SCREEN_CURRENT_RATIO = 0.25f;
constexpr float SCREEN_REDIST_FACTOR = 0.15f;
constexpr float R_SCREEN_N          = VS::r(470.0f);
constexpr float EL84_CUTOFF_N       = VS::v(-10.0f);
constexpr float EL84_IP_MAX_N       = VS::i(0.065f);   // 65.0 normalized

// ── Grid conduction ─────────────────────────────────────────
constexpr float GRID_CONDUCT_THRESH_N       = VS::v(0.5f);
constexpr float GRID_CONDUCT_THRESH_EL84_N  = VS::v(0.0f);
// Per-stage source impedances (real stage output Z)
constexpr float GRID_SOURCE_R_V1B_N         = VS::Z_OUT_V1A;  // V1a Zout: 100K||62.5K ≈ 38.5KΩ
constexpr float GRID_SOURCE_R_PI_N          = VS::Z_OUT_V1B;  // V1b Zout: (100K+68K)||62.5K ≈ 47KΩ
constexpr float GRID_SOURCE_R_EL84_N        = VS::Z_OUT_PI;   // PI Zout: 100K||62.5K ≈ 38.5KΩ per side
// Per-stage grid conduction impedance — scaled to preserve loading ratio (gridR/sourceR)
// from the original calibration (triode 300Ω/1KΩ = 0.3, pentode 500Ω/1KΩ = 0.5)
constexpr float GRID_R_V1A_N               = VS::R_300;              // 300Ω  (source=100Ω, ratio=3.0)
constexpr float GRID_R_V1B_N               = VS::r(11538.0f);        // ~11.5KΩ (source=38.5KΩ, ratio=0.3)
constexpr float GRID_R_PI_N                = VS::r(14151.0f);        // ~14.2KΩ (source=47KΩ, ratio=0.3)
constexpr float GRID_R_EL84_N              = VS::r(19231.0f);        // ~19.2KΩ (source=38.5KΩ, ratio=0.5)
constexpr float GRID_CHARGE_RATE            = 0.003f;   // was 0.01 → caused PI grid charge saturation at gain≥5 (pumping)
constexpr float MAX_BIAS_SHIFT_N            = VS::v(-1.5f); // was -5V → excessive for 12AX7 (clips at ~-1.5V grid)
// Coupling-cap charge-decay time constants (seconds) per stage
constexpr float GRID_TAU_PREAMP_S           = 0.022f;  // V1a / V1b
constexpr float GRID_TAU_PI_S               = 0.047f;  // phase inverter
constexpr float GRID_TAU_EL84_S             = 0.100f;  // EL84 pair

// ── Sag envelope (cathode bias sag) ─────────────────────────
constexpr float SAG_ATTACK_MS       = 10.0f;
constexpr float SAG_RELEASE_MS      = 100.0f;
constexpr float SAG_R_CATHODE_N     = VS::r(100.0f);   // shared EL84 cathode resistor (100Ω)
constexpr float SAG_QUIESCENT_I_N   = 96.0f;            // quiescent total cathode current (2×48mA normalized)
// biasOffset() clamp: keeps effective bias out of deep Class C (cold) / forward grid (warm)
constexpr float SAG_BIAS_CLAMP_COLD_N = VS::v(-3.0f);  // -0.01 normalized
constexpr float SAG_BIAS_CLAMP_WARM_N = VS::v(1.5f);   //  0.005 normalized

// ── Phase inverter ──────────────────────────────────────────
constexpr float PI_TAIL_R_N         = VS::R_1K2;
constexpr float PI_RPLATE_A_N       = VS::R_100K;          // non-inv plate load
constexpr float PI_RPLATE_B_N       = VS::r(82000.0f);     // inv plate load (lower → less gain on B side)
constexpr float PI_BIAS_REF_N       = VS::v(-1.5f);
constexpr float PI_CATHODE_INIT_N   = VS::v(1.5f);

// ── Dynamic Miller ──────────────────────────────────────────
constexpr float CGP_12AX7           = 1.7e-12f;
constexpr float CGK_12AX7           = 1.6e-12f;
constexpr float V1A_RSOURCE_REAL    = 10000.0f;
constexpr float V1B_RSOURCE_BASE_REAL = 38500.0f;
constexpr float GAIN_POT_R_REAL     = 500000.0f;
constexpr float V1B_RSOURCE_MAX_REAL = 150000.0f;
constexpr float MILLER_FC_SMOOTH_MS = 1.0f;

// ── Speaker coupling ────────────────────────────────────────
constexpr float SPK_XOVER_LOW       = 200.0f;   // bass/mid crossover (above Fs resonance)
constexpr float SPK_XOVER_HIGH      = 2000.0f;  // mid/HF crossover (before voice coil rise)
constexpr float OT_NOMINAL_LOAD_N   = VS::r(8000.0f);
constexpr float Z_REFLECTED_BASS_N  = VS::r(35000.0f);
constexpr float Z_REFLECTED_MID_N   = VS::r(8000.0f);
constexpr float Z_REFLECTED_HF_N    = VS::r(20000.0f);
constexpr float SPK_ENV_TAU_MS      = 20.0f;   // smooth over several bass cycles (was 2ms → per-cycle ripple)
constexpr float SPK_GAINMOD_DEPTH   = 0.5f;    // keep tanh in useful region (was 2.0 → saturated)
constexpr float SPK_GAINMOD_RANGE   = 0.5f;
constexpr float SPK_MOD_THRESHOLD   = 0.001f;  // envelope level for full coupling engagement
constexpr float SPK_GAINMOD_TAU_MS  = 15.0f;   // gainMod smoothing (speaker cone inertia, was 2ms → per-cycle AM)
constexpr float XCUR_WEIGHT_BASS    = 4.0f;     // (200/100)^2 — bass excursion dominance
constexpr float XCUR_WEIGHT_MID     = 0.10f;    // (200/632)^2 — mid excursion contribution
constexpr float XCUR_WEIGHT_HF      = 0.0025f;  // (200/4000)^2 — HF excursion negligible
constexpr float XCUR_SENSITIVITY    = 2.0f;     // excursion-to-tanh scaling (ear-tune)

// ── OT core saturation ─────────────────────────────────────
constexpr float OT_CURRENT_SCALE    = 0.015f;
constexpr float OT_SAT_DRIVE        = 400.0f;
constexpr float OT_SAT_DC_FREQ      = 20.0f;
// Hard rail on the flux integrator. Steady-state flux for a normalized input
// is ~input/(2π·OT_SAT_DC_FREQ) ≈ 0.008·input, so this clamp is ~3 orders of
// magnitude above normal operation — it never engages musically, but caps a
// pathological DC-biased input before the integrator can drift toward Inf.
constexpr float OT_SAT_FLUX_MAX     = 10.0f;

// ── Gain taper ──────────────────────────────────────────────
constexpr float GAIN_TAPER_EXPONENT = 2.0f;

// ── Effects module (front-of-amp + post-cab) ───────────────
// These live OUTSIDE the oversampled amp loop and never touch the frozen
// amp-tone math. All run at base sample rate.

// Noise gate (downward expander, pre-amp)
constexpr float GATE_THRESH_MIN_DB  = -80.0f;   // param min (gate effectively open)
constexpr float GATE_THRESH_MAX_DB  = -20.0f;   // param max (aggressive)
constexpr float GATE_ATTACK_MS      = 1.0f;     // fast gain open
constexpr float GATE_RELEASE_MS     = 80.0f;    // musical gain close
constexpr float GATE_DETECT_MS      = 30.0f;    // peak-follower release hold
                                                // (envelope decays toward |x|;
                                                //  long enough to hold peak
                                                //  between low-E cycles)
constexpr float GATE_HYSTERESIS_DB  = 4.0f;     // close threshold sits this far below open

// Boost (clean make-up + tight low-cut + bright high shelf, pre-amp)
constexpr float BOOST_LEVEL_MAX_DB  = 18.0f;    // param maps 0..1 → 0..+18 dB
constexpr float BOOST_TIGHT_MIN_HZ  = 20.0f;    // tight=0 → essentially full-range
constexpr float BOOST_TIGHT_MAX_HZ  = 400.0f;   // tight=1 → tight, focused low end
constexpr float BOOST_BRIGHT_FREQ   = 2000.0f;  // bright shelf corner
constexpr float BOOST_BRIGHT_MAX_DB = 9.0f;     // bright=1 → +9 dB above corner

// MONO/STEREO widener (final output stage, post-everything). The knob centre
// (0.5) is unity/bit-exact; below collapses toward mono, above widens and
// injects synthesised decorrelation into the SIDE only (mono sum preserved).
constexpr float WIDTH_HAAS_MS       = 12.0f;  // decorrelation delay (Mid → side voice)
constexpr float WIDTH_MAX_SIDE_GAIN = 2.0f;   // full-wide side boost (knob = 1)
constexpr float WIDTH_SYNTH_MAX     = 0.6f;   // max decorrelated content injected into side
constexpr float WIDTH_ALLPASS_COEFF = 0.7f;   // all-pass diffusion coefficient
constexpr float WIDTH_SMOOTH_MS     = 50.0f;  // knob smoothing (avoids zipper)

// Post-effects safety limiter (catches wet delay/reverb peaks that would
// otherwise escape the amp's output limiter and hard-clip the DAC). Soft knee:
// transparent below the threshold, smoothly bounded to ±1 above it.
constexpr float SAFETY_LIMIT_THRESH = 0.9f;

// Delay (post-cab)
constexpr float DELAY_TIME_MIN_MS   = 20.0f;
constexpr float DELAY_TIME_MAX_MS   = 1000.0f;
constexpr float DELAY_MAX_FEEDBACK  = 0.95f;    // clamp to keep it from self-oscillating
// Analog/tape voicing — feedback-path tone shaping + wow/flutter modulation.
// The high-cut sits IN the feedback loop, so each repeat is darkened again →
// echoes fade toward the dark, like a tape/analog echo. TONE convention
// matches the reverb: 0 = dark, 1 = bright. The low-cut stops bass buildup.
constexpr float DELAY_TONE_LP_MIN   = 2000.0f;  // TONE=0 → dark, tape-like repeats
constexpr float DELAY_TONE_LP_MAX   = 16000.0f; // TONE=1 → bright, near-digital repeats
constexpr float DELAY_LOOP_HP_HZ    = 120.0f;   // fixed feedback low-cut (no bass mud on repeats)
constexpr float DELAY_WOW_RATE_HZ      = 0.4f;  // slow wow LFO rate
constexpr float DELAY_WOW_DEPTH_MS     = 0.3f;  // ± tap sway for living tape character
constexpr float DELAY_FLUTTER_RATE_HZ  = 6.5f;  // faster, subtler flutter LFO rate
constexpr float DELAY_FLUTTER_DEPTH_MS = 0.06f; // ± tap sway (adds high-frequency "buzz" wobble)
// Feedback-loop saturation — soft-clips the tone-shaped repeat before it's
// heard and fed back (models what a tape head/BBD chip actually plays back).
// Unity gain at low level; tanh(x*k)/tanh(k) keeps it transparent until hot.
constexpr float DELAY_FB_SAT_DRIVE  = 1.8f;

// Reverb (post-cab) — spring tank: dispersive all-pass chain in a damped
// feedback delay loop (published parametric-spring technique; see SpringReverb.h)
constexpr int   SPRING_NUM           = 2;       // parallel detuned springs per channel
constexpr int   SPRING_DISP_STAGES   = 100;     // 1st-order all-passes → chirp/dispersion
constexpr float SPRING_DISP_COEFF    = 0.62f;   // all-pass coefficient (|a|<1)
constexpr float SPRING_LOOP1_MS      = 28.0f;   // base loop length, spring 1
constexpr float SPRING_LOOP2_MS      = 37.0f;   // base loop length, spring 2 (mutually prime-ish)
constexpr float SPRING_LEN_SCALE_MIN = 0.75f;   // SIZE=0 → shorter tank
constexpr float SPRING_LEN_SCALE_MAX = 1.30f;   // SIZE=1 → longer tank
constexpr float SPRING_FB_MIN        = 0.70f;   // SIZE=0 → shorter decay
constexpr float SPRING_FB_MAX        = 0.935f;  // SIZE=1 → long, lush decay (capped < 1)
constexpr float SPRING_TONE_LP_MIN   = 1600.0f; // TONE=0 → dark loop LP cutoff
constexpr float SPRING_TONE_LP_MAX   = 6000.0f; // TONE=1 → bright (springs stay capped)
constexpr float SPRING_LOOP_HP_HZ    = 90.0f;   // loop high-pass: kill rumble / DC buildup
constexpr float SPRING_MAX_LOOP_MS   = 60.0f;   // ring-buffer sizing headroom (loop2 × max scale)
// Delay-modulation LFO (per spring) — subtle chorus-like wobble for a living tank
constexpr float SPRING_MOD_DEPTH_SAMP = 2.0f;   // ± samples of loop-length sway
constexpr float SPRING_MOD_RATE1_HZ   = 0.7f;   // spring 1 LFO rate
constexpr float SPRING_MOD_RATE2_HZ   = 1.1f;   // spring 2 LFO rate (decorrelated)

// ── Debug assertions ────────────────────────────────────────
#ifdef DEBUG
#define VS_ASSERT_RANGE(val, lo, hi, stage) \
    jassert((val) >= (lo) && (val) <= (hi) && "Signal out of expected range at " stage)
#else
#define VS_ASSERT_RANGE(val, lo, hi, stage) ((void)0)
#endif

} // namespace sr
