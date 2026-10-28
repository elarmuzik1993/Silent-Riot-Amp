# Architecture

A guided tour of how Silent Riot is put together. For the detailed engineering notes,
calibration history and the traps that have bitten before, read the **Important
Pitfalls** section of [`../../CLAUDE.md`](../../CLAUDE.md) before changing DSP code.

## The model in one paragraph

Silent Riot models one fictional amplifier: two cascaded 12AX7 triode stages with a
dual-gang gain pot, a long-tail-pair phase inverter, a post-PI "Shape" EQ, a master
volume, and a cathode-biased push-pull EL84 pair (15 W or 7 W) into an output transformer
and speaker. Gain, compression and "feel" are not added as effects: they come out of tube
models solving their load lines, a power supply that droops under current, cathode sag,
grid conduction and speaker-impedance interaction. The DSP runs oversampled (2×, 4× or 8×).

## Normalised voltage domain

All processing uses voltages normalised to the supply: **300 V B+ = 1.0**
(`src/utils/VoltageScale.h`). Ohm's law still holds (V_n = I_n × R_n), so circuit
equations read the same as on a schematic. Constants live in `src/utils/Constants.h`
with a suffix for their unit: `_N` normalised, `_REAL` real-world units, none for
dimensionless.

Bias voltages are negative and are added: `vGrid + vBias`.

## Signal chain

There are two output paths, selected by the cabinet mode.

**Built-in cab (IIR) path** — `IR_INDEX = 0`, everything per sample at the oversampled rate:

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

**IR path** — `IR_INDEX > 0`, the cabinet is a convolution at the host rate:

```
... → Speaker coupling → OT saturation → OT LP 7kHz → Downsample
→ IR convolution → Output scale → tanh limiter (4× oversampled) → Cab-mode fade → Output
```

The effects run at the host rate around the amp: INPUT trim → noise gate → boost → *amp*
→ delay → spring reverb → stereo widener → OUTPUT trim. The orchestration is all in
`SilentRiotProcessor::processBlock` (`src/PluginProcessor.cpp`).

## Source layout

```
src/
├── PluginProcessor.*   parameters, prepareToPlay, processBlock, state save/restore
├── PluginEditor.*      the GUI shell
├── utils/              VoltageScale, Constants, Parameters (IDs), ParamSmoother, ParamSanitize
├── dsp/
│   ├── filters/        one-pole, biquad, filter bank, dynamic Miller low-pass
│   ├── tubes/          triode and pentode waveshapers, grid conduction
│   ├── power/          power supply, sag envelope, LTP (phase inverter) solver
│   ├── cabinet/        speaker coupling, OT saturation, convolution cab, IR level match,
│   │                   cab-mode switcher, user IR state
│   ├── effects/        boost, noise gate, delay, spring reverb, stereo width, tempo sync,
│   │                   safety limiter
│   ├── analysis/       pitch detector (the tuner)
│   ├── IdleOperatingPoint.h   the amp's DC operating point at start-up
│   └── OversampleSwitcher.*   click-free oversampling-factor changes
└── gui/                custom JUCE components (knobs, meters, panels, LookAndFeel)
```

DSP code is in `namespace sr` and has no dependency on the GUI.

## Threads

| Thread | Does | Must not |
|---|---|---|
| Audio (`processBlock`) | All DSP; reads parameters; publishes meter values to atomics; writes the raw input to the tuner ring | Allocate, lock, or load files |
| Message (GUI) | Editor painting, a 30 Hz timer that polls meters and runs pitch detection, IR file loading | Touch DSP state directly |
| JUCE background | Builds new convolution engines for `juce::dsp::Convolution` | — |

- **IR loading** reads and level-matches the file on the message thread, then hands the
  buffer over through a try-lock transfer the audio thread polls without blocking
  (`src/dsp/cabinet/CabinetSim.h`). The only synchronous install is in `prepareToPlay`,
  so a saved session plays its IR from the first block.
- **The tuner** gets raw input samples from a lock-free ring the audio thread writes
  (`pushTunerSamples`), before the power-off check, so it works with the amp silenced. The
  editor reads the latest window (`readLatestTunerSamples`) and detects pitch off the
  audio thread (`src/dsp/analysis/PitchDetector.h`).

## Parameters, smoothing and oversampling

- Parameters live in a JUCE `AudioProcessorValueTreeState`; IDs are in
  `src/utils/Parameters.h`. New parameters are **appended** to `createParameterLayout()`,
  because hosts store automation by position.
- Knob values are smoothed **per sample at the oversampled rate** with `ParamSmoother`.
- Changing the oversampling factor fades out, reconfigures and fades back in
  (`OversampleSwitcher`), with the power supply frozen across the change so it can't pop.
- Reported latency is an exact whole number of samples (10 at 2×, 12 at 4× and 8×), the
  same in both cab modes; the built-in cab path is delayed to match the IR path.

## Start-up and state

- `prepareToPlay` puts the amp on its **idle operating point** (`IdleOperatingPoint.h`):
  coupling capacitors charged and the supply at its idle sag, as a real amp sits after
  warm-up. Starting from zeroed filters would play half a second of settling as a burst.
- Plugin state is the APVTS `ValueTree` saved as XML (`getStateInformation`), plus the
  user IRs' raw audio and UI preferences (panel states, theme, scale) stored as properties
  on the same tree. The tuner mute is never restored.
