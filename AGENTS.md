# Silent Riot Project Instructions

## Project Overview

Silent Riot is a JUCE 7 amp simulator plugin built with C++17, CMake, and Ninja on Ubuntu Linux. The product target is `SilentRiot`; the plugin/product name is `Silent Riot`.

## Architecture

- DSP code lives under `src/dsp/`.
- Shared DSP utilities and constants live under `src/utils/`.
- GUI components live under `src/gui/`.
- Main plugin integration is in `src/PluginProcessor.*` and `src/PluginEditor.*`.
- Use `namespace sr` for project code.
- Keep processing in the normalized voltage domain where `300V B+ = 1.0`; see `VoltageScale.h`.
- Keep DSP constants in `Constants.h`. Use `_N` for normalized units, `_REAL` for real-world units, and no suffix for dimensionless values.

## DSP Rules

- Do not allocate dynamically in `processBlock`; pre-allocate buffers and state.
- Inject `ANTI_DENORMAL` into IIR filter state where needed.
- Smooth parameters per sample at the oversampled rate using the existing `ParamSmoother` target/tick pattern.
- Bias values are negative. Apply bias with `vGrid + vBias`.
- Do not add artificial stage gain after `TriodeWaveshaper`; gain should emerge from the tube/load-line model.
- Preserve the current signal-chain order unless a change is explicitly intended and tested.

## GUI Rules

- Follow the existing custom JUCE component style in `src/gui/`.
- Use the project colour palette, font helpers, and `SilentRiotLookAndFeel`.
- Preserve the fixed-size window plus X11 shape-mask strategy for collapsible panels.
- Keep UI state in the APVTS `ValueTree` when it needs to persist with plugin state.

## Build And Test

```bash
./scripts/build.sh
./scripts/build.sh release
cmake -B build -DSR_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests use Catch2 v3:

- `SilentRiotTests` for pure C++ DSP tests.
- `SilentRiotJuceTests` for JUCE-dependent cabinet, convolution, and IR-loading tests.

Verify DSP, parameter, and GUI behavior with focused tests whenever changing behavior.
