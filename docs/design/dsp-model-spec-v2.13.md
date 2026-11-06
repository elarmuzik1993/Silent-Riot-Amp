# Silent Riot (AD5) — DSP Amp Model Specification

> **Status: design rationale, not authoritative (2026-10-23).**
> This document has not been maintained since 2026-03. Its reasoning (circuit overview,
> normalized voltage domain, numerical hygiene, Class A/AB discussion, the inertia system) is
> still useful background. Its **constants and many function bodies are out of date**: 11 shared
> constants have different values in `src/utils/Constants.h`, 82 of its constants no longer
> exist, and 109 constants in the code are not in it. Where this spec and the code disagree,
> **the code, `CLAUDE.md` and `docs/records/handoffs/handoff-*.md` win**. Do not change code to match this spec.
>
> Superseded designs. Several were root-caused as bugs; see
> `docs/records/handoffs/handoff-power-section-calibration.md` and `docs/records/handoffs/handoff-ltp-convergence.md`:
>
> | Area | This spec | Current code |
> |---|---|---|
> | LTP solver (§3.5) | One-iteration Newton / damped relaxation | Bracketed root-find (secant + bisection). The relaxation limit-cycled into a Nyquist-rate square wave |
> | Gain pot (§3.3) | `audioTaper()` on both gangs | Gang A tapered, Gang B linear, plus `gainDriveTaper()`. Both gangs gave an x⁴ law |
> | Grid conduction (§4.1.1) | Resistive divider above threshold | Logarithmic diode clamp. The divider never clamped (121 V on the PI grid) |
> | EL84 grid threshold | 0 V | 8.6 V. 0 V caused motorboating at gain ≥ 9 on silence |
> | Sag (§3.8) | `−SAG_DEPTH × envelope` | Cathode-resistor physics, `−(I − I_idle) × R_k`, idle from `pentodeQuiescentTotal()` |
> | Tone (§3.6) | Vox-style post-PI cut | Orange Shape-style 3-band EQ on the differential signal |
> | Master volume (§3.7) | `audioTaper(volume)` | `volumeDriveTaper()` with `VOLUME_KNOB_HEADROOM`, plus `OUTPUT_MAKEUP_GAIN` |
> | Power stage additions | — | OT flux saturation, speaker-coupling crossover, effects, I/O trims (none in this spec) |

> **Provenance.** This spec was written from general knowledge of common tube-amp circuits, not
> from a Tiny Terror schematic or any manufacturer document. Textbook and datasheet values are
> separated from unverified guesses in `attribution/CREDITS.md`. References here to "the Tiny
> Terror" or "the original amp" describe the amp this design is modelled on; they are not
> measurements of one.

## Monolit Beatz — Amp Sim Plugin v1.0

---

```
═══════════════════════════════════════════════════════════════════
HOW TO READ THIS DOCUMENT
═══════════════════════════════════════════════════════════════════

  This spec blends five types of content within each section. When
  implementing, focus on the MATH MODEL and IMPLEMENTATION sections.
  When debugging unexpected behavior, read the CIRCUIT CONTEXT first.

  The five content types, and how to identify them:

  ── CIRCUIT CONTEXT (what the real hardware does) ──────────────
  Identified by: component names (ECC83, EL84, coupling cap),
  real-world units (volts, ohms, farads), circuit topology
  descriptions, and explanatory text about analog behavior.
  Purpose: Explains WHY the DSP model works the way it does.
  When to read: Before implementing a stage for the first time,
  or when the model produces unexpected behavior and you need to
  understand the physics. Skip when you already understand the
  stage and just need the code.

  ── DESIGN INTENT (what the model should achieve) ─────────────
  Identified by: "should sound like", "the goal is", sonic
  descriptions (warm, compressed, bloom, sag), comparison to
  other amps, and sections titled "WHY" or "WHAT THIS MEANS".
  Purpose: Defines the target behavior that the math must produce.
  When to read: When tuning parameters or A/B testing. This tells
  you what "correct" sounds like.

  ── MATH MODEL (the equations and algorithms) ─────────────────
  Identified by: function signatures, equations, normalized domain
  values, transfer curves, and variable names matching Constants.h.
  Purpose: The authoritative definition of what the code computes.
  When to read: During implementation. This is what you type.
  IMPORTANT: When the math model and circuit context appear to
  conflict, the math model wins — it may intentionally simplify
  the circuit for DSP efficiency.

  ── IMPLEMENTATION (code, structs, processBlock wiring) ───────
  Identified by: C++ code blocks, struct definitions, function
  bodies, processBlock pseudocode, and references to JUCE API.
  Purpose: Copy-paste starting points and reference implementations.
  When to read: During coding. Cross-reference with Constants.h
  and the signal chain in Section 2.5.

  ── KNOWN TRADEOFFS (what was simplified and why) ─────────────
  Identified by: "simplified", "approximation", "future work",
  "Phase 3/4", "optional", and MVP/upgrade labels.
  Purpose: Documents what's intentionally missing or approximate.
  When to read: When deciding whether a discrepancy is a bug or
  a known simplification. Also useful for planning future work.

  Within each section (e.g., 3.8 Power Amp), these content types
  appear in roughly this order:
    1. Circuit context (what the real amp does)
    2. Design intent (what it should sound like)
    3. Math model (the equations)
    4. Implementation (the code)
    5. Known tradeoffs (what's simplified)

  Not every section has all five. Some (like VoltageScale) are
  pure math with no circuit context needed. Others (like the PSU
  model) are heavy on circuit context because the droop behavior
  requires understanding the RC filter cascade.

  CROSS-REFERENCE GUIDE:
    Section 2.5  → Operating levels table (expected signal ranges)
    Section 4.x  → Waveshaper math (triode, pentode, grid conduction)
    Section 8.0  → Build order and dependency chain
    Section 10   → All DSP constants with real-world derivations
    processBlock → Section 2.5 pseudocode (the master signal chain)
```

---

```
═══════════════════════════════════════════════════════════════════
STRUCTURED REFERENCE — QUICK LOOKUP FOR AI AND DEVELOPER
═══════════════════════════════════════════════════════════════════

SIGNAL CHAIN (order of processing, per sample, inside oversampled loop):

  Input (DAW ±1.0)
    → Scale to guitar domain (× VS::V_GUITAR)
    → HP filter (7Hz coupling cap)
    → Grid conduction (V1a — mild, hot pickups only)
    → Triode waveshaper (V1a, drive=70, supply=preampNode)
    → Clamp (supply × HEADROOM_FACTOR)
    → LP filter (4.2Hz cathode bypass)
    → HP filter (72Hz coupling cap)
    → Dynamic Miller LP (V1a, gain-dependent fc)
    → Report preamp current to PSU
    → Gain pot gang A (audio taper)
    → Grid conduction (V1b — aggressive at high gain)
    → Triode waveshaper (V1b, drive=70, supply=preampNode)
    → Clamp (supply × HEADROOM_FACTOR)
    → LP filter (4.2Hz cathode bypass)
    → HP filter (7Hz coupling cap)
    → Dynamic Miller LP (V1b, Rsource modulated by gain pot)
    → Report preamp current to PSU
    → Gain pot gang B (audio taper)
    → LP filter (5kHz gain loading)
    → Grid conduction (PI input)
    → LTP nodal solver (shared cathode, differential outputs)
    → HP filter ×2 (16Hz PI coupling caps)
    → Report PI current to PSU
    → Tone control (Vox cut, differential-only, preserves common-mode)
    → Master volume (audio taper)
    → Grid conduction ×2 (EL84 grids)
    → Soft conduction curve ×2 (Class A/AB crossover)
    → Pentode waveshaper ×2 (screen-aware, supply from powerNode)
    → Push-pull sum (A×condA − B×condB)
    → Report plate + screen current to PSU
    → Sag envelope update
    → Speaker coupling (frequency-dependent impedance → gain modulation)
    → LP filter (7kHz OT leakage)
    → Cabinet sim (presence peak, rolloff, notch — if enabled)
    → Output scaling (× outputGainCompensation)
    → Soft limiter (tanh)
  Output (DAW ±1.0)

NONLINEAR BLOCKS:

  Triode waveshaper (§4.1)
    - Asymmetric tanh (Option A, MVP)
    - Input: x = vGrid × stageGain + vBias (bias is negative)
    - Positive: tanh(x), Negative: tanh(x×1.5) / 1.5
    - Output scaled by vSupply, clamped at supply × 0.85
    - Supply-aware: PSU droop reduces output swing

  Triode current model (§3.5)
    - 3/2 power law: Ip = K × softRect(Vgk)^1.5
    - K_12AX7 = 3394 (recalibrated v2.12 with cutoff offset)
    - Used by LTP solver only (not the preamp waveshaper)

  Pentode waveshaper (§4.2)
    - Screen-aware Koren-style
    - vg1 = vGrid + vBias (addition — bias is negative)
    - Screen influence: vEffective = vg1 + vScreen/mu_g2
    - Screen gain factor: vScreen / V_SCREEN_NOMINAL
    - Power law: (driveScaled)^1.35 / EL84_KG
    - Plate saturation: 1 − exp(−vPlate / V_PLATE_KNEE)
    - Screen redistribution: 25% nominal → 40% at plate starvation
    - Returns {plateCurrent, screenCurrent} for PSU reporting

  Grid conduction (§4.1.1)
    - Per-stage models: V1a, V1b, PI, EL84×2
    - Above threshold: impedance loading (voltage divider)
    - Coupling cap charge accumulation (bias shift)
    - Exponential charge decay (stage-specific tau: 22–100ms)

  Soft conduction curve (§3.8)
    - Smooth Class A/AB transition per EL84
    - Overlap width = vBias × 0.5 (bias-dependent)
    - 7W: wider overlap → more Class A
    - 15W: narrower overlap → subtle AB crossover

  Dynamic Miller LP (§3.3)
    - Per-sample fc from: Cgp × (1 + Av) + Cgk
    - V1b: Rsource modulated by gain pot (38.5K → 538.5K)
    - Gain-dependent treble rolloff

SUPPLY / DYNAMICS:

  PSU model (§3.11)
    - 4 nodes: power (tau=10ms), screen (tau=6ms), PI (tau=35ms),
      preamp (tau=80ms)
    - Cascaded: rectifier → powerNode → screenNode / piNode → preampNode
    - Current reports from previous sample (causal, one-sample delay)
    - Each node: exponential tracking toward target voltage
    - Droop: current draw × impedance reduces target voltage

  Sag envelope (§3.8)
    - Cathode bias sag, separate from PSU droop
    - Attack: 10ms, Release: 100ms
    - biasOffset() returns negative (shifts bias colder under load)
    - Depth: 15% of envelope value

  Speaker coupling (§3.9)
    - 3-band impedance estimation (bass/mid/HF)
    - Reflected impedance modulates pentode gain
    - Bass resonance (75Hz): 35KΩ primary → gain boost
    - Mid minimum (400Hz–1kHz): 8KΩ nominal
    - HF rise (voice coil): 20KΩ → presence peak
    - Gain mod: 1.0 + tanh((zRatio−1) × 2) × 0.5

LINEAR BLOCKS (24 filters total):

  OnePole HP: input (7Hz), V1a coupling (72Hz), V1b coupling (7Hz),
              PI coupling ×2 (16Hz)
  OnePole LP: V1a cathode bypass (4.2Hz), V1b cathode bypass (4.2Hz),
              gain loading (5kHz), OT leakage (7kHz), tone cut (variable),
              HF envelope (3kHz for speaker coupling)
  Biquad:     cab presence (2.5kHz peak), cab rolloff (5.5kHz LP),
              cab notch (400Hz), bass extract (75Hz peak),
              mid extract (500Hz peak)
  DynamicMiller ×2: V1a (10K source), V1b (38.5K+ source)

OVERSAMPLING:

  Strategy: JUCE dsp::Oversampling, polyphase FIR
  Modes: 2× / 4× (default) / 8×
  State machine: Idle → FadingOut → Reconfiguring → FadingIn
  Fade length: 128 samples (~3ms at 44.1kHz)
  Pre-allocated: 3 instances, swap active pointer during reconfigure
  PSU frozen during reconfiguration (prevents voltage recovery pop)
  All filters, smoothers, grid conduction recalculated on mode change

NORMALIZED VOLTAGE DOMAIN:

  V_REF = 300V → 1.0
  R_REF = 300KΩ → 1.0
  I_REF = 1mA → 1.0
  Ohm's law preserved: V_n = I_n × R_n
  ALL processing in normalized domain — no raw real-world values
  VoltageScale: v(volts), r(ohms), i(amps) → normalized
  Stage gains are dimensionless (70×, 17×) — not normalized

ASSUMPTIONS:

  - Input is mono guitar signal (DAW delivers ±1.0 stereo, we process ch0)
  - Guitar pickup output ≈ 0.5V peak (hot humbuckers)
  - 12AX7 plate resistance ≈ 62.5KΩ at operating point
  - 12AX7 mu ≈ 100 (used in LTP solver)
  - K_12AX7 = 3394 calibrated to Ip≈1.2mA at Vgk=−1.5V with cutoff offset (v2.12)
  - EL84 screen mu ≈ 12 (screen-to-grid amplification factor)
  - EL84 transfer exponent = 1.35
  - Plate knee voltage ≈ 40V (pentode → triode transition)
  - Output transformer turns ratio² ≈ 1000 (8KΩ:8Ω)
  - Celestion G12H-30 speaker: fs=75Hz, Re=6Ω, Qts≈3.5
  - Cathode bypass caps set low-frequency rolloff at 4.2Hz
  - No negative feedback loop in the circuit

CONSTRAINTS:

  - Target: real-time at 4× oversampling, 44.1kHz, 256-sample buffer
  - Anti-denormal: ANTI_DENORMAL (1e-18f) in all IIR state,
    snap-to-zero on all envelopes, ScopedNoDenormals per processBlock
  - No dynamic allocation in processBlock (all buffers pre-allocated)
  - All oversamplers pre-allocated (no new/delete on audio thread)
  - Filter coefficients: recalcAll on SR change, updateModulated per block
  - Parameter smoothing: 5ms exponential, block-rate (not sample-rate)

KNOWN TRADEOFFS:

  - Triode uses asymmetric tanh (Option A), not Koren (Option B)
    -> Simpler, cheaper, less accurate harmonics. Upgrade path exists.
  - LTP solver uses one-iteration Newton, not full convergence
    -> ~1% error per sample, inaudible at audio rates
  - Grid conduction is MVP model (impedance loading + cap charge only)
    -> Missing: blocking distortion, HF loss -- deferred to Phase 4
  - PSU droop calculation is stubbed (targets track to nominal)
    -> No sag compression until update() implements droop from currents
  - Cabinet sim is IIR only -- no IR convolution yet (Phase 4)
  - Oversampling mode switch state machine not wired into processBlock
  - outputGainCompensation hardcoded to 2.0 -- needs tuning
  - No preset system, no A/B comparison tools (Phase 4)
  - No noise floor or mains hum -- model is silent at zero input (Phase 4)
  - Phase shift is automatic from IIR filters but not validated
    against real Silent Riot impulse response (Phase 4)

INERTIA SYSTEM (five layers that create "tube feel"):

  Layer 1: Filter state inertia      us-ms       IIR z1/z2 phase shift       IMPLEMENTED
  Layer 2: Sag envelope inertia      10-200ms    Cathode cap bias shift       IMPLEMENTED
  Layer 3: Grid conduction inertia   22-100ms    Coupling cap charge drift    IMPLEMENTED (MVP)
  Layer 4: PSU droop inertia         6-80ms      Cascaded RC supply sag       STUBBED (Phase 3)
  Layer 5: Speaker coupling inertia  2ms         Impedance gain modulation    IMPLEMENTED

  These layers interact: sag -> grid threshold, droop -> waveshaper ceiling,
  grid charge -> plate current -> PSU load -> droop (feedback loop).
  Phase 3 wiring completes this loop. See Section 9.2 for full details.

GOALS (per phase):

  Phase 1: Preamp distortion character correct, internal levels match
           spec operating table, VoltageScale + filters + triode verified
  Phase 2: Full amp tone clean to cranked — LTP, pentode, push-pull,
           tone/volume all producing correct differential signal
  Phase 3: Amp feels responsive and dynamic — PSU droop creates
           compression, sag creates bloom, self-limiting feedback loop works
  Phase 4: Release-ready — cabinet IR, GUI polish, presets, optimization
```

---

```
═══════════════════════════════════════════════════════════════════
FUNCTION CONTRACTS — AUTHORITATIVE INTERFACE REFERENCE
═══════════════════════════════════════════════════════════════════

  Each contract is the SINGLE SOURCE OF TRUTH for that function's
  interface, behavior, and wiring. When implementing or modifying
  a function, read its contract FIRST, then the detailed spec section
  only if you need deeper understanding.

  Format:
    FUNCTION: name
      FILE:       source location
      INPUTS:     parameter name (type, range, notes)
      OUTPUT:     return type and range
      FORMULA:    the math, exactly as implemented
      INVARIANTS: properties that must always hold (test these)
      CALLS FROM: where in processBlock this function is invoked
      DEPENDS ON: constants and other functions used internally
      TEST FILE:  which test file verifies this function

──────────────────────────────────────────────────────────────────
NONLINEAR: TRIODE WAVESHAPER
──────────────────────────────────────────────────────────────────

  FUNCTION: triodeWaveshaper
    FILE:       dsp/tubes/TriodeWaveshaper.cpp
    SPEC:       §4.1 Option A
    INPUTS:     vGrid_n    (float, normalized grid voltage, ±0.01 typical at V1a)
                vBias_n    (float, NEGATIVE: VS::v(-1.5f) = -0.005)
                vSupply_n  (float, from preampNode, nominal 0.833)
                stageGain  (float, V1A_GAIN=70 or V1B_GAIN=70 — THIS IS the drive)
    OUTPUT:     float — plate voltage swing, clamped to ±(vSupply_n × 0.85)
    FORMULA:    x = vGrid_n × stageGain + vBias_n
                if x ≥ 0: shaped = tanh(x)
                if x < 0: shaped = tanh(x × 1.5) / 1.5
                output = clamp(shaped × vSupply_n, ±vSupply_n × HEADROOM_FACTOR)
    INVARIANTS: |output| ≤ vSupply_n × HEADROOM_FACTOR (always)
                output(0, bias, supply, gain) ≈ 0 (near-zero at quiescent)
                |output(-x)| < |output(+x)| for same |x| (asymmetry)
                lower vSupply_n → lower |output| (PSU sensitivity)
    CALLS FROM: processBlock ×2 (V1a with V1A_GAIN, V1b with V1B_GAIN)
    DEPENDS ON: HEADROOM_FACTOR (0.85), TRIODE_ASYM_FACTOR (1.5)
    TEST FILE:  triode_tests.cpp
    PITFALLS:   Do NOT create separate TRIODE_DRIVE constant.
                Do NOT subtract vBias — it is already negative, use +.
                Do NOT scale output by stageGain — gain is on INPUT only.

──────────────────────────────────────────────────────────────────
NONLINEAR: TRIODE CURRENT MODEL
──────────────────────────────────────────────────────────────────

  FUNCTION: triodeCurrentModel
    FILE:       dsp/tubes/TriodeWaveshaper.cpp
    SPEC:       §3.5 (triode current for LTP solver)
    INPUTS:     vgk_n  (float, grid-to-cathode voltage, normalized)
                mu     (float, unused in current impl — placeholder)
                rp_n   (float, unused in current impl — placeholder)
    OUTPUT:     float — plate current, normalized, ≥ 0
    FORMULA:    v2 = vgk² + V_THERMAL²
                softVgk = 0.5 × (vgk + √v2)      [smooth rectifier]
                ip = K_12AX7 × softVgk × √|softVgk|  [3/2 power law]
                output = max(0, ip)
    INVARIANTS: output ≥ 0 (always)
                output ≈ 0 for vgk_n << 0 (deep cutoff)
                output monotonically increasing with vgk_n
    CALIBRATION: At vgk = VS::v(-1.5f), should produce ip ≈ VS::i(0.0012f)
                (12AX7 operating point: Vgk=-1.5V → Ip≈1.2mA)
    CALLS FROM: solveLTP ×2 (once per LTP triode)
    DEPENDS ON: K_12AX7 (3394), V_CUTOFF_12AX7_N (VS::v(-3.0) = -0.01), VS::V_THERMAL
    TEST FILE:  triode_tests.cpp

──────────────────────────────────────────────────────────────────
NONLINEAR: PENTODE WAVESHAPER
──────────────────────────────────────────────────────────────────

  FUNCTION: pentodeWaveshaper
    FILE:       dsp/tubes/PentodeWaveshaper.cpp
    SPEC:       §4.2
    INPUTS:     vGrid_n    (float, from PI output after tone/volume)
                vBias_n    (float, NEGATIVE: -0.032 for 15W, -0.023 for 7W)
                vPlate_n   (float, from powerNode, nominal 1.0)
                vScreen_n  (float, from screenNode, nominal 0.933)
    OUTPUT:     PentodeOutput { plateCurrent (≥0), screenCurrent (≥0) }
    FORMULA:    vg1 = vGrid_n + vBias_n                    [addition, not subtraction]
                vEffective = vg1 + vScreen_n / MU_SCREEN
                screenGainFactor = vScreen_n / V_SCREEN_NOMINAL_N
                driveScaled = vEffective × screenGainFactor
                if driveScaled > 0: ip = driveScaled^1.35 / EL84_KG
                else: ip = 0
                plateLimit = 1 − exp(−vPlate_n / V_PLATE_KNEE_N)
                ip × = plateLimit
                screenRatio = 0.25 + (1 − plateLimit) × 0.15
                iScreen = ip × screenRatio
    INVARIANTS: plateCurrent ≥ 0, screenCurrent ≥ 0
                lower vScreen → lower plateCurrent (gain reduction)
                lower vPlate → lower plateCurrent (headroom reduction)
                screenRatio increases at low plate voltage (redistribution)
                deep cutoff (vGrid << 0) → zero current
    CALLS FROM: processBlock ×2 (one per EL84 push-pull tube)
    DEPENDS ON: MU_SCREEN (12), EL84_EXPONENT (1.35), EL84_KG (800),
                V_PLATE_KNEE_N, V_SCREEN_NOMINAL_N, SCREEN_CURRENT_RATIO (0.25),
                SCREEN_REDIST_FACTOR (0.15)
    TEST FILE:  pentode_tests.cpp
    PITFALLS:   Do NOT subtract vBias — use +. Bias is already negative.
                Do NOT use tanh for plate saturation — use 1−exp(−x).

──────────────────────────────────────────────────────────────────
NONLINEAR: SOFT CONDUCTION CURVE (CLASS A/AB CROSSOVER)
──────────────────────────────────────────────────────────────────

  FUNCTION: softConductionCurve
    FILE:       dsp/tubes/PentodeWaveshaper.cpp
    SPEC:       §3.8
    INPUTS:     vGrid_n   (float, EL84 grid voltage from PI)
                vBias_n   (float, NEGATIVE: -0.032 for 15W, -0.023 for 7W)
    OUTPUT:     float — conduction fraction, range [0, 1]
                0 = tube fully cut off, 1 = tube fully conducting
    FORMULA:    cutoffMargin = vGrid_n − EL84_CUTOFF_N
                overlapWidth = vBias_n × 0.5       [negative → negative width]
                output = 0.5 × (1 + tanh(cutoffMargin / overlapWidth))
    INVARIANTS: output ∈ [0, 1]
                output ≈ 1 for grid well above cutoff
                output ≈ 0 for grid well below cutoff
                monotonically increasing with vGrid_n
                7W (more negative bias) → wider overlap → more Class A
    CALLS FROM: processBlock ×2 (one per EL84), BEFORE pentodeWaveshaper
    DEPENDS ON: EL84_CUTOFF_N (VS::v(-10.0f) ≈ -0.0333)
    TEST FILE:  pentode_tests.cpp
    PITFALLS:   Do NOT use fixed scaling factor (e.g. 3.0f) — must be
                bias-dependent to differentiate 7W/15W character.

──────────────────────────────────────────────────────────────────
NONLINEAR: GRID CONDUCTION
──────────────────────────────────────────────────────────────────

  FUNCTION: applyGridConduction
    FILE:       dsp/tubes/GridConduction.cpp
    SPEC:       §4.1.1 MVP model
    INPUTS:     vGrid   (float, grid voltage arriving at this stage)
                model   (GridConductionModel&, mutable state)
    OUTPUT:     float — grid voltage after loading and bias shift
    FORMULA:    if vGrid ≤ threshold:
                  charge decays, output = vGrid − charge
                if vGrid > threshold:
                  conductionDepth = tanh(vAbove × 3)
                  effectiveGridR = gridR / conductionDepth
                  loadingFactor = gridR / (gridR + sourceR)
                  vLoaded = threshold + vAbove × loadingFactor
                  charge += gridCurrent × GRID_CHARGE_RATE
                  charge × = chargeDecay
                  output = vLoaded − charge
    STATE:      model.couplingCapCharge (persistent, decays exponentially)
    INVARIANTS: below threshold with zero charge: output == input
                above threshold: output < input (positive peaks attenuated)
                charge accumulates under sustained drive
                charge decays toward zero during silence
                snap-to-zero at < 1e-10 (anti-denormal)
    CALLS FROM: processBlock ×5 (V1a, V1b, PI, EL84a, EL84b)
    FACTORY:    makeV1aGrid()  — threshold=VS::v(0.5), sourceR=100Ω
                makeV1bGrid()  — threshold=VS::v(0.5), sourceR=Z_OUT_V1A
                makePIGrid()   — threshold=VS::v(0.5), sourceR=Z_OUT_V1B
                makeEL84Grid() — threshold=VS::v(0.0), sourceR=Z_OUT_PI
    RECALC:     recalcGridChargeDecay(model, tauSec, effectiveSR)
                  V1a/V1b tau = 0.022s, PI tau = 0.047s, EL84 tau = 0.100s
    DEPENDS ON: GRID_CONDUCT_THRESH_N, GRID_R_TRIODE_N, GRID_R_PENTODE_N,
                GRID_CHARGE_RATE (0.01), VS::Z_OUT_V1A/V1B/PI
    TEST FILE:  grid_conduction_tests.cpp

──────────────────────────────────────────────────────────────────
NONLINEAR: LTP SOLVER (PHASE INVERTER)
──────────────────────────────────────────────────────────────────

  FUNCTION: solveLTP
    FILE:       dsp/power/LTPSolver.cpp
    SPEC:       §3.5
    INPUTS:     vInput_n  (float, AC signal to LTP input triode)
                vSupply_n (float, from piNode, nominal 0.933)
                ltp       (LTPModel&, mutable state)
    OUTPUT:     LTPOutput { piA, piB, vCathode }
                piA = non-inverting plate output (AC, centered at 0)
                piB = inverting plate output (AC, centered at 0)
    FORMULA:    vGridA = vBiasRef + vInput_n   [signal grid]
                vGridB = vBiasRef               [reference grid]
                vgkA = vGridA − vCathode
                vgkB = vGridB − vCathode
                ipA = triodeCurrentModel(vgkA)
                ipB = triodeCurrentModel(vgkB)
                vCathode_new = (ipA + ipB) × tailR_n
                vPlateA = vSupply − ipA × rPlateA
                vPlateB = vSupply − ipB × rPlateB
                clamp plates to [vCathode, vSupply]
                piA = vPlateA − vSupply×0.5    [AC extraction]
                piB = vPlateB − vSupply×0.5
    STATE:      ltp.vCathode (persistent, one-iteration Newton)
    INVARIANTS: zero input → piA ≈ piB (symmetric within 0.01)
                positive input → piA decreases, piB increases (phase inversion)
                vCathode rises under drive (current steering)
                plates bounded by [vCathode, vSupply]
                natural gain imbalance emerges at high drive
    INIT:       LTPModel::init() → tailR=R_1K2, rPlate=R_100K, bias=VS::v(-1.5),
                cathode=VS::v(1.5), mu=100, rp=VS::r(62500)
    CALLS FROM: processBlock ×1 (after gain pot B, before tone)
    DEPENDS ON: triodeCurrentModel, PI_TAIL_R_N, PI_RPLATE_N, PI_BIAS_REF_N
    TEST FILE:  ltp_tests.cpp

──────────────────────────────────────────────────────────────────
DYNAMICS: POWER SUPPLY MODEL
──────────────────────────────────────────────────────────────────

  CLASS: PowerSupplyModel
    FILE:       dsp/power/PowerSupplyModel.h/.cpp
    SPEC:       §3.11

    INIT:       init() → 4 nodes at nominal:
                  powerNode  = 1.000 (300V, tau=10ms)
                  screenNode = 0.933 (280V, tau=6ms)
                  piNode     = 0.933 (280V, tau=35ms)
                  preampNode = 0.833 (250V, tau=80ms)

    RECALC:     recalcDecayCoeffs(effectiveSR)
                  Must call on: prepareToPlay, oversampling mode change

    UPDATE:     update() — called once per sample, BEFORE processing stages
                  Currently STUBBED: targets track to nominal (no droop)
                  Phase 3: compute droop from current reports:
                    vRect = nominal − totalDraw × sourceImpedance
                    powerNode.track(vRect)
                    screenTarget = powerNode.voltage − screenDraw × R_screen
                    piTarget = powerNode.voltage − piDraw × R_drop1
                    preampTarget = piNode.voltage − preampDraw × R_drop2

    REPORTING:  resetCurrentDraw()         — start of each sample
                reportPreampLevel(signal)  — pass SIGNAL not current (divides by R_100K internally)
                reportPiLevel(signal)      — pass SIGNAL not current
                reportPowerDirect(plate, screen) — pass currents from PentodeOutput

    ACCESSORS:  vPower(), vScreen(), vPi(), vPreamp() → current node voltages

    INVARIANTS: all node voltages > 0 (supply never goes negative)
                after init, all nodes at nominal
                without current reports, nodes stay at nominal
    DEPENDS ON: V_POWER/SCREEN/PI/PREAMP_NOMINAL_N, TAU_*_MS,
                SOURCE_IMPEDANCE_N, R_DROP1/2_N, R_SCREEN_N
    TEST FILE:  psu_sag_tests.cpp
    PITFALLS:   reportPreampLevel takes signal level, NOT current.

  STRUCT: SupplyNode
    FORMULA:    track(target): voltage = target + (voltage − target) × decay
                recalcDecay(SR): decay = exp(−1 / (tau_ms × 0.001 × SR))
    INVARIANTS: converges to target when called repeatedly with same target
                snap-to-zero when |voltage − target| < 1e-10

──────────────────────────────────────────────────────────────────
DYNAMICS: SAG ENVELOPE
──────────────────────────────────────────────────────────────────

  STRUCT: SagEnvelope
    FILE:       dsp/power/SagEnvelope.h
    SPEC:       §3.8

    RECALC:     recalcCoeffs(effectiveSR)
                  attackDecay  = exp(−1 / (10ms × SR))
                  releaseDecay = exp(−1 / (100ms × SR))

    UPDATE:     update(powerOutputLevel) — called AFTER computing powerOut
                  level = |powerOutputLevel|
                  if level > envelope: use attackDecay (fast, 10ms)
                  if level ≤ envelope: use releaseDecay (slow, 100ms)
                  envelope = level + (envelope − level) × coeff
                  snap-to-zero at < 1e-10

    BIAS:       biasOffset() → returns −envelope × SAG_DEPTH
                  ALWAYS NEGATIVE (shifts bias colder under load)
                  Usage: effectiveBias = el84BaseBias + sag.biasOffset()
                  Do NOT use += on bias across samples — recompute fresh each sample

    INVARIANTS: envelope ≥ 0
                biasOffset() ≤ 0
                attack faster than release
                returns 0 when no signal (envelope decays to 0)
    DEPENDS ON: SAG_ATTACK_MS (10), SAG_RELEASE_MS (100), SAG_DEPTH (0.15)
    TEST FILE:  psu_sag_tests.cpp

──────────────────────────────────────────────────────────────────
DYNAMICS: SPEAKER COUPLING
──────────────────────────────────────────────────────────────────

  STRUCT: SpeakerCoupling
    FILE:       dsp/cabinet/SpeakerCoupling.h/.cpp
    SPEC:       §3.9

    RECALC:     recalcFilters(effectiveSR)
                  bassExtract: peakEQ at 75Hz
                  midExtract: peakEQ at 500Hz
                  hfEnvelope: LP at 3kHz
                  envDecay: exp(−1 / (2ms × SR))

    PROCESS:    process(powerOut) → float (gain-modulated output)
                  Extract 3 bands → envelope follow each →
                  weighted sum of reflected impedances →
                  gainMod = 1.0 + tanh((zRatio−1) × 2.0) × 0.5
                  output = powerOut × gainMod

    INVARIANTS: process(0) = 0
                gainMod range ≈ [0.5, 1.5]
                bass-heavy signal → higher gain (resonance impedance)
                mid-only signal → near-unity gain
    DEPENDS ON: SPK_FS, Z_REFLECTED_BASS/MID/HF_N, OT_NOMINAL_LOAD_N,
                SPK_GAINMOD_DEPTH (2.0), SPK_GAINMOD_RANGE (0.5)
    TEST FILE:  speaker_tests.cpp

──────────────────────────────────────────────────────────────────
LINEAR: ONEPOLE LP / HP
──────────────────────────────────────────────────────────────────

  STRUCT: OnePoleLP / OnePoleHP
    FILE:       dsp/filters/OnePole.h
    SPEC:       §2.6.2

    DESIGN:     setFreq(fc, SR) — BLT with tan() pre-warping (accurate at all fc)
                setFreqFast(fc, SR) — linear approximation (fast, accurate fc < SR/6)
    PROCESS:    process(x) → float
                  y = x×b0 + z1; z1 = x×b1 − y×a1 + ANTI_DENORMAL
    RESET:      reset() → z1 = 0
    STATE:      z1 (persistent per filter instance)

    INVARIANTS: LP: passes DC (magnitude=1.0), attenuates above fc
                HP: blocks DC (magnitude→0), passes above fc
                Both: −3dB at cutoff frequency (magnitude ≈ 0.707)
                z1 never becomes denormal (ANTI_DENORMAL injection)
    INSTANCES:  inputHP (7Hz), v1a_cathodeBypass (4.2Hz LP),
                v1a_couplingHP (72Hz), v1b_cathodeBypass (4.2Hz LP),
                v1b_couplingHP (7Hz), pi_couplingHP_A/B (16Hz),
                toneCutFilter (1.5k–20kHz variable LP),
                gainLoadingLP (5kHz LP)
    TEST FILE:  filter_tests.cpp
    PITFALLS:   Use setFreq for recalcAll (accurate). Use setFreqFast only
                for per-sample modulation (DynamicMillerLP).

──────────────────────────────────────────────────────────────────
LINEAR: BIQUAD (TRANSPOSED DIRECT FORM II)
──────────────────────────────────────────────────────────────────

  STRUCT: Biquad
    FILE:       dsp/filters/Biquad.h
    SPEC:       §2.6.2

    DESIGNS:    makeLowPass(fc, Q, SR)
                makeHighPass(fc, Q, SR)
                makePeakEQ(fc, Q, gainDb, SR)
                makeNotch(fc, Q, SR)
    PROCESS:    process(x) → float
                  y = x×b0 + z1; z1 = x×b1 − y×a1 + z2; z2 = x×b2 − y×a2
    RESET:      reset() → z1 = z2 = 0
    STATE:      z1, z2 (persistent)

    INVARIANTS: LP: passes DC, attenuates above fc (steeper than OnePole)
                HP: blocks DC
                PeakEQ: boosts/cuts at fc, unity away from fc
                Notch: deep null at fc, unity away from fc
                ANTI_DENORMAL on both z1 and z2
    INSTANCES:  ot_leakageLP (7kHz LP, Q=0.707),
                cab_presence (2.5kHz peak), cab_rolloff (5.5kHz LP),
                cab_notch (400Hz notch),
                bassExtract (75Hz peak, in SpeakerCoupling),
                midExtract (500Hz peak, in SpeakerCoupling)
    TEST FILE:  filter_tests.cpp

──────────────────────────────────────────────────────────────────
LINEAR: DYNAMIC MILLER LP
──────────────────────────────────────────────────────────────────

  STRUCT: DynamicMillerLP
    FILE:       dsp/filters/DynamicMillerLP.h
    SPEC:       §3.3

    INIT:       init(cgp, cgk, rSource_real, nominalGain)
                  V1a: cgp=1.7pF, cgk=1.6pF, rSource=10KΩ, gain=70
                  V1b: cgp=1.7pF, cgk=1.6pF, rSource=38.5KΩ, gain=70
    RECALC:     recalc(effectiveSR) → fcSmooth decay coefficient
    PROCESS:    process(x, gainEstimate, effectiveSR) → float
                  cMiller = cgk + cgp × (1 + gainEstimate)
                  targetFc = 1 / (2π × rSource × cMiller)
                  fc smoothed exponentially (1ms tau)
                  apply OnePoleLP at smoothed fc

    INVARIANTS: higher gainEstimate → lower cutoff → more treble rolloff
                V1b: rSource varies with gain pot (38.5K → 538.5K)
    CALLS FROM: processBlock ×2 (after each triode stage)
    DEPENDS ON: CGP_12AX7 (1.7pF), CGK_12AX7 (1.6pF),
                V1A_RSOURCE_REAL, V1B_RSOURCE_BASE_REAL, GAIN_POT_R_REAL
    TEST FILE:  filter_tests.cpp

──────────────────────────────────────────────────────────────────
LINEAR: FILTER BANK
──────────────────────────────────────────────────────────────────

  STRUCT: FilterBank
    FILE:       dsp/filters/FilterBank.h/.cpp
    SPEC:       §2.6.2

    RECALCALL:  recalcAll(baseSampleRate, oversampleFactor)
                  Sets effectiveSR = baseSR × factor
                  Recalculates ALL fixed-frequency filters
                  Also initializes toneCutFilter and gainLoadingLP with defaults
                  Call on: prepareToPlay, oversampling mode change

    MODULATED:  updateModulated(toneFreq, gainLoadFreq)
                  Updates toneCutFilter and gainLoadingLP per block
                  toneFreq: 1500Hz (dark) to 20000Hz (bright)
                  gainLoadFreq: from gainLoadingLP constant

    RESETALL:   resetAll() → clears all filter state (z1, z2)
                  Call on: oversampling mode change, silence detect

    INVARIANTS: after recalcAll, all filters have valid coefficients (b0≠0)
                toneCutFilter and gainLoadingLP initialized in recalcAll
    TEST FILE:  filter_tests.cpp (individual filters),
                signal_chain_tests.cpp (integration)

──────────────────────────────────────────────────────────────────
UTILITY: PARAM SMOOTHER
──────────────────────────────────────────────────────────────────

  STRUCT: ParamSmoother
    FILE:       utils/ParamSmoother.h

    INIT:       init(tauMs, controlRate)
                  tauMs = 5.0 (default smoothing time)
                  controlRate = sampleRate / samplesPerBlock (BASE rate, not oversampled)
    UPDATE:     setTarget(target)
                  current = target + (current − target) × decay
                  Call once per processBlock (block rate)
    IMMEDIATE:  setImmediate(value) → current = value (no smoothing)

    INVARIANTS: converges to target over ~5ms
                controlRate uses BASE sample rate (not oversampled)
    INSTANCES:  gainSmoother, toneSmoother, volumeSmoother
    PITFALLS:   controlRate must be baseSR / block, NOT oversampledSR / block.
                Using oversampled rate makes smoothers 4× too slow.

──────────────────────────────────────────────────────────────────
UTILITY: VOLTAGE SCALE
──────────────────────────────────────────────────────────────────

  STRUCT: VS (VoltageScale)
    FILE:       utils/VoltageScale.h
    SPEC:       §2.5

    CONVERSIONS:
      v(volts) → normalized     (÷ 300)
      r(ohms)  → normalized     (÷ 300000)
      i(amps)  → normalized     (÷ 0.001)
      toVolts(n) → real volts   (× 300)
      toOhms(n)  → real ohms    (× 300000)
      toAmps(n)  → real amps    (× 0.001)

    CONSTANTS:  V_REF=300, R_REF=300000, I_REF=0.001
                V_B_PLUS=1.0, V_SCREEN=0.933, V_PREAMP=0.833
                V_GUITAR=0.00167, V_BIAS_12AX7=-0.005
                R_100/300/1K2/1K5/68K/100K/500K/1M (pre-computed)
                Z_OUT_V1A/V1B/PI (parallel plate/rp combos)

    INVARIANTS: Ohm's law: V_n = I_n × R_n
                Round-trip: toVolts(v(x)) == x
                v(300) == 1.0 (B+ reference)
                v(0) == 0.0 (ground)
    TEST FILE:  voltage_scale_tests.cpp

──────────────────────────────────────────────────────────────────
UTILITY: AUDIO TAPER
──────────────────────────────────────────────────────────────────

  FUNCTION: audioTaper
    FILE:       dsp/tubes/TriodeWaveshaper.h (inline)
    FORMULA:    output = x × x    (x² taper)
    INPUT:      x ∈ [0, 1] (knob position)
    OUTPUT:     [0, 1] (attenuated level)
    INVARIANTS: audioTaper(0) = 0, audioTaper(1) = 1
                audioTaper(0.5) = 0.25
                monotonically increasing
    CALLS FROM: processBlock — gain and volume parameters
    DEPENDS ON: GAIN_TAPER_EXPONENT (2.0, but inline uses literal x*x)
```

---

## 1. Circuit Overview

The Silent Riot is a deceptively simple all-valve amplifier built around two ECC83/12AX7 preamp tubes and two EL84 power tubes in push-pull configuration. Despite having only three controls (Gain, Tone, Volume), the circuit achieves a massive range from sparkling cleans to rich, compressed overdrive.

### Key Circuit Characteristics
- **Tubes:** 2× ECC83/12AX7 (preamp), 2× EL84 (power amp)
- **Output:** 15W / 7W switchable (cathode bias)
- **Topology:** Cascaded preamp → Long-tail pair phase inverter → Push-pull EL84 output (hot Class AB, marketed as "Class A")
- **Notable circuit features (shared with the Orange Tiny Terror this design is modelled on):**
  - Dual-gang gain pot simultaneously controls two attenuation points
  - Tone control is post-phase-inverter (Vox "cut" style), not in the preamp
  - Volume control is also post-PI, so gain structure is unaffected by volume/tone
  - AC15-inspired architecture with an additional cascaded gain stage

---

## 2. Signal Chain — Stage by Stage

```
Guitar Input
    │
    ▼
┌──────────────────────────────┐
│  STAGE 1: Input / V1a        │
│  (First triode of ECC83 #1)  │
│  Common-cathode gain stage    │
│  ~34dB gain, 25µF bypass cap  │
└──────────┬───────────────────┘
           │
    ┌──────▼──────┐
    │ GAIN POT    │  Dual-gang 500K audio
    │ Gang A      │  Attenuates V1a output → V1b input
    └──────┬──────┘
           │
┌──────────▼───────────────────┐
│  STAGE 2: Second Gain / V1b   │
│  (Second triode of ECC83 #1)  │
│  Common-cathode gain stage     │
│  ~34dB gain, 25µF bypass cap   │
└──────────┬───────────────────┘
           │
    ┌──────▼──────┐
    │ GAIN POT    │  Dual-gang 500K audio
    │ Gang B      │  Attenuates V1b output → PI input
    │             │  (changes impedance/loading)
    └──────┬──────┘
           │
┌──────────▼───────────────────┐
│  STAGE 3: Phase Inverter      │
│  (ECC83 #2 — both triodes)    │
│  Long-tail pair (differential) │
│  1.2K tail resistor to B+     │
│  ground ref via high-R tail    │
│  Splits signal into push/pull  │
└──────────┬───────────────────┘
           │
    ┌──────▼──────┐
    │ TONE        │  500K pot + 2.2nF cap
    │ CONTROL     │  Between EL84 grids (post-PI)
    │ (Vox Cut)   │  High-freq cancellation
    └──────┬──────┘
           │
    ┌──────▼──────┐
    │ VOLUME      │  Post-PI master volume
    │ CONTROL     │  Controls level into power tubes
    └──────┬──────┘
           │
┌──────────▼───────────────────┐
│  STAGE 4: Power Amp           │
│  2× EL84 push-pull             │
│  Hot Class AB (marketed "A")   │
│  Cathode biased (~90-96%)     │
│  100Ω shared cathode resistor  │
│  15W / 7W switchable           │
└──────────┬───────────────────┘
           │
┌──────────▼───────────────────┐
│  STAGE 5: Output Transformer  │
│  5-section interleaved OT     │
│  Frequency response shaping   │
│  Low-end resonance / sag      │
└──────────┬───────────────────┘
           │
    ┌──────▼──────┐
    │  SPEAKER     │
    │  SIM / IR    │
    └─────────────┘
```

---

## 2.5 Internal Voltage Scaling Framework

Real tube circuits operate at voltages from ~1Vpp (guitar input) up to ~600Vpp (plate swings) and resistances from 100Ω to 1MΩ. DSP operates in floating point where ±1.0 is the natural range. A consistent voltage scaling framework prevents gain staging nightmares and makes every tube model comparable.

### The Problem
Without a framework, each stage uses arbitrary internal levels. You end up with magic numbers everywhere, waveshapers tuned to random input ranges, and impossible-to-debug gain interactions. When you change one stage, everything downstream breaks.

### The Solution: Normalized Voltage Domain

Define a single reference mapping between real circuit voltages and DSP internal values. Every voltage, resistance, current, and component value is converted to this domain. No raw real-world values appear in processing code — ever.

```
═══════════════════════════════════════════════════════════════════
VoltageScale UTILITY — COMPLETE IMPLEMENTATION
═══════════════════════════════════════════════════════════════════

  struct VoltageScale {
      // ── Primary reference ──
      static constexpr float V_REF     = 300.0f;   // volts — B+ supply reference
      static constexpr float R_REF     = 300000.0f; // ohms — resistance reference (300K)
      static constexpr float I_REF     = V_REF / R_REF; // 1mA — current reference
      
      // ── Derived conversion factors ──
      static constexpr float V_TO_N    = 1.0f / V_REF;      // 0.00333 V⁻¹
      static constexpr float N_TO_V    = V_REF;              // 300.0 V
      static constexpr float R_TO_N    = 1.0f / R_REF;       // 3.33e-6 Ω⁻¹
      static constexpr float N_TO_R    = R_REF;              // 300000.0 Ω
      static constexpr float I_TO_N    = 1.0f / I_REF;       // 1000.0 A⁻¹
      static constexpr float N_TO_I    = I_REF;              // 0.001 A
      
      // ── Voltage conversions ──
      static constexpr float v(float volts)      { return volts * V_TO_N; }
      static constexpr float toVolts(float n)    { return n * N_TO_V; }
      
      // ── Resistance conversions ──
      static constexpr float r(float ohms)       { return ohms * R_TO_N; }
      static constexpr float toOhms(float n)     { return n * N_TO_R; }
      
      // ── Current conversions ──
      static constexpr float i(float amps)       { return amps * I_TO_N; }
      static constexpr float toAmps(float n)     { return n * N_TO_I; }
      
      // ── Convenience: common component values ──
      // Precomputed so they never appear as magic numbers in code
      
      // Resistances (normalized)
      static constexpr float R_100     = r(100.0f);       // 100Ω   = 0.000333
      static constexpr float R_300     = r(300.0f);       // 300Ω   = 0.001
      static constexpr float R_470     = r(470.0f);       // 470Ω   = 0.00157
      static constexpr float R_500     = r(500.0f);       // 500Ω   = 0.00167
      static constexpr float R_820     = r(820.0f);       // 820Ω   = 0.00273
      static constexpr float R_1K2     = r(1200.0f);      // 1.2K   = 0.004
      static constexpr float R_1K5     = r(1500.0f);      // 1.5K   = 0.005
      static constexpr float R_68K     = r(68000.0f);     // 68K    = 0.227
      static constexpr float R_100K    = r(100000.0f);    // 100K   = 0.333
      static constexpr float R_500K    = r(500000.0f);    // 500K   = 1.667
      static constexpr float R_1M      = r(1000000.0f);   // 1MΩ    = 3.333
      
      // Voltages (normalized)
      static constexpr float V_B_PLUS  = v(300.0f);       // 300V B+ = 1.0
      static constexpr float V_SCREEN  = v(280.0f);       // 280V    = 0.933
      static constexpr float V_PI      = v(280.0f);       // 280V    = 0.933
      static constexpr float V_PREAMP  = v(250.0f);       // 250V    = 0.833
      static constexpr float V_GUITAR  = v(0.5f);         // 0.5V pk = 0.00167
      static constexpr float V_BIAS_12AX7 = v(-1.5f);     // -1.5V   = -0.005
      static constexpr float V_THERMAL = v(0.026f);       // 26mV    = 0.0000867
      
      // Stage output impedances (Rplate || rp)
      //   rp for 12AX7 ≈ 62.5K at typical operating point
      static constexpr float Z_OUT_V1A = r(38461.0f);     // 100K||62.5K ≈ 38.5K = 0.128
      static constexpr float Z_OUT_V1B = r(47170.0f);     // (100K+68K)||62.5K ≈ 47K = 0.157
      static constexpr float Z_OUT_PI  = r(38461.0f);     // 100K||62.5K per side = 0.128
  };

  // ── Shorthand alias for readability in processing code ──
  using VS = VoltageScale;

═══════════════════════════════════════════════════════════════════
WHY RESISTANCE AND CURRENT REFERENCES?
═══════════════════════════════════════════════════════════════════

  The original spec only normalized voltages. But the grid conduction
  model, the PSU model, and the gain pot loading all involve Ohm's law
  relationships (V = I × R). If voltages are normalized but resistances
  aren't, you get dimensional mismatches:

    WRONG:  gridCurrent = vAboveThreshold / 300.0f;   // what unit is 300?
    RIGHT:  gridCurrent = vAboveThreshold / VS::R_300; // 300Ω in normalized domain

  By choosing R_REF = V_REF / I_REF, Ohm's law works identically in
  the normalized domain:
    V_n = I_n × R_n    (just like V = I × R in real units)

  This means every equation from circuit analysis can be transcribed
  directly into code with normalized values and it remains physically
  correct. No scale factors, no unit bugs.

═══════════════════════════════════════════════════════════════════
VOLTAGE REFERENCE TABLE
═══════════════════════════════════════════════════════════════════

  Real World                    Normalized (VS::v())   Context
  ──────────────────────────────────────────────────────────────
  0V                            0.0                    Ground reference
  26mV                          0.0000867              Thermal voltage Vt
  ±0.5V peak (~1Vpp)           ±0.00167               Guitar input (hot humbuckers)
  -1.5V (grid bias)            -0.005                  12AX7 operating point
  ±60V peak (plate swing)      ±0.2                    V1a typical output
  ±150V peak (max swing)       ±0.5                    plate swing at B+≈300V
  250V (preamp B+)              0.833                  preamp supply rail
  280V (PI / screen B+)         0.933                  PI and screen supply
  300V (power B+)               1.0                    full supply rail
  ──────────────────────────────────────────────────────────────

  Resistance Reference Table
  ──────────────────────────────────────────────────────────────
  100Ω  (EL84 cathode)          0.000333               VS::R_100
  300Ω  (grid conduction R)     0.001                  VS::R_300
  1.2K  (PI tail)               0.004                  VS::R_1K2
  1.5K  (cathode R)             0.005                  VS::R_1K5
  68K   (V1b dropper)           0.227                  VS::R_68K
  100K  (plate load)            0.333                  VS::R_100K
  500K  (gain pot)              1.667                  VS::R_500K
  1MΩ   (grid leak)             3.333                  VS::R_1M
  ──────────────────────────────────────────────────────────────

═══════════════════════════════════════════════════════════════════
STAGE-BY-STAGE OPERATING LEVELS (all normalized)
═══════════════════════════════════════════════════════════════════

  Stage          Supply Node     Input Level      Output Level       Notes
  ──────────────────────────────────────────────────────────────────────────────
  Guitar In      n/a             ±0.00167         ±0.00167           ~1Vpp humbucker
  V1a            preampNode      ±0.00167         ±0.05 to ±0.15    gain ≈ 70×
  Gain A         n/a             ±0.05 to ±0.15   ±0.0 to ±0.15     attenuated
  V1b            preampNode      (from Gain A)    ±0.05 to ±0.20    clips at supply
  Gain B         n/a             ±0.05 to ±0.20   ±0.0 to ±0.20     attenuated
  PI (per side)  piNode          (from Gain B)    ±0.15 to ±0.40    LTP split
  Tone/Vol       n/a             (from PI)        ±0.0 to ±0.40     level-controlled
  EL84 (per tube) powerNode      (from Tone/Vol)  ±0.3 to ±0.8      clips at supply
  OT Out         n/a             ±0.3 to ±0.8     ±0.3 to ±0.7      bandpass-shaped
  Cab Out        n/a             ±0.3 to ±0.7     ±0.0 to ±1.0      final scaling

═══════════════════════════════════════════════════════════════════
ENFORCING THE NORMALIZED DOMAIN — RULES
═══════════════════════════════════════════════════════════════════

  Rule 1: NO RAW REAL-WORLD VALUES IN PROCESSING CODE
  
    WRONG:  float cutoffPoint = -8.0f;          // -8V? -8 normalized? who knows
    RIGHT:  float cutoffPoint = VS::v(-8.0f);   // explicitly -8 volts, normalized
    
    WRONG:  float rPlate = 100000.0f;           // ohms? normalized? 
    RIGHT:  float rPlate = VS::R_100K;          // 100KΩ, pre-normalized
    
    WRONG:  .sourceResistance = 0.333f,         // magic number
    RIGHT:  .sourceResistance = VS::R_100K,     // self-documenting
    
    WRONG:  .gridResistance = 0.001f,           // what is this?
    RIGHT:  .gridResistance = VS::R_300,        // 300Ω grid R

  Rule 2: WAVESHAPERS TAKE NORMALIZED SUPPLY AND CLIP AGAINST IT
  
    Every triode and pentode waveshaper receives the supply voltage from
    its PSU node and clips output against it:
    
    float triodeStage(float vGrid_n, float vSupply_n, float vBias_n) {
        float output = triodeWaveshaper(vGrid_n, vBias_n);
        float ceiling = vSupply_n * HEADROOM_FACTOR;
        return std::clamp(output, -ceiling, ceiling);
    }

  Rule 3: STAGE GAINS ARE DIMENSIONLESS RATIOS — NEVER SCALED
  
    Gain = 70× means a 0.00167 input produces a 0.117 output.
    The gain constant itself (70.0f) is dimensionless. It doesn't need
    normalization. But the input and output it operates on ARE normalized.

  Rule 4: DEBUG ASSERTIONS AT STAGE BOUNDARIES
  
    In debug builds, assert that signals stay within expected ranges
    at each stage boundary. This catches domain violations immediately.
    
    #ifdef DEBUG
    #define VS_ASSERT_RANGE(val, lo, hi, stage) \
        jassert((val) >= (lo) && (val) <= (hi) && \
                "Signal out of expected range at " stage)
    #else
    #define VS_ASSERT_RANGE(val, lo, hi, stage) ((void)0)
    #endif
    
    // Usage in processBlock:
    float v1a_out = triodeStage(v1a_in, preampNode.voltage, v1aBias);
    VS_ASSERT_RANGE(v1a_out, -0.3f, 0.3f, "V1a output");
    
    LTPOutput ltp = solveLTP(piInput, piNode.voltage, ltpModel);
    VS_ASSERT_RANGE(ltp.piA, -0.6f, 0.6f, "PI non-inv output");
    
    float powerOut = el84NonInv - el84Inv;
    VS_ASSERT_RANGE(powerOut, -1.5f, 1.5f, "Power amp output");

  Rule 5: PSU NODES STORE VOLTAGES IN NORMALIZED DOMAIN
  
    Already the case (powerNode.voltage = 1.0 means 300V).
    The screen node, PI node, preamp node all follow this convention.
    The VoltageScale utility makes the mapping explicit:
      powerNode.voltage initialized to VS::V_B_PLUS  (1.0)
      screenNode.voltage initialized to VS::V_SCREEN  (0.933)
      piNode.voltage initialized to VS::V_PI           (0.933)
      preampNode.voltage initialized to VS::V_PREAMP   (0.833)

  Rule 6: KOREN MODEL CONSTANTS ARE PRE-NORMALIZED
  
    The Koren triode equation uses real-world tube parameters (mu, kp, kg,
    kvb). These must be pre-normalized so the model operates entirely in
    the DSP domain. See Section 4.1 for the normalized Koren implementation.

═══════════════════════════════════════════════════════════════════
GRID CONDUCTION — MAGIC NUMBERS REPLACED
═══════════════════════════════════════════════════════════════════

  The grid conduction model instances (Section 4.1.1) previously used
  raw normalized magic numbers. With VoltageScale, they become:

  GridConductionModel v1aGrid = {
      .conductionThreshold = VS::v(0.5f),         // 0.5V grid conduction onset
      .gridResistance      = VS::R_300,            // 300Ω during conduction
      .sourceResistance    = VS::r(100.0f),        // guitar pickup ~100Ω
      .couplingCapCharge   = 0.0f,
      .gridLeakR           = VS::R_1M,             // 1MΩ grid leak
      .couplingCapValue    = 22e-9f,               // 22nF (real, for tau calc only)
  };

  GridConductionModel v1bGrid = {
      .conductionThreshold = VS::v(0.5f),
      .gridResistance      = VS::R_300,
      .sourceResistance    = VS::Z_OUT_V1A,            // V1a Zout: Rp||rp ≈ 38.5KΩ
      ...
  };

  GridConductionModel piGrid = {
      .conductionThreshold = VS::v(0.5f),
      .gridResistance      = VS::R_300,
      .sourceResistance    = VS::Z_OUT_V1B,            // V1b Zout: (Rp+68K)||rp ≈ 47KΩ
      ...
  };

  GridConductionModel el84Grid = {
      .conductionThreshold = VS::v(0.0f),           // pentode: conducts near 0V
      .gridResistance      = VS::R_500,             // 500Ω
      .sourceResistance    = VS::Z_OUT_PI,           // PI Zout: Rp||rp ≈ 38.5KΩ per side
      ...
  };

═══════════════════════════════════════════════════════════════════
OUTPUT SCALING
═══════════════════════════════════════════════════════════════════

  After the cabinet sim, normalize to ±1.0 for DAW output:
    finalOutput = cabOutput * outputGain;  // outputGain tuned so max ≈ ±1.0
  
  A final soft limiter prevents overs:
    finalOutput = softClip(finalOutput);  // gentle protection, not tone-shaping

═══════════════════════════════════════════════════════════════════
PROCESSBLOCK — FULL NORMALIZED SIGNAL CHAIN
═══════════════════════════════════════════════════════════════════

  void processBlock(AudioBuffer<float>& buffer, ...) {
      ScopedNoDenormals noDenormals;
      
      // Smooth parameters
      gainSmoother.setTarget(gainParam);
      toneSmoother.setTarget(toneParam);
      volumeSmoother.setTarget(volumeParam);
      filterBank.updateModulated(toneFreq, gainLoadFreq);
      
      // PSU update (uses previous sample's current reports — causal)
      
      auto osBlock = oversampler->processSamplesUp(buffer);
      
      for (int i = 0; i < osBlock.getNumSamples(); ++i) {
          // ── PSU update: uses PREVIOUS sample's current reports ──
          //    Skip if frozen during OS reconfiguration (Section 5.1)
          if (!psuFrozen) psu.update();
          psu.resetCurrentDraw();  // clear accumulators for this sample's reports
          // Input: DAW delivers ±1.0 — scale to guitar voltage domain
          float x = osBlock.getSample(0, i) * VS::V_GUITAR;
          VS_ASSERT_RANGE(x, -0.005f, 0.005f, "Input scaled");
          
          // Read supply voltages for this sample
          float vPreamp = psu.vPreamp();
          float vPi     = psu.vPi();
          float vPlate  = psu.vPower();
          float vScreen = psu.vScreen();
          
          // ── Stage 1: V1a ──
          x = filterBank.inputHP.process(x);
          x = applyGridConduction(x, v1aGrid);
          float v1a_in = x;  // save for gain estimation
          x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, StageGains::V1A_GAIN);
          x = std::clamp(x, -vPreamp * HEADROOM_FACTOR,
                              vPreamp * HEADROOM_FACTOR);
          x = filterBank.v1a_cathodeBypass.process(x);
          x = filterBank.v1a_couplingHP.process(x);
          // Dynamic Miller: gain estimate from input/output ratio
          float v1aGainEst = (std::abs(v1a_in) > 1e-8f)
                           ? std::abs(x) / std::abs(v1a_in)
                           : StageGains::V1A_GAIN;
          x = v1a_miller.process(x, v1aGainEst, filterBank.effectiveSR);
          psu.reportPreampLevel(x);  // V1a current draw
          VS_ASSERT_RANGE(x, -0.3f, 0.3f, "V1a out");
          
          // ── Gain Pot Gang A ──
          float gainA = audioTaper(gainSmoother.current);
          x *= gainA;
          
          // ── Stage 2: V1b ──
          x = applyGridConduction(x, v1bGrid);
          float v1b_in = x;  // save for gain estimation
          x = triodeWaveshaper(x, VS::V_BIAS_12AX7, vPreamp, StageGains::V1B_GAIN);
          x = std::clamp(x, -vPreamp * HEADROOM_FACTOR,
                              vPreamp * HEADROOM_FACTOR);
          x = filterBank.v1b_cathodeBypass.process(x);
          x = filterBank.v1b_couplingHP.process(x);
          // Dynamic Miller: V1b Rsource modulated by gain pot position
          // At gain=1.0: Rsource ≈ V1a Zout (38.5KΩ), Miller barely audible
          // At gain=0.0: Rsource ≈ V1a Zout + 500KΩ pot, fc drops to ~2.6kHz
          float gainPotR = (1.0f - gainA) * 500000.0f;  // pot R adds to source Z
          v1b_miller.rSource_real = 38500.0f + gainPotR;
          float v1bGainEst = (std::abs(v1b_in) > 1e-8f)
                           ? std::abs(x) / std::abs(v1b_in)
                           : StageGains::V1B_GAIN;
          x = v1b_miller.process(x, v1bGainEst, filterBank.effectiveSR);
          psu.reportPreampLevel(x);  // V1b current draw (accumulates with V1a above)
          VS_ASSERT_RANGE(x, -0.4f, 0.4f, "V1b out");
          
          // ── Gain Pot Gang B ──
          float gainB = audioTaper(gainSmoother.current);
          x *= gainB;
          x = filterBank.gainLoadingLP.process(x);
          
          // ── Stage 3: Phase Inverter (LTP Nodal Solver) ──
          x = applyGridConduction(x, piGrid);
          LTPOutput ltp_out = solveLTP(x, vPi, ltpModel);
          float piA = ltp_out.piA;
          float piB = ltp_out.piB;
          // Note: supply clamp is handled inside solveLTP (plate can't
          // exceed supply or drop below cathode). No external clamp needed.
          // Note: gain imbalance emerges naturally from differential current
          // steering — no PI_IMBALANCE multiplier needed.
          piA = filterBank.pi_couplingHP_A.process(piA);
          piB = filterBank.pi_couplingHP_B.process(piB);
          psu.reportPiLevel(std::max(std::abs(piA), std::abs(piB)));
          VS_ASSERT_RANGE(piA, -0.6f, 0.6f, "PI non-inv");
          
          // ── Tone Control (Vox cut — differential-only) ──
          // The real circuit connects a cap+pot between the two EL84 grids.
          // This filters only the DIFFERENTIAL signal (push-pull content).
          // Common-mode content (even harmonics from PI asymmetry) must be
          // preserved — the old implementation (subtract/add) altered it.
          float diff = piA - piB;                                    // differential
          float sum  = piA + piB;                                    // common-mode
          float filteredDiff = filterBank.toneCutFilter.process(diff);
          float toneAmt = 1.0f - toneSmoother.current;  // 0=no cut, 1=full cut
          float outputDiff = diff + (filteredDiff - diff) * toneAmt; // blend
          piA = (sum + outputDiff) * 0.5f;                           // reconstruct
          piB = (sum - outputDiff) * 0.5f;
          
          // ── Master Volume ──
          float vol = audioTaper(volumeSmoother.current);
          piA *= vol;
          piB *= vol;
          
          // ── Stage 4: Power Amp (EL84 push-pull) ──
          //
          // el84Bias must START from baseBias each sample, then add sag offset.
          // Using += would accumulate drift across samples → runaway bias.
          //
          // The sag envelope modifies the bias SETPOINT, not the bias itself.
          // Think of it as: the cathode cap charges/discharges, shifting the
          // cathode voltage, which shifts the effective grid-to-cathode bias.
          //
          float effectiveBias = el84BaseBias + sag.biasOffset();
          
          piA = applyGridConduction(piA, el84GridA);
          piB = applyGridConduction(piB, el84GridB);
          float condA = softConductionCurve(piA, effectiveBias);
          float condB = softConductionCurve(piB, effectiveBias);
          PentodeOutput ppA = pentodeWaveshaper(piA, effectiveBias, vPlate, vScreen);
          PentodeOutput ppB = pentodeWaveshaper(piB, effectiveBias, vPlate, vScreen);
          float powerOut = ppA.plateCurrent * condA - ppB.plateCurrent * condB;
          
          // Report ACTUAL plate + screen currents from both tubes to PSU
          // Screen current is plate-voltage-dependent (computed inside waveshaper),
          // not a fixed ratio — more accurate sag under varying load conditions
          float totalPlate  = std::abs(ppA.plateCurrent)  + std::abs(ppB.plateCurrent);
          float totalScreen = ppA.screenCurrent + ppB.screenCurrent;
          psu.reportPowerDirect(totalPlate, totalScreen);
          VS_ASSERT_RANGE(powerOut, -1.5f, 1.5f, "Power amp out");
          
          // ── Sag envelope (cathode bias shift — separate from PSU droop) ──
          // Envelope tracks power output level, feeds FORWARD to next sample's bias
          sag.update(powerOut);
          
          // ── Speaker Interaction (dynamic load-line modulation) ──
          // Speaker impedance varies with frequency → changes pentode load
          // → changes gain and distortion character per-frequency band
          float otIn = speakerCoupling.process(powerOut);
          
          // ── OT leakage inductance (transformer HF rolloff) ──
          float otOut = filterBank.ot_leakageLP.process(otIn);
          
          // ── Cabinet Sim (acoustic response only) ──
          // Speaker resonance boost now comes from SpeakerCoupling above
          float cabOut = filterBank.cab_presence.process(otOut);
          cabOut = filterBank.cab_rolloff.process(cabOut);
          cabOut = filterBank.cab_notch.process(cabOut);
          
          // ── Output: scale back to DAW ±1.0 domain ──
          float finalOut = cabOut * outputGainCompensation;
          finalOut = std::tanh(finalOut);  // gentle limiter
          VS_ASSERT_RANGE(finalOut, -1.0f, 1.0f, "DAW output");
          
          osBlock.setSample(0, i, finalOut);
      }
      
      oversampler->processSamplesDown(buffer);
  }
```

---

## 2.6 Numerical Hygiene

Two classes of bug that will silently destroy your plugin if not handled from day one: denormals and sample-rate-dependent filter behavior.

### 2.6.1 Anti-Denormal Handling

```
=== THE PROBLEM ===

  Denormalized floats (subnormals) are tiny values near zero (~1e-38 to ~1e-45)
  that CPUs process 10-100× slower than normal floats. In an amp sim they
  appear CONSTANTLY:
  
    - IIR filter feedback paths that decay toward zero (every coupling cap,
      cathode bypass, tone filter, OT filter)
    - Envelope followers during silence (sag model, PSU droop)
    - Waveshaper outputs when input is near the bias point
    - Push-pull difference signal during balanced operation
    
  A single denormal in one filter can spike your CPU from 5% to 80%.
  With 15+ cascaded IIR filters in this amp model, it WILL happen.

=== STRATEGY: DEFENSE IN DEPTH ===

  Layer 1: CPU flush-to-zero mode (global, set once in prepareToPlay)
  
    void prepareToPlay(double sampleRate, int samplesPerBlock) {
        // Enable FTZ + DAZ on the audio thread
        // Flushes denormals to zero at the hardware level
        _mm_setcsr(_mm_getcsr() | 0x8040);  // FTZ + DAZ bits
        
        // JUCE helper (does the same thing, more portable)
        juce::ScopedNoDenormals noDenormals;
        // Note: ScopedNoDenormals is scoped — for processBlock, 
        // place it at the top of processBlock(), not prepareToPlay()
    }
    
    void processBlock(juce::AudioBuffer<float>& buffer, ...) {
        juce::ScopedNoDenormals noDenormals;  // ← EVERY processBlock call
        // ... processing ...
    }

  Layer 2: DC offset injection (belt-and-suspenders for filter feedback)
  
    // Inject a tiny constant into feedback paths to keep values out of
    // the denormal range. This is inaudible but keeps filters "alive."
    static constexpr float ANTI_DENORMAL = 1e-18f;
    
    // Apply to IIR filter state variables after each update:
    void updateFilter(float input) {
        z1 = input * b0 + z1 * a1 + ANTI_DENORMAL;
        // or equivalently:
        z1 = input * b0 + z1 * a1;
        z1 += ANTI_DENORMAL;
        z1 -= ANTI_DENORMAL;  // add then subtract = no DC offset, breaks denormal
    }

  Layer 3: Snap-to-zero on envelope followers and slow state variables
  
    // The PSU model and sag envelope have long time constants (80-150ms)
    // They WILL generate denormals during silence
    // SupplyNode::track() handles this internally:
    void track(float target_n) {
        voltage = target_n + (voltage - target_n) * decay;
        // Snap to target if effectively converged
        if (std::abs(voltage - target_n) < 1e-10f)
            voltage = target_n;
    }
    }

=== WHERE TO APPLY (EVERY filter and state variable in the plugin) ===

  ☐ Input coupling HP filter (z1 state)
  ☐ V1a cathode bypass filter (z1 state)
  ☐ V1a output coupling HP (z1 state)
  ☐ V1a Miller cap LP (z1 state)
  ☐ V1b cathode bypass filter (z1 state)
  ☐ V1b output coupling HP (z1 state)
  ☐ V1b Miller cap LP (z1 state)
  ☐ PI coupling HP filters ×2 (z1 states)
  ☐ Tone control cut filter (z1 state)
  ☐ Sag envelope follower (envelope state)
  ☐ PSU supply nodes ×4 (power, screen, PI, preamp voltage states)
  ☐ OT leakage LP (z1, z2 states)
  ☐ SpeakerCoupling: band extraction biquads (z1, z2 ×3)
  ☐ SpeakerCoupling: envelope followers (bassEnv, midEnv, hfEnv)
  ☐ Cabinet sim IIR filters: presence, rolloff, notch (z1, z2 states)
  ☐ Any smoothed parameter ramps (current value state)
  ☐ Grid conduction coupling cap charge ×4 (v1aGrid, v1bGrid, piGrid, el84Grid)
  ☐ LTP cathode voltage (ltpModel.vCathode — slow-moving DC state)
  ☐ DynamicMillerLP fcSmooth ×2 (v1a_miller, v1b_miller — smoothed cutoff)
```

### 2.6.2 Sample-Rate-Invariant Filter Implementation Strategy

```
═══════════════════════════════════════════════════════════════════
THE PROBLEM
═══════════════════════════════════════════════════════════════════

  This plugin runs at multiple effective sample rates:
    - Base rate: 44100, 48000, 88200, 96000 Hz (DAW setting)
    - Oversampled rate: base × 2, 4, or 8 (internal processing)
    - User can change oversampling mode at runtime
    
  If filter coefficients are computed with naive formulas like:
    alpha = fc / sampleRate;  // WRONG — breaks at high sample rates
  
  ...the filters will sound completely different at 44.1kHz vs 96kHz,
  and will shift character when oversampling mode changes. Every coupling
  cap, cathode bypass, and tone filter will be wrong.

  There are also modulated filters in this plugin (tone control, gain-
  dependent loading) where coefficients change per-block or per-sample.
  These need to be cheap to recompute AND still sample-rate correct.

═══════════════════════════════════════════════════════════════════
STRATEGY OVERVIEW
═══════════════════════════════════════════════════════════════════

  1. ALL filters use bilinear transform (BLT) — OnePole types use
     explicit frequency pre-warping (tan). Biquad types use the
     Robert Bristow-Johnson cookbook, which includes sinusoidal
     frequency mapping (implicit BLT) but not additional pre-warping.
     At 4× OS, biquad frequency error is negligible (<0.4%).
     At 1× OS, the highest biquad (OT LP 7kHz) has ~3.9% error —
     acceptable for speaker/cabinet modeling where exact fc is tuned
     by ear anyway.
  2. Three filter structs cover every case: OnePoleLP, OnePoleHP, Biquad
  3. A central FilterBank manages all instances and bulk-recalculates
  4. Modulated filters use a fast BLT path that skips the tan() when
     the modulation rate is slower than audio rate
  5. Every filter stores its target fc and Q so recalc is stateless

═══════════════════════════════════════════════════════════════════
1st ORDER FILTERS — OnePoleLP / OnePoleHP
═══════════════════════════════════════════════════════════════════

  Used for: coupling caps, cathode bypass, Miller capacitance, tone cut,
  grid conduction charge decay, PSU-related smoothing.

  Analog prototype → BLT with pre-warping:
    LP: H(s) = wc / (s + wc)
    HP: H(s) = s  / (s + wc)

  struct OnePoleLP {
      float b0, b1, a1;
      float z1 = 0.0f;
      float fc = 0.0f;       // stored for recalc
      
      void setFreq(float newFc, float sampleRate) {
          fc = newFc;
          float wa = 2.0f * sampleRate * std::tan(PI * fc / sampleRate);
          float g  = wa / (2.0f * sampleRate);
          b0 = g / (1.0f + g);
          b1 = b0;
          a1 = (1.0f - g) / (1.0f + g);
      }
      
      // Fast path: skip tan() when fc/sr ratio is small (<0.1)
      // Error < 0.3% for fc < sr/10, which covers ALL our filters
      // at 4× oversampling (highest filter = 7kHz, sr = 176.4kHz)
      void setFreqFast(float newFc, float sampleRate) {
          fc = newFc;
          float g = PI * fc / sampleRate;  // small-angle approx: tan(x)≈x
          b0 = g / (1.0f + g);
          b1 = b0;
          a1 = (1.0f - g) / (1.0f + g);
      }
      
      float process(float x) {
          float y = x * b0 + z1;
          z1 = x * b1 - y * a1 + ANTI_DENORMAL;
          return y;
      }
      
      void reset() { z1 = 0.0f; }
  };

  struct OnePoleHP {
      float b0, b1, a1;
      float z1 = 0.0f;
      float fc = 0.0f;
      
      void setFreq(float newFc, float sampleRate) {
          fc = newFc;
          float wa = 2.0f * sampleRate * std::tan(PI * fc / sampleRate);
          float g  = wa / (2.0f * sampleRate);
          b0 = 1.0f / (1.0f + g);
          b1 = -b0;
          a1 = (1.0f - g) / (1.0f + g);
      }
      
      void setFreqFast(float newFc, float sampleRate) {
          fc = newFc;
          float g = PI * fc / sampleRate;
          b0 = 1.0f / (1.0f + g);
          b1 = -b0;
          a1 = (1.0f - g) / (1.0f + g);
      }
      
      float process(float x) {
          float y = x * b0 + z1;
          z1 = x * b1 - y * a1 + ANTI_DENORMAL;
          return y;
      }
      
      void reset() { z1 = 0.0f; }
  };

═══════════════════════════════════════════════════════════════════
2nd ORDER FILTERS — Biquad
═══════════════════════════════════════════════════════════════════

  Used for: OT resonant HP/LP, cabinet sim EQ, speaker resonance,
  notch filters, presence peaks.

  We use our own Biquad rather than juce::dsp::IIR everywhere because:
    - JUCE IIR allocates Coefficients on the heap (shared_ptr)
    - We need 8+ biquads and want zero allocation in processBlock
    - We want the fast BLT path for any modulated 2nd-order filters
    - Consistent anti-denormal handling across all filters

  Analog prototypes → BLT with pre-warping (cookbook formulas):

  struct Biquad {
      float b0, b1, b2, a1, a2;
      float z1 = 0.0f, z2 = 0.0f;
      float fc = 0.0f, Q = 0.707f, gainDb = 0.0f;
      
      // ── Transposed Direct Form II (preferred for floating point) ──
      float process(float x) {
          float y = x * b0 + z1;
          z1 = x * b1 - y * a1 + z2 + ANTI_DENORMAL;
          z2 = x * b2 - y * a2 + ANTI_DENORMAL;
          return y;
      }
      
      void reset() { z1 = z2 = 0.0f; }
      
      // ── Design methods (BLT + pre-warping) ──
      
      void makeLowPass(float newFc, float newQ, float sr) {
          fc = newFc; Q = newQ;
          float w0 = 2.0f * PI * fc / sr;
          float sinW0 = std::sin(w0);
          float cosW0 = std::cos(w0);
          float alpha = sinW0 / (2.0f * Q);
          
          float a0_inv = 1.0f / (1.0f + alpha);
          b0 = ((1.0f - cosW0) * 0.5f) * a0_inv;
          b1 = (1.0f - cosW0) * a0_inv;
          b2 = b0;
          a1 = (-2.0f * cosW0) * a0_inv;
          a2 = (1.0f - alpha) * a0_inv;
      }
      
      void makeHighPass(float newFc, float newQ, float sr) {
          fc = newFc; Q = newQ;
          float w0 = 2.0f * PI * fc / sr;
          float sinW0 = std::sin(w0);
          float cosW0 = std::cos(w0);
          float alpha = sinW0 / (2.0f * Q);
          
          float a0_inv = 1.0f / (1.0f + alpha);
          b0 = ((1.0f + cosW0) * 0.5f) * a0_inv;
          b1 = -(1.0f + cosW0) * a0_inv;
          b2 = b0;
          a1 = (-2.0f * cosW0) * a0_inv;
          a2 = (1.0f - alpha) * a0_inv;
      }
      
      void makePeakEQ(float newFc, float newQ, float newGainDb, float sr) {
          fc = newFc; Q = newQ; gainDb = newGainDb;
          float A = std::pow(10.0f, gainDb / 40.0f);
          float w0 = 2.0f * PI * fc / sr;
          float sinW0 = std::sin(w0);
          float cosW0 = std::cos(w0);
          float alpha = sinW0 / (2.0f * Q);
          
          float a0_inv = 1.0f / (1.0f + alpha / A);
          b0 = (1.0f + alpha * A) * a0_inv;
          b1 = (-2.0f * cosW0) * a0_inv;
          b2 = (1.0f - alpha * A) * a0_inv;
          a1 = b1;  // same for peakEQ
          a2 = (1.0f - alpha / A) * a0_inv;
      }
      
      void makeNotch(float newFc, float newQ, float sr) {
          fc = newFc; Q = newQ;
          float w0 = 2.0f * PI * fc / sr;
          float sinW0 = std::sin(w0);
          float cosW0 = std::cos(w0);
          float alpha = sinW0 / (2.0f * Q);
          
          float a0_inv = 1.0f / (1.0f + alpha);
          b0 = a0_inv;
          b1 = (-2.0f * cosW0) * a0_inv;
          b2 = b0;
          a1 = b1;
          a2 = (1.0f - alpha) * a0_inv;
      }
      
      void makeLowShelf(float newFc, float newQ, float newGainDb, float sr) {
          fc = newFc; Q = newQ; gainDb = newGainDb;
          float A = std::pow(10.0f, gainDb / 40.0f);
          float w0 = 2.0f * PI * fc / sr;
          float sinW0 = std::sin(w0);
          float cosW0 = std::cos(w0);
          float alpha = sinW0 / (2.0f * Q);
          float twoSqrtA_alpha = 2.0f * std::sqrt(A) * alpha;
          
          float a0_inv = 1.0f / ((A+1.0f) + (A-1.0f)*cosW0 + twoSqrtA_alpha);
          b0 = (A * ((A+1.0f) - (A-1.0f)*cosW0 + twoSqrtA_alpha)) * a0_inv;
          b1 = (2.0f * A * ((A-1.0f) - (A+1.0f)*cosW0)) * a0_inv;
          b2 = (A * ((A+1.0f) - (A-1.0f)*cosW0 - twoSqrtA_alpha)) * a0_inv;
          a1 = (-2.0f * ((A-1.0f) + (A+1.0f)*cosW0)) * a0_inv;
          a2 = ((A+1.0f) + (A-1.0f)*cosW0 - twoSqrtA_alpha) * a0_inv;
      }
      
      void makeHighShelf(float newFc, float newQ, float newGainDb, float sr) {
          fc = newFc; Q = newQ; gainDb = newGainDb;
          float A = std::pow(10.0f, gainDb / 40.0f);
          float w0 = 2.0f * PI * fc / sr;
          float sinW0 = std::sin(w0);
          float cosW0 = std::cos(w0);
          float alpha = sinW0 / (2.0f * Q);
          float twoSqrtA_alpha = 2.0f * std::sqrt(A) * alpha;
          
          float a0_inv = 1.0f / ((A+1.0f) - (A-1.0f)*cosW0 + twoSqrtA_alpha);
          b0 = (A * ((A+1.0f) + (A-1.0f)*cosW0 + twoSqrtA_alpha)) * a0_inv;
          b1 = (-2.0f * A * ((A-1.0f) + (A+1.0f)*cosW0)) * a0_inv;
          b2 = (A * ((A+1.0f) + (A-1.0f)*cosW0 - twoSqrtA_alpha)) * a0_inv;
          a1 = (2.0f * ((A-1.0f) - (A+1.0f)*cosW0)) * a0_inv;
          a2 = ((A+1.0f) - (A-1.0f)*cosW0 - twoSqrtA_alpha) * a0_inv;
      }
  };

═══════════════════════════════════════════════════════════════════
COMPLETE FILTER REGISTRY
═══════════════════════════════════════════════════════════════════

  Every filter instance in the plugin, its type, frequency, and when
  its coefficients need recalculation:

  ┌─────────────────────────────┬──────────┬──────────┬────────────────────────────┐
  │ Filter                      │ Type     │ fc (Hz)  │ Recalc trigger             │
  ├─────────────────────────────┼──────────┼──────────┼────────────────────────────┤
  │ inputHP                     │ OnePoleHP│ 7        │ SR change only             │
  │ v1a_cathodeBypass           │ OnePoleLP│ 4.2      │ SR change only             │
  │ v1a_couplingHP              │ OnePoleHP│ 72       │ SR change only             │
  │ v1a_miller (DynamicMillerLP) │ OnePoleLP│ ~130k*   │ Per-sample (gain-dependent) │
  │ v1b_miller (DynamicMillerLP) │ OnePoleLP│ 2.6-33k* │ Per-sample (gain+pot-dep.)  │
  │ v1b_cathodeBypass           │ OnePoleLP│ 4.2      │ SR change only             │
  │ v1b_couplingHP              │ OnePoleHP│ 7        │ SR change only             │
  │ pi_couplingHP_A             │ OnePoleHP│ 16       │ SR change only             │
  │ pi_couplingHP_B             │ OnePoleHP│ 16       │ SR change only             │
  │ toneCutFilter               │ OnePoleLP│ 1.5k-20k │ SR change + tone knob move │
  │ gainLoadingLP (optional)    │ OnePoleLP│ variable │ SR change + gain knob move │
  │ ot_leakageLP                │ Biquad   │ 7000     │ SR change only             │
  │ cab_presence                │ Biquad   │ 2500     │ SR change only             │
  │ cab_rolloff                 │ Biquad   │ 5500     │ SR change only             │
  │ cab_notch                   │ Biquad   │ 400      │ SR change only             │
  │ gridCond_v1a.couplingCapHP  │ OnePoleHP│ ~7       │ SR change only             │
  │ gridCond_v1b.couplingCapHP  │ OnePoleHP│ ~72      │ SR change only             │
  │ gridCond_pi.couplingCapHP   │ OnePoleHP│ ~3.4     │ SR change only             │
  │ gridCond_el84a.couplingCapHP│ OnePoleHP│ ~1.6     │ SR change only             │
  │ gridCond_el84b.couplingCapHP│ OnePoleHP│ ~1.6     │ SR change only             │
  └─────────────────────────────┴──────────┴──────────┴────────────────────────────┘

  Total: 10 × OnePoleLP/HP (FilterBank static)
       + 2 × DynamicMillerLP (per-sample coefficient update)
       + 3 × Biquad (FilterBank: OT leakage, cab presence/rolloff/notch)
       + 5 × grid conduction HP
       + 3 × Biquad + 1 × OnePoleLP (SpeakerCoupling internal)
       = 24 filter instances

═══════════════════════════════════════════════════════════════════
CENTRAL FILTER MANAGEMENT — FilterBank
═══════════════════════════════════════════════════════════════════

  All filter instances live in a single FilterBank struct. This ensures:
    - One call recalculates everything on SR change
    - No filter gets missed during oversampling transitions
    - Easy to iterate for reset() during crossfade transitions

  struct FilterBank {
      float effectiveSR = 44100.0f;
      
      // ── Input / Preamp ──
      OnePoleHP inputHP;
      OnePoleLP v1a_cathodeBypass;
      OnePoleHP v1a_couplingHP;
      // v1a Miller: now DynamicMillerLP (outside FilterBank)
      OnePoleLP v1b_cathodeBypass;
      OnePoleHP v1b_couplingHP;
      // v1b Miller: now DynamicMillerLP (outside FilterBank)
      
      // ── Phase Inverter ──
      OnePoleHP pi_couplingHP_A, pi_couplingHP_B;
      
      // ── Tone ──
      OnePoleLP toneCutFilter;
      OnePoleLP gainLoadingLP;  // optional, gain-dependent
      
      // ── Output Transformer ──
      Biquad ot_leakageLP;    // leakage inductance HF rolloff (transformer property)
      
      // ── Cabinet Sim (acoustic response — post SpeakerCoupling) ──
      Biquad cab_presence, cab_rolloff, cab_notch;
      
      // ── Recalculate ALL coefficients ──
      void recalcAll(float baseSampleRate, int oversampleFactor) {
          effectiveSR = baseSampleRate * oversampleFactor;
          
          // Fixed-frequency filters: full BLT (only on SR change)
          inputHP.setFreq(INPUT_HP_FREQ, effectiveSR);
          v1a_cathodeBypass.setFreq(CATHODE_BYPASS_FREQ, effectiveSR);
          v1a_couplingHP.setFreq(STAGE1_COUPLING_HP, effectiveSR);
          // v1a Miller: recalc handled by DynamicMillerLP.recalc()
          v1b_cathodeBypass.setFreq(CATHODE_BYPASS_FREQ, effectiveSR);
          v1b_couplingHP.setFreq(STAGE2_COUPLING_HP, effectiveSR);
          // v1b Miller: recalc handled by DynamicMillerLP.recalc()
          pi_couplingHP_A.setFreq(PI_COUPLING_HP, effectiveSR);
          pi_couplingHP_B.setFreq(PI_COUPLING_HP, effectiveSR);
          
          ot_leakageLP.makeLowPass(OT_LP_FREQ, 0.707f, effectiveSR);
          
          cab_presence.makePeakEQ(2500.0f, 1.5f, 3.0f, effectiveSR);
          cab_rolloff.makeLowPass(5500.0f, 0.5f, effectiveSR);
          cab_notch.makeNotch(400.0f, 2.0f, effectiveSR);
      }
      
      // ── Recalculate modulated filters only (call per block) ──
      void updateModulated(float toneFreq, float gainLoadFreq) {
          // Use fast path — these change often, sr ratio is always safe
          toneCutFilter.setFreqFast(toneFreq, effectiveSR);
          gainLoadingLP.setFreqFast(gainLoadFreq, effectiveSR);
      }
      
      // ── Reset all states (for OS transition crossfade) ──
      void resetAll() {
          inputHP.reset();
          v1a_cathodeBypass.reset(); v1a_couplingHP.reset();
          v1b_cathodeBypass.reset(); v1b_couplingHP.reset();
          pi_couplingHP_A.reset(); pi_couplingHP_B.reset();
          toneCutFilter.reset(); gainLoadingLP.reset();
          ot_leakageLP.reset();
          cab_presence.reset(); cab_rolloff.reset(); cab_notch.reset();
      }
  };

═══════════════════════════════════════════════════════════════════
MODULATED FILTER STRATEGY
═══════════════════════════════════════════════════════════════════

  Two filters in this plugin have parameters that change at audio-
  related rates:
  
  1. TONE CONTROL (toneCutFilter)
     - fc range: 1.5kHz → 20kHz based on tone knob
     - Modulation rate: per-block (knob is smoothed)
     - Strategy: use setFreqFast() per block. The small-angle
       approximation is valid because even at minimum oversampling
       (1× @ 44.1kHz), the highest tone freq (20kHz) gives
       fc/sr = 0.45, where the fast path error is ~5%. At 2×
       oversampling (88.2kHz), fc/sr = 0.23, error < 1%.
       At 4× (176.4kHz), error is negligible.
     
     DECISION: Use setFreq() (with tan) for the tone filter at 1×
     oversampling, setFreqFast() at 2× and above. The 5% error at
     1× and 20kHz is audible as a slight frequency shift near Nyquist
     that the user won't notice in context but could measure.
     
     void updateTone(float toneKnob) {
         float fc = mapRange(toneKnob, 0.0f, 1.0f, TONE_MIN_FREQ, TONE_MAX_FREQ);
         if (oversampleFactor >= 2)
             filterBank.toneCutFilter.setFreqFast(fc, filterBank.effectiveSR);
         else
             filterBank.toneCutFilter.setFreq(fc, filterBank.effectiveSR);
     }
  
  2. GAIN-DEPENDENT LOADING LP (gainLoadingLP)
     - fc varies with gain pot position (models impedance interaction
       of gain pot Gang B loading V1b output)
     - Modulation rate: per-block (knob is smoothed)
     - Strategy: same as tone. setFreqFast() is always fine here
       because the loading LP frequency is in the low kHz range.

  For both: smooth the knob parameter with a one-pole smoother
  (exponential, ~5ms tau) to avoid zipper noise when recalculating
  coefficients per block.

  struct ParamSmoother {
      float current = 0.0f;
      float decay;  // exp(-1.0 / (tau_sec * controlRate))
      
      void setTarget(float target) {
          current = target + (current - target) * decay;
      }
      
      void init(float tauMs, float controlRate) {
          decay = std::exp(-1.0f / (tauMs * 0.001f * controlRate));
      }
  };

═══════════════════════════════════════════════════════════════════
WHEN COEFFICIENTS GET RECALCULATED — COMPLETE TRIGGER MAP
═══════════════════════════════════════════════════════════════════

  ┌──────────────────────────────┬──────────────────────────────────┐
  │ Event                        │ Action                           │
  ├──────────────────────────────┼──────────────────────────────────┤
  │ prepareToPlay()              │ filterBank.recalcAll(sr, osFact) │
  │                              │ speakerCoupling.recalcFilters()  │
  │                              │ gridConduction.recalcAll(sr*os)  │
  │                              │ psu.recalcDecayCoeffs(sr*os)     │
  │                              │ sag.recalcCoeffs(sr*os)          │
  │                              │ paramSmoothers.init(sr/blockSz)  │
  ├──────────────────────────────┼──────────────────────────────────┤
  │ Oversampling mode change     │ Same as prepareToPlay() above    │
  │ (inside Reconfiguring state) │ + filterBank.resetAll()          │
  │                              │ + gridConduction.recalcAll()     │
  │                              │ + paramSmoothers.init()          │
  │                              │ (see Section 5.1 for full list)  │
  ├──────────────────────────────┼──────────────────────────────────┤
  │ Tone knob moved              │ filterBank.updateModulated()     │
  │ (per block, after smoothing) │ (tone filter only)               │
  ├──────────────────────────────┼──────────────────────────────────┤
  │ Gain knob moved              │ filterBank.updateModulated()     │
  │ (per block, after smoothing) │ (loading LP only, if used)       │
  ├──────────────────────────────┼──────────────────────────────────┤
  │ Volume knob moved            │ No filter recalc needed          │
  │                              │ (pure gain, no filtering)        │
  ├──────────────────────────────┼──────────────────────────────────┤
  │ Power mode switch (15W/7W)   │ No filter recalc needed          │
  │                              │ (changes bias only, not filters) │
  └──────────────────────────────┴──────────────────────────────────┘

═══════════════════════════════════════════════════════════════════
PRE-WARPING: WHEN IT MATTERS AND WHEN IT DOESN'T
═══════════════════════════════════════════════════════════════════

  The tan() in the full BLT formula corrects for frequency cramping
  near Nyquist. Here's when the correction actually matters:

  ┌────────────────┬───────────┬────────────────┬────────────────┐
  │ Filter         │ fc (Hz)   │ fc/sr at 1×44k │ Error w/o warp │
  ├────────────────┼───────────┼────────────────┼────────────────┤
  │ inputHP        │ 7         │ 0.00016        │ ~0.00%         │
  │ cathodeBypass  │ 4.2       │ 0.00010        │ ~0.00%         │
  │ couplingHP 72  │ 72        │ 0.0016         │ ~0.00%         │
  │ piCouplingHP   │ 16        │ 0.00036        │ ~0.00%         │
  │ OT HP          │ 60        │ 0.0014         │ ~0.00%         │
  │ cab resonance  │ 100       │ 0.0023         │ ~0.00%         │
  │ cab notch      │ 400       │ 0.0091         │ ~0.01%         │
  │ cab presence   │ 2500      │ 0.057          │ ~0.5%          │
  │ cab rolloff    │ 5500      │ 0.125          │ ~2.4%          │
  │ OT LP          │ 7000      │ 0.159          │ ~3.9%          │
  │ Miller LP      │ 12000     │ 0.272          │ ~12%           │
  │ tone (max)     │ 20000     │ 0.454          │ ~49%   ← BIG  │
  └────────────────┴───────────┴────────────────┴────────────────┘

  At 4× oversampling (176.4kHz), divide fc/sr by 4 — all errors < 1%.
  
  Conclusion:
    - At 4× OS: setFreqFast() is safe for everything
    - At 2× OS: setFreqFast() safe except tone at max (use setFreq)
    - At 1× OS: use setFreq() for Miller LP, OT LP, cab rolloff,
      cab presence, and tone filter. setFreqFast() for everything else.
    - In practice: just use setFreq() everywhere at 1× (the tan() cost
      is only on SR changes, not per sample), and setFreqFast() for
      modulated filters at 2×+ oversampling.

═══════════════════════════════════════════════════════════════════
INTEGRATION WITH PROCESSBLOCK
═══════════════════════════════════════════════════════════════════

  void processBlock(AudioBuffer<float>& buffer, ...) {
      ScopedNoDenormals noDenormals;
      
      // 1. Smooth parameters (once per block, before processing)
      gainSmoother.setTarget(gainParam);
      toneSmoother.setTarget(toneParam);
      volumeSmoother.setTarget(volumeParam);
      
      // 2. Update modulated filter coefficients (once per block)
      float toneFreq = mapRange(toneSmoother.current, 0, 1, 
                                TONE_MIN_FREQ, TONE_MAX_FREQ);
      filterBank.updateModulated(toneFreq, gainLoadFreq);
      
      // 3. Upsample
      auto osBlock = oversampler->processSamplesUp(buffer);
      
      // 4. Per-sample processing at oversampled rate
      for (int i = 0; i < osBlock.getNumSamples(); ++i) {
          float x = osBlock.getSample(0, i);
          
          x = filterBank.inputHP.process(x);
          x = applyGridConduction(x, v1aGrid);
          x = triodeWaveshaper(x, ...);
          x = filterBank.v1a_cathodeBypass.process(x); // etc...
          // ... full signal chain ...
          
          osBlock.setSample(0, i, x);
      }
      
      // 5. Downsample
      oversampler->processSamplesDown(buffer);
  }
```

---

## 3. DSP Processing Blocks

### 3.1 Input Conditioning
```
Parameters:
  - Input impedance simulation: ~1MΩ (high-Z guitar input)
  - Input coupling cap: HP filter ~7Hz (blocks DC, shapes low-end)

DSP Implementation:
  - 1st order highpass filter, fc ≈ 7Hz
  - Optional input buffer gain normalization
```

### 3.2 Preamp Stage 1 (V1a) — Common Cathode Triode

```
Signal Flow:
  Input → Coupling Cap HP → Grid Circuit → Triode Waveshaper → Plate Load → Cathode Bypass

Key Component Values:
  - Plate resistor: 100K
  - Cathode resistor: 1.5K
  - Cathode bypass cap: 25µF (fc ≈ 4.2Hz — full bypass)
  - Coupling cap (output): 0.022µF
  - Grid leak: 1M

DSP Implementation:
  1. Input coupling highpass: 1st order, fc ≈ 7Hz
  2. Triode waveshaping (see Section 4)
  3. Cathode bypass filter: shapes low-frequency gain
     - Bypassed: full gain ~34dB
     - Unbypassed portion adds negative feedback above fc
  4. Output coupling cap: 1st order HP, fc ≈ 72Hz (with following grid leak)
  5. Dynamic Miller capacitance effect: signal-dependent HF rolloff

═══════════════════════════════════════════════════════════════════
MILLER EFFECT — WHY STATIC FILTERING IS WRONG
═══════════════════════════════════════════════════════════════════

  The Miller effect multiplies a triode's grid-to-plate capacitance
  (C_gp ≈ 1.7pF for 12AX7) by the stage's voltage gain:

    C_miller = C_gp × (1 + |Av|)

  For a 12AX7 at full linear gain (Av ≈ 70):
    C_miller = 1.7pF × 71 = ~121pF

  Combined with the driving source impedance, this forms a 1st-order
  LP filter:
    fc = 1 / (2π × Rsource × C_miller)

  The critical insight: Av is NOT constant. It depends on:
    1. Signal level — when the tube saturates (clips), instantaneous
       gain drops toward zero. C_miller shrinks. The LP opens up.
    2. Operating point — bias shifts from grid conduction or supply
       droop change the stage gain. C_miller changes with it.
    3. Frequency — at HF where the Miller cap is most relevant, the
       effective gain is already reduced by the capacitive loading
       itself, creating a self-limiting feedback.

  What this means for tone:
    - At LOW gain (clean): Av ≈ 70, C_miller is large, HF rolloff
      is strong (~12kHz cutoff). Clean tones are naturally warmer.
    - At HIGH gain (saturated): Av drops during clipping peaks,
      C_miller shrinks dynamically, HF rolloff loosens. Distorted
      tones are brighter than you'd expect from a static 12kHz LP.
    - The net effect: an adaptive HF contour that brightens with
      overdrive — this is a signature characteristic of tube preamps
      that solid-state overdrive pedals struggle to replicate.

═══════════════════════════════════════════════════════════════════
DSP IMPLEMENTATION: DynamicMillerLP
═══════════════════════════════════════════════════════════════════

  struct DynamicMillerLP {
      // ── Component values ──
      float cgp;           // grid-plate capacitance (real Farads)
      float cgk;           // grid-cathode capacitance (real Farads, NOT Miller-multiplied)
      float rSource_n;     // driving source impedance (normalized)
      float rSource_real;  // driving source impedance (real Ohms, for fc calc)
      float baseGain;      // nominal stage voltage gain (dimensionless)
      
      // ── Filter state ──
      OnePoleLP filter;
      float currentFc;     // current cutoff frequency (Hz)
      float fcSmooth;      // smoothed fc to avoid zipper noise
      float fcSmoothDecay; // exp decay for fc smoothing
      
      // ── Precomputed limits ──
      float fcMin;         // fc at full gain (most filtering)
      float fcMax;         // fc at zero gain (no filtering, but capped)
      
      void init(float cgp_farads, float cgk_farads, float rSource_ohms, float gain) {
          cgp = cgp_farads;
          cgk = cgk_farads;
          rSource_real = rSource_ohms;
          rSource_n = VS::r(rSource_ohms);
          baseGain = gain;
          
          // Total input capacitance = Cgk + Cgp × (1 + Av)
          // Cgk is NOT Miller-multiplied — it's a fixed shunt from grid to cathode
          // Cgp IS multiplied by (1 + gain) — the Miller effect
          // At Av=70: Cinput = 1.6pF + 1.7pF×71 = 122.3pF
          // At Av=1:  Cinput = 1.6pF + 1.7pF×2  = 5.0pF
          // Cgk matters more at low gain (when Miller shrinks, Cgk is a larger fraction)
          
          // fc at full linear gain (most filtering)
          float cInputMax = cgk + cgp * (1.0f + baseGain);
          fcMin = 1.0f / (2.0f * M_PI * rSource_real * cInputMax);
          
          // fc when gain drops to ~1 (minimal Miller)
          float cInputMin = cgk + cgp * 2.0f;
          fcMax = 1.0f / (2.0f * M_PI * rSource_real * cInputMin);
          // Cap at Nyquist/2 in practice
          fcMax = std::min(fcMax, 20000.0f);
          
          currentFc = fcMin;
          fcSmooth = fcMin;
      }
      
      void recalc(float effectiveSR) {
          filter.setFreq(fcSmooth, effectiveSR);
          // ~1ms smoothing for fc changes
          fcSmoothDecay = std::exp(-1.0f / (MILLER_FC_SMOOTH_MS * 0.001f * effectiveSR));
      }
      
      // ── Per-sample processing ──
      float process(float signal, float gainEstimate, float effectiveSR) {
          // Clamp gain estimate to sane range
          float av = std::clamp(gainEstimate, 0.5f, baseGain);
          
          // Total input capacitance: Cgk (fixed) + Cgp × (1 + Av) (Miller)
          float cInput = cgk + cgp * (1.0f + av);
          
          // Dynamic cutoff frequency
          float fc = 1.0f / (2.0f * M_PI * rSource_real * cInput);
          fc = std::clamp(fc, fcMin, fcMax);
          
          // Smooth fc changes to avoid per-sample coefficient jitter
          fcSmooth = fc + (fcSmooth - fc) * fcSmoothDecay;
          
          // Update filter coefficient
          filter.setFreqFast(fcSmooth, effectiveSR);
          
          return filter.process(signal);
      }
  };

  // ── Per-stage instances ──
  
  DynamicMillerLP v1a_miller;
  DynamicMillerLP v1b_miller;
  
  // V1a: driven by guitar pickup / input buffer
  // Rsource ≈ pickup impedance or buffer output: ~10KΩ typical
  // But Miller sees the PLATE side, so Rsource for Miller is Rplate||rp
  // For the input stage: Rsource_miller ≈ Rplate || rp ≈ 38.5KΩ
  v1a_miller.init(CGP_12AX7, CGK_12AX7, 38500.0f, 70.0f);  // ← WRONG, corrected below
  //   fcMin = 1/(2π × 38.5K × 121pF) ≈ 34kHz  (at full gain)
  //   fcMax = 1/(2π × 38.5K × 3.4pF)  ≈ capped at 20kHz
  //   Wait — that gives fcMin ABOVE fcMax. The issue is that Rsource
  //   for the Miller calculation is the PREVIOUS stage's output impedance,
  //   not the current stage's plate impedance. Let me correct:
  //
  //   Actually, the Miller input capacitance loads the DRIVING source.
  //   For V1a: Rsource = guitar pickup ≈ 10KΩ (or input buffer)
  //   C_miller_input = C_gp × (1 + Av) + C_gk ≈ 121pF + 1.6pF ≈ 123pF
  //   fc = 1/(2π × 10K × 123pF) ≈ 130kHz  → above audible, minimal effect
  //
  //   For V1b: Rsource = V1a plate output Z + coupling network
  //   Rsource ≈ Rplate||rp + series R = 38.5KΩ + gain pot wiper
  //   At high gain setting: pot wiper ≈ 0Ω, Rsource ≈ 38.5KΩ
  //   fc = 1/(2π × 38.5K × 123pF) ≈ 33kHz  → still above audible at 1×
  //   At low gain setting: pot wiper adds up to 500KΩ
  //   fc = 1/(2π × 500K × 123pF) ≈ 2.6kHz  → VERY audible darkening!

  // CORRECTED instances:
  
  // V1a Miller: input source is low impedance, Miller barely audible
  // (included for completeness but fc stays above 20kHz at all gains)
  v1a_miller.init(CGP_12AX7, CGK_12AX7, 10000.0f, 70.0f);
  
  // V1b Miller: THIS is where the action is.
  // Rsource varies with gain pot position!
  // At gain=max: Rsource ≈ 38.5KΩ (V1a Zout),     fc ≈ 33kHz (inaudible)
  // At gain=mid: Rsource ≈ 250KΩ (pot + Zout),     fc ≈ 5.2kHz (darkening!)
  // At gain=low: Rsource ≈ 500KΩ (full pot + Zout), fc ≈ 2.6kHz (very dark)
  //
  // This means the gain pot doesn't just attenuate — it also DARKENS the
  // tone. This is a fundamental interaction that static Miller cannot model.
  // Low gain settings sound darker not just because of less drive, but
  // because the higher source impedance increases the Miller rolloff.
  v1b_miller.init(CGP_12AX7, CGK_12AX7, 38500.0f, 70.0f);  // base Rsource = V1a Zout
  // Rsource is MODULATED per block based on gain pot position (see below)
```

### 3.3 Gain Control (Dual-Gang 500K Audio)

```
Unique Behavior:
  Gang A: Variable voltage divider AFTER V1a (attenuates signal into V1b)
  Gang B: Variable impedance loading AFTER V1b (attenuates signal into PI)

  At minimum: Both gangs fully attenuate — clean signal barely reaches power tubes
  At maximum: Full preamp gain drives hard into PI

DSP Implementation:
  - Two linked attenuation parameters (0.0 to 1.0)
  - Audio taper curve: gain_linear = pow(knob_position, 2.0) or lookup table
  - Gang A: simple multiply after stage 1 output
  - Gang B: multiply after stage 2 output + slight impedance interaction
    (loading effect can be modeled as gentle LP filter that varies with gain setting)

  // Pseudo-code
  float gainA = audioTaper(gainKnob);  // 0.0 — 1.0
  float gainB = audioTaper(gainKnob);  // linked, same knob
  
  stage1_out *= gainA;
  stage2_out *= gainB;
  // Optional: LP filter on stage2_out with cutoff modulated by gainB
```

### 3.4 Preamp Stage 2 (V1b) — Common Cathode Triode

```
Identical topology to V1a with potentially different bias point due to
different signal levels and the loading from Gang B of the gain pot.

Key Component Values:
  - Plate resistor: 100K
  - Cathode resistor: 1.5K (some revisions: 820Ω — more gain)
  - Cathode bypass cap: 25µF
  - Dropping resistor after plate: 68K (some revisions: 100K)
  - Coupling cap (output): 0.047µF

DSP Implementation:
  Same triode model as Stage 1, potentially with:
  - Different bias point (slightly hotter due to signal chain position)
  - Output coupling cap: HP fc ≈ ~7Hz (with 1M grid leak of PI)
```

### 3.5 Phase Inverter (Long-Tail Pair — Nodal Solver)

```
Circuit:
  ECC83 #2 — both triodes configured as a long-tail pair (differential amplifier).
  This is NOT a cathodyne/split-load inverter — those use a single triode with
  equal plate and cathode resistors. The LTP uses both triodes in a differential
  configuration with a shared tail resistor.

  Topology:
    - Triode A: signal input on grid, plate drives non-inverting output
    - Triode B: grid tied to AC ground via voltage divider (reference),
                plate drives inverting output
    - Shared tail resistor: 1.2K from joined cathodes → ground
      (sets tail current, provides common-mode rejection)
    - Plate load resistors: ~100K each side (matched)
    - Coupling caps to EL84 grids: 0.1µF each

═══════════════════════════════════════════════════════════════════
WHY A NODAL SOLVER INSTEAD OF STATIC IMBALANCE
═══════════════════════════════════════════════════════════════════

  The naive approach:
    vGridA = input * 0.5;  vGridB = -input * 0.5;
    piA = waveshaper(vGridA); piB = waveshaper(vGridB);
    piB *= 0.97;  // static imbalance

  This misses everything that makes the LTP interesting:

  1. CURRENT STEERING: The tail resistor constrains total cathode
     current to Itail ≈ Vbias / Rtail. When one triode draws more
     current (louder signal on its grid), the other MUST draw less.
     The shared cathode voltage rises, biasing both triodes colder.
     This is differential pair behavior — not two independent triodes.

  2. DYNAMIC IMBALANCE: At low signal, both triodes operate in their
     linear region with ~equal gain. At high signal, one triode clips
     (runs out of current or headroom) while the other picks up the
     excess tail current. This creates signal-level-dependent imbalance
     that gets STRONGER with drive — exactly the asymmetric distortion
     character of a driven LTP.

  3. SOFT CURRENT LIMITING: The 1.2K tail resistor (low for an LTP)
     means the total available current is limited. When driven hard,
     the tail current can't increase proportionally — the LTP naturally
     compresses and generates soft crossover harmonics. This is a
     MAJOR contributor to the Silent Riot's crunch character.

  4. CATHODE VOLTAGE MODULATION: The shared cathode node voltage moves
     with the signal, creating common-mode feedback that reduces gain
     for differential signals and increases it for common-mode. This
     affects the effective bias of both triodes dynamically.

═══════════════════════════════════════════════════════════════════
DSP IMPLEMENTATION: NODAL LTP SOLVER
═══════════════════════════════════════════════════════════════════

  // ─── LTP state (persistent across samples) ───
  struct LTPModel {
      float tailR_n;           // tail resistor (normalized, VS::r())
      float rPlateA_n;         // plate load A (normalized)
      float rPlateB_n;         // plate load B (normalized)
      float vBiasRef;          // reference grid DC bias (normalized)
      float vCathode;          // shared cathode node voltage (normalized, state)
      float mu;                // triode amplification factor (dimensionless)
      float rp;                // triode plate resistance (normalized)
      
      void init() {
          tailR_n   = VS::R_1K2;             // 1.2KΩ tail
          rPlateA_n = VS::R_100K;            // 100KΩ plate load A
          rPlateB_n = VS::R_100K;            // 100KΩ plate load B
          vBiasRef  = VS::v(-1.5f);          // DC bias reference
          vCathode  = VS::v(1.5f);           // initial cathode voltage (~1.5V)
          mu        = 100.0f;                // 12AX7 mu
          rp        = VS::r(62500.0f);       // 12AX7 rp ≈ 62.5KΩ
      }
  };

  // ─── Per-sample LTP solver ───
  //
  // Given: input signal (normalized), supply voltage (from PSU)
  // Solves: shared cathode voltage, individual plate currents, output voltages
  //
  // This is a ONE-ITERATION Newton approximation, NOT a full convergence
  // loop. It's accurate enough because:
  //   a) The cathode voltage changes slowly relative to sample rate
  //   b) We use the PREVIOUS sample's cathode voltage as the starting point
  //   c) The triode transfer function is smooth (no discontinuities)
  //   d) At audio rates (44kHz+), one iteration tracks the solution well
  
  struct LTPOutput {
      float piA;      // non-inverting plate output (normalized)
      float piB;      // inverting plate output (normalized)
      float vCathode; // updated cathode voltage for next sample
  };
  
  LTPOutput solveLTP(float vInput_n, float vSupply_n, LTPModel& ltp) {
      // ── Grid voltages (grid-to-cathode) ──
      //
      // vInput_n is the AC signal arriving at triode A's grid.
      // In the real circuit, this signal rides on top of a DC bias
      // set by the grid leak and coupling cap. The vBiasRef encodes
      // the quiescent grid voltage (which includes the DC bias).
      //
      // Triode A: signal grid. Grid voltage = vBiasRef + vInput_n
      //   (signal adds to the DC operating point)
      // Triode B: reference grid. Grid voltage = vBiasRef
      //   (no signal, just the DC bias from the voltage divider)
      //
      // Vgk = Vgrid - Vcathode
      float vGridA = ltp.vBiasRef + vInput_n;
      float vGridB = ltp.vBiasRef;
      
      float vgkA = vGridA - ltp.vCathode;
      float vgkB = vGridB - ltp.vCathode;
      
      // ── Plate currents via triode transfer function ──
      // Use the same waveshaper model as the preamp stages, but
      // with the grid-to-cathode voltage (not grid-to-ground).
      // The triode model returns plate current in normalized domain.
      //
      // For the simplified tanh model:
      //   Ip ≈ triodeGm(Vgk) where gm is the transconductance
      //   This naturally handles cutoff (Ip → 0 for Vgk << 0)
      //   and saturation (Ip → max for Vgk >> 0)
      
      float ipA = triodeCurrentModel(vgkA, ltp.mu, ltp.rp);
      float ipB = triodeCurrentModel(vgkB, ltp.mu, ltp.rp);
      
      // ── Tail current constraint ──
      // Total cathode current flows through tail resistor:
      //   vCathode = (ipA + ipB) × Rtail
      // This is the coupling equation — it links both triodes.
      float iTail = ipA + ipB;
      float vCathodeNew = iTail * ltp.tailR_n;
      
      // One-iteration Newton correction:
      // If the new cathode voltage differs from our assumption,
      // the grid-to-cathode voltages were slightly wrong.
      // Rather than iterate, we accept this and let the next
      // sample use the updated cathode voltage. The error is
      // small (< 1% of signal) and creates no audible artifacts.
      ltp.vCathode = vCathodeNew;
      
      // ── Plate voltages ──
      // Vplate = Vsupply - Ip × Rplate
      float vPlateA = vSupply_n - ipA * ltp.rPlateA_n;
      float vPlateB = vSupply_n - ipB * ltp.rPlateB_n;
      
      // Clamp: plates can't go below cathode (triode saturates)
      // or above supply (no current = plate at supply rail)
      vPlateA = std::clamp(vPlateA, ltp.vCathode, vSupply_n);
      vPlateB = std::clamp(vPlateB, ltp.vCathode, vSupply_n);
      
      // ── Output: AC component of plate voltage ──
      // Remove the DC operating point to get the AC signal
      float dcPlate = vSupply_n * 0.5f;  // approximate quiescent plate V
      float piA = vPlateA - dcPlate;
      float piB = vPlateB - dcPlate;
      
      return { piA, piB, vCathodeNew };
  }
  
  // ─── Triode current model for LTP ───
  //
  // Pure Vgk → Ip transfer function. No internal bias offset — the
  // caller (solveLTP) provides Vgk already referenced to cathode.
  //
  // Uses a 3/2 power law approximation of the Langmuir-Child equation:
  //   Ip ∝ (Vgk + Vgk_cutoff)^(3/2)  for Vgk > cutoff
  //   Ip = 0                          for Vgk ≤ cutoff
  //
  // The cutoff voltage is where the tube stops conducting (~-1.5V for
  // a 12AX7 at typical operating point, but this is already encoded
  // in the Vgk calculation — the solver computes vgk = vGrid - vCathode,
  // so at the quiescent point vgk ≈ 0 and the tube is conducting.
  // Cutoff occurs when vgk goes sufficiently negative.
  
  float triodeCurrentModel(float vgk_n, float mu, float rp_n) {
      // ── Calibrated 3/2 power law for 12AX7 ──
      //
      // Real 12AX7 data: at Vgk = -1.5V, Vp = 250V → Ip ≈ 1.2mA
      // Normalized: vgk_n ≈ -0.005, ip_target ≈ VS::i(0.0012) ≈ 1.2
      //
      // The 3/2 power law: Ip = K × Vgk_eff^1.5
      // where K is a tube-specific perveance constant.
      //
      // We derive K from the known operating point rather than using
      // gm = mu/rp (which is the small-signal LINEAR transconductance,
      // not the large-signal nonlinear transfer coefficient).
      //
      // Calibration:
      //   At Vgk = -1.5V (bias point), triode draws ~1.2mA.
      //   In normalized domain: vgk_n = VS::v(-1.5) = -0.005
      //   After soft rectification: softVgk ≈ V_THERMAL/2 ≈ 0.0000434
      // Calibration (v2.12 — with cutoff offset):
      //   At Vgk = -1.5V (bias point), triode draws ~1.2mA.
      //   In normalized domain: vgk_n = VS::v(-1.5) = -0.005
      //   Cutoff at Vgk = -3V: V_CUTOFF_12AX7_N = VS::v(-3.0) = -0.01
      //   vAboveCutoff = -0.005 - (-0.01) = 0.005
      //   After soft rectification: softVgk ≈ 0.005
      //   Target ip_n = VS::i(0.0012) ≈ 1.2
      //   K = 1.2 / (0.005^1.5) = 1.2 / 0.000354 ≈ 3394
      
      constexpr float K_12AX7 = 3394.0f;  // perveance constant (v2.12 recalibration)
      // VERIFY: Sweep Vgk from -3V to 0V, compare Ip against 12AX7 datasheet
      // curves. Adjust K_12AX7 if the operating point or slope doesn't match.
      
      // Apply cutoff offset — tube conducts when vgk > -3V (V_CUTOFF)
      float vAboveCutoff = vgk_n - V_CUTOFF_12AX7_N;
      
      // Soft rectification: smooth transition through cutoff
      // softVgk ≈ vAboveCutoff when vAboveCutoff >> 0, ≈ 0 when vAboveCutoff << 0
      float v2 = vAboveCutoff * vAboveCutoff + VS::V_THERMAL * VS::V_THERMAL;
      float softVgk = 0.5f * (vAboveCutoff + std::sqrt(v2));
      
      // 3/2 power law: Ip = K × Vgk_eff^1.5
      float ip = K_12AX7 * softVgk * std::sqrt(std::abs(softVgk));
      
      return std::max(0.0f, ip);
  }
  }

═══════════════════════════════════════════════════════════════════
WHAT EMERGES FROM THE SOLVER (vs static model)
═══════════════════════════════════════════════════════════════════

  1. NATURAL GAIN IMBALANCE
     At low signal: both triodes operate near-symmetrically.
     At high signal: one triode clips, the other picks up excess
     tail current → asymmetric outputs → even harmonics.
     The imbalance is SIGNAL-DEPENDENT, not a fixed 3% multiplier.
     
  2. CURRENT STEERING COMPRESSION
     The total tail current is limited by Rtail. When driven hard,
     one triode can't hog all the current — the cathode voltage
     rises, biasing both tubes colder. This is a natural soft limiter
     that the naive model completely misses.
     
  3. SUPPLY SENSITIVITY
     Because vSupply_n feeds directly into the plate voltage
     calculation, PSU droop naturally reduces LTP headroom.
     The PI clips earlier when the supply droops, and the clipping
     is ASYMMETRIC (one plate hits the rail while the other still
     has headroom). This is physically correct and creates the
     dynamic interaction between PSU sag and PI distortion.
     
  4. CROSS-COUPLED DISTORTION
     When triode A clips hard and its current flattens, the cathode
     voltage drops slightly (less total current), which INCREASES
     vgk_B and drives triode B harder. The two halves are cross-
     coupled through the tail — clipping in one half modulates the
     other. This creates intermodulation that static processing
     cannot replicate.

  5. TAIL RESISTOR VALUE MATTERS
     The 1.2K tail (unusually low) means high tail current and
     less differential rejection. The solver naturally captures
     this — a higher tail R would reduce cathode voltage swing
     and increase linearity. Changing one component value changes
     the entire character, without needing to retune magic numbers.

═══════════════════════════════════════════════════════════════════
```

### 3.6 Tone Control (Post-PI "Cut" Control)

```
Circuit:
  500K pot in series with 2.2nF cap, connected between the two EL84 grids.
  This is NOT a traditional tone stack — it's a Vox-style "cut" control.

Behavior:
  - When pot is at minimum resistance: high frequencies pass between the two
    push-pull paths and cancel out → darker tone
  - When pot is at maximum: minimal HF cancellation → brighter tone

DSP Implementation:
  Model as variable high-frequency attenuation of the DIFFERENTIAL signal.
  Common-mode content (even harmonics from PI asymmetry) is preserved.
  
  // The cut control filters only the differential (push-pull) signal
  // between the two grids. Common-mode is untouched.
  float diff = piNonInv - piInv;                         // differential
  float sum  = piNonInv + piInv;                         // common-mode
  float cutoff = map(toneKnob, 0.0, 1.0, 1500.0, 20000.0);
  float filteredDiff = lowpass(diff, cutoff);
  
  // Blend between original diff and filtered diff
  float toneAmt = 1.0f - toneKnob;  // 0 = no cut, 1 = full cut
  float outputDiff = diff + (filteredDiff - diff) * toneAmt;
  
  // Reconstruct push-pull signals with original common-mode
  piNonInv = (sum + outputDiff) * 0.5f;
  piInv    = (sum - outputDiff) * 0.5f;

  // Alternative simpler approach: 
  // 1st order LP on both paths, variable cutoff based on tone knob
  // fc range: ~1.5kHz (tone=0, dark) to ~20kHz (tone=10, bright/bypass)
```

### 3.7 Master Volume (Post-PI)

```
Simple level control after tone circuit, before power tubes.

DSP Implementation:
  float volumeLinear = audioTaper(volumeKnob);
  piNonInv *= volumeLinear;
  piInv *= volumeLinear;
```

### 3.8 Power Amp (2× EL84 Push-Pull)

```
Circuit:
  - 2× EL84 in push-pull, cathode biased: 100Ω shared resistor
  - ~90% bias in 15W mode, ~96% in 7W mode (very hot!)
  - No negative feedback loop

=== CLASS A vs CLASS AB — WHAT THE TINY TERROR ACTUALLY IS ===

  The original amp is marketed as "Class A." This is a simplification.
  True Class A push-pull means both tubes conduct for the FULL 360° of
  the signal cycle — neither tube ever cuts off, regardless of signal
  level. This requires biasing at or above 100% of the maximum signal
  current.

  The Silent Riot's actual operating class depends on the power mode:

  15W mode (~90% bias):
    At idle and low signal: both tubes conduct → Class A behavior.
    At full swing: one tube approaches cutoff before the other reaches
    its peak. The tube doesn't hard-clip off — cathode bias naturally
    shifts to keep partial conduction — but there IS a region where
    one tube carries the bulk of the current.
    → This is technically hot Class AB. The crossover region is soft
    and brief, not the hard notch of a cold-biased Class AB (like a
    Marshall). But it exists and contributes to the amp's harmonic
    character at full tilt.

  7W mode (~96% bias):
    Much closer to true Class A. At 96% of max signal current, the
    tubes barely approach cutoff even at full swing. Both conduct
    through nearly the entire cycle.
    → Practically Class A for all real-world signal levels.
    → This is why 7W mode feels "spongier" and more compressed — the
    tubes are deep in their conducting region with almost no crossover.

  Why this matters for DSP:
    - At low gain / 7W: model as pure Class A (no crossover effects)
    - At high gain / 15W: model the soft crossover transition
    - The crossover is NOT the hard notch of textbook Class B — it's a
      smooth, gradual reduction in one tube's contribution as the other
      takes over. This adds subtle odd-order harmonics at high levels.
    - Orange's "Class A" marketing isn't wrong in spirit — the amp
      operates in Class A for most of its usable range. But the edge
      cases at full power in 15W mode are Class AB territory.

DSP Implementation:
  1. EL84 waveshaping per tube (different from 12AX7 — more headroom, 
     different clipping character: softer knee, more compression)
  2. Push-pull summing with SOFT crossover modeling:
     - NOT hard crossover distortion (that's cold Class B/AB)
     - Model as a smooth conduction overlap function:
       each tube's contribution tapers gradually near its cutoff point
     - The overlap width is controlled by bias percentage:
       90% bias → narrower overlap → subtle crossover harmonics
       96% bias → wide overlap → nearly pure Class A (no crossover)
  3. Cathode bias sag: slow envelope follower that modulates bias point
     - Attack: ~5-20ms (caps charge)
     - Release: ~50-200ms (caps discharge)
     - This creates the "bloom" and "sag" feel
     - SAG ALSO SHIFTS THE CLASS: as the bias point drops under heavy
       signal, the operating point moves TOWARD Class AB temporarily.
       When the signal drops and bias recovers, it moves BACK toward
       Class A. This dynamic class-shifting is a real phenomenon and
       contributes to the amp's "breathing" feel.
  4. Supply voltage droop feeds back from PowerSupplyModel (see 3.11)
     — plate voltage and screen voltage both drop under load, reducing
     headroom and gain across the power stage dynamically

  // Power tube model — conduction overlap for soft crossover
  // Each tube has a conduction function that tapers near cutoff
  //
  // IMPORTANT: effectiveBias is computed from baseBias + sag offset
  // each sample. NEVER use += on the bias — it would accumulate
  // across samples and drift to negative infinity.
  float effectiveBias = el84BaseBias + sag.biasOffset();
  
  float conductionA = softConductionCurve(piNonInv, effectiveBias);
  float conductionB = softConductionCurve(piInv, effectiveBias);
  
  PentodeOutput el84A = pentodeWaveshaper(piNonInv, effectiveBias, psu.vPower(), psu.vScreen());
  PentodeOutput el84B = pentodeWaveshaper(piInv,    effectiveBias, psu.vPower(), psu.vScreen());
  float powerOut = el84A.plateCurrent * conductionA - el84B.plateCurrent * conductionB;
  
  // Soft conduction curve: 1.0 in linear region, tapers to ~0 near cutoff
  // Width of taper depends on bias — hotter bias = wider = more Class A
  //
  // cutoffPoint: grid voltage at which tube fully cuts off (no plate current).
  // For EL84 at Vscreen ≈ 280V: cutoff at Vgk ≈ -10V.
  // In normalized domain: EL84_CUTOFF_N = VS::v(-10.0f) ≈ -0.0333
  float softConductionCurve(float gridVoltage, float bias) {
      float cutoffMargin = gridVoltage - EL84_CUTOFF_N;
      float overlapWidth = bias * 0.5f;  // wider overlap at higher bias %
      return 0.5f * (1.0f + std::tanh(cutoffMargin / overlapWidth));
  }
  
  // ═══════════════════════════════════════════════════════════════
  // SagEnvelope: cathode bias sag — exponential envelope follower
  // ═══════════════════════════════════════════════════════════════
  //
  // Models the shared cathode resistor/capacitor network on the EL84s.
  // Under drive, cathode current charges the cap → cathode voltage rises
  // → effective grid bias shifts negative → less gain, more compression.
  // When signal drops, cap discharges → bias recovers → "bloom."
  //
  // This is SEPARATE from PSU droop (which affects plate/screen supply).
  // Sag = cathode bias shift (operating point). Droop = headroom ceiling.
  // Both driven by power output but on different timescales.

  struct SagEnvelope {
      float envelope = 0.0f;    // current envelope value (normalized, state)
      float attackDecay;         // exp(-1/(attack_sec * effectiveSR)), precomputed
      float releaseDecay;        // exp(-1/(release_sec * effectiveSR)), precomputed
      float depth;               // max bias shift fraction (dimensionless)
      
      void init() {
          envelope = 0.0f;
          depth = SAG_DEPTH;     // 0.15 default
      }
      
      void recalcCoeffs(float effectiveSR) {
          attackDecay  = std::exp(-1.0f / (SAG_ATTACK_MS  * 0.001f * effectiveSR));
          releaseDecay = std::exp(-1.0f / (SAG_RELEASE_MS * 0.001f * effectiveSR));
      }
      
      // Per-sample update: track power output level with asymmetric time constants
      // Call AFTER computing powerOut, BEFORE next sample's bias calculation
      void update(float powerOutputLevel) {
          float level = std::abs(powerOutputLevel);
          float coeff = (level > envelope) ? attackDecay : releaseDecay;
          envelope = level + (envelope - level) * coeff;
          // Anti-denormal snap
          if (envelope < 1e-10f) envelope = 0.0f;
      }
      
      // Returns bias offset to ADD to el84BaseBias for next sample
      // Cathode cap charges under drive → cathode voltage rises →
      // effective Vgk becomes more NEGATIVE → colder bias → less gain.
      // el84BaseBias is already negative (e.g. -0.9), so we subtract
      // the sag amount to make it MORE negative.
      float biasOffset() const {
          return -depth * envelope;  // negative: shifts bias colder
      }
  };

  // ── Usage in processBlock (see Section 2.5) ──
  // BEFORE power amp:
  //   float effectiveBias = el84BaseBias + sag.biasOffset();
  // AFTER power amp:
  //   sag.update(powerOut);
  // Next sample uses the updated envelope.

  // 15W vs 7W: changes BASE bias point and headroom
  // el84BaseBias is set once on power mode switch, NOT per sample
  // effectiveBias = el84BaseBias + sag.biasOffset() (per sample)
  float el84BaseBias = (powerMode == 15W) ? PENTODE_BIAS_15W_N 
                                          : PENTODE_BIAS_7W_N;
  // 7W mode: higher bias % → earlier compression, less headroom
  
  // Report actual plate + screen currents from both tubes to PSU
  float totalPlate  = std::abs(el84A.plateCurrent)  + std::abs(el84B.plateCurrent);
  float totalScreen = el84A.screenCurrent + el84B.screenCurrent;
  psu.reportPowerDirect(totalPlate, totalScreen);

=== MASTER VOLUME vs SAG INTERACTION ===

  The master volume sits between the PI and power amp (Section 3.7).
  This is the correct circuit position for the Silent Riot.
  
  The interaction works as follows:
  
  Volume LOW (bedroom levels):
    - PI runs at full gain — preamp distortion character preserved
    - Volume pot attenuates the PI output before the power tubes
    - Power tubes receive a small signal → small output → minimal sag
    - PSU barely droops → supply voltages near nominal
    - Result: preamp crunch with clean power section
  
  Volume HIGH (stage levels):
    - PI output passes through at full level
    - Power tubes driven hard → large output → heavy sag
    - PSU droops significantly → supply voltages drop
    - Sag shifts bias colder → crossover narrows → more AB harmonics
    - Result: full amp distortion with power section compression
  
  This is the essential character of a single-channel amp with post-PI
  master volume: the gain knob sets the preamp distortion amount, the
  volume knob sets how hard the power section works. At low volume,
  you get preamp-only distortion. At high volume, the power section
  adds its own compression, sag, and harmonic content on top.
  
  The key architectural constraint: the sag envelope and PSU droop
  are both driven by POWER AMP output level (after volume), not by
  preamp level (before volume). This is physically correct — the power
  transformer and output tubes don't know what the preamp is doing,
  they only respond to the current they're actually drawing.
  
  The PI current report (psu.reportPiLevel) is placed BEFORE volume
  because in the real circuit the PI triodes draw the same current
  regardless of where the volume pot is set — the pot is a passive
  attenuator on the PI output, not a current control.
```

### 3.9 Output Transformer + Speaker Interaction (Dynamic Load)

```
═══════════════════════════════════════════════════════════════════
THE PROBLEM WITH STATIC EQ
═══════════════════════════════════════════════════════════════════

  The standard approach — static HP/LP for the OT, static peakEQ/notch
  for the cab — treats the speaker as a one-way filter applied AFTER
  the power amp. This misses the most important interaction in the
  output stage: the speaker's impedance reflects back through the OT
  and changes the LOAD the power tubes see.

  A real speaker is not a resistor. Its impedance varies wildly with
  frequency:

  ┌─────────────────────────────────────────────────────────────────┐
  │                                                                 │
  │  Impedance                                                      │
  │  (Ω)    ╭──╮                                         ╭──       │
  │  40 ─── │  │                                        ╱          │
  │         │  │                                       ╱           │
  │  30 ─── │  │                                      ╱            │
  │         │  │                                     ╱             │
  │  20 ─── │  │                            ╱───────╱              │
  │         │   ╲                          ╱                       │
  │  10 ───  ╲   ╲────────────────────────╱                        │
  │           ╲                                                    │
  │   8 ───    ╰─ minimum (DC resistance)                          │
  │                                                                 │
  │         80Hz              1kHz           5kHz      10kHz        │
  │       (resonance)      (minimum)     (voice coil inductance)   │
  └─────────────────────────────────────────────────────────────────┘

  Celestion G12H-30 (Silent Riot stock speaker):
    - DC resistance (Re): ~6Ω
    - Resonance (fs): ~75Hz, peak impedance ~30-40Ω
    - Minimum impedance: ~8Ω around 400Hz-1kHz
    - Rising impedance above ~2kHz due to voice coil inductance

  Why this matters for tone:
    - The OT reflects speaker impedance to the primary side:
      Zprimary = Zspk × (Np/Ns)²   (turns ratio squared)
    - For a typical 8KΩ:8Ω OT, turns ratio² ≈ 1000
    - At 75Hz resonance: Zspk ≈ 35Ω → Zprimary ≈ 35KΩ
    - At 400Hz minimum: Zspk ≈ 8Ω → Zprimary ≈ 8KΩ
    - At 5kHz rising: Zspk ≈ 20Ω → Zprimary ≈ 20KΩ

  Higher load impedance = higher gain from the pentode (EL84 output
  impedance is high, so it's sensitive to load changes). This means:
    - Bass frequencies near resonance: BOOSTED (high load → high gain)
    - Midrange: FLAT (nominal load → nominal gain)
    - Treble: BOOSTED (rising load → rising gain → "presence" peak)
    - This is why tube amps have a natural mid-scoop without any EQ
    - This is also why different speakers change the amp's CHARACTER,
      not just its frequency response — they change the distortion too

═══════════════════════════════════════════════════════════════════
DSP MODEL: SpeakerCoupling
═══════════════════════════════════════════════════════════════════

  The SpeakerCoupling model sits between the power amp and the OT,
  feeding the speaker's impedance curve back to modulate the power
  amp's effective output level. It replaces the static OT filters
  with a dynamic load interaction.

  struct SpeakerModel {
      // ── Speaker electrical equivalent circuit ──
      // Impedance modeled as: Re + Lvc*s + (Rp || (1/Cp*s + Lp*s))
      //   Re:  DC resistance (voice coil copper)
      //   Lvc: voice coil inductance (HF rise)
      //   Rp:  parallel resistance at resonance peak
      //   Lp:  resonant inductor (with Cp forms resonance)
      //   Cp:  resonant capacitor
      //
      // In DSP: modeled as a frequency-dependent impedance magnitude
      // using two biquad filters that approximate the impedance curve.
      
      // ── Resonance peak (LF hump) ──
      // Bandpass centered at fs with Q matching the speaker's Qms/Qes
      Biquad resonancePeak;       // peaks at ~75Hz, Q ≈ 3-5
      
      // ── Voice coil inductance (HF rise) ──
      // 1st order shelving rise above ~2kHz
      OnePoleLP voiceCoilLP;      // LP to extract signal for HF shelf
      
      // ── Speaker parameters (normalized via VS::) ──
      float re_n;                 // DC resistance (normalized)
      float zpeak_n;              // peak impedance at resonance (normalized)
      float zmin_n;               // minimum impedance (= re, normalized)
      float turnsRatioSq;         // OT turns ratio squared (dimensionless)
      float nominalLoad_n;        // nominal primary impedance (normalized)
      
      void init() {
          re_n          = VS::r(6.0f);         // 6Ω DC
          zpeak_n       = VS::r(35.0f);        // 35Ω at resonance
          zmin_n        = VS::r(8.0f);          // 8Ω minimum
          turnsRatioSq  = 1000.0f;              // 8KΩ:8Ω → N²=1000
          nominalLoad_n = VS::r(8000.0f);       // 8KΩ nominal primary
      }
      
      void recalcFilters(float effectiveSR) {
          // Resonance peak: bandpass centered at speaker fs
          resonancePeak.makePeakEQ(75.0f, 3.5f, 0.0f, effectiveSR);
          // We don't use the peakEQ for gain — we use it as a detector
          // to extract the resonance-frequency content for impedance calc
          
          // Voice coil inductance: LP to separate HF content
          voiceCoilLP.setFreq(2000.0f, effectiveSR);
      }
  };

  // ─── Impedance-modulated output ───
  //
  // Instead of: powerOut → static OT HP/LP → static cab EQ → output
  //
  // We do:  powerOut → impedance calc → gain modulation → OT shaping → output
  //
  // The impedance modulation happens PER SAMPLE and creates a
  // frequency-dependent, signal-dependent gain variation that
  // static EQ cannot replicate.

  struct SpeakerCoupling {
      SpeakerModel speaker;
      
      // ── Impedance estimation filters ──
      // These extract frequency-band content to estimate instantaneous
      // impedance at the dominant frequency of the signal
      Biquad bassExtract;      // BP around speaker resonance (~75Hz)
      Biquad midExtract;       // BP around impedance minimum (~400Hz)
      OnePoleLP hfEnvelope;    // envelope of HF content (>2kHz)
      
      // ── Envelope followers for each band ──
      float bassEnv = 0.0f;
      float midEnv  = 0.0f;
      float hfEnv   = 0.0f;
      float envDecay;          // exp(-1/(tau_env * effectiveSR))
      
      void init() {
          speaker.init();
      }
      
      void recalcFilters(float effectiveSR) {
          speaker.recalcFilters(effectiveSR);
          bassExtract.makePeakEQ(75.0f, 2.0f, 0.0f, effectiveSR);
          midExtract.makePeakEQ(500.0f, 1.0f, 0.0f, effectiveSR);
          hfEnvelope.setFreq(3000.0f, effectiveSR);
          envDecay = std::exp(-1.0f / (0.002f * effectiveSR));  // 2ms tau
      }
      
      void reset() {
          bassExtract.reset();
          midExtract.reset();
          hfEnvelope.reset();
          bassEnv = midEnv = hfEnv = 0.0f;
      }
      
      // ─── Per-sample processing ───
      //
      // Returns: gain-modulated output representing the OT secondary
      // voltage accounting for the speaker's reactive load.
      //
      // The key insight: at frequencies where speaker impedance is HIGH,
      // the pentode "sees" a higher load and produces MORE voltage.
      // At frequencies where impedance is LOW (minimum), the pentode
      // load is nominal and output is at its baseline level.
      //
      // This is NOT the same as EQ. EQ boosts or cuts the signal after
      // the fact. Load-line modulation changes how hard the tubes WORK
      // at each frequency — which changes the distortion character too.
      // Bass notes near resonance drive the tubes into a higher load
      // line → less current, more voltage swing, softer clipping.
      // Mid notes near impedance minimum → more current, harder clipping.
      
      float process(float powerOut) {
          // 1. Extract band content for impedance estimation
          float bassContent = bassExtract.process(powerOut);
          float midContent  = midExtract.process(powerOut);
          float hfContent   = powerOut - hfEnvelope.process(powerOut); // highpass
          
          // 2. Envelope follow each band (rectify + smooth)
          float bAbs = std::abs(bassContent);
          float mAbs = std::abs(midContent);
          float hAbs = std::abs(hfContent);
          bassEnv = std::max(bAbs, bassEnv * envDecay);
          midEnv  = std::max(mAbs, midEnv  * envDecay);
          hfEnv   = std::max(hAbs, hfEnv   * envDecay);
          
          // 3. Estimate dominant impedance from band ratios
          //    When bass dominates → impedance is high (near resonance)
          //    When mid dominates  → impedance is low (near minimum)
          //    When HF dominates   → impedance is rising (voice coil L)
          float total = bassEnv + midEnv + hfEnv + 1e-10f;
          float bassWeight = bassEnv / total;
          float midWeight  = midEnv  / total;
          float hfWeight   = hfEnv   / total;
          
          // Impedance = weighted blend of band impedances (reflected to primary)
          //   bass band: ~35KΩ reflected (resonance peak)
          //   mid band:  ~8KΩ reflected  (impedance minimum = nominal)
          //   HF band:   ~20KΩ reflected (voice coil inductance rise)
          float zReflected = bassWeight * VS::r(35000.0f)
                           + midWeight  * VS::r(8000.0f)
                           + hfWeight   * VS::r(20000.0f);
          
          // 4. Gain modulation: pentode gain ∝ load impedance
          //    At nominal 8KΩ: gain factor = 1.0
          //    At higher Z: gain > 1.0 (tubes swing wider)
          //    At lower Z: gain < 1.0 (tubes loaded harder)
          float gainMod = zReflected / speaker.nominalLoad_n;
          
          // Soft-limit the gain modulation to prevent runaway
          // Real transformers saturate, limiting the maximum swing
          gainMod = 1.0f + std::tanh((gainMod - 1.0f) * 2.0f) * 0.5f;
          // Range: ~0.7 to ~1.5 (±30% from nominal, tanh-limited)
          
          return powerOut * gainMod;
      }
  };

═══════════════════════════════════════════════════════════════════
WHAT THIS REPLACES AND WHAT REMAINS
═══════════════════════════════════════════════════════════════════

  REPLACED by SpeakerCoupling:
    - The static OT HP/LP biquads (ot_highpass, ot_lowpass)
      → OT bandpass character now emerges naturally from the
        impedance interaction — bass below resonance sees low
        impedance (attenuated), treble above voice coil rise
        eventually hits transformer leakage (still modeled as LP)
    - The static cab_resonance PeakEQ
      → Speaker resonance boost now comes from the load-line
        modulation, not a static peak filter

  RETAINED (still applied AFTER SpeakerCoupling):
    - OT leakage inductance LP (~7kHz) — this is a transformer
      property, not a speaker property
    - Cab presence peak (~2.5kHz) — baffle diffraction effect
    - Cab rolloff LP (~5.5kHz) — cone breakup rolloff
    - Cab notch (~400Hz) — cabinet/baffle resonance cancellation

  The static cab filters model the ACOUSTIC behavior of the cabinet
  (box resonances, baffle diffraction, cone breakup). These are
  genuinely one-way and don't interact with the amp electrically.
  The SpeakerCoupling model handles the ELECTRICAL interaction
  between speaker and amplifier.

═══════════════════════════════════════════════════════════════════
OPTIONAL: CONVOLUTION HYBRID
═══════════════════════════════════════════════════════════════════

  For maximum accuracy, combine both approaches:
    1. SpeakerCoupling handles the dynamic load-line interaction
    2. A short IR (50-100ms) handles the acoustic cab response
       (replaces the static cab EQ filters)
  
  This gives you the dynamic "feel" from the impedance coupling
  plus the accurate frequency response from the IR. The IR captures
  the acoustic behavior (mic position, room, cone breakup patterns)
  that can't be replicated with a few biquads.
  
  Signal flow:
    powerOut → SpeakerCoupling → OT leakage LP → convolution IR → output
```

### 3.10 Speaker Simulation / Cabinet IR (Acoustic Response)

```
The cabinet filters model ACOUSTIC properties that don't interact
electrically with the amplifier. These are applied AFTER the speaker
coupling model (Section 3.9).

Options:
  A) Convolution IR (most accurate acoustic response)
     - Load a 1×12 Orange cab IR (Celestion G12H-30 or V30)
     - IR length: 200-500ms sufficient
     - Use partitioned convolution for low latency
     - If using IR, skip the static cab filters below

  B) Minimum-phase IIR approximation (lighter CPU)
     - These model baffle diffraction, cone breakup, and box resonance
     - Speaker RESONANCE boost is NO LONGER here — it comes from
       SpeakerCoupling (Section 3.9) via load-line modulation
     
DSP Implementation (IIR approach, post-SpeakerCoupling):
  // OT leakage inductance (transformer property, not speaker)
  output = lowpass2(output, 7000.0, 0.707);
  // Presence (baffle diffraction)
  output = peakEQ(output, 2500.0, 1.5, +3.0dB);
  // HF rolloff (speaker cone breakup)
  output = lowpass2(output, 5500.0, 0.5);
  // Notch at ~400Hz (typical for closed-back 1x12)
  output = notch(output, 400.0, 2.0, -3.0dB);
```

### 3.11 Power Supply Model (Multi-Stage Droop)

```
The Silent Riot uses a simple solid-state rectified supply with RC filtering.
Under load, the supply voltage droops — and this affects EVERY stage in the amp,
not just the power tubes. This is a huge part of the "feel" and compression
character. Modeling droop only at the power stage is a common oversimplification
that misses half the interaction.

=== REAL CIRCUIT SUPPLY CHAIN ===

  Mains → PT Secondary (~250-300V AC) → Solid-state rectifier → B+ rail
  
  B+ rail feeds through a series of dropper resistors and filter caps:
  
  ┌─────────────────────────────────────────────────────────────┐
  │                                                             │
  │  Rectifier → C1 (main filter) → R_drop1 → C2 → R_drop2 → C3
  │               │                             │                │
  │               ▼                             ▼                ▼
  │          EL84 Plates                   PI Plates        Preamp Plates
  │          (~300V B+)                    (~280V)           (~250V)
  │          (highest current)             (moderate)        (lowest current)
  │                                                             │
  └─────────────────────────────────────────────────────────────┘

  Each RC node has its own time constant:
    - C1 node (power tubes): largest cap, fastest droop, most sag
    - C2 node (PI): medium filtering, couples to power tube droop
    - C3 node (preamp): most filtered, but still affected by upstream droop

=== DSP MODEL — COMPLETE PowerSupplyModel IMPLEMENTATION ===

  // ═══════════════════════════════════════════════════════════════
  // SupplyNode: single RC-filtered supply rail
  // ═══════════════════════════════════════════════════════════════
  
  struct SupplyNode {
      float voltage;           // current voltage at this node (normalized, VS domain)
      float nominalVoltage;    // no-load voltage (normalized)
      float decay;             // exp(-1/(tau_sec * effectiveSR)), precomputed
      float tauMs;             // time constant in real ms (stored for recalc)
      
      void init(float nominal_n, float tau_ms) {
          nominalVoltage = nominal_n;
          voltage = nominal_n;
          tauMs = tau_ms;
          decay = 0.999f;       // safe default until recalc
      }
      
      void recalcDecay(float effectiveSR) {
          decay = std::exp(-1.0f / (tauMs * 0.001f * effectiveSR));
      }
      
      // Exponential tracking toward target voltage
      void track(float target_n) {
          voltage = target_n + (voltage - target_n) * decay;
          // Snap-to-target anti-denormal
          if (std::abs(voltage - target_n) < 1e-10f)
              voltage = target_n;
      }
  };

  // ═══════════════════════════════════════════════════════════════
  // CurrentReport: per-stage current draw reported by processing stages
  // ═══════════════════════════════════════════════════════════════
  //
  // Each processing stage reports its current draw to the PSU after
  // computing its output. The PSU model then uses these reports on the
  // NEXT sample to update supply voltages. This one-sample delay is
  // physically accurate (current draw at time t affects voltage at t+dt)
  // and avoids circular dependency.
  //
  // Current draw is estimated from signal level and operating point:
  //   - Triode: Ip ≈ |vPlateSwing| / Rplate (simplified)
  //   - Pentode: Ip ≈ |powerOutput| / loadImpedance
  //   - Screen: Ig2 ≈ Ip * SCREEN_CURRENT_RATIO
  //
  // All currents are in normalized domain (VS::i()).

  struct CurrentReport {
      float power;     // EL84 plate current (normalized)
      float screen;    // EL84 screen current (normalized)
      float pi;        // PI plate current (normalized)
      float preamp;    // preamp plate current (normalized)
      
      void reset() { power = screen = pi = preamp = 0.0f; }
  };

  // ═══════════════════════════════════════════════════════════════
  // PowerSupplyModel: 4-node cascaded RC supply with current reporting
  // ═══════════════════════════════════════════════════════════════

  class PowerSupplyModel {
  public:
      // ── Supply nodes ──
      SupplyNode powerNode;    // EL84 plates — B+ after C1 (fastest sag)
      SupplyNode screenNode;   // EL84 screens — after R_screen (fastest droop)
      SupplyNode piNode;       // PI plates — after R_drop1 + C2 (medium)
      SupplyNode preampNode;   // Preamp plates — after R_drop2 + C3 (slowest)
      
      // ── Current state (from previous sample's reports) ──
      CurrentReport currentDraw;
      
      // ── Circuit constants (all normalized via VS::) ──
      float sourceImpedance_n;  // PT winding + rectifier series R
      float rDrop1_n;           // dropper resistor: power → PI
      float rDrop2_n;           // dropper resistor: PI → preamp
      float rScreen_n;          // screen grid dropping resistor
      
      // ─────────────────────────────────────────────────────────
      // INITIALIZATION
      // ─────────────────────────────────────────────────────────
      
      void init() {
          // Supply node voltages and time constants
          powerNode.init(  VS::v(300.0f),  10.0f);   // 300V, tau=10ms
          screenNode.init( VS::v(280.0f),   6.0f);   // 280V, tau=6ms (fastest)
          piNode.init(     VS::v(280.0f),  35.0f);   // 280V, tau=35ms
          preampNode.init( VS::v(250.0f),  80.0f);   // 250V, tau=80ms (slowest)
          
          // Dropper resistors (normalized)
          sourceImpedance_n = VS::r(24000.0f);  // PT + rectifier ≈ 24KΩ
          rDrop1_n          = VS::r(4700.0f);   // 4.7KΩ power → PI dropper
          rDrop2_n          = VS::r(22000.0f);  // 22KΩ PI → preamp dropper
          rScreen_n         = VS::r(470.0f);    // 470Ω screen dropping resistor
          
          currentDraw.reset();
      }
      
      // ─────────────────────────────────────────────────────────
      // RECALCULATE DECAY COEFFICIENTS
      // Call on: prepareToPlay(), oversampling mode change
      // ─────────────────────────────────────────────────────────
      
      void recalcDecayCoeffs(float effectiveSampleRate) {
          powerNode.recalcDecay(effectiveSampleRate);
          screenNode.recalcDecay(effectiveSampleRate);
          piNode.recalcDecay(effectiveSampleRate);
          preampNode.recalcDecay(effectiveSampleRate);
      }
      
      // ─────────────────────────────────────────────────────────
      // CURRENT REPORTING API
      //
      // Each processing stage calls its report method after computing
      // output. Reports accumulate in currentDraw and are consumed on
      // the next call to update(). This gives a one-sample delay
      // between current draw and supply response — physically correct
      // and avoids circular dependency.
      // ─────────────────────────────────────────────────────────
      
      // ── PREFERRED: report plate and screen current separately ──
      // Use this when the pentode waveshaper returns PentodeOutput
      // with both currents computed from the actual operating point.
      // Screen current is plate-voltage-dependent (see Section 4.2),
      // so this is more accurate than a fixed ratio.
      
      void reportPowerDirect(float plateCurrent_n, float screenCurrent_n) {
          currentDraw.power = plateCurrent_n;
          currentDraw.screen = screenCurrent_n;
      }
      
      // ── CONVENIENCE: report plate current, estimate screen as fixed ratio ──
      // Less accurate — screen-to-plate ratio actually varies with plate
      // voltage (higher at low Vp due to current redistribution). Use
      // reportPowerDirect() when possible.
      
      void reportPowerCurrent(float plateCurrent_n) {
          currentDraw.power = plateCurrent_n;
          currentDraw.screen = plateCurrent_n * SCREEN_CURRENT_RATIO;
      }
      
      void reportPiCurrent(float piCurrent_n) {
          currentDraw.pi = piCurrent_n;
      }
      
      void reportPreampCurrent(float preampCurrent_n) {
          currentDraw.preamp += preampCurrent_n;  // accumulates — called once per preamp stage
      }
      
      // Reset accumulators at start of each sample (before reporting)
      void resetCurrentDraw() {
          currentDraw.preamp = 0.0f;
          // power, screen, pi are reported once each — no accumulation needed
      }
      
      // ── FALLBACK: estimate current from signal level and load ──
      // Least accurate — use only if you don't have waveshaper output
      
      void reportPowerLevel(float powerOutputLevel_n) {
          float ip = std::abs(powerOutputLevel_n) / VS::r(8000.0f);
          reportPowerCurrent(ip);  // uses fixed screen ratio
      }
      
      void reportPiLevel(float piOutputLevel_n) {
          float ip = std::abs(piOutputLevel_n) / VS::R_100K;
          reportPiCurrent(ip);
      }
      
      void reportPreampLevel(float preampOutputLevel_n) {
          float ip = std::abs(preampOutputLevel_n) / VS::R_100K;
          reportPreampCurrent(ip);
      }
      
      // ─────────────────────────────────────────────────────────
      // PER-SAMPLE UPDATE
      //
      // Call once per sample, BEFORE processing the audio stages.
      // Uses current reports from the PREVIOUS sample (one-sample
      // delay = physically accurate causal response).
      //
      // Signal flow:
      //   Rectifier → [sourceR] → C1 (powerNode)
      //                              │
      //                         [R_screen] → screenNode
      //                              │
      //                         [R_drop1] → C2 (piNode)
      //                                        │
      //                                   [R_drop2] → C3 (preampNode)
      //
      // Each node tracks a target voltage that depends on:
      //   - upstream node's current voltage
      //   - local current draw through the dropper resistor
      //
      // ─────────────────────────────────────────────────────────
      
      void update() {
          float totalDraw = currentDraw.power + currentDraw.screen
                          + currentDraw.pi + currentDraw.preamp;
          
          // ── Rectifier output: B+ sags under total load ──
          // vRect = nominal B+ minus voltage drop across source impedance
          float vRect = powerNode.nominalVoltage 
                      - (totalDraw * sourceImpedance_n);
          
          // ── C1: Power node (EL84 plates) ──
          // Fastest response. Tracks rectifier output minus power stage draw.
          // Heavy power tube current pulls this node down immediately.
          powerNode.track(vRect);
          
          // ── Screen node: fed from power node through R_screen ──
          // Screen current creates additional voltage drop through R_screen.
          // Screen node has its OWN time constant (smaller cap = faster).
          // This means screen voltage dips BEFORE plate voltage on transients.
          float vScreenTarget = powerNode.voltage 
                              - (currentDraw.screen * rScreen_n);
          screenNode.track(vScreenTarget);
          
          // ── C2: PI node ──
          // Fed from power node through R_drop1. PI current creates
          // additional drop. Inherits power node droop + own local droop.
          float vPiTarget = powerNode.voltage 
                          - (currentDraw.pi * rDrop1_n);
          piNode.track(vPiTarget);
          
          // ── C3: Preamp node ──
          // Fed from PI node through R_drop2. Most filtered, slowest.
          // Inherits all upstream droop + own minimal local droop.
          float vPreampTarget = piNode.voltage 
                              - (currentDraw.preamp * rDrop2_n);
          preampNode.track(vPreampTarget);
      }
      
      // ─────────────────────────────────────────────────────────
      // ACCESSORS (for processing stages to read supply voltages)
      // ─────────────────────────────────────────────────────────
      
      float vPower()  const { return powerNode.voltage; }
      float vScreen() const { return screenNode.voltage; }
      float vPi()     const { return piNode.voltage; }
      float vPreamp() const { return preampNode.voltage; }
      
      // ── Debug: convert all nodes to real volts for scope comparison ──
      struct DebugState {
          float powerV, screenV, piV, preampV;
          float totalCurrentMa;
      };
      
      DebugState getDebugState() const {
          return {
              VS::toVolts(powerNode.voltage),
              VS::toVolts(screenNode.voltage),
              VS::toVolts(piNode.voltage),
              VS::toVolts(preampNode.voltage),
              VS::toAmps(currentDraw.power + currentDraw.screen
                       + currentDraw.pi + currentDraw.preamp) * 1000.0f
          };
      }
  };

  // ═══════════════════════════════════════════════════════════════
  // INTEGRATION INTO processBlock
  // ═══════════════════════════════════════════════════════════════
  //
  // The update-then-process order ensures causal response:
  //
  //   for (int i = 0; i < numSamples; ++i) {
  //       // 1. Update PSU using previous sample's current reports
  //       psu.update();
  //       
  //       // 2. Read supply voltages for this sample
  //       float vPreamp = psu.vPreamp();
  //       float vPi     = psu.vPi();
  //       float vPlate  = psu.vPower();
  //       float vScreen = psu.vScreen();
  //       
  //       // 3. Process preamp stages
  //       float v1a_out = triodeWaveshaper(v1a_in, bias, vPreamp, drive);
  //       psu.reportPreampLevel(v1a_out);
  //       
  //       // 4. Process PI (nodal LTP solver)
  //       LTPOutput ltp = solveLTP(piInput, vPi, ltpModel);
  //       psu.reportPiLevel(max(abs(ltp.piA), abs(ltp.piB)));
  //       
  //       // 5. Process power amp — waveshaper returns BOTH currents
  //       PentodeOutput ppA = pentodeWaveshaper(elA_in, bias, vPlate, vScreen);
  //       PentodeOutput ppB = pentodeWaveshaper(elB_in, bias, vPlate, vScreen);
  //       float powerOut = ppA.plateCurrent * condA - ppB.plateCurrent * condB;
  //       
  //       // 6. Report actual plate + screen currents (not estimated)
  //       psu.reportPowerDirect(
  //           abs(ppA.plateCurrent)  + abs(ppB.plateCurrent),
  //           ppA.screenCurrent + ppB.screenCurrent
  //       );
  //       
  //       // ... rest of signal chain ...
  //   }
  //
  // ═══════════════════════════════════════════════════════════════
  // CURRENT REPORTING: THREE ACCURACY TIERS
  // ═══════════════════════════════════════════════════════════════
  //
  // Tier 1 — PREFERRED: reportPowerDirect(plateCurrent, screenCurrent)
  //   Both pentode waveshapers now return PentodeOutput with actual
  //   plate current AND screen current computed from the operating point.
  //   Screen current uses a plate-voltage-dependent ratio:
  //     screenRatio = 0.25 + (1 - plateLimit) * 0.15
  //   At healthy plate voltage: 25% to screen (normal operation)
  //   At saturated plate voltage: up to 40% to screen (current redistribution)
  //   This creates a physical feedback loop: heavy drive → plate sags →
  //   more current to screen → screen sags harder → gain drops faster.
  //
  // Tier 2 — CONVENIENCE: reportPowerCurrent(plateCurrent)
  //   Estimates screen as fixed 25% of plate. Less accurate because it
  //   misses the plate-voltage-dependent redistribution effect. Still
  //   usable for quick prototyping.
  //
  // Tier 3 — FALLBACK: reportPowerLevel(signalLevel)
  //   Estimates plate current from output level via Ip ≈ |Vout| / Zload.
  //   Least accurate — double approximation. Use only if the waveshaper
  //   doesn't return current directly.
  //
  // For triode stages (preamp, PI): reportXxxLevel() is fine because
  // triode current is approximately proportional to plate swing, and
  // these stages have minimal sag influence anyway (preamp droop ≤5%).
  //
  // The key insight: it doesn't need to be perfectly accurate to SOUND
  // right. Getting the TIME CONSTANTS right matters more than getting
  // the absolute current values right. If the sag has the right shape
  // and speed, it will feel correct even if voltages are off by 10-20%.
  //
  // Tuning approach:
  //   1. Get time constants close (from schematic RC values)
  //   2. Adjust dropper resistances to set sag DEPTH (how far it drops)
  //   3. Listen: does a palm-muted power chord have the right "bloom"?
  //   4. Fine-tune sourceImpedance for overall sag amount
  //   5. Compare 15W vs 7W mode — 7W should sag harder (higher bias %)
  //   6. Check screen sag timing: transient attack should "squash" before
  //      plate sag "bloom" kicks in (screen tau=6ms vs plate tau=10ms)

=== HOW DROOP AFFECTS EACH STAGE ===

  Preamp (V1a, V1b):
    - Reduced plate voltage → lower headroom → earlier clipping
    - Gain drops slightly (plate resistance changes)
    - Effect is subtle but adds "squish" on sustained chords
    - Slowest response — acts like a slow compressor on the preamp

  Phase Inverter:
    - Reduced B+ → reduced swing before clipping
    - Asymmetry increases (one triode runs out of headroom first)
    - Medium response time — noticeable on hard transients

  Power Amp (EL84s):
    - Most dramatic effect — direct sag on plate voltage
    - Reduced plate voltage = reduced output power = compression
    - Screen voltage also drops → changes pentode transfer curve:
        Screen droop reduces GAIN (not just headroom) — the tubes
        produce less current for the same grid drive. This is a
        fundamentally different compression character than plate sag.
        Plate sag = hard ceiling drops. Screen sag = soft slope flattens.
    - Screen sag is FASTER than plate sag (smaller filter cap, higher
      series resistance) → screen dips first on transients, pulling
      gain down before the plate voltage fully sags. This temporal
      separation creates the "squash then bloom" attack envelope:
        0-3ms:  screen dips → gain drops → initial transient softened
        3-15ms: plate dips → headroom drops → sustain compresses
        15ms+:  caps recharge → gain and headroom recover → bloom
    - Fastest response — the "bloom" and "punch" feeling
    - Attack transients hit → voltage dips → sustain swells back as
      caps recharge = the classic "tube amp feel"

  Interactions:
    - Power tube droop pulls down PI supply (cascading compression)
    - At full tilt, even the preamp supply droops, reducing gain into
      the PI, which reduces power tube drive, which is a natural
      negative feedback loop — this is why tube amps "self-limit"
      and feel dynamically controlled even without an NFB loop
```

---

## 4. Tube Waveshaping Models

### 4.1 12AX7 / ECC83 Triode Model

The core nonlinearity. All inputs and outputs are in the normalized voltage domain (Section 2.5). Every model receives the supply voltage from its PSU node and clips against it.

#### Option A: Asymmetric Tanh (Simplest)
```cpp
// All arguments in normalized domain via VoltageScale
float triodeWaveshaper(float vGrid_n, float vBias_n, float vSupply_n, float drive) {
    float x = vGrid_n * drive + vBias_n;
    
    // Asymmetric clipping: harder on negative (grid conduction), 
    // softer on positive (cutoff)
    float shaped;
    if (x >= 0.0f)
        shaped = std::tanh(x);                       // soft positive clip
    else
        shaped = std::tanh(x * 1.5f) / 1.5f;         // harder negative clip
    
    // Scale output by gain factor (dimensionless, not normalized)
    shaped *= vSupply_n;  // output swing proportional to available supply
    
    // Clip against supply ceiling — this is where PSU droop becomes audible
    float ceiling = vSupply_n * HEADROOM_FACTOR;
    return std::clamp(shaped, -ceiling, ceiling);
}
```

```
═══════════════════════════════════════════════════════════════════
AI/DEVELOPER DISAMBIGUATION — COMMON MISTAKES
═══════════════════════════════════════════════════════════════════

  These patterns have caused bugs in scaffold generation. Read
  carefully before implementing ANY tube model code.

  MISTAKE 1: Creating a separate TRIODE_DRIVE constant

    The `drive` parameter in triodeWaveshaper IS the stage gain.
    processBlock calls triodeWaveshaper(x, bias, supply, StageGains::V1A_GAIN).
    V1A_GAIN = 70.0 is the drive. There is no separate saturation constant.
    Do NOT create a "TRIODE_DRIVE = 2.0" or similar — it doesn't exist
    in the circuit. The 12AX7's voltage gain of ~70× IS the drive.

  MISTAKE 2: Subtracting bias instead of adding

    WRONG: float x = (vGrid_n - vBias_n) * drive;
    RIGHT: float x = vGrid_n * drive + vBias_n;

    vBias_n is ALREADY NEGATIVE (VS::v(-1.5f) = -0.005 for triode,
    VS::v(-9.6f) = -0.032 for pentode). The + operator combined with
    the negative value produces the correct subtraction. Using - would
    DOUBLE the subtraction.

    This applies to BOTH triodeWaveshaper and pentodeWaveshaper.
    In the pentode: vg1 = vGrid + vBias (addition, not subtraction).

  MISTAKE 3: Confusing triode bias vs pentode bias scale

    Triode:  vBias_n = VS::v(-1.5f) = -0.005  (small — grid bias)
    Pentode: vBias_n = VS::v(-9.6f) = -0.032  (cathode bias, 15W mode)

    These are fundamentally different electrical quantities that happen
    to share the parameter name "vBias". The triode bias is the DC grid
    operating point (~-1.5V). The pentode bias is the cathode voltage
    determined by the shared cathode resistor (100Ω × 96mA = 9.6V).
    NOTE: The old value of -270V/-0.9 was WRONG — that was the plate
    supply voltage, not the cathode bias. Fixed in v2.12.

  MISTAKE 4: Scaling output by stageGain × V_GUITAR

    WRONG: shaped *= stageGain * VS::V_GUITAR;
    RIGHT: shaped *= vSupply_n;

    The output swing is proportional to the supply voltage only.
    The stage gain is already applied to the INPUT (as the drive
    parameter). Multiplying the output by gain again squares it.

  MISTAKE 5: Using tanh for plate saturation in pentode

    WRONG: float plateLimit = std::tanh(vPlate_n / V_PLATE_KNEE_N);
    RIGHT: float plateLimit = 1.0f - std::exp(-vPlate_n / V_PLATE_KNEE_N);

    tanh(x) and 1-exp(-x) have different shapes. The exponential form
    correctly models the plate current saturation knee where the tube
    transitions from pentode mode (plate has headroom) to triode mode
    (plate is starved). tanh is too symmetric.

  MISTAKE 6: reportPreampLevel takes signal level, not current

    reportPreampLevel(plateSwing_n) internally divides by R_100K to
    estimate current. Do NOT pass a pre-computed current value — that
    would double-convert. Pass the plate output voltage swing directly.

  MISTAKE 7: softConductionCurve must use bias-dependent overlap

    The overlap width between Class A and AB depends on the bias point:
      float overlapWidth = vBias * 0.5f;
    PENTODE_BIAS_15W_N is now -0.032 (9.6V cathode), not -0.9 (270V) (narrower → more AB character)
    At -0.96 (7W):  overlapWidth = -0.48 (wider → more Class A)
    Do NOT use a fixed scaling factor like 3.0f — this eliminates the
    7W/15W character difference that the bias modes are designed to create.
```

#### Option B: Koren Model (More Accurate, Pre-Normalized)
```cpp
// Koren constants pre-normalized to DSP domain using VoltageScale
// Original real-world values shown in comments for traceability
struct KorenTriodeParams {
    float mu;           // amplification factor (dimensionless, no normalization needed)
    float ex;           // exponent (dimensionless)
    float kg_n;         // grid constant (normalized — see derivation below)
    float kp;           // plate constant (dimensionless ratio, no normalization)
    float kvb_n;        // knee voltage (normalized: VS::v(kvb_real))
    
    // kg_n derivation:
    //   Real domain:  Ip_real = E1_real^ex / Kg_real       [amps]
    //   Normalized:   E1_n = E1_real * V_TO_N
    //                 E1_n^ex = E1_real^ex * V_TO_N^ex
    //                 Ip_n = Ip_real * I_TO_N
    //   Therefore:    Ip_n = (E1_n^ex / V_TO_N^ex) / Kg_real * I_TO_N
    //                      = E1_n^ex / (Kg_real * V_TO_N^ex / I_TO_N)
    //   So:           kg_n = Kg_real * V_TO_N^ex / I_TO_N
    //
    // VERIFY: Write a unit test. Feed known Vg=-1.5V, Vp=200V through both
    // real Koren and normalized Koren. Confirm Ip matches to 3+ sig figs.
    
    static constexpr KorenTriodeParams ECC83() {
        return {
            .mu    = 100.0f,
            .ex    = 1.4f,
            .kg_n  = 1060.0f * std::pow(VS::V_TO_N, 1.4f) / VS::I_TO_N,
            .kp    = 600.0f,
            .kvb_n = VS::v(300.0f),
        };
    }
};

float korenTriode(float vGrid_n, float vPlate_n, const KorenTriodeParams& p) {
    // All inputs already normalized — Koren equation works directly
    float e1 = (vPlate_n / p.kp) 
             * std::log(1.0f + std::exp(
                 p.kp * (1.0f / p.mu + vGrid_n 
                         / std::sqrt(p.kvb_n + vPlate_n * vPlate_n))));
    
    float ip_n = (std::pow(std::max(e1, 0.0f), p.ex)) / p.kg_n;
    return ip_n;  // plate current in normalized domain
}
```

#### Option C: Lookup Table + Interpolation (Best Performance)
```cpp
// LUT generated from Koren model at nominal supply (vPlate_n = VS::V_PREAMP)
// Input range: [-VS::V_PREAMP, +VS::v(1.0f)] normalized grid voltage
// Output range: [0, VS::V_PREAMP] normalized plate current
// At runtime, scale output by (actual vSupply / nominal vSupply) to track droop

float triodeLUT(float vGrid_n, float vSupply_n) {
    // Map normalized grid voltage to table index
    float rangeMin = -VS::V_PREAMP;
    float rangeMax = VS::v(1.0f);
    float normalized = (vGrid_n - rangeMin) / (rangeMax - rangeMin);
    float index = normalized * (TABLE_SIZE - 1);
    
    int i = (int)index;
    i = std::clamp(i, 1, TABLE_SIZE - 3);
    float frac = index - i;
    
    float ip_n = cubicInterp(table[i-1], table[i], table[i+1], table[i+2], frac);
    
    // Scale by supply ratio to track PSU droop
    ip_n *= vSupply_n / VS::V_PREAMP;
    
    return ip_n;
}
```

#### 4.1.1 Grid Conduction Loading — MVP MODEL (Used in processBlock)

```
═══════════════════════════════════════════════════════════════════
WHAT IS GRID CONDUCTION?
═══════════════════════════════════════════════════════════════════

In normal operation, a triode's control grid is NEGATIVE relative to
the cathode. No current flows into the grid — it's a voltage-controlled
device with effectively infinite input impedance. The driving stage
sees a pure resistive load (the grid leak resistor, typically 1MΩ).

When the signal swings the grid POSITIVE relative to the cathode, the
grid-cathode junction acts like a forward-biased diode. Current flows
INTO the grid. This has three effects:

  1. INPUT IMPEDANCE COLLAPSE
     The grid's input impedance drops from ~1MΩ to a few hundred ohms.
     The driving stage (previous plate + coupling cap + grid leak) is
     suddenly loaded heavily. The signal voltage DROPS because the
     source impedance of the previous stage (~100K plate resistor) forms
     a voltage divider with the now-low grid impedance.

  2. ASYMMETRIC CLIPPING ON THE INPUT SIDE
     The positive-going signal gets clipped/compressed by the loading
     before it even reaches the waveshaper. Meanwhile the negative-going
     signal (grid more negative) sees no loading at all. This creates
     asymmetric distortion that is NOT modeled by the waveshaper alone.

  3. COUPLING CAP CHARGE BIAS SHIFT
     Grid current flows through the coupling capacitor, charging it.
     This shifts the DC bias point of the grid NEGATIVE (the cap
     accumulates charge that opposes the signal). The tube "biases
     itself colder" under heavy drive. This is called GRID BLOCKING
     or BIAS SHIFT and creates:
       - Volume drop on sustained notes (the bias shifts away from
         the linear region)
       - "Gating" effect at extreme settings (tube cuts off entirely
         until the cap discharges through the grid leak)
       - The recovery time is set by the grid leak R × coupling cap C
         time constant: tau = 1MΩ × 0.022µF = 22ms for V1a

═══════════════════════════════════════════════════════════════════
WHERE IT HAPPENS IN THE TINY TERROR
═══════════════════════════════════════════════════════════════════

  V1a grid:  Rarely — guitar signal is too small to hit grid conduction
             UNLESS using very hot pickups (active EMGs, boosted signal).
             Grid conduction threshold ≈ +0.5V above cathode voltage.

  V1b grid:  FREQUENTLY at high gain settings. V1a output can swing
             ±50-90V at the plate. After the coupling cap and gain pot,
             the signal reaching V1b's grid can easily exceed the
             conduction threshold. This is a primary contributor to
             the Silent Riot's crunch character at gain >6.

  PI grids:  FREQUENTLY at high gain. V1b output drives the PI hard,
             and the LTP input grid can be driven into conduction.
             The PI's input triode then loads V1b's output, further
             compressing the signal.

  EL84 grids: ROUTINELY in 15W mode at full tilt. The PI drives the
              power tubes hard enough that grid conduction loads the PI
              output. This is part of the power section's compression
              character (on top of bias sag and supply droop).

═══════════════════════════════════════════════════════════════════
DSP MODEL — THREE COMPONENTS
═══════════════════════════════════════════════════════════════════

── Component 1: Dynamic Input Impedance ──

  When the grid voltage exceeds the conduction threshold, the input
  impedance drops. Model as a soft voltage divider that engages above
  the threshold.

  struct GridConductionModel {
      float conductionThreshold;  // grid voltage where conduction starts (normalized, VS::v())
      float gridResistance;       // effective grid R during conduction (normalized, VS::r())
      float sourceResistance;     // driving stage output impedance (normalized, VS::r())
      float couplingCapCharge;    // accumulated DC shift on coupling cap (normalized voltage)
      float gridLeakR;           // grid leak resistance (normalized, VS::r()) — for display only
      float couplingCapValue;    // coupling cap value in FARADS (real, for tau calc only)
      float chargeDecay;         // exp(-1/(R_leak_real * C_couple * sr)), precomputed
  };

  float applyGridConduction(float vGrid, GridConductionModel& model) {
      float vAboveThreshold = vGrid - model.conductionThreshold;
      
      if (vAboveThreshold <= 0.0f) {
          // Below threshold: no conduction, infinite grid impedance
          // Only discharge existing coupling cap charge
          model.couplingCapCharge *= model.chargeDecay;
          if (std::abs(model.couplingCapCharge) < 1e-10f)
              model.couplingCapCharge = 0.0f;  // snap-to-zero: prevent denormal
          return vGrid - model.couplingCapCharge;
      }
      
      // ── Above threshold: grid conducts ──
      
      // Impedance loading: voltage divider between source R and grid R
      // The harder the grid is driven, the lower the effective impedance
      float conductionDepth = std::tanh(vAboveThreshold * 3.0f);  // 0→1
      float effectiveGridR = model.gridResistance / conductionDepth;
      float loadingFactor = effectiveGridR / (effectiveGridR + model.sourceResistance);
      
      // Clamp the positive swing — the grid "eats" the excess voltage
      float vLoaded = model.conductionThreshold 
                    + vAboveThreshold * loadingFactor;
      
      // ── Coupling cap charge accumulation (bias shift) ──
      // Grid current charges the coupling cap, shifting bias negative
      float gridCurrent = vAboveThreshold * (1.0f - loadingFactor);
      model.couplingCapCharge += gridCurrent * GRID_CHARGE_RATE;
      
      // Cap discharge through grid leak (always active, exponential)
      model.couplingCapCharge *= model.chargeDecay;
      if (std::abs(model.couplingCapCharge) < 1e-10f)
          model.couplingCapCharge = 0.0f;  // snap-to-zero: prevent denormal
      
      // Apply the accumulated bias shift
      return vLoaded - model.couplingCapCharge;
  }

── Component 2: Per-Stage Instances ──

  Each grid in the amp gets its own GridConductionModel with appropriate
  parameters. All values use VoltageScale (VS::) — no raw magic numbers.

  // V1a grid — mild, only with very hot inputs
  GridConductionModel v1aGrid = {
      .conductionThreshold = VS::v(0.5f),              // 0.5V grid conduction onset
      .gridResistance      = VS::R_300,                // 300Ω during conduction
      .sourceResistance    = VS::r(100.0f),            // guitar pickup ~100Ω
      .couplingCapCharge   = 0.0f,
      .gridLeakR           = VS::R_1M,                 // 1MΩ grid leak
      .couplingCapValue    = 22e-9f,                   // 22nF (real, for tau calc only)
      // tau = R_leak_real * C_couple = 1MΩ × 22nF = 22ms
  };

  // V1b grid — aggressive at high gain
  GridConductionModel v1bGrid = {
      .conductionThreshold = VS::v(0.5f),
      .gridResistance      = VS::R_300,
      .sourceResistance    = VS::Z_OUT_V1A,               // V1a Zout: Rp||rp ≈ 38.5KΩ
      .couplingCapCharge   = 0.0f,
      .gridLeakR           = VS::R_1M,                 // 1MΩ (or VS::R_500K via gain pot)
      .couplingCapValue    = 22e-9f,                   // 22nF
      // tau = 22ms (or 11ms with 500K)
  };

  // PI input grid — driven by V1b output
  GridConductionModel piGrid = {
      .conductionThreshold = VS::v(0.5f),
      .gridResistance      = VS::R_300,
      .sourceResistance    = VS::Z_OUT_V1B,               // V1b Zout: (Rp+68K)||rp ≈ 47KΩ
      .couplingCapCharge   = 0.0f,
      .gridLeakR           = VS::R_1M,
      .couplingCapValue    = 47e-9f,                   // 47nF
      // tau = 47ms
  };

  // EL84 grids — power tube grid conduction
  GridConductionModel el84GridA = {
      .conductionThreshold = VS::v(0.0f),              // pentodes conduct near 0V
      .gridResistance      = VS::R_500,                // 500Ω during conduction
      .sourceResistance    = VS::Z_OUT_PI,             // PI Zout: Rp||rp ≈ 38.5KΩ per side
      .couplingCapCharge   = 0.0f,
      .gridLeakR           = VS::R_1M,                 // grid stopper + leak
      .couplingCapValue    = 100e-9f,                  // 100nF
      // tau = 100ms — slow recovery = audible gating at extreme drive
  };

── Component 3: Integration into Signal Chain ──

  Apply grid conduction BEFORE the waveshaper at each stage:

  // In processBlock, per sample:
  
  // Stage 1
  float v1a_in = inputSample;
  v1a_in = applyGridConduction(v1a_in, v1aGrid);     // usually no-op
  float v1a_out = triodeWaveshaper(v1a_in, ...);
  
  // After gain pot gang A
  float v1b_in = v1a_out * gainA;
  v1b_in = applyGridConduction(v1b_in, v1bGrid);     // clips positive peaks at high gain
  float v1b_out = triodeWaveshaper(v1b_in, ...);
  
  // After gain pot gang B, into PI (nodal LTP solver)
  float pi_in = v1b_out * gainB;
  pi_in = applyGridConduction(pi_in, piGrid);         // further compression
  LTPOutput ltp = solveLTP(pi_in, vPi, ltpModel);     // nodal solver
  float piNonInv = ltp.piA;
  float piInv    = ltp.piB;
  
  // After tone/volume, into power tubes
  float el84a_in = applyGridConduction(piNonInv, el84GridA);
  float el84b_in = applyGridConduction(piInv, el84GridB);
  float el84NonInv = pentodeWaveshaper(el84a_in, ...);
  float el84Inv    = pentodeWaveshaper(el84b_in, ...);

═══════════════════════════════════════════════════════════════════
SONIC IMPACT — WHAT GRID CONDUCTION SOUNDS LIKE
═══════════════════════════════════════════════════════════════════

  Without grid conduction modeling:
    - Distortion is symmetric (waveshaper only)
    - Sustained notes maintain constant volume
    - Attack transients pass through unmodified
    - Feels "stiff" and "digital" at high gain

  With grid conduction modeling:
    - Positive peaks get squashed → asymmetric harmonics (even-order)
    - Sustained power chords slowly lose volume as coupling caps charge
      (bias shifts cold) → natural "sag" that's SEPARATE from PSU sag
    - Attack of the next note is briefly louder as the bias recovers →
      percussive, "punchy" transient response
    - At extreme gain: V1b and PI grids conduct heavily, creating a
      thick, compressed "wall of sound" where individual pick attacks
      are smoothed into a sustained mass
    - The gate-like cutoff at extreme settings (cap fully charged, tube
      biased to cutoff) is how the TT does its "sputtery dying" thing
      when you crank gain to 10 and let a note decay

  This is arguably the MOST IMPORTANT nonlinearity to get right for
  realistic feel at high gain. Supply sag and screen droop affect
  dynamics. Grid conduction affects the actual WAVESHAPE of the signal
  in a way that no static waveshaper can replicate — it's signal-history-
  dependent and asymmetric.

═══════════════════════════════════════════════════════════════════
ANTI-DENORMAL NOTE
═══════════════════════════════════════════════════════════════════

  couplingCapCharge is a slow state variable (tau = 22-100ms) that
  decays toward zero during silence. Apply snap-to-zero:

  if (std::abs(model.couplingCapCharge) < 1e-10f)
      model.couplingCapCharge = 0.0f;

  chargeDecay coefficient uses the same exponential precomputation
  as the PSU model:
    chargeDecay = exp(-1.0 / (gridLeakR * couplingCapValue * effectiveSR))

  Recalculate on sample rate or oversampling mode change.
```

### 4.2 EL84 Pentode Model

```cpp
// ═══════════════════════════════════════════════════════════════════
// WHY SCREEN VOLTAGE MATTERS IN A PENTODE
// ═══════════════════════════════════════════════════════════════════
//
// A pentode (EL84) has THREE electrodes that control current flow:
//   - Control grid (g1): the audio signal input
//   - Screen grid (g2): sets the "reach" of the plate — controls gain
//                        and maximum current capability
//   - Plate (anode):     collects current, sets voltage headroom
//
// In a triode (12AX7), plate voltage directly controls gain and headroom.
// In a pentode, the screen grid DECOUPLES the plate from the cathode
// current — the screen sets the operating point more than the plate does.
//
// This means:
//   - Screen voltage controls GAIN: lower Vg2 → less current → less gain
//   - Screen voltage controls CLIPPING CHARACTER: lower Vg2 → earlier
//     compression, softer knee, more "sag" feel
//   - Plate voltage controls HEADROOM: lower Vp → earlier hard clipping,
//     but the shape of clipping is still set by the screen
//   - When supply droops, BOTH plate and screen drop — but their effects
//     are different and compound:
//       Vp drop  → reduced swing before hard clip (ceiling drops)
//       Vg2 drop → reduced gain AND softer transfer curve (floor rises)
//       Combined → the amp compresses from BOTH ends simultaneously
//
// This dual compression is a huge part of why EL84 push-pull amps feel
// so responsive. Modeling only plate voltage misses half the dynamic.
//
// ═══════════════════════════════════════════════════════════════════
// EL84 TRANSFER CURVE WITH SCREEN INFLUENCE
// ═══════════════════════════════════════════════════════════════════
//
// Koren-style pentode model adapted for EL84:
//   Ip = (Kg1 * (Vg1 + Vg2/mu_g2))^ex  *  (1 - exp(-Vp/Vp_knee))
//
// The key terms:
//   Vg1:     control grid voltage (audio signal + bias)
//   Vg2:     screen grid voltage (from PSU, droops under load)
//   mu_g2:   screen-to-grid amplification factor (~12 for EL84)
//   ex:      transfer exponent (~1.35 for EL84, sets harmonic content)
//   Vp:      plate voltage (from PSU power node)
//   Vp_knee: plate knee voltage (~30-60V for EL84)
//            below this, the tube enters "plate saturation" — the region
//            where plate voltage limits current regardless of grid drive

// ─── Pentode Output: both currents returned for PSU reporting ───

struct PentodeOutput {
    float plateCurrent;     // normalized plate current (= output signal)
    float screenCurrent;    // normalized screen current (for PSU loading)
};

// ─── Screen-Aware Pentode Waveshaper (Full) ───

PentodeOutput pentodeWaveshaper(float vGrid, float vBias, float vPlate, float vScreen) {
    // Control grid effective voltage
    float vg1 = vGrid + vBias;
    
    // Screen contribution to cathode current
    // Higher screen voltage → more current capability → more gain
    // mu_g2 ≈ 12 for EL84 (how much screen voltage "counts" vs grid)
    float screenInfluence = vScreen / MU_SCREEN;
    
    // Effective drive: grid voltage boosted by screen contribution
    float vEffective = vg1 + screenInfluence;
    
    // Transfer curve: power-law with screen-dependent gain
    // Screen voltage scales the gain coefficient directly
    float screenGainFactor = vScreen / V_SCREEN_NOMINAL_N;  // 1.0 at nominal
    float driveScaled = vEffective * screenGainFactor;
    
    // Core nonlinearity — asymmetric power law
    float ip;
    if (driveScaled > 0.0f) {
        ip = std::pow(driveScaled, EL84_EXPONENT) / EL84_KG;
    } else {
        ip = 0.0f;  // cutoff — no current below grid cutoff
    }
    
    // Plate saturation: current LIMITED by available plate voltage
    // Below the knee voltage, the plate can't collect all the current
    // the screen is trying to push — current redistributes to screen
    float plateLimit = 1.0f - std::exp(-vPlate / V_PLATE_KNEE_N);
    ip *= plateLimit;
    
    // Screen current: the cathode current splits between plate and screen.
    // At high plate voltage: most current goes to plate (screen gets ~20-25%)
    // At LOW plate voltage (near knee): plate can't collect → screen gets MORE
    // This is the "current redistribution" effect that makes screen sag
    // load-dependent in a physically interesting way.
    //
    // plateLimit ranges 0→1: when it's low (plate starved), more current
    // goes to screen. When it's high (plate healthy), screen gets less.
    float screenRatio = SCREEN_CURRENT_RATIO + (1.0f - plateLimit) * 0.15f;
    //   nominal: 0.25 + 0*0.15 = 0.25 (25% at healthy plate)
    //   starved: 0.25 + 1*0.15 = 0.40 (40% at saturated plate)
    float ig2 = ip * screenRatio;
    
    return { ip, ig2 };
}

// ─── Simplified Pentode Waveshaper ───
// Still screen-aware, tanh-based, returns both currents

PentodeOutput pentodeWaveshaperSimple(float vGrid, float vBias, float vPlate, float vScreen) {
    float vg1 = vGrid + vBias;
    
    // Screen controls the gain and compression threshold
    float screenFactor = vScreen / V_SCREEN_NOMINAL_N;  // 0.0-1.0
    float gain = EL84_GAIN_BASE * screenFactor;
    float threshold = EL84_THRESH_BASE * screenFactor;
    
    float x = vg1 * gain;
    
    // Asymmetric soft clip (pentode character)
    float output;
    if (x >= 0.0f) {
        // Positive swing: soft compression against plate voltage ceiling
        float ceiling = vPlate * HEADROOM_FACTOR;
        output = ceiling * std::tanh(x / ceiling);
    } else {
        // Negative swing: cutoff — sharper than positive side
        output = -threshold * std::tanh(-x * 1.8f / threshold);
    }
    
    // Plate saturation knee — same as full model
    float plateLimit = 1.0f - std::exp(-vPlate / V_PLATE_KNEE_N);
    output *= plateLimit;
    
    // Screen current with plate-voltage-dependent ratio
    float screenRatio = SCREEN_CURRENT_RATIO + (1.0f - plateLimit) * 0.15f;
    float ig2 = std::abs(output) * screenRatio;
    
    return { output, ig2 };
}
```

#### Screen Voltage in the Supply Chain

```
The EL84's screen grid is typically fed from the same B+ node as the plates
but through a dropping resistor (often 470Ω-1K) and its own filter cap.
This creates a FOURTH supply node with its own droop characteristics:

  B+ rail → R_screen (470Ω) → C_screen → EL84 screen grids
                                  │
                                  ▼
                          Screen supply node

Screen supply behavior:
  - At idle: Vscreen ≈ Vplate (screen resistor drop is small)
  - Under drive: screen draws 20-30% of plate current through R_screen
  - This creates an ADDITIONAL voltage drop that:
    1. Reduces screen voltage → reduces gain → natural compression
    2. Is partially independent of plate droop (own RC time constant)
    3. Has a FASTER time constant than the main plate supply because
       C_screen is typically smaller (22-47µF vs main filter 47-100µF)
  - The result: screen voltage dips FIRST on transients, pulling gain
    down before the plate voltage fully sags. This is a major component
    of the "squash then bloom" feel of EL84 amps.

DSP: The screen supply node is integrated into PowerSupplyModel (Section 3.11):

  // Screen node is the 2nd node in the 4-node PSU cascade:
  //   powerNode → screenNode → piNode → preampNode
  //
  // Screen voltage accessed via psu.vScreen()
  // Screen current returned by pentodeWaveshaper() in PentodeOutput.screenCurrent
  // Screen-to-plate ratio is plate-voltage-dependent (not fixed):
  //   screenRatio = 0.25 + (1 - plateLimit) * 0.15
  //   → 25% at healthy plate, up to 40% at saturated plate
  // Reported to PSU via psu.reportPowerDirect(totalPlate, totalScreen)
  //
  // Pass both plate and screen to pentode model:
  PentodeOutput el84A = pentodeWaveshaper(piNonInv, el84Bias,
                                           psu.vPower(),
                                           psu.vScreen());
```

### 4.3 Grid Conduction Loading — PHASE 4 UPGRADE (Future)

```
NOTE: This section describes an ADVANCED grid conduction model intended for
Phase 4 implementation. It is NOT used in the current processBlock (Section 2.5).

The processBlock uses the simpler model from Section 4.1.1 (applyGridConduction),
which captures the essential behavior: tanh impedance loading + coupling cap
charge accumulation + exponential decay. That model is sufficient for MVP.

This Section 4.3 model adds:
  - Exponential diode equation for grid current (expm1, more physically accurate)
  - Separate applyGridLoading / updateBlockingDistortion / applyCondLPF pipeline
  - HF loss proportional to conduction depth (condLPF)
  - Different grid current saturation curve

To upgrade: replace applyGridConduction() calls in processBlock with
InterStageCoupling.process(). The struct and function interfaces are designed
to be drop-in compatible. Do NOT mix both models — use one or the other.
previous stage's plate load, causing four effects:

  1. LOADING: The previous stage's output impedance is suddenly loaded
     by a few kΩ instead of 1MΩ. This clamps the signal — the positive
     peak gets squashed.

  2. COUPLING CAP CHARGING (blocking distortion): Grid current charges
     the coupling cap between stages. This shifts the grid's DC bias
     point NEGATIVE. The effect is self-biasing: harder drive → more
     grid conduction → more negative shift → less average gain. This is
     a natural compression mechanism with a slow time constant set by
     the coupling cap × grid leak resistor (tens to hundreds of ms).

  3. HF LOSS: The grid-cathode capacitance increases during conduction
     (Miller effect amplified). High frequencies are attenuated more
     than lows during the conduction period — a signal-dependent LP.

  4. ASYMMETRIC DISTORTION: Only POSITIVE peaks are affected. Negative
     peaks clip against tube cutoff (hard). Positive peaks clip against
     grid conduction (soft, squishy). This asymmetry generates strong
     even-order harmonics — the "warm" character of tube overdrive.


═══════════════════════════════════════════════════════════════════
WHERE IT HAPPENS IN THE TINY TERROR
═══════════════════════════════════════════════════════════════════

Grid conduction loading occurs at EVERY stage boundary where signal
levels are high enough to reach the conduction threshold:

  V1a → V1b boundary:
    - At high gain, V1a's plate swing can easily exceed V1b's grid
      conduction threshold after the Gang A attenuator
    - At low gain: Gang A attenuates enough that conduction never occurs
    - At full gain: heavy conduction → preamp "squishes" in a way that
      the waveshaper alone cannot model
    - Coupling cap: 0.022µF × 1M grid leak → tau ≈ 22ms
      (bias shift recovers in ~60-80ms after transient)

  V1b → PI boundary:
    - V1b drives into the PI grid (through Gang B) with large swings
    - PI grid conduction loads V1b's plate, compressing the drive
    - This is a MAJOR contributor to the TT's crunch character at high gain
    - Coupling cap: 0.047µF × 1M → tau ≈ 47ms (slower recovery)

  PI → EL84 boundary:
    - PI outputs drive EL84 grids through 0.1µF coupling caps
    - At high volume, EL84 grids enter conduction — power tubes draw
      more grid current than preamp tubes (higher transconductance)
    - EL84 grid conduction loads the PI plates ASYMMETRICALLY (different
      conduction depth on each push-pull side) → adds even harmonics
    - Coupling cap: 0.1µF × grid resistance → tau depends on bias network
    - This is where "power amp compression" partially comes from — it's
      not only the EL84 transfer curve and supply sag, but also the grid
      conduction clamping the PI's ability to drive the output stage


═══════════════════════════════════════════════════════════════════
DSP MODEL
═══════════════════════════════════════════════════════════════════

Model grid conduction as three interacting components applied BETWEEN
stages, at each coupling point — not inside the waveshaper.

─── Component 1: Grid Current (diode approximation) ───

  Grid current follows a diode-like exponential curve. Below the
  conduction threshold: zero current. Above: exponential onset.

  struct GridCondModel {
      float biasShift = 0.0f;       // blocking distortion accumulator
      float biasDecay;              // exp(-1 / (Ccoupling * Rleak * sr))
      float prevStageRout;          // output impedance of driving stage
      
      // Returns grid current given grid-cathode voltage difference
      float gridCurrent(float vGrid, float vCathode) {
          float vForward = vGrid - vCathode + biasShift;
          
          if (vForward <= 0.0f)
              return 0.0f;   // below threshold — no conduction
          
          // Soft exponential onset (diode equation)
          return GRID_IS * (std::expm1(vForward / GRID_VT));
          // expm1 = exp(x)-1, more accurate near zero than exp(x)-1
      }
  };

─── Component 2: Loading (positive peak clamping) ───

  Grid current creates a voltage drop across the previous stage's
  output impedance. This only affects positive peaks — the negative
  half-cycle sees no grid current and passes unchanged.

  float applyGridLoading(float signal, float gridI, float rOut) {
      // Voltage drop from grid current flowing through prev stage Rout
      float vDrop = gridI * rOut;
      return signal - vDrop;   // clamps positive peaks
  }

  // Stage output impedances (Rplate || rp), normalized to DSP domain:
  //   V1a  Rout:  100K || 62.5K ≈ 38K  → 0.127 normalized
  //   V1b  Rout:  (100K + 68K dropper) || 62.5K ≈ 47K → 0.157 normalized
  //   PI   Rout:  100K || 62.5K ≈ 38K per side → 0.127 normalized

─── Component 3: Blocking Distortion (coupling cap bias shift) ───

  Grid current charges the coupling cap, creating a slowly-varying
  negative DC shift on the grid. This is the "self-biasing compression"
  that makes tube amps feel dynamically controlled at high gain.

  void updateBlockingDistortion(GridCondModel& gc, float gridI) {
      // Grid current deposits charge on coupling cap → negative shift
      // Rate of shift proportional to current / capacitance
      float chargeRate = gridI * BLOCKING_CHARGE_SCALE;
      
      // Accumulate shift (grid current pushes bias negative)
      gc.biasShift -= chargeRate;
      
      // Coupling cap discharges through grid leak resistor
      // Exponential recovery toward zero (no permanent shift)
      // biasDecay = exp(-1 / (Ccoupling * Rleak * effectiveSR))
      gc.biasShift *= gc.biasDecay;
      
      // Clamp — real circuits can't shift below grid cutoff
      gc.biasShift = std::max(gc.biasShift, MAX_BIAS_SHIFT);
  }

─── Component 4: HF Loss During Conduction ───

  During conduction, the effective input capacitance spikes. Model
  as a signal-dependent lowpass whose cutoff drops with grid current.

  void applyCondLPF(OnePoleLP& condFilter, float gridI, float sr) {
      // No conduction → bypass (cutoff at Nyquist, no filtering)
      // Heavy conduction → cutoff drops to a few kHz
      float condAmount = std::min(gridI / GRID_I_SATURATION, 1.0f);
      float fc = lerp(20000.0f, COND_LP_MIN_FREQ, condAmount);
      condFilter.setFreq(fc, sr);
  }

─── Full Inter-Stage Grid Conduction Processor ───

  Applied between every pair of stages:

  struct InterStageCoupling {
      GridCondModel gridCond;
      OnePoleLP     condLPF;        // conduction HF loss filter
      OnePoleHP     couplingCapHP;  // coupling cap highpass
      
      float process(float signal, float nextStageCathodeV, float sr) {
          // 1. Coupling cap highpass (already in spec)
          signal = couplingCapHP.process(signal);
          
          // 2. Apply accumulated bias shift from blocking distortion
          float vGridEffective = signal + gridCond.biasShift;
          
          // 3. Calculate grid current
          float gridI = gridCond.gridCurrent(vGridEffective, nextStageCathodeV);
          
          // 4. Loading — clamp positive peaks
          signal = applyGridLoading(signal, gridI, gridCond.prevStageRout);
          
          // 5. HF loss during conduction
          applyCondLPF(condLPF, gridI, sr);
          signal = condLPF.process(signal);
          
          // 6. Update blocking distortion (slow bias shift)
          updateBlockingDistortion(gridCond, gridI);
          
          // 7. Apply bias shift to output (shifts operating point of next stage)
          signal += gridCond.biasShift;
          
          return signal;
      }
  };


═══════════════════════════════════════════════════════════════════
CONSTANTS
═══════════════════════════════════════════════════════════════════

  // Grid diode model
  constexpr float GRID_IS              = 1e-7f;   // reverse saturation (normalized)
  constexpr float GRID_VT              = 0.0001f;  // thermal voltage (normalized)
                                                    // ~0.026V / 300V = 8.67e-5, rounded

  // Blocking distortion
  constexpr float BLOCKING_CHARGE_SCALE = 0.001f;  // charge rate per unit grid current
  constexpr float MAX_BIAS_SHIFT        = -0.15f;  // max negative shift (≈-45V real)

  // Coupling cap × grid leak time constants (for biasDecay precomputation)
  //   tau = C_coupling * R_gridleak
  constexpr float TAU_BLOCKING_V1A_V1B = 0.022f;   // 22ms (0.022µF × 1MΩ)
  constexpr float TAU_BLOCKING_V1B_PI  = 0.047f;   // 47ms (0.047µF × 1MΩ)
  constexpr float TAU_BLOCKING_PI_EL84 = 0.100f;   // 100ms (0.1µF × ~1MΩ effective)
  //   biasDecay = exp(-1 / (tau * effectiveSR))

  // Conduction-dependent lowpass
  constexpr float COND_LP_MIN_FREQ     = 3000.0f;  // Hz — heaviest conduction
  constexpr float GRID_I_SATURATION    = 0.01f;    // grid current at full conduction

  // Stage output impedances (Rplate || rp), normalized
  constexpr float ROUT_V1A             = 0.127f;   // ~38KΩ
  constexpr float ROUT_V1B             = 0.157f;   // ~47KΩ (includes dropper R)
  constexpr float ROUT_PI              = 0.127f;   // ~38KΩ per side


═══════════════════════════════════════════════════════════════════
IMPLEMENTATION PRIORITY
═══════════════════════════════════════════════════════════════════

  Phase 1 (MVP): Skip grid conduction entirely. The waveshapers and
  coupling cap filters alone give a reasonable approximation.

  Phase 2: Add Component 1 + 2 only (grid current + positive peak
  clamping). This captures the main asymmetric compression character
  and costs very little CPU — one conditional + one multiply per
  stage boundary per sample.

  Phase 3: Add Component 3 (blocking distortion). This adds the slow
  self-biasing compression and "memory" of recent dynamics. One extra
  state variable per coupling point + exponential decay.

  Phase 4: Add Component 4 (conduction HF loss). Adds the signal-
  dependent filtering. Most subtle effect — only audible A/B testing
  will tell you if it's worth the extra filter per coupling point.

  The V1b → PI boundary is the most impactful — if only implementing
  one coupling point, start there.
```

---

## 5. Oversampling Strategy

Given your existing oversampling work in the Monolit distortion plugin:

```
Recommended: 4× oversampling around all waveshaper stages

Processing chain at 4× sample rate:
  1. Upsample input (4× with polyphase FIR)
  2. Run entire preamp + power amp chain at 4× rate
  3. Downsample output (4× with polyphase FIR + anti-alias)

Why 4×:
  - 12AX7 generates significant harmonics up to 8th-10th order
  - At 44.1kHz base, 4× gives 176.4kHz — clean up to ~80kHz
  - 2× is acceptable for lower-gain settings
  - 8× for audiophile mode (optional)

Alternative: Antiderivative Anti-Aliasing (ADAA)
  - Apply ADAA to each waveshaper function
  - Can run at 1× or 2× with minimal aliasing
  - More CPU-efficient for static waveshapers
  - Harder to apply to stateful models (Koren)
```

### 5.1 Oversampling Mode Transition Artifacts

```
=== THE PROBLEM ===

  When the user switches oversampling from 2× to 4× (or any mode change)
  during playback, several things break simultaneously:

  1. FILTER STATE DISCONTINUITY
     Every IIR filter in the chain has internal state (z1, z2) that was
     computed at the OLD sample rate. These states encode signal history
     at the old rate's time resolution. When the effective sample rate
     doubles, these states represent "wrong" history — the filter will
     ring, click, or produce a transient spike.

  2. OVERSAMPLER INTERNAL BUFFER MISMATCH
     juce::dsp::Oversampling maintains internal FIR filter states and
     buffers sized for the current mode. Switching modes mid-stream
     leaves stale data in the old buffers. The JUCE Oversampling class
     requires reset() after mode change — but reset() zeros the FIR
     states, causing a brief dropout in the anti-alias filter.

  3. PSU MODEL STATE JUMP
     The supply droop model uses per-sample decay coefficients derived
     from exp(-1/(tau * sampleRate)). When sampleRate changes, the decay
     values change, but the voltage states don't — the supply model will
     "jump" as the RC time constants suddenly shift.

  4. SAG ENVELOPE DISCONTINUITY
     Same issue as PSU — the envelope follower's attack/release 
     coefficients are sample-rate-dependent. A mode switch mid-note
     causes the sag to suddenly tighten or loosen.

  5. LATENCY REPORTING CHANGE
     Different oversampling factors have different FIR filter latencies.
     The DAW needs to be notified via setLatencySamples() or plugin
     delay compensation will be wrong until the next play-start.

=== SOLUTION: DEFERRED CROSSFADE SWITCH ===

  NEVER switch oversampling mode instantaneously. Use a deferred crossfade:

  // ─── Mode Switch State Machine ───
  
  enum class OversampleTransition {
      Idle,           // normal processing
      FadingOut,      // crossfading old mode to silence
      Reconfiguring,  // swapping oversampler + recalc coefficients
      FadingIn        // crossfading new mode from silence
  };

  struct OversampleSwitcher {
      OversampleTransition state = OversampleTransition::Idle;
      int fadeLength;          // samples for crossfade (e.g. 64-256)
      int fadeCounter = 0;
      int pendingMode = -1;    // requested new oversampling factor
      
      // Two oversampler instances — old and new
      std::unique_ptr<juce::dsp::Oversampling<float>> activeOversampler;
      std::unique_ptr<juce::dsp::Oversampling<float>> pendingOversampler;
  };

  // ─── Per-Block Processing ───
  
  void processBlock(AudioBuffer<float>& buffer, ...) {
      ScopedNoDenormals noDenormals;
      
      switch (oversampleSwitcher.state) {
          
          case OversampleTransition::Idle:
              // Normal processing through active oversampler
              processWithOversampler(buffer, activeOversampler);
              break;
              
          case OversampleTransition::FadingOut:
              // Process with OLD oversampler, apply fade-out gain ramp
              processWithOversampler(buffer, activeOversampler);
              applyFadeOut(buffer, fadeCounter, fadeLength);
              fadeCounter += buffer.getNumSamples();
              
              if (fadeCounter >= fadeLength) {
                  state = OversampleTransition::Reconfiguring;
              }
              break;
              
          case OversampleTransition::Reconfiguring:
              // === CRITICAL SECTION: all state updates happen here ===
              // Must match the prepareToPlay() trigger map exactly.
              
              float newEffectiveRate = baseSampleRate * pendingMode;
              
              // 1. Prepare new oversampler
              pendingOversampler->initProcessing(buffer.getNumSamples());
              pendingOversampler->reset();
              
              // 2. Recalculate ALL filter coefficients at new effective rate
              filterBank.recalcAll(baseSampleRate, pendingMode);
              
              // 3. Reset all IIR filter states to zero
              //    (the fade-out ensures silence, so zeroed states = no click)
              filterBank.resetAll();
              speakerCoupling.reset();
              
              // 4. Recalculate PSU model decay coefficients at new rate
              psu.recalcDecayCoeffs(newEffectiveRate);
              //    DON'T reset supply voltages — preserve the DC operating point
              //    Only the per-sample decay coefficient changes, voltage states are valid
              
              // 5. Recalculate sag envelope coefficients
              sag.recalcCoeffs(newEffectiveRate);
              //    Same — preserve envelope state, just update coefficients
              
              // 6. Recalculate grid conduction chargeDecay coefficients ×4
              //    These use exp(-1/(R_leak_real * C_couple * effectiveSR))
              //    DON'T reset couplingCapCharge — it's DC bias, not signal
              gridConduction.recalcAll(newEffectiveRate);
              
              // 7. Recalculate SpeakerCoupling filters at new rate
              speakerCoupling.recalcFilters(newEffectiveRate);
              
              // 8. Recalculate parameter smoother decay coefficients
              //    Control rate changes with block size / effective rate
              float controlRate = newEffectiveRate / (float)buffer.getNumSamples();
              gainSmoother.init(5.0f, controlRate);
              toneSmoother.init(5.0f, controlRate);
              volumeSmoother.init(5.0f, controlRate);
              
              // 9. Swap oversamplers
              std::swap(activeOversampler, pendingOversampler);
              
              // 10. Update latency reporting
              int newLatency = activeOversampler->getLatencyInSamples();
              setLatencySamples(newLatency);
              
              // 11. Output silence for this block (transition buffer)
              //     FREEZE PSU: do NOT call psu.update() during this silent block.
              //     If we called update() with zero current draw (silence), the PSU
              //     would recover toward nominal voltage during the transition.
              //     When audio resumes in FadingIn, the sudden current draw would
              //     cause an exaggerated sag "pop." Freezing preserves the voltage
              //     state at whatever level it was when FadingOut ended.
              psuFrozen = true;
              buffer.clear();
              
              fadeCounter = 0;
              state = OversampleTransition::FadingIn;
              break;
              
          case OversampleTransition::FadingIn:
              // Unfreeze PSU — resume normal update-then-process cycle
              psuFrozen = false;
              // Process with NEW oversampler, apply fade-in gain ramp
              processWithOversampler(buffer, activeOversampler);
              applyFadeIn(buffer, fadeCounter, fadeLength);
              fadeCounter += buffer.getNumSamples();
              
              if (fadeCounter >= fadeLength) {
                  state = OversampleTransition::Idle;
              }
              break;
      }
  }

=== FADE LENGTH TUNING ===

  Too short: audible click/pop at the transition boundaries
  Too long: noticeable volume dip during switch
  
  Recommended: 128 samples at base rate (~3ms at 44.1kHz)
  
  Total transition time = fadeOut + 1 block (reconfig) + fadeIn
                        ≈ 3ms + 5ms + 3ms = ~11ms
                        
  This is fast enough to be inaudible in practice. The brief silence
  during reconfiguration is masked by the fade-out tail.

=== WHAT TO RESET vs PRESERVE DURING TRANSITION ===

  RESET to zero (signal path states):
    ☐ All IIR filter z1/z2 states (coupling caps, cathode bypass, tone,
      OT filters, cab sim) — the fade ensures these are at/near zero anyway
    ☐ Oversampler internal FIR buffers (via reset())
    ☐ Any smoothed parameter ramp states
  
  PRESERVE (operating point states):
    ☐ PSU supply node voltages — these represent DC bias, not signal
    ☐ Sag envelope value — preserve the current dynamic state
    ☐ Triode/pentode bias points — these are parameters, not signal
    ☐ Gain/tone/volume current smoothed values
    ☐ Grid conduction couplingCapCharge ×4 — represents DC bias shift,
      not signal; zeroing would cause an audible bias "pop"
    ☐ LTP cathode voltage (ltpModel.vCathode) — DC operating point
  
  RECALCULATE (sample-rate-dependent coefficients):
    ☐ All filter coefficients (via recalcAllFilters(newEffectiveRate))
    ☐ PSU model per-sample decay values
    ☐ Sag envelope attack/release decay coefficients
    ☐ Grid conduction chargeDecay coefficients ×4
    ☐ Any per-sample smoothing coefficients (parameter ramps)

=== JUCE IMPLEMENTATION NOTES ===

  juce::dsp::Oversampling does NOT support hot-swapping modes.
  You cannot call initProcessing() with a different factor on the same
  instance without first destroying and recreating it. Options:

  Option A (recommended): Pre-allocate one Oversampling instance per mode
    std::array<std::unique_ptr<Oversampling<float>>, 3> oversamplers;
    // [0] = 2×, [1] = 4×, [2] = 8×
    // All initialized in prepareToPlay()
    // Switch just changes the active pointer

  Option B: Destroy and recreate on switch
    // Simpler but causes memory allocation on the audio thread
    // Acceptable if transitions are rare (user-initiated only)

  Option A avoids any allocation on the audio thread and makes the
  swap in Reconfiguring a simple pointer swap + reset().

=== AUTOMATION SAFETY ===

  If the oversampling parameter is exposed to DAW automation:
    - Debounce rapid changes (ignore if already transitioning)
    - Minimum hold time between switches: 100ms
    - Never allow mode changes during the transition state machine
    
  if (pendingMode != currentMode && state == Idle) {
      // Accept the change
      startTransition(pendingMode);
  } else {
      // Reject or queue — don't stack transitions
  }
```

---

## 6. JUCE Plugin Architecture

```
Class Hierarchy:

SilentRiotProcessor : public juce::AudioProcessor
├── VoltageScale (static utility — real↔DSP conversion)
├── FilterBank (central filter management — bulk recalc/reset)
│   ├── 10× OnePoleLP/HP (coupling caps, cathode bypass, tone)
│   ├── 3× Biquad (OT leakage LP, cab presence/rolloff/notch)
│   └── 5× grid conduction coupling cap HP
├── DynamicMillerLP ×2 (V1a, V1b — per-sample fc from gain estimate)
│   └── V1b: Rsource modulated by gain pot position (tone-shaping interaction)
├── SpeakerCoupling (dynamic load-line interaction)
│   ├── SpeakerModel (impedance curve: Re, fs, Lvc)
│   ├── 3× band extraction filters (bass/mid/HF envelope)
│   └── impedance → gain modulation (replaces static OT HP + cab resonance)
├── ParamSmoother ×3 (gain, tone, volume — ~5ms exponential)
├── PowerSupplyModel
│   ├── SupplyNode: powerNode (EL84 plates, tau=10ms)
│   ├── SupplyNode: screenNode (EL84 screens, tau=6ms) ← fast sag
│   ├── SupplyNode: piNode (PI plates, tau=35ms)
│   └── SupplyNode: preampNode (preamp plates, tau=80ms)
├── InputStage
│   └── HighpassFilter (coupling cap)
├── PreampStage1 ← reads preampNode.voltage
│   ├── GridConductionModel (v1aGrid — mild, hot pickups only)
│   ├── TriodeWaveshaper (V1a)
│   ├── CathodeBypassFilter
│   └── OutputCouplingFilter
├── GainControl
│   ├── GangA_Attenuator
│   └── GangB_Attenuator (+ optional loading filter)
├── PreampStage2 ← reads preampNode.voltage
│   ├── GridConductionModel (v1bGrid — aggressive at high gain)
│   ├── TriodeWaveshaper (V1b)
│   ├── CathodeBypassFilter
│   └── OutputCouplingFilter
├── PhaseInverter ← reads piNode.voltage
│   ├── GridConductionModel (piGrid — PI input loading)
│   ├── TriodeWaveshaper (non-inverting, LTP triode A)
│   ├── LTPModel (nodal solver — shared cathode, differential current steering)
│   │   ├── triodeCurrentModel ×2 (A: signal grid, B: reference grid)
│   │   ├── tail resistor constraint (Ia + Ib = Vcathode / Rtail)
│   │   └── vCathode state (persistent, one-iteration Newton per sample)
│   └── No static GainImbalance — emerges from current steering
├── ToneControl
│   └── VariableCutFilter (Vox cut style)
├── MasterVolume
├── PowerAmp ← reads vPower + vScreen, returns PentodeOutput{plateCurrent, screenCurrent}
│   └── reports to PSU via reportPowerDirect(totalPlate, totalScreen)
│   ├── GridConductionModel ×2 (el84GridA, el84GridB — power tube loading)
│   ├── PentodeWaveshaper (tube 1, screen-aware)
│   ├── PentodeWaveshaper (tube 2, screen-aware)
│   ├── PushPullSummer
│   └── CathodeBiasSag (envelope follower)
├── OutputTransformer
│   ├── BandpassFilter
│   └── HysteresisModel (optional)
├── CabinetSim
│   └── ConvolutionEngine / IIR filters
├── OutputScaler (normalize to ±1.0 + soft limiter)
├── OversampleSwitcher (state machine: Idle/FadingOut/Reconfig/FadingIn)
│   ├── oversamplers[3] (pre-allocated: 2×, 4×, 8×)
│   ├── fadeLength = 128 samples
│   └── transition state + pending mode
└── Oversampler (active instance, swapped by OversampleSwitcher)

Note: ScopedNoDenormals placed at top of every processBlock() call.
      ANTI_DENORMAL constant (1e-18f) applied to all IIR filter states.
      Snap-to-zero on all envelope followers and PSU node voltages.

Parameters (AudioProcessorValueTreeState):
  - gain      (0.0 — 10.0, audio taper)
  - tone      (0.0 — 10.0)
  - volume    (0.0 — 10.0, audio taper)
  - powerMode (15W / 7W toggle)
  - cabSim    (on/off)
  - cabIR     (file path, if convolution)
  - oversample (2× / 4× / 8×)
```

---

## 7. Parameter Ranges & Mapping

| Parameter | ID | Internal Range | Display Range | Default | Taper | Notes |
|-----------|-----|---------------|---------------|---------|-------|-------|
| Gain | `gain` | 0.0 — 1.0 | 0.0 — 10.0 | 0.6 (6.0) | Audio (x²) | Controls both gang A and B simultaneously |
| Tone | `tone` | 0.0 — 1.0 | 0.0 — 10.0 | 0.5 (5.0) | Linear | Maps to cut filter freq: 1.5kHz — 20kHz |
| Volume | `volume` | 0.0 — 1.0 | 0.0 — 10.0 | 0.5 (5.0) | Audio (x²) | Post-PI level control |
| 7W Mode | `halfPower` | bool | off/on | false | Switch | false = 15W (more headroom), true = 7W (more sag) |
| Cab Sim | `cabSim` | bool | off/on | true | Switch | Bypass cabinet simulation |
| Power | `power` | bool | off/on | true | Switch | Master bypass |
| Oversample | `oversample` | choice | 2x/4x/8x | 4x (idx 1) | Switch | Multiplier for internal sample rate |
| Preset | `presetIndex` | 0 — 4 | 0 — 4 | 0 | Step | Preset index |
| IR Selection | `irIndex` | 0 — N | 0 — N | 0 | Step | Selected IR index |

UI display range: Knob labels show 0.0–10.0 (internal × 10). See UI Spec §7.1.

---

## 8. Implementation Phases

### 8.0 Foundation Dependencies — What Breaks Everything If Wrong

```
═══════════════════════════════════════════════════════════════════
DEPENDENCY CHAIN — BUILD AND VERIFY IN THIS ORDER
═══════════════════════════════════════════════════════════════════

  The implementation phases below (Phase 1–4) describe WHAT to build.
  This section describes the DEPENDENCY ORDER — which pieces must be
  correct before anything downstream can work.

  Three layers form the foundation. If any layer is broken, every
  stage above it produces wrong results. Verify each layer's tests
  pass before moving to the next.

═══════════════════════════════════════════════════════════════════
LAYER 1: VOLTAGE SCALE (verify first — everything depends on this)
═══════════════════════════════════════════════════════════════════

  VoltageScale (Section 2.5) and Constants (Section 10) define the
  normalized domain that ALL processing code operates in.

  If VoltageScale is wrong:
    - Every stage clips at the wrong level (supply ceiling is wrong)
    - Every filter operates at the wrong impedance (R_n values off)
    - Every PSU node droops by the wrong amount (nominal voltages off)
    - Grid conduction triggers at the wrong drive level (thresholds off)
    - The LTP solver produces wrong cathode voltage (Ohm's law breaks)
    - Stage output impedances are wrong (Miller cap, grid loading off)

  VoltageScale is a pure math utility with no state — it either maps
  300V → 1.0 and preserves Ohm's law, or it doesn't. This is fully
  testable with the voltage_scale_tests before writing ANY DSP code.

  Tests: voltage_scale_tests.cpp
  Verify: All reference table values match spec Section 2.5.
          Ohm's law: V_n = I_n × R_n in normalized domain.
          Round-trip: toVolts(v(x)) == x for all x.

═══════════════════════════════════════════════════════════════════
LAYER 2: FILTERS (verify second — every stage uses them)
═══════════════════════════════════════════════════════════════════

  OnePole LP/HP (Section 2.6.2) and Biquad (RBJ cookbook) are used
  24 times across the signal chain: coupling caps, cathode bypass,
  tone control, OT shaping, cabinet, band extraction.

  If the BLT transform is wrong:
    - Coupling caps don't block DC → bias drift through chain
    - Cathode bypass doesn't shape low-end → wrong distortion character
    - Tone control has wrong frequency range
    - OT rolloff is wrong → fizzy or muffled output

  If anti-denormal injection is broken:
    - CPU spikes to 80% during silence (24 cascaded IIR filters)
    - Plugin becomes unusable in a DAW

  The filters are stateless math (given fc and SR, produce coefficients)
  plus a simple feedback loop. Fully testable with sine sweeps.

  Tests: filter_tests.cpp
  Verify: OnePole -3dB at cutoff frequency.
          Biquad LP/HP/notch/peak at expected frequencies.
          Circuit-specific filters (7Hz input HP, 72Hz coupling, 7kHz OT).
          Anti-denormal: no FP_SUBNORMAL in state after long silence.

═══════════════════════════════════════════════════════════════════
LAYER 3: WAVESHAPERS (verify third — this IS the amp sound)
═══════════════════════════════════════════════════════════════════

  triodeWaveshaper (Section 4.1) and pentodeWaveshaper (Section 4.2)
  are the actual nonlinearities that create the amp's distortion
  character. Everything else — filters, PSU, grid conduction — just
  shapes HOW signal enters and exits these two functions.

  If the triode waveshaper is wrong:
    - Wrong harmonic content (symmetric vs asymmetric distortion)
    - Wrong saturation curve (too hard, too soft, or no clipping)
    - Wrong supply sensitivity (PSU droop has no audible effect)
    - Signal levels don't match the spec's operating table

  triodeCurrentModel (Section 3.5) is used by the LTP solver to
  compute plate currents and drive the phase inverter. If K_12AX7
  is miscalibrated, the PI produces wrong gain and imbalance.

  Tests: triode_tests.cpp, pentode_tests.cpp
  Verify: Output bounded by supply × HEADROOM_FACTOR.
          Asymmetric: negative clips harder than positive.
          Zero input near bias → near-zero output.
          Guitar input → output in spec operating level range.
          Lower supply → lower output (PSU sensitivity).
          triodeCurrentModel: zero at deep cutoff, positive above.
          Pentode: screen voltage reduces gain, plate saturation works.

═══════════════════════════════════════════════════════════════════
DEPENDENCY GRAPH
═══════════════════════════════════════════════════════════════════

  VoltageScale + Constants
       │
       ├──→ OnePole / Biquad filters (use PI_F, ANTI_DENORMAL, freq constants)
       │         │
       │         ├──→ FilterBank (manages 24 instances)
       │         └──→ DynamicMillerLP (per-sample fc from gain)
       │
       ├──→ triodeWaveshaper (uses VS::V_BIAS, supply_n, HEADROOM_FACTOR)
       │         │
       │         └──→ triodeCurrentModel (uses K_12AX7, VS::V_THERMAL)
       │                   │
       │                   └──→ LTP Solver (calls triodeCurrentModel ×2)
       │
       ├──→ pentodeWaveshaper (uses MU_SCREEN, EL84_KG, V_PLATE_KNEE_N)
       │
       ├──→ GridConduction (uses VS::v() thresholds, VS::r() impedances)
       │
       ├──→ PowerSupplyModel (uses VS::v() nominal voltages, VS::r() droppers)
       │
       └──→ SagEnvelope (uses SAG_DEPTH, time constants)

  If you change a constant in VoltageScale or Constants.h, re-run
  ALL tests — not just the ones for the component you're working on.

═══════════════════════════════════════════════════════════════════
VERIFICATION SEQUENCE (run after every change)
═══════════════════════════════════════════════════════════════════

  1. voltage_scale_tests    — domain math correct?
  2. filter_tests           — frequency response correct?
  3. triode_tests           — waveshaper behavior correct?
  4. pentode_tests          — EL84 model correct?
  5. grid_conduction_tests  — loading and bias shift correct?
  6. ltp_tests              — phase inverter correct?
  7. psu_sag_tests          — supply dynamics correct?
  8. speaker_tests          — load interaction correct?
  9. signal_chain_tests     — operating levels match spec table?

  If a test fails in layer 1, don't bother debugging layer 3.
  Fix the foundation first.
```

### Phase 1: Core Preamp (MVP)
- [ ] Voltage scaling framework (VoltageScale struct, stage gain constants)
- [ ] Input conditioning (coupling cap filter)
- [ ] Triode waveshaper (start with asymmetric tanh, supply-aware clipping)
- [ ] Two cascaded gain stages with coupling filters
- [ ] Dual-gang gain control
- [ ] Basic oversampling (2×)
- [ ] Simple output level control + output normalizer
- **Goal:** Get the preamp distortion character right, with proper internal levels

### Phase 2: Power Section
- [ ] Phase inverter (long-tail pair model with tail resistor interaction)
- [ ] Tone control (Vox cut filter)
- [ ] Master volume
- [ ] EL84 power tube model (pentode waveshaper)
- [ ] Push-pull summing
- [ ] Upgrade to 4× oversampling
- **Goal:** Full amp tone, clean to cranked

### Phase 3: Dynamics & Feel
- [ ] PowerSupplyModel with four supply nodes (power/screen/PI/preamp)
- [ ] Wire supply nodes into all tube stages (plate voltage ceiling from PSU)
- [ ] Cathode bias sag (envelope follower on power stage)
- [ ] Multi-stage droop interaction (power draw → PI droop → preamp droop)
- [ ] Output transformer frequency shaping
- [ ] Output transformer saturation (optional)
- [ ] 15W / 7W mode switching (changes bias points + supply behavior)
- **Goal:** Amp "feels" responsive and dynamic — the self-limiting compression loop works

### Phase 4: Cabinet & Polish
- [ ] Cabinet simulation (IIR or convolution)
- [ ] IR loader (WAV file loading)
- [ ] GUI design (Orange-inspired aesthetics)
- [ ] Preset system
- [ ] A/B comparison tools
- [ ] ADAA exploration for CPU optimization
- **Goal:** Release-ready plugin

---

## 9. Tuning & Validation Reference

### 9.1 What the Silent Riot Should Sound Like
- **Clean (gain low):** Bright, chimey, Vox-like. Slightly thin but articulate. Strong pick attack.
- **Edge of breakup (gain ~4-5):** Touch-sensitive. Clean on soft picking, crunches on dig-in. Musical compression starts.
- **Crunch (gain ~6-7):** Classic British rock crunch. Strong midrange, controlled low end. EL84 compression audible.
- **Full gain (gain ~8-10):** Rich, saturated overdrive. Not high-gain metal -- more classic rock saturation. Very compressed, harmonically dense. Notes "bloom." In 15W mode at full tilt, subtle odd-order harmonics appear from soft Class AB crossover -- this adds grit without harshness. In 7W mode the saturation stays smoother (near-Class A) with more even-order warmth.
- **Tone knob:** Subtle. Full bright = open and present. Full dark = woolly, rounded, less fizz.

### 9.2 The Inertia System -- Why Tube Amps Have "Feel"

```
WHAT IS "FEEL" IN A TUBE AMP?

  A tube amp does not respond to all frequencies at the same time.
  Every reactive component -- coupling cap, cathode bypass, Miller
  capacitance, output transformer, power supply capacitors -- stores
  energy and releases it over time. This creates a system of
  overlapping inertias at different timescales.

  When a player hits a chord, the amp's response unfolds over time:
    - 0-1ms: pick transient passes through (high frequencies first)
    - 1-10ms: sag envelope begins compressing, screen voltage dips
    - 10-50ms: coupling cap charge accumulates, grid bias shifts cold
    - 50-200ms: PSU droop propagates through RC chain, preamp supply sags
    - 200ms+: everything recovers, amp "breathes" back to nominal

  This is NOT one mechanism -- it is five independent inertia systems
  that interact to create the composite "tube feel." The model must
  implement all five to avoid sounding "digital" or "stiff."

THE FIVE INERTIA LAYERS

  LAYER 1: FILTER STATE INERTIA (microseconds-milliseconds)
    Mechanism:  IIR filter z1/z2 state variables
    Timescale:  7Hz coupling cap ~23ms, 72Hz coupling ~2.2ms, 4.2Hz bypass ~38ms
    Effect:     Frequency-dependent phase shift. Low frequencies arrive later than
                high frequencies. Pick attack leads the fundamental. 24 cascaded
                filters create substantial phase rotation.
    Source:     OnePole/Biquad instances in FilterBank
    Status:     IMPLEMENTED -- inherent in IIR topology via BLT.
    Tuning:     Filter cutoff frequencies (Section 10). Wrong fc = wrong transient feel.

  LAYER 2: SAG ENVELOPE INERTIA (10-200ms)
    Mechanism:  Cathode capacitor charges under drive, shifts bias cold
    Timescale:  Attack 10ms, Release 100ms
    Effect:     "Bloom" -- note starts loud, compresses, recovers. Breathing dynamic.
    Source:     SagEnvelope struct
    Status:     IMPLEMENTED
    Tuning:     SAG_ATTACK_MS, SAG_RELEASE_MS, SAG_DEPTH
                Too fast = compressor. Too slow = stiff. Too deep = notes die.

  LAYER 3: GRID CONDUCTION INERTIA (22-100ms)
    Mechanism:  Coupling cap charges when grid conducts, shifts DC bias
    Timescale:  V1a/V1b 22ms, PI 47ms, EL84 100ms
    Effect:     Signal-history-dependent asymmetric distortion. Sustained chords
                lose volume. Next attack briefly louder. Extreme gain: sputtery gating.
    Source:     GridConductionModel.couplingCapCharge
    Status:     IMPLEMENTED (MVP)
    Tuning:     GRID_CHARGE_RATE, chargeDecay tau per stage

  LAYER 4: PSU DROOP INERTIA (6-80ms)
    Mechanism:  Current draw sags RC-filtered supply voltages. Cascaded nodes.
    Timescale:  Screen 6ms, Power 10ms, PI 35ms, Preamp 80ms
    Effect:     Heavy playing reduces headroom. Screen droop reduces gain before
                plate droop reduces headroom. Multi-stage cascade = rolling compression.
    Source:     PowerSupplyModel, SupplyNode.track()
    Status:     STUBBED -- Phase 3 required.
    Tuning:     sourceImpedance_n, rDrop resistors, tau per node

  LAYER 5: SPEAKER COUPLING INERTIA (2ms)
    Mechanism:  Frequency-dependent impedance modulates pentode gain
    Timescale:  Band envelope followers at 2ms tau
    Effect:     Bass-heavy signal gets more gain. Palm mute vs lead line differ
                in gain and compression, not just EQ.
    Source:     SpeakerCoupling.process()
    Status:     IMPLEMENTED
    Tuning:     SPK_ENV_TAU_MS, SPK_GAINMOD_DEPTH, SPK_GAINMOD_RANGE

WHY ALL FIVE MATTER TOGETHER

  The five layers operate on overlapping timescales and interact:
    - Sag shifts bias -> changes grid conduction threshold
    - PSU droop reduces supply -> changes waveshaper ceiling
    - Grid cap charge shifts bias -> changes plate current -> changes PSU load
    - Speaker coupling changes load -> changes power tube current -> changes droop

  This self-referencing feedback loop makes a tube amp feel "alive."
  Phase 3 (wiring PSU droop) is the most important phase for feel.

  When A/B testing: if harmonics are right but DYNAMICS are wrong
  (too stiff, too instant, no bloom), the problem is in the inertia
  system, not the waveshaper.
```

### 9.3 Phase Shift Through the Signal Chain

```
PHASE SHIFT -- WHAT THE MODEL PRODUCES AUTOMATICALLY

  Every IIR filter introduces frequency-dependent phase shift. This is
  inherent in BLT-derived topology -- not separately coded. A 1st-order
  HP at 7Hz shifts phase by +90 deg at DC, +45 deg at 7Hz, ~0 above.

  With 24 cascaded filters, total phase rotation is substantial.
  At 100Hz, combined phase shift might be 200-400 deg. Harmonics of a
  note arrive at slightly different times, changing waveform shape even
  in the clean regime. THIS IS CORRECT -- the real circuit has identical
  phase relationships from matching RC time constants.

  Significant phase contributors:
    Input HP (7Hz):           +90 deg below cutoff
    V1a cathode bypass (4.2Hz LP): -90 deg above 4.2Hz
    V1a coupling HP (72Hz):   +45 deg at 72Hz, significant through 200Hz
    V1b coupling HP (7Hz):    +90 deg below 7Hz
    PI coupling x2 (16Hz):   +90 deg each below 16Hz
    Tone cut LP (variable):  -90 deg above cutoff
    OT leakage LP (7kHz):    -45 deg at 7kHz

  Approximate group delay vs 1kHz reference:
    80Hz:  5-8ms additional
    200Hz: 2-3ms additional
    1kHz:  reference
    5kHz:  0.5-1ms from OT rolloff

WHERE PHASE MIGHT BE WRONG

  DynamicMillerLP: rapid per-sample fc changes can produce phase
  artifacts absent in analog. The 1ms fc smoothing mitigates but
  does not eliminate.

  Output transformer: real OTs have LF phase rotation from primary
  inductance. Our 7kHz LP captures HF rolloff magnitude but not LF
  phase. Speaker coupling partially compensates. Phase 4 refinement.
```

### 9.4 Noise Floor and Power Supply Hum (Phase 4)

```
WHY THE MODEL IS SILENT WHEN INPUT IS ZERO

  DSP is deterministic. Zero input = zero output. A real Silent Riot
  has audible noise from three sources:

  SOURCE 1: THERMAL NOISE (resistor Johnson noise)
    100K plate R: ~4uV/sqrt(Hz). After V1a (70x): ~280uV.
    After V1b (70x): ~20mV/sqrt(Hz). Audible broadband hiss at high gain.
    Implementation: pink noise at each grid input
      NOISE_FLOOR_V1A ~ VS::v(4e-6f) ~ 1.3e-8 normalized
      Tiny but amplified 70x per stage. Pink spectrum (-3dB/oct).

  SOURCE 2: SHOT NOISE (tube electron granularity)
    Slightly more textured than thermal. Subtle difference.
    For v1.0: thermal noise at grid inputs is sufficient.

  SOURCE 3: POWER SUPPLY RIPPLE (mains hum)
    Rectifier produces 100Hz (50Hz mains) or 120Hz (60Hz mains) ripple.
    After RC filtering: -60dB to -80dB below B+.
    After two gain stages: audible low hum at high gain.
    Implementation: sine injected into PSU node voltages
      MAINS_FREQ: 50 or 60Hz (user-selectable)
      HUM_AMPLITUDE: ~VS::v(0.5f) at power node
      Downstream nodes get filtered ripple through SupplyNode.track()

  TARGET NOISE FLOOR: -70dB to -80dB at output, gain=10, no input.
    Too much: sounds broken. Too little: sounds sterile.
    Right: barely noticeable when playing, audible when you stop.

  STATUS: NOT IMPLEMENTED. Phase 4 feature.
```

### 9.5 Ear-Tuning Guide -- What to Adjust When Something Sounds Wrong

```
TUNING MAP: SONIC PROBLEM -> CONSTANT TO ADJUST

  These stages CANNOT be verified by tests. Tests verify range.
  Only ears verify correctness.

  STAGE 1: V1a ALONE (first thing to get right)

    Harsh/fizzy clean:
      V1A_RSOURCE_REAL (10K) -> increase to darken Miller rolloff
      OT_LP_FREQ (7kHz) -> decrease

    Muddy/dull clean:
      STAGE1_COUPLING_HP (72Hz) -> increase to thin low end
      CATHODE_BYPASS_FREQ (4.2Hz) -> increase to reduce LF gain

    Too symmetric distortion (buzzy, not warm):
      TRIODE_ASYM_FACTOR (1.5) -> increase for more asymmetry

    Clips too early:
      V1A_GAIN (70) -> reduce
      HEADROOM_FACTOR (0.85) -> increase toward 1.0

    Clips too late:
      V1A_GAIN (70) -> increase
      HEADROOM_FACTOR (0.85) -> reduce toward 0.7

  STAGE 2: V1a + V1b + GAIN POT (crunch character)

    No touch-sensitivity at gain 4-5:
      GRID_CHARGE_RATE (0.01) -> increase
      Grid conduction tau (22ms) -> decrease

    Pumping/breathing unnaturally:
      GRID_CHARGE_RATE -> decrease
      Grid conduction tau -> increase

    V1b sounds identical to V1a:
      Verify V1B_RSOURCE_BASE_REAL (38.5K) is different from V1A (10K)
      Verify coupling HP frequencies differ (72Hz vs 7Hz)

  STAGE 3: LTP PHASE INVERTER

    Too symmetric (lacking even harmonics):
      K_12AX7 (3394) -> recalibrate against datasheet

    Distorts too early:
      PI_BIAS_REF_N -> more negative for more headroom

  STAGE 4: SAG AND DYNAMICS

    Stiff, no response:
      SAG_ATTACK_MS (10) -> decrease
      SAG_DEPTH (0.15) -> increase
      Requires Phase 3 PSU implementation

    Too obvious compression:
      SAG_ATTACK_MS -> increase
      SAG_RELEASE_MS (100) -> increase
      SAG_DEPTH -> decrease

    Notes die too quickly at high gain:
      SAG_DEPTH -> decrease
      GRID_CHARGE_RATE -> decrease

    7W and 15W sound identical:
      Increase gap between PENTODE_BIAS_15W/7W_N
      Check softConductionCurve uses bias-dependent overlap

  STAGE 5: OUTPUT AND FINAL TONE

    Output too quiet/loud:
      outputGainCompensation (2.0) -> tune so peak hits +/-0.95

    Tone knob does nothing:
      TONE_MIN_FREQ (1500) -> decrease for darker minimum
      TONE_MAX_FREQ (20000) -> verify near-bypass

    Fizzy at all settings:
      OT_LP_FREQ (7kHz) -> decrease to 5-6kHz
      Check oversampling is enabled (aliasing = fizz)

TUNING ORDER (sequential, not parallel):
  1. V1a alone, clean tone
  2. V1a alone, driven
  3. Add V1b + gain pot, crunch
  4. Add LTP, PI character
  5. Add EL84s, power distortion
  6. Add sag + PSU, dynamics
  7. Add speaker coupling + cab, final tone
  8. Output level tuning
  9. Noise/hum (Phase 4)
```

### 9.6 A/B Testing Sources
- YouTube: "Silent Riot amp sim demo" -- reference clips
- Rig Rundowns featuring the Silent Riot
- Compare against Neural DSP / Amplitube Orange models if available



## 10. Key DSP Constants (Starting Points)

```cpp
// ═══════════════════════════════════════════════════════════════════
// ALL normalized values derived from VoltageScale (VS::). No magic
// numbers. Every constant traces back to a real-world value through
// an explicit conversion, or is dimensionless and needs no conversion.
//
// Convention:
//   _n suffix  = value in normalized domain
//   _real      = value in real-world units (volts, ohms, amps)
//   no suffix  = dimensionless (ratios, exponents, gains)
// ═══════════════════════════════════════════════════════════════════

// === VOLTAGE SCALING FRAMEWORK (defined in VoltageScale struct, Section 2.5) ===
// VS::V_REF = 300.0V,  VS::R_REF = 300KΩ,  VS::I_REF = 1mA
// ═══════════════════════════════════════════════════════════════════
// ALL CONSTANTS — COMPLETE REFERENCE
// ═══════════════════════════════════════════════════════════════════
// Naming convention:
//   _N suffix   = normalized domain value (derived via VS::v/r/i)
//   _real suffix = real-world units (volts, ohms, amps, farads)
//   no suffix   = dimensionless (ratios, exponents, gains, tuning)
//
// All conversions: VS::v(volts), VS::r(ohms), VS::i(amps)

// ═══════════════════════════════════════════════════════════════════
// STAGE GAINS (dimensionless ratios — no normalization needed)
// ═══════════════════════════════════════════════════════════════════
constexpr float V1A_GAIN              = 70.0f;    // 12AX7 Av ≈ mu * Rp/(Rp+rp)
constexpr float V1B_GAIN              = 70.0f;    // same topology
// PI gain: emerges from LTP nodal solver (Section 3.5), no constant needed
constexpr float EL84_GAIN_BASE        = 17.0f;    // pentode gain at nominal screen voltage
constexpr float EL84_THRESH_BASE      = 0.5f;     // pentode simplified model clipping threshold (normalized)

// ═══════════════════════════════════════════════════════════════════
// SUPPLY NODE VOLTAGES (derived from VS::v())
// ═══════════════════════════════════════════════════════════════════
constexpr float V_POWER_NOMINAL_N     = VS::v(300.0f);   // 1.0    — EL84 plates
constexpr float V_SCREEN_NOMINAL_N    = VS::v(280.0f);   // 0.933  — EL84 screens
// V_SCREEN_NOMINAL_N defined above in supply section — used by both PSU and pentode model
constexpr float V_PI_NOMINAL_N        = VS::v(280.0f);   // 0.933  — PI plates
constexpr float V_PREAMP_NOMINAL_N    = VS::v(250.0f);   // 0.833  — preamp plates

// ═══════════════════════════════════════════════════════════════════
// SUPPLY DROOP — TIME CONSTANTS (real milliseconds, for exp() precomp)
// ═══════════════════════════════════════════════════════════════════
constexpr float TAU_POWER_MS          = 10.0f;    // C1 node — fast sag
constexpr float TAU_SCREEN_MS         = 6.0f;     // screen node — fastest (small cap)
constexpr float TAU_PI_MS             = 35.0f;    // C2 node — medium
constexpr float TAU_PREAMP_MS         = 80.0f;    // C3 node — slow
// Precompute: node.decay = exp(-1.0 / (TAU_xx_MS * 0.001 * effectiveSR))

// ═══════════════════════════════════════════════════════════════════
// SUPPLY DROOP — SENSITIVITY (dimensionless fractions)
// ═══════════════════════════════════════════════════════════════════
constexpr float DROOP_POWER           = 0.20f;    // 20% max droop at full tilt
constexpr float DROOP_SCREEN          = 0.25f;    // 25% max — screen sags harder
constexpr float DROOP_PI              = 0.10f;    // 10% max
constexpr float DROOP_PREAMP          = 0.05f;    // 5% max

// ═══════════════════════════════════════════════════════════════════
// SUPPLY — IMPEDANCES AND DROPPER RESISTORS (derived from VS::r())
// ═══════════════════════════════════════════════════════════════════
constexpr float SOURCE_IMPEDANCE_N    = VS::r(24000.0f);  // PT + rectifier ≈ 24KΩ → 0.08
constexpr float R_DROP1_N             = VS::r(4700.0f);   // power → PI dropper, 4.7KΩ → 0.0157
constexpr float R_DROP2_N             = VS::r(22000.0f);  // PI → preamp dropper, 22KΩ → 0.0733

// ═══════════════════════════════════════════════════════════════════
// FILTER FREQUENCIES (real Hz — used by FilterBank)
// ═══════════════════════════════════════════════════════════════════
constexpr float INPUT_HP_FREQ         = 7.0f;     // Hz — input coupling cap
constexpr float CATHODE_BYPASS_FREQ   = 4.2f;     // Hz — 25µF / 1.5K cathode
constexpr float STAGE1_COUPLING_HP    = 72.0f;    // Hz — V1a → V1b coupling cap
constexpr float STAGE2_COUPLING_HP    = 7.0f;     // Hz — V1b → PI coupling cap
constexpr float PI_COUPLING_HP        = 16.0f;    // Hz — PI → power tubes coupling cap
constexpr float OT_LP_FREQ            = 7000.0f;  // Hz — OT leakage inductance rolloff
constexpr float GAIN_LOADING_LP_FREQ  = 5000.0f;  // Hz — gain pot impedance loading LP
// OT_HP_FREQ removed — LF rolloff now emerges from SpeakerCoupling impedance
// MILLER_LP_FREQ removed — Miller now dynamic, fc from Cgp × (1+Av)

// ═══════════════════════════════════════════════════════════════════
// FILTER — ANTI-DENORMAL
// ═══════════════════════════════════════════════════════════════════
constexpr float ANTI_DENORMAL         = 1e-18f;   // injected into all IIR filter states

// ═══════════════════════════════════════════════════════════════════
// TONE CONTROL (real Hz)
// ═══════════════════════════════════════════════════════════════════
constexpr float TONE_MIN_FREQ         = 1500.0f;  // Hz — darkest setting (max cut)
constexpr float TONE_MAX_FREQ         = 20000.0f; // Hz — brightest (bypass)

// ═══════════════════════════════════════════════════════════════════
// TRIODE MODEL — PREAMP STAGES (normalized via VS::)
// ═══════════════════════════════════════════════════════════════════
constexpr float TRIODE_BIAS_N         = VS::v(-1.5f);    // -1.5V grid bias → -0.005
// TRIODE_DRIVE removed (v2.12) — drive parameter IS the stage gain.
// processBlock passes StageGains::V1A_GAIN (70) or V1B_GAIN (70) directly.
// See §4.1 AI disambiguation, Mistake 1.
constexpr float TRIODE_ASYM_FACTOR    = 1.5f;             // dimensionless neg/pos clip ratio
constexpr float HEADROOM_FACTOR       = 0.85f;            // dimensionless plate swing / supply ratio
constexpr float TRIODE_MU             = 100.0f;           // 12AX7 amplification factor (dimensionless)
constexpr float TRIODE_RP_N           = VS::r(62500.0f);  // 12AX7 plate resistance → 0.208

// ── Triode current model (Child-Langmuir) ───────────────────────
// 12AX7 cutoff: Vgk ~ -3V (plate current drops to zero)
// Operating point: Vgk = -1.5V, Ip ~ 1.2mA
// ip = K * softRect(vgk_n - V_CUTOFF_12AX7_N)^1.5
// With offset: softRect(-0.005 - (-0.01)) = softRect(0.005) = 0.005
// K calibrated: 3394 * 0.005^1.5 ≈ 1.2 (v2.12 fix)
constexpr float V_CUTOFF_12AX7_N      = VS::v(-3.0f);    // -3V cutoff → -0.01 normalized

// ═══════════════════════════════════════════════════════════════════
// PENTODE MODEL — EL84 (normalized via VS::)
// ═══════════════════════════════════════════════════════════════════
constexpr float PENTODE_BIAS_15W_N    = VS::v(-9.6f);   // cathode bias 9.6V (100Ω × 96mA) → -0.032
constexpr float PENTODE_BIAS_7W_N     = VS::v(-7.0f);   // hotter bias 7V (7W mode) → -0.023
constexpr float MU_SCREEN             = 12.0f;            // screen amplification factor (dimensionless)
constexpr float EL84_EXPONENT         = 1.35f;            // transfer curve exponent (dimensionless)
constexpr float EL84_KG               = 0.000324f;        // recalibrated v2.12: idle driveScaled^1.35 / 48mA
constexpr float V_PLATE_KNEE_N        = VS::v(40.0f);     // plate saturation knee, 40V → 0.133
constexpr float SCREEN_CURRENT_RATIO  = 0.25f;            // base Ig2/Ip ratio (dimensionless)
constexpr float SCREEN_REDIST_FACTOR  = 0.15f;            // additional screen ratio at saturated plate (dimensionless)
// Actual screen ratio = SCREEN_CURRENT_RATIO + (1 - plateLimit) * SCREEN_REDIST_FACTOR
// Range: 0.25 (healthy plate) to 0.40 (saturated plate)
constexpr float R_SCREEN_N            = VS::r(470.0f);    // screen grid dropping resistor → 0.00157
constexpr float EL84_CUTOFF_N         = VS::v(-10.0f);   // grid cutoff voltage at nominal Vscreen → -0.0333
// EL84 max plate current: absolute max rating ~65mA continuous
// The power law is unbounded, so clamp ip before plate saturation (v2.12 fix)
constexpr float EL84_IP_MAX_N         = VS::i(0.065f);   // 65mA → 65.0 normalized

// ═══════════════════════════════════════════════════════════════════
// GRID CONDUCTION (normalized via VS::)
// ═══════════════════════════════════════════════════════════════════
constexpr float GRID_CONDUCT_THRESH_N      = VS::v(0.5f);      // triode onset, 0.5V → 0.00167
constexpr float GRID_CONDUCT_THRESH_EL84_N = VS::v(0.0f);      // pentode onset, 0V
constexpr float GRID_R_TRIODE_N            = VS::R_300;        // 300Ω conducting R → 0.001
constexpr float GRID_R_PENTODE_N           = VS::R_500;        // 500Ω conducting R → 0.00167
constexpr float GRID_CHARGE_RATE           = 0.01f;            // dimensionless charge scaling
constexpr float GRID_VT_N                  = VS::v(0.026f);    // thermal voltage, 26mV → 0.0000867
constexpr float GRID_IS                    = 1e-6f;            // diode saturation current (normalized)
constexpr float BLOCKING_CHARGE_SCALE      = 0.001f;           // charge rate per unit grid current (dimensionless)
constexpr float MAX_BIAS_SHIFT_N           = VS::v(-45.0f);    // max blocking bias shift → -0.15
constexpr float COND_LP_MIN_FREQ           = 3000.0f;          // Hz — Miller-like LP at heaviest conduction
constexpr float GRID_I_SATURATION          = 0.01f;            // grid current at full conduction (normalized)
// Grid conduction tau per stage (R_leak_real × C_couple):
//   V1a:  1MΩ × 22nF  = 22ms
//   V1b:  1MΩ × 22nF  = 22ms  (or 500K via gain pot → 11ms)
//   PI:   1MΩ × 47nF  = 47ms
//   EL84: 1MΩ × 100nF = 100ms
// chargeDecay = exp(-1.0 / (tau_sec * effectiveSR))

// ═══════════════════════════════════════════════════════════════════
// SAG / DYNAMICS (exponential envelope)
// ═══════════════════════════════════════════════════════════════════
constexpr float SAG_ATTACK_MS         = 10.0f;    // real ms — cathode cap charge
constexpr float SAG_RELEASE_MS        = 100.0f;   // real ms — cathode cap discharge
constexpr float SAG_DEPTH             = 0.15f;    // dimensionless max bias shift fraction
// Precompute via sag.recalcCoeffs(effectiveSR):
//   attackDecay  = exp(-1.0 / (SAG_ATTACK_MS  * 0.001 * effectiveSR))
//   releaseDecay = exp(-1.0 / (SAG_RELEASE_MS * 0.001 * effectiveSR))

// ═══════════════════════════════════════════════════════════════════
// PHASE INVERTER — LTP NODAL SOLVER (normalized via VS::)
// ═══════════════════════════════════════════════════════════════════
constexpr float PI_TAIL_R_N           = VS::R_1K2;             // 1.2K tail resistor → 0.004
constexpr float PI_RPLATE_N           = VS::R_100K;            // 100KΩ plate load each side → 0.333
constexpr float PI_BIAS_REF_N         = VS::v(-1.5f);          // reference grid DC bias → -0.005
constexpr float PI_RP_N              = VS::r(62500.0f);        // 12AX7 rp for LTP triodes → 0.208
constexpr float PI_MU                = 100.0f;                 // 12AX7 mu (dimensionless)
constexpr float PI_CATHODE_INIT_N    = VS::v(1.5f);            // initial cathode voltage → 0.005
constexpr float PI_GM_DRIVE          = 200.0f;                 // transconductance sharpness (dimensionless)
constexpr float K_12AX7              = 3394.0f;                // recalibrated v2.12: K * softRect(0.005)^1.5 = 1.2mA
// Previous value 4.2e6 was wrong — it was calibrated WITHOUT the cutoff offset.
// With V_CUTOFF_12AX7_N offset: softRect(vgk - cutoff) at operating point = 0.005
// K = 1.2 / (0.005^1.5) = 1.2 / 0.000354 ≈ 3394
// PI_IMBALANCE removed (v2.4) — emerges from differential current steering
// PI_TAIL_SHIFT_COEFF removed (v2.4) — cathode modulation solved nodally

// ═══════════════════════════════════════════════════════════════════
// DYNAMIC MILLER LP (v2.3)
// ═══════════════════════════════════════════════════════════════════
constexpr float CGP_12AX7             = 1.7e-12f;              // grid-plate capacitance (real Farads)
constexpr float CGK_12AX7             = 1.6e-12f;              // grid-cathode capacitance (real Farads)
constexpr float V1A_RSOURCE_REAL      = 10000.0f;              // V1a driving source impedance (real Ω)
constexpr float V1B_RSOURCE_BASE_REAL = 38500.0f;              // V1b base Rsource = V1a Zout (real Ω)
constexpr float GAIN_POT_R_REAL       = 500000.0f;             // gain pot total resistance (real Ω)
// V1b effective Rsource = V1B_RSOURCE_BASE_REAL + (1 - gainA) × GAIN_POT_R_REAL
// V1a fc ≈ 1/(2π × 10K × 121pF) ≈ 130kHz (inaudible, included for completeness)
// V1b fc range: 2.6kHz (gain=0, dark) to 33kHz (gain=1, open)
constexpr float MILLER_FC_SMOOTH_MS   = 1.0f;                  // fc smoothing tau (real ms)
// fcSmoothDecay = exp(-1.0 / (MILLER_FC_SMOOTH_MS * 0.001 * effectiveSR))

// ═══════════════════════════════════════════════════════════════════
// SPEAKER COUPLING — DYNAMIC LOAD LINE (v2.2)
// ═══════════════════════════════════════════════════════════════════
// Speaker parameters: Celestion G12H-30 (Silent Riot stock)
constexpr float SPK_RE_REAL           = 6.0f;                  // DC resistance (real Ω)
constexpr float SPK_RE_N              = VS::r(6.0f);           // → 0.00002
constexpr float SPK_ZPEAK_REAL        = 35.0f;                 // impedance at resonance (real Ω)
constexpr float SPK_ZPEAK_N           = VS::r(35.0f);          // → 0.000117
constexpr float SPK_ZMIN_REAL         = 8.0f;                  // minimum impedance (real Ω)
constexpr float SPK_ZMIN_N            = VS::r(8.0f);           // → 0.0000267
constexpr float SPK_FS                = 75.0f;                 // resonance frequency (real Hz)
constexpr float SPK_Q                 = 3.5f;                  // resonance Q (dimensionless)
constexpr float OT_TURNS_RATIO_SQ     = 1000.0f;               // (Np/Ns)² for 8KΩ:8Ω OT (dimensionless)
constexpr float OT_NOMINAL_LOAD_N     = VS::r(8000.0f);        // nominal primary impedance → 0.0267
constexpr float SPK_VOICECOIL_LP      = 2000.0f;               // voice coil inductance rolloff (real Hz)
// Reflected impedances (primary side, used in gainMod calculation):
constexpr float Z_REFLECTED_BASS_N    = VS::r(35000.0f);       // resonance: 35Ω × 1000 → 35KΩ
constexpr float Z_REFLECTED_MID_N     = VS::r(8000.0f);        // minimum: 8Ω × 1000 → 8KΩ
constexpr float Z_REFLECTED_HF_N      = VS::r(20000.0f);       // voice coil rise: 20Ω × 1000 → 20KΩ
constexpr float SPK_ENV_TAU_MS        = 2.0f;                  // band envelope follower tau (real ms)
constexpr float SPK_GAINMOD_DEPTH     = 2.0f;                  // tanh scaling for gain modulation (dimensionless)
constexpr float SPK_GAINMOD_RANGE     = 0.5f;                  // max gain deviation from 1.0 (dimensionless)
// gainMod = 1.0 + tanh((zRatio - 1.0) * SPK_GAINMOD_DEPTH) * SPK_GAINMOD_RANGE
// Range: ~0.5 to ~1.5 (±50% from nominal)

// ═══════════════════════════════════════════════════════════════════
// GAIN TAPER
// ═══════════════════════════════════════════════════════════════════
constexpr float GAIN_TAPER_EXPONENT   = 2.0f;                  // audio taper curve: y = x^2 (dimensionless)

// ═══════════════════════════════════════════════════════════════════
// STAGE OUTPUT IMPEDANCES (derived from VS::r())
// ═══════════════════════════════════════════════════════════════════
// Z_out = Rplate || rp, where rp(12AX7) ≈ 62.5K at typical operating point
constexpr float Z_OUT_V1A_N           = VS::Z_OUT_V1A;         // 100K||62.5K ≈ 38.5KΩ → 0.128
constexpr float Z_OUT_V1B_N           = VS::Z_OUT_V1B;         // (100K+68K)||62.5K ≈ 47KΩ → 0.157
constexpr float Z_OUT_PI_N            = VS::Z_OUT_PI;          // 100K||62.5K per side → 0.128

// ═══════════════════════════════════════════════════════════════════
// LUT WAVESHAPER (Section 4.1 Option C)
// ═══════════════════════════════════════════════════════════════════
constexpr int   TABLE_SIZE            = 2048;                   // LUT entries (power of 2 for fast indexing)
```

---

*Document: Monolit Beatz — Silent Riot DSP Model Spec v2.13*
*Revisions: v1.x–v2.6: see git history. v2.7: Section 8.0 foundation dependencies, Section 4.1 AI disambiguation. v2.8: reading guide (five content types). v2.9: structured reference block (signal chain, blocks, assumptions, constraints, tradeoffs, goals). v2.10: per-function contract blocks for all 15 DSP functions/structs. v2.11: Section 9 expanded. v2.12: Numerical audit found 5 bugs -- triodeCurrentModel missing cutoff offset (K recalibrated 4.2e6->3394), pentode bias was plate supply 270V not cathode voltage 9.6V (EL84_KG recalibrated 800->0.000324), LTP solver oscillates without damping (added alpha=0.25 iteration), softConductionCurve overlap width used raw bias not cutoff distance, pentode power law unbounded (added EL84_IP_MAX_N clamp) -- inertia system (5 layers of tube feel), phase shift documentation, noise/hum plan (Phase 4), ear-tuning guide with sonic problem -> constant mapping, tuning order. v2.13: Sync audit — Section 10 constants updated to match v2.12 scaffold fixes (PENTODE_BIAS 9.6V/7V, EL84_KG=0.000324, K_12AX7=3394, TRIODE_DRIVE removed, V_CUTOFF_12AX7_N and EL84_IP_MAX_N added). Section 7 parameter table expanded with UI spec param IDs (halfPower, cabSim, power, presetIndex, irIndex), display ranges (0–10 for knobs), and defaults. §4.1 Mistake 3 disambiguation fixed (was still referencing -270V). Product name: Silent Riot (CMake PRODUCT_NAME), internal codename: SilentRiot.*
*Target: JUCE VST3/AU Plugin — Ubuntu 24.x / CMake + clangd*
*Author: Boris / Monolit Beatz*
