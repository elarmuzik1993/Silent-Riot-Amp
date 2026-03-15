# Silent Riot — Amp Simulator Plugin

[![Build & Test](https://github.com/elarmuzik1993/Silent-Riot-Amp/actions/workflows/build.yml/badge.svg)](https://github.com/elarmuzik1993/Silent-Riot-Amp/actions/workflows/build.yml)

**Monolit Beatz** amp sim plugin — full analog circuit model of the Orange Tiny Terror (AD5).

VST3 + Standalone for Linux and Windows. Built with [JUCE 7](https://juce.com/), C++17, CMake.

## Features

- Full tube signal chain: dual 12AX7 preamp, LTP phase inverter, push-pull EL84 power amp
- Nonlinear DSP: triode/pentode waveshapers, grid conduction, output transformer saturation
- Power supply droop with 4-node PSU model and sag envelope
- Speaker coupling with 3-band dynamic impedance
- Dual cabinet mode: IIR filters or convolution IR loader
- 2x/4x/8x oversampling with crossfade state machine
- 15W / 7W power mode (Class AB / Class A bias shift)
- Drag-and-drop user IR loading (.wav)
- 14 custom GUI components with animated collapsible panels
- UI scaling (75% / 100% / 125% / 150%)

## Build

### Linux

```bash
./scripts/install-deps.sh    # Ubuntu packages (one-time)
./scripts/build.sh           # Debug build
./scripts/build.sh release   # Release build
```

### Windows

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Tests

```bash
cmake -B build -G Ninja -DTT_BUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```

161 tests across two targets:
- **TinyTerrorTests** — 148 pure C++ tests (DSP modules, signal chain)
- **TinyTerrorJuceTests** — 13 JUCE-dependent tests (cabinet IR, convolution)

## Signal Chain

All processing uses a **normalized voltage domain** (300V B+ = 1.0).

```
Input -> HP 7Hz -> Grid Cond -> Triode V1a -> Miller LP -> Gain
      -> Grid Cond -> Triode V1b -> Miller LP -> LP 5kHz
      -> LTP Phase Inverter -> Tone (Vox Cut) -> Master Volume
      -> Grid Cond -> EL84 x2 Push-Pull -> PSU Sag
      -> Speaker Coupling -> OT LP 7kHz
      -> Cabinet (IIR or IR Convolution) -> Soft Limiter -> Output
```

## Source Layout

```
src/
├── utils/           VoltageScale, Constants, Parameters, ParamSmoother
├── dsp/
│   ├── filters/     OnePole, Biquad, FilterBank (24 instances), DynamicMillerLP
│   ├── tubes/       TriodeWaveshaper, PentodeWaveshaper, GridConduction
│   ├── power/       PowerSupplyModel, SagEnvelope, LTPSolver
│   └── cabinet/     SpeakerCoupling, CabinetSim, IRLibrary
├── gui/             14 custom components (NotchKnob, VUMeter, ToggleSwitch,
│                    HeaderBar, FooterBar, SectionBar, AdvancedPanel,
│                    PresetSelector, SegmentedSelector, SettingsPanel,
│                    IRLoaderPanel, IRWaveformDisplay, LookAndFeel, FontData)
├── PluginProcessor  Full signal chain
└── PluginEditor     GUI shell with shape-clipped dynamic window
```

## Implementation Status

- [x] **Phase 1:** Preamp — VoltageScale, triode waveshaper, cascaded stages, gain, oversampling
- [x] **Phase 2:** Power section — LTP solver, tone, EL84 pentode, push-pull, 15W/7W mode
- [x] **Phase 3:** Dynamics — PSU droop, sag envelope, grid conduction, speaker coupling, OT saturation
- [x] **Phase 4:** Cabinet IR loader, GUI (14 components), presets, polish

## CI

GitHub Actions builds on every push to `main`/`develop`:
- Linux Release (VST3)
- Windows Release (VST3 + Standalone)
- Test suite (Debug, 161 tests)

Download build artifacts from the [Actions tab](https://github.com/elarmuzik1993/Silent-Riot-Amp/actions).

## License

Copyright Monolit Beatz. All rights reserved.
