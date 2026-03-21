# Silent Riot — CLAUDE.md

## Project Overview

**Silent Riot** (v0.3.0) is a JUCE 7 amp simulator plugin (VST3/Standalone) by Monolit Beatz, modelling the Orange Tiny Terror (AD5). The internal codename is `TinyTerror`. Built with C++17, CMake, and Ninja on Ubuntu Linux.

## Key Specs

- **DSP Spec**: `docs/Silent Riot DSP Model Spec v2.13.md` — authoritative source for all signal chain math, function contracts, and constants
- **UI Spec**: `docs/SilentRiot_UI_Spec.md` — layout, components, colours, typography, JUCE painting strategy
- **UI Prototype**: `docs/SilentRiot_Final.jsx` — React reference prototype (pixel-accurate interactive mockup)

## Architecture

All processing uses a **normalized voltage domain**: 300V B+ = 1.0. See `VoltageScale.h`. Ohm's law is preserved: V_n = I_n × R_n.

### Signal Chain (dual-path)

**IIR path** (IR_INDEX=0, per-sample oversampled):
```
Input → Scale to guitar domain → HP 7Hz → Grid cond V1a → Triode V1a → Clamp
→ Cathode bypass shelf → HP 72Hz → Miller LP V1a → Report PSU → Gain pot A
→ Grid cond V1b → Triode V1b → Clamp → Cathode bypass shelf → HP 7Hz
→ Miller LP V1b → Report PSU → Gain pot B → LP 5kHz → Grid cond PI
→ LTP solver (100K/82K asymmetric plate loads) → HP×2 16Hz → Report PSU
→ Tone (Vox cut) → Master volume → Grid cond EL84×2
→ Soft conduction×2 (bias-dependent Class A/AB) → Pentode×2 → Push-pull sum
→ Report PSU → Sag update (raw plate current) → OT current scale
→ Speaker coupling (bandpass extraction) → OT saturation → OT LP 7kHz
→ IIR cab filters → Output scale → tanh limiter → Downsample → Output
```

**IR path** (IR_INDEX>0, convolution at base SR after downsample):
```
... → Speaker coupling → OT saturation → OT LP 7kHz → Downsample
→ IR convolution → Output scale → tanh limiter → Output
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
├── gui/             14 custom components (see GUI Architecture below)
├── PluginProcessor  Full signal chain (§2.5 processBlock)
└── PluginEditor     GUI shell (shape-clipped dynamic window)
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
- **Phase 3** (done): Dynamics — PSU droop, sag envelope, grid conduction, speaker coupling, OT saturation
- **Phase 4** (done): Cabinet IR loader, GUI (14 custom components), presets, polish (animations, live metering, keyboard nav, UI persistence)
- **DSP Audit** (done): All filter frequencies, Koren tube parameters, and signal chain order verified against spec v2.13 and open-source reference implementations (Guitarix, SwankyAmp, ChowDSP)
- **Next**: Listening test and ear-tuning (outputGainCompensation, V1B_RSOURCE_MAX_REAL, dynamics feel)

## Conventions

- All DSP constants live in `Constants.h` — suffix `_N` = normalized, `_REAL` = real units, no suffix = dimensionless
- `namespace tt` for all DSP code; `using VS = VoltageScale` shorthand
- No dynamic allocation in `processBlock`; all buffers pre-allocated
- `ANTI_DENORMAL` (1e-18f) injected in all IIR filter state
- Parameter smoothing per-sample at oversampled rate (target/tick model in ParamSmoother)
- CMake target is `TinyTerror`; product name is `Silent Riot`
- Tests use Catch2 v3 — one test file per DSP module, two test targets:
  - `TinyTerrorTests` — 161 pure C++ tests (no JUCE dependency)
  - `TinyTerrorJuceTests` — 13 JUCE-dependent tests (cabinet IR, convolution, user IR loading)

## Cabinet IR Loader Architecture

- **CabinetSim** wraps `juce::dsp::Convolution` with the canonical `BufferTransfer` pattern (from JUCE ConvolutionDemo)
- **IRLibrary.h** — stock IR metadata registry (8 IRs) + synthetic IR generator (placeholders until real WAVs arrive)
- **Thread safety**: IR loading is deferred to the message thread via `juce::AsyncUpdater`. Audio thread only reads `useIR` (atomic) and calls `bufferTransfer.get()` (try-lock, never blocks). Zero allocation on audio thread.
- **IR_INDEX param**: 0=IIR mode, 1–8=stock IRs, 9+=user slots (require `loadIRFromFile()`)
- **User IR loading**: `TinyTerrorProcessor::loadUserIR(file)` delegates to `CabinetSim::loadIRFromFile()`. Wired from `IRLoaderPanel::onUserIRLoaded` (drag-and-drop + file browser)
- `useIR` is only set to `true` AFTER the IR buffer is pushed to `bufferTransfer`
- Convolution runs at base SR (after downsample), not at oversampled rate
- `getTailLengthSeconds()` reports IR tail using the IR's native sample rate

## GUI Architecture

14 custom components in `src/gui/`, all in `namespace tt`:

- **ColourPalette.h** — 23 inline colour constants (dark theme)
- **FontData.h/.cpp** — Embedded Space Mono + DM Sans fonts via BinaryData
- **LookAndFeel.h/.cpp** — `SilentRiotLookAndFeel` (aliased as `OrangeLookAndFeel`)
- **NotchKnob.h/.cpp** — Segmented-arc rotary knob (20 segments over 270° sweep, numbered 0–10, notch pointer)
- **VUMeter.h/.cpp** — 24-segment LED bar, 60Hz timer, green/yellow/red zones
- **ToggleSwitch.h/.cpp** — iOS-style toggle wrapping `juce::ToggleButton` for APVTS attachment
- **HeaderBar.h/.cpp** — Brand + PresetSelector + gear icon
- **FooterBar.h/.cpp** — OS factor, latency, sample rate, bit depth
- **SectionBar.h/.cpp** — Clickable expand/collapse bar with chevron + hint text
- **AdvancedPanel.h/.cpp** — PSU supply rails (teal gradient bars) + engine state readout
- **PresetSelector.h/.cpp** — `[◂ | name | ▸]` cyclic preset browser
- **SegmentedSelector.h/.cpp** — Row of mutually-exclusive buttons
- **SettingsPanel.h/.cpp** — Overlay (340px) with UI preferences + About section
- **IRLoaderPanel.h/.cpp** — Category tabs, scrollable IR list, waveform display, drag-and-drop
- **IRWaveformDisplay.h/.cpp** — Waveform plot with envelope fill

**Window strategy**: Window set to max size (700×772 at 1× scale); X11 Shape Extension clips visible/input region to actual content height. Collapsible panels animate within the fixed-size window — `applyShapeMask()` updates the clip rect each frame. No `setSize()` during panel toggle = no compositor blink. Shape accounts for standalone wrapper title bar offset via `getTopLevelComponent()->getLocalPoint()`.

**Editor timer** (30Hz): Polls atomic level meters (real input/output RMS + grid current), DSP readout for advanced panel, footer stats. Drives animated section expand/collapse (28px/frame linear steps). SectionBar chevrons animate at 300ms, gear icon at 400ms, toggle thumb at ~200ms.

**Keyboard**: Arrow keys ±0.1 display (±0.01 internal), PageUp/Down ±1.0 display (±0.1 internal) via `NotchKnob::KnobSlider` override. Tab order: GAIN→TONE→VOLUME→7W→CAB→PWR.

**UI state persistence**: Panel open states, theme, meter ballistics, knob mode, and UI scale stored as properties on `apvts.state` ValueTree — automatically saved/restored with plugin state.

## DSP Implementation Details

- **Triode model**: Koren with iterative load-line solve. Standard 12AX7 parameters (mu=100, ex=1.4, Kg1=1060, Kp=600, Kvb=300 — matches Guitarix/industry reference). `TriodeState` caches converged plate voltage between samples → 4 iterations sufficient (was 8). Stateless overload available for tests.
- **Pentode model**: Koren pentode equations (mu=19.5, ex=1.35, Kg1=820, Kp=84, Kvb=300) — screen grid acts as virtual anode in the Koren formula. Softplus cutoff, tanh IP clamp, plate saturation knee, screen redistribution.
- **LTP solver**: Asymmetric plate loads (100K/82K). Caches plate voltages A/B between samples. 4 iterations with alpha=0.25 damping.
- **Oversample switching**: Crossfade state machine (FadingOut → Reconfiguring → FadingIn), 128-sample fade at base SR. PSU frozen during transition to prevent voltage recovery pop.
- **Sag model**: Cathode resistor physics — `biasOffset = -(Itotal - Iquiescent) × R_cathode`. Fed with raw `|ipA| + |ipB|` before OT scaling.
- **Tone control**: Vox cut on differential signal only (preserves common-mode). Exponential frequency mapping (1.5kHz–20kHz) for perceptually linear knob response.
- **Tone coefficient threshold**: Skip `std::pow` + filter recalc when smoothed tone value hasn't changed by >0.001 (~99% of samples).

## DSP Audit Status (v0.3.0)

All filter frequencies now match spec v2.13:
- Tone: 1.5kHz (dark) to 20kHz (bright)
- Gain loading LP: 5kHz
- Cab presence: 2.5kHz peak, Q=1.5, +3dB
- Cab rolloff: 5.5kHz LP, Q=0.5
- Cab notch: 400Hz, Q=2.0
- PI coupling HP: 16Hz (was already correct)
- All preamp filters verified correct

Koren tube parameters verified against Guitarix and standard datasheets.

**Tuning items for listening test:**
- `outputGainCompensation = 1.0` — ear-tune for optimal headroom (clean: -3.9 dBFS, driven: -0.1 dBFS at comp=1.0)
- `V1B_RSOURCE_MAX_REAL = 150KΩ` — code-only clamp not in spec; may limit Miller darkening at low gain (consider removing if low-gain tones sound too bright)

## Important Pitfalls (from spec)

- Bias values are NEGATIVE — use `+` not `-` when applying (e.g. `vGrid + vBias`)
- `triodeWaveshaper` gain emerges from Koren model (mu, Rp, rp) — do NOT scale output by stageGain
- `reportPreampLevel()` takes SIGNAL level, NOT current
- ParamSmoother runs per-sample at oversampled rate (NOT block rate) — init with effectiveSR
- `sag.update()` must be called with raw plate current BEFORE `OT_CURRENT_SCALE` multiplication
- OT saturation: `OT_SAT_DRIVE=400`, `OT_SAT_DC_FREQ=20Hz` — DC block prevents integrator blowup from asymmetric tube DC
- LTP plate loads are asymmetric: `PI_RPLATE_A_N` (100K) / `PI_RPLATE_B_N` (82K)
- Speaker coupling uses `makeBandPass` (NOT `makePeakEQ` with 0dB gain — that's a flat passthrough)
- `softConductionCurve` biasFactor: `PENTODE_BIAS_15W_N / vBias_n` (NOT the inverse — 7W must get wider overlap)
- PSU droop uses impedance-based model (I×R voltage drops through SOURCE_IMPEDANCE_N, R_DROP1_N, R_DROP2_N) — droop depth emerges from physics, not from percentage constants
- Cathode bypass is a **high-pass gain shelf** (not a lowpass): `x -= lp.process(x) * CATHODE_BYPASS_DEPTH`. Full gain above 4.2 Hz, reduced gain (~0.29×) below.
- IR loading must NEVER happen on the audio thread — always use `AsyncUpdater` to defer to message thread
- `bufferTransfer.set()` (blocking SpinLock) is message-thread only; `bufferTransfer.get()` (try-lock) is audio-thread only
- ToggleSwitch uses `onStateChange` internally for thumb animation — external callbacks must use `onClick` (not `onStateChange`) to avoid breaking the animation timer
