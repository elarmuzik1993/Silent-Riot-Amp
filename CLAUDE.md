# Silent Riot — CLAUDE.md

## Project Overview

**Silent Riot** is a JUCE 7 amp simulator plugin (VST3/Standalone) by Monolit Beatz, modelling the Orange Tiny Terror (AD5). The internal codename is `TinyTerror`. Built with C++17, CMake, and Ninja on Ubuntu Linux.

## Key Specs

- **DSP Spec**: `docs/Silent Riot DSP Model Spec v2.13.md` — authoritative source for all signal chain math, function contracts, and constants
- **UI Spec**: `docs/SilentRiot_UI_Spec.md` — layout, components, colours, typography, JUCE painting strategy
- **UI Prototype**: `docs/SilentRiot_Final.jsx` — React reference prototype (pixel-accurate interactive mockup)

## Architecture

All processing uses a **normalized voltage domain**: 300V B+ = 1.0. See `VoltageScale.h`. Ohm's law is preserved: V_n = I_n × R_n.

### Signal Chain (per-sample, oversampled)

```
Input → Scale to guitar domain → HP 7Hz → Grid cond V1a → Triode V1a → Clamp
→ Cathode bypass LP → HP 72Hz → Miller LP V1a → Report PSU → Gain pot A
→ Grid cond V1b → Triode V1b → Clamp → Cathode bypass LP → HP 7Hz
→ Miller LP V1b → Report PSU → Gain pot B → LP 5kHz → Grid cond PI
→ LTP solver → HP×2 16Hz → Report PSU → Tone (Vox cut) → Master volume
→ Grid cond EL84×2 → Soft conduction×2 → Pentode×2 → Push-pull sum
→ Report PSU → Sag update → Speaker coupling → OT LP 7kHz → Cab sim
→ Output scale → tanh limiter → Output
```

### Source Layout

```
src/
├── utils/           VoltageScale.h, Constants.h, Parameters.h, ParamSmoother.h
├── dsp/
│   ├── filters/     OnePole.h, Biquad.h, FilterBank.h/.cpp, DynamicMillerLP.h
│   ├── tubes/       TriodeWaveshaper, PentodeWaveshaper, GridConduction
│   ├── power/       PowerSupplyModel, SagEnvelope, LTPSolver
│   └── cabinet/     SpeakerCoupling, CabinetSim
├── gui/             LookAndFeel, AmpKnob
├── PluginProcessor  Full signal chain (§2.5 processBlock)
└── PluginEditor     GUI shell
```

## Build

```bash
./scripts/install-deps.sh          # Ubuntu deps (one-time)
./scripts/build.sh                 # Debug build (Ninja)
./scripts/build.sh release         # Release build
cmake -B build -DTT_BUILD_TESTS=ON # Build with tests
```

## Implementation Phases

- **Phase 1** (done): Preamp — VoltageScale, triode waveshaper, cascaded stages, gain, oversampling
- **Phase 2** (done): Power section — LTP solver, tone, EL84 pentode, push-pull, 15W/7W mode
- **Phase 3** (in progress): Dynamics — PSU droop ✓, sag envelope ✓, grid conduction ✓, speaker coupling ✓, OT saturation (optional, not yet)
- **Phase 4**: Cabinet IR loader, GUI (see UI spec), presets, polish

## Conventions

- All DSP constants live in `Constants.h` — suffix `_N` = normalized, `_REAL` = real units, no suffix = dimensionless
- `namespace tt` for all DSP code; `using VS = VoltageScale` shorthand
- No dynamic allocation in `processBlock`; all buffers pre-allocated
- `ANTI_DENORMAL` (1e-18f) injected in all IIR filter state
- Parameter smoothing at block rate (base SR / block), NOT oversampled rate
- CMake target is `TinyTerror`; product name is `Silent Riot`
- Tests use Catch2 v3 — one test file per DSP module

## Important Pitfalls (from spec)

- Bias values are NEGATIVE — use `+` not `-` when applying (e.g. `vGrid + vBias`)
- `triodeWaveshaper` gain is on INPUT only — do NOT scale output by stageGain
- `reportPreampLevel()` takes SIGNAL level, NOT current
- ParamSmoother `controlRate` must use BASE sample rate, not oversampled
- PSU droop impedance values (SOURCE_IMPEDANCE_N=0.08) produce aggressive sag with full pentode currents (~48mA/tube normalized to 48.0) — may need constant tuning after listening tests
- `outputGainCompensation` hardcoded to 2.0 — needs tuning
