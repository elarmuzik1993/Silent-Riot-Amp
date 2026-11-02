# Silent Riot — CLAUDE.md

@AGENTS.md

## Project Overview

**Silent Riot** (v0.4.0) is a JUCE 7 amp simulator plugin (VST3/Standalone) by Monolit Beatz. Built with C++17, CMake, and Ninja on Ubuntu Linux.

## Key Specs

- **DSP Spec**: `docs/design/dsp-model-spec-v2.13.md` — the original design rationale (circuit, voltage domain, numerical hygiene, why tube feel works). **Not authoritative**: unmaintained since 2026-03, and its constants and several core designs (LTP solver, gain taper, grid conduction, sag, tone stack) are superseded. Where it disagrees with the code, the code, this file and `docs/records/handoffs/handoff-*.md` win. Never "fix" code to match it
- **UI Spec**: `docs/design/ui-spec.md` — layout, components, colours, typography, JUCE painting strategy
- **UI Prototype**: `docs/design/ui-prototype.jsx` — early React visual reference, frozen since 2026-06. **Out of date**: still shows the old notch-pointer knob and has no tuner, EFFECTS panel or level trims. For anything it disagrees on, `src/gui/` and the UI spec win
- **Credits**: `attribution/CREDITS.md` — originality statement, Koren equation sourcing, third-party library licenses; confirms no code copied from Guitarix/SwankyAmp/ChowDSP/NAM

## Architecture

Human overview: `docs/dev/architecture.md`.

All processing uses a **normalized voltage domain**: 300V B+ = 1.0. See `VoltageScale.h`. Ohm's law is preserved: V_n = I_n × R_n.

### Signal Chain (dual-path)

**IIR path** (IR_INDEX=0, per-sample oversampled):
```
Input → Scale to guitar domain → HP 7Hz → Grid cond V1a → Triode V1a → Clamp
→ Cathode bypass shelf → HP 72Hz → Miller LP V1a → Report PSU → Gain pot A
→ Grid cond V1b → Triode V1b → Clamp → Cathode bypass shelf → HP 7Hz
→ Miller LP V1b → Report PSU → Gain pot B → LP 5kHz → Grid cond PI
→ LTP solver (100K/82K asymmetric plate loads) → HP×2 16Hz → Report PSU
→ Shape (3-band mid scoop/boost) → Master volume → Grid cond EL84×2
→ Soft conduction×2 (bias-dependent Class A/AB) → Pentode×2 → Push-pull sum
→ Report PSU → Sag update (raw plate current) → OT current scale
→ Speaker coupling (bandpass extraction) → OT saturation → OT LP 7kHz
→ IIR cab filters → Output scale → tanh limiter → Downsample → (align delay) → Cab-mode fade → Output
```

**IR path** (IR_INDEX>0, convolution at base SR after downsample):
```
... → Speaker coupling → OT saturation → OT LP 7kHz → Downsample
→ IR convolution → Output scale → tanh limiter (4× oversampled) → Cab-mode fade → Output
```

### Source Layout

```
src/
├── utils/           VoltageScale.h, Constants.h, Parameters.h, ParamSmoother.h
├── dsp/
│   ├── filters/     OnePole.h, Biquad.h, FilterBank.h/.cpp, DynamicMillerLP.h
│   ├── tubes/       TriodeWaveshaper, PentodeWaveshaper, GridConduction
│   ├── power/       PowerSupplyModel, SagEnvelope, LTPSolver
│   └── cabinet/     SpeakerCoupling, CabinetSim, IRLibrary, OTSaturation
├── gui/             19 custom components (see GUI Architecture below)
├── PluginProcessor  Full signal chain (§2.5 processBlock)
└── PluginEditor     GUI shell (window sized to content height)
```

## Build

```bash
./scripts/install-deps.sh          # Ubuntu deps (one-time)
./scripts/build.sh                 # Debug build (Ninja)
./scripts/build.sh release         # Release build
cmake -B build -DSR_BUILD_TESTS=ON # Build with tests
```

## Conventions

- All DSP constants live in `Constants.h` — suffix `_N` = normalized, `_REAL` = real units, no suffix = dimensionless
- `namespace sr` for all DSP code; `using VS = VoltageScale` shorthand
- No dynamic allocation in `processBlock`; all buffers pre-allocated
- `ANTI_DENORMAL` (1e-18f) injected in all IIR filter state
- Parameter smoothing per-sample at oversampled rate (target/tick model in ParamSmoother)
- CMake target is `SilentRiot`; product name is `Silent Riot`
- Tests use Catch2 v3 — one test file per DSP module, three test targets:
  - `SilentRiotTests` — pure C++ (no JUCE); `SilentRiotJuceTests` — cabinet IR, convolution, oversampling, effects
  - `SilentRiotProcessorTests` — the real `SilentRiotProcessor` via `tests/processor_harness.h`, GUI components that need the plugin's code, and the **tone lock**. Use it for anything that lives in `processBlock`'s wiring: a unit test of the DSP class it calls cannot see that. Each case takes seconds, so keep runs short. Full list of what it covers: `docs/dev/testing.md`

## Status

Phases 1–4, the DSP audit, the listening rounds, the power-section calibration and the IR-path work are **done**; the tone is **frozen at `tone-v1`** (see `AGENTS.md`). Recalibration (A1/B1/E3) is on hold and the neural model waits for a stable DSP target. History and numbers: `docs/records/implementation-history.md`, `docs/records/handoffs/`, `.agents/STATE.md`.

## Where the detail lives

| Area | Read |
|---|---|
| Cabinet IR loader, level match, cab-mode switcher, thread rules | `docs/dev/ir-loader.md` |
| GUI components (19 in `src/gui/`), window strategy, editor timer, keyboard, state persistence | `docs/dev/gui-architecture.md` |
| Tube/LTP/sag/latency/widener/shape implementation, DSP audit, ear-tuning knobs | `docs/dev/dsp-notes.md` |
| Every pitfall below, with the measurements behind it | `docs/dev/pitfalls.md` |
| Tone lock procedure | `docs/dev/tone-lock.md` |

Read the matching file **before** changing that area. The bullets below are the rules, not the reasons.

## Ear-tuning knobs (in the order they bite)

`GRID_CLAMP_KNEE_N = 6V` (primary gain-staging control; reach for it before the gain taper) · `GAIN_KNOB_HEADROOM = 0.8` · `VOLUME_KNOB_HEADROOM = 0.21` (the give/touch control; re-measure D1 **at knob 10**) · `OUTPUT_MAKEUP_GAIN = 3.5` (set together with the volume headroom; calibrate on a real DI, not the D1 gate signal) · `OT_CURRENT_SCALE = 0.009` · `OT_SAT_DRIVE = 190` (inverse to level; retune with `tools/sr_thump.py`) · `outputGainCompensation = 1.0` · `V1B_RSOURCE_MAX_REAL = 150KΩ`. Reasons and numbers: `docs/dev/dsp-notes.md`. All of it is frozen by the tone lock.

## Important Pitfalls

**Voltage domain and tubes**
- Bias values are NEGATIVE: `vGrid + vBias`, never `-`.
- `triodeWaveshaper` gain emerges from the Koren model; never scale its output by stageGain.
- `reportPreampLevel()` takes SIGNAL level, not current. `sag.update()` takes raw plate current BEFORE `OT_CURRENT_SCALE`.
- ParamSmoother runs per sample at the oversampled rate (init with effectiveSR).
- Gain pot: taper **Gang A only** (`audioTaper()`); Gang B is linear. Tapering both gives x⁴.
- Grid conduction is a **diode clamp**, not a resistive divider. Thresholds track |bias|: `GRID_CONDUCT_THRESH_EL84_N` stays near 8.6 V, NOT 0 (motorboating on silence; detect with `tools/sr_idle.py`).
- `GRID_CHARGE_RATE` ≤ 0.003 and `MAX_BIAS_SHIFT_N` ≤ 1.5 V; the rate is per sample at 176.4 kHz (`GRID_CHARGE_REF_SR`), never applied raw. A pinned rail may be load-bearing: relax it and measure before calling it the bug.
- The master volume is the **power-stage drive control**. If sag/give measures low, check plate-current swing before touching any sag constant.
- `GridConductionModel::lastGridDrop` is a voltage; convert to current only in the readout.
- `softConductionCurve` biasFactor is `PENTODE_BIAS_15W_N / vBias_n` (not the inverse).
- Cathode bypass is a high-pass gain shelf, not a low-pass. LTP plate loads are asymmetric (100K/82K).
- LTP solver **root-finds** (secant + bisection). Never "simplify" it to a damped fixed point; `alpha = 1.0` is a trap; inner iterations stay at 4.
- Speaker coupling: 3-way complementary OnePole crossover (not band-pass extraction); `SPK_GAINMOD_TAU_MS` ≥ 15 ms.
- OT saturation: `OT_SAT_DRIVE = 190`, `OT_SAT_DC_FREQ = 20 Hz` (the DC block prevents integrator blowup).
- PSU droop is impedance-based; depth emerges from physics, not percentage constants.

**State, resets and system boundaries**
- `resetDSPState()` starts on the idle operating point (`IdleOperatingPoint.h`). Any new stateful DSP that sits on a DC operating point at idle must be seeded there too, or the startup burst returns.
- Effects skipped while off (delay, reverb, IR convolution, widener) freeze their state: clear them on the off→on edge, re-arm via `tailsStale`, and add a test in `processor_effects_tests.cpp`. `reset()` does not touch the amp.
- Audio input and parameters are system boundaries: `scrubNonFiniteInput()` runs first in `processBlock` and `processBlockBypassed`; never remove it because the output is scrubbed. New APVTS parameters are **appended** to `createParameterLayout()`.
- INPUT/OUTPUT trims are gain staging: unity at centre, multiply skipped at unity, read via `sanitizeUnitParamOr(v, 0.5f)`.
- Host bypass is `processBlockBypassed` (delayed by `engineLatency`), not JUCE's default. Latency is an exact integer (10/12/12); `makeIntegerLatencyOversampler()` builds every oversampler.
- Delay: loop gain is exactly FEEDBACK (`loopNorm`); it must never self-oscillate. Two ceilings on purpose (`DELAY_TIME_KNOB_MAX_MS` vs `DELAY_SYNC_MAX_MS`); don't merge them. Reverb SIZE is ramped, then glided; don't drop the ramp.

**Threads and the IR loader**
- IR loading never happens on the audio thread, and the audio thread never posts it (`triggerAsyncUpdate()` locks). State goes through atomics; the processor's 20 Hz `timerCallback()` polls it and reports latency (`pendingLatency`). The timer takes `irStateLock`; never take it on the audio thread. Destroy the processor on the message thread.
- `bufferTransfer.set()` is message-thread only; `.get()` is audio-thread only. `useIR` is set `true` only after the buffer is pushed.
- Never pass JUCE's convolution `Normalise::yes`; the IR level match (`IRLevel.h`) passes `Normalise::no`. `installIRNow()` only from `prepareToPlay`.
- IR_INDEX → convolution always through `applyUserIRIndex()`. The built-in cab's biquads run in every mode; don't gate or reset them.

**GUI**
- `ToggleSwitch` uses `onStateChange` internally; external callbacks use `onClick`. Size every child against what it paints (`ToggleSwitch::minHeight`, `NotchKnob::stripHeight`). No focus ring on knobs without asking.
- Non-ASCII UI text goes in `juce::CharPointer_UTF8`, only with glyphs the embedded fonts have (`scripts/check_text.py`).
