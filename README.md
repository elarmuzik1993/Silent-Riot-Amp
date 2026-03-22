# Silent Riot — Amp Simulator Plugin

[![Build & Test](https://github.com/elarmuzik1993/Silent-Riot-Amp/actions/workflows/build.yml/badge.svg)](https://github.com/elarmuzik1993/Silent-Riot-Amp/actions/workflows/build.yml)

**Monolit Beatz** amp sim plugin — full analog circuit model of the Orange Tiny Terror (AD5).

VST3 + Standalone for Linux and Windows. Built with [JUCE 7](https://juce.com/), C++17, CMake.

## Features

- Full tube signal chain: dual 12AX7 preamp, LTP phase inverter, push-pull EL84 power amp
- Nonlinear DSP: Koren triode/pentode waveshapers (softplus cutoff), grid conduction, output transformer saturation
- Power supply droop with 4-node PSU model and sag envelope
- Speaker coupling with 3-band dynamic impedance and excursion-based amplitude scaling
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

190 tests across two targets:
- **TinyTerrorTests** — 177 pure C++ tests (DSP modules, signal chain, dynamics diagnostics)
- **TinyTerrorJuceTests** — 13 JUCE-dependent tests (cabinet IR, convolution)

## Signal Chain

All processing uses a **normalized voltage domain** (300V B+ = 1.0).

```
Input -> HP 7Hz -> Grid Cond -> Triode V1a -> Cathode Shelf -> Miller LP -> Gain
      -> Grid Cond -> Triode V1b -> Cathode Shelf -> Miller LP -> LP 5kHz
      -> LTP Phase Inverter -> Tone (Vox Cut) -> Master Volume
      -> Grid Cond -> EL84 x2 Push-Pull -> PSU Sag
      -> Speaker Coupling -> OT Saturation -> OT LP 7kHz
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
│   └── cabinet/     SpeakerCoupling, CabinetSim, IRLibrary, OTSaturation
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
- [x] **DSP Audit:** All filter frequencies, Koren tube params, and signal chain verified against spec v2.13
- [x] **Listening Test Round 1:** DI chain trace analysis, audio taper fix, speaker coupling smoothing
- [x] **Listening Test Round 2:** Fix PI grid conduction pumping at gain≥5
- [ ] **Listening Test & Tuning:** A/B against real Tiny Terror captures, ear-tune output gain and dynamics

## Version History

- **v0.3.3** — Listening test round 2: fix PI grid conduction pumping at gain≥5 — reduce charge rate (0.01→0.003) and max bias shift (-5V→-1.5V) to prevent coupling cap charge saturation that caused 23 dB output swing.
- **v0.3.2** — Listening test round 1: apply audio taper to both gain pot gangs (Spec §3.3), fixing 11 dB dip/bloom at medium dynamics. Increase speaker coupling gainMod smoothing (2ms → 15ms) to eliminate per-cycle AM artifact.
- **v0.3.1** — Excursion-based speaker coupling: impedance deviation now scales with cone excursion (amplitude-dependent, bass-weighted). Palm mutes and heavy bass passages drive more coupling than lead lines.
- **v0.3.0** — DSP audit complete: all filter frequencies aligned with spec (tone 1.5k-20kHz, cab presence 2.5kHz, cab rolloff 5.5kHz, cab notch 400Hz, gain loading 5kHz). Ready for listening test and tuning.
- **v0.2.0** — Phase 4 complete: GUI, IR loader, presets, polish
- **v0.1.0** — Phase 1-3 complete: full DSP signal chain

## CI

GitHub Actions builds on every push to `main`/`develop`:
- Linux Release (VST3)
- Windows Release (VST3 + Standalone)
- Test suite (Debug, 190 tests)

Download build artifacts from the [Actions tab](https://github.com/elarmuzik1993/Silent-Riot-Amp/actions).

## License

Copyright Monolit Beatz. All rights reserved.
