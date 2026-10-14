# Silent Riot — CLAUDE.md

@AGENTS.md

## Project Overview

**Silent Riot** (v0.3.4) is a JUCE 7 amp simulator plugin (VST3/Standalone) by Monolit Beatz. Built with C++17, CMake, and Ninja on Ubuntu Linux.

## Key Specs

- **DSP Spec**: `docs/Silent Riot DSP Model Spec v2.13.md` — authoritative source for all signal chain math, function contracts, and constants
- **UI Spec**: `docs/SilentRiot_UI_Spec.md` — layout, components, colours, typography, JUCE painting strategy
- **UI Prototype**: `docs/SilentRiot_Final.jsx` — React reference prototype (pixel-accurate interactive mockup)
- **Credits**: `CREDITS.md` — originality statement, Koren equation sourcing, third-party library licenses; confirms no code copied from Guitarix/SwankyAmp/ChowDSP/NAM

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
→ Shape (3-band mid scoop/boost) → Master volume → Grid cond EL84×2
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
├── gui/             17 custom components (see GUI Architecture below)
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

## Implementation Phases

- **Phase 1** (done): Preamp — VoltageScale, triode waveshaper, cascaded stages, gain, oversampling
- **Phase 2** (done): Power section — LTP solver, shape/tone, EL84 pentode, push-pull, 15W/7W mode
- **Phase 3** (done): Dynamics — PSU droop, sag envelope, grid conduction, speaker coupling, OT saturation
- **Phase 4** (done): Cabinet IR loader, GUI (custom components), presets, polish (animations, live metering, keyboard nav, UI persistence)
- **DSP Audit** (done): All filter frequencies, Koren tube parameters, and signal chain order verified against spec v2.13 and open-source reference implementations (Guitarix, SwankyAmp, ChowDSP)
- **Listening Test Round 1** (done): DI chain trace analysis, audio taper fix on gain pots, speaker coupling smoothing
- **Listening Test Round 2** (done): Fix PI grid conduction pumping at gain≥5 (charge rate + max bias shift)
- **Listening Test Round 3** (done): Fix volume knob causing distortion/signal loss above noon — tightened sag cold clamp -6V → -3V
- **Shape Control** (done): Replaced Vox-cut tone with Orange Shape-inspired 3-band EQ (low shelf 250Hz, mid bell 800Hz, high shelf 3.5kHz) — sweeps from warm/mid-forward (CCW) to scooped/modern (CW)
- **Power-Section Calibration** (done, USE-59): the amp core, root-caused against measurements rather than by ear. See `docs/handoff-power-section-calibration.md` for the full record and `docs/handoff-ltp-convergence.md` for the solver.
  - Gain pot law was **x⁴** — `audioTaper()` applied at both gangs, so the laws multiplied. Now Gang A tapers, Gang B is linear
  - **LTP solver did not converge** — a period-2 limit cycle, i.e. a Nyquist-rate square wave synthesised from DC. Replaced with a bracketed root-find
  - Sag reference desynchronised from the tube model → pinned at its warm clamp. Now derived via `pentodeQuiescentTotal()`
  - **EL84 drew 3.9× too little current** vs datasheet. `EL84_KG1` 820 → 210.5, with `EL84_IP_MAX_N` / `SCREEN_CURRENT_RATIO` corrected
  - **Grid conduction never clamped** (resistive divider) → PI grid reached 121 V on a −1.5 V-biased 12AX7. Now a logarithmic diode law
  - **EL84 grid threshold was 0 V** on a −9.6 V-biased tube → coupling-cap charge railed on silence, driving a 17.7 Hz motorboating oscillation at gain ≥ 9
  - Shape EQ silently reset to noon after any oversample change
  - Result: all three tone sign-off THD bands pass together for the first time (clean 1.03%, edge 9.44%, crank 30.1%)
- **Power-Stage Drive / "give"** (done, USE-59): D1 sag depth 0.38 → **2.93 dB** at the gate point and **1.98 dB** at knob 10 (target 1.5–4.0); touch sensitivity 2.4 → **10.8 dB** over a 36 dB input span (14.8:1 → 3.3:1); the volume knob's top half now spreads 12.2 dB of level instead of 1.15 dB. Root cause was *not* `MAX_BIAS_SHIFT_N`: the sag offset never rails, it is **starved** — average plate current was flat (78.4 → 79.5 mA, +1.4%) at every gain and across a 100× input range, so B+ drooped 0.20% where a real amp droops 10–20%. The master volume sits in front of the EL84 grids and the chain hands it 3–5× more drive than a −9.6 V-biased tube can use, so the grids passed `GRID_CONDUCT_THRESH_EL84_N` around knob 4 and the diode clamp set the level from there up. Fixed with `VOLUME_KNOB_HEADROOM` + `volumeDriveTaper()`, mirroring `GAIN_KNOB_HEADROOM`. B4 (even-harmonic warmth) now passes as a side effect. See defect 8 in `docs/handoff-power-section-calibration.md`
- **Next**: A1 clean loudness + the P-\* volume sign-off positions need re-ratification (Calibration Pass §5) — the volume knob's meaning changed by design, and A1 failed on the *loud* side before this change and the quiet side after; B1 clean THD 0.99% vs a 1.0% floor; E3 aliasing (−37.9 dBc vs ≤ −85, MUST-PASS) — despite the gate's name this is whole-engine aliasing on the IIR path, not the IR path: the gate's bright IR is an unsupplied placeholder that `signoff.py` skips, and the IR-limiter bug it was written for is fixed (`508c842`). See `.agents/STATE.md`; ~0.5 s startup thump while the PSU settles (this is what E1's "idle noise floor" is averaging in)
- **Neural model**: On hold until ear-tuning is finalized — training against DSP output that's still changing produces a model that must be retrained after every constant tweak. Finalize the DSP first, then capture and train against a stable target.

## Conventions

- All DSP constants live in `Constants.h` — suffix `_N` = normalized, `_REAL` = real units, no suffix = dimensionless
- `namespace sr` for all DSP code; `using VS = VoltageScale` shorthand
- No dynamic allocation in `processBlock`; all buffers pre-allocated
- `ANTI_DENORMAL` (1e-18f) injected in all IIR filter state
- Parameter smoothing per-sample at oversampled rate (target/tick model in ParamSmoother)
- CMake target is `SilentRiot`; product name is `Silent Riot`
- Tests use Catch2 v3 — one test file per DSP module, two test targets:
  - `SilentRiotTests` — 270 pure C++ test cases (no JUCE dependency)
  - `SilentRiotJuceTests` — 38 JUCE-dependent test cases (cabinet IR, convolution, user IR loading, oversample switcher, delay/reverb)

## Cabinet IR Loader Architecture

- **CabinetSim** wraps `juce::dsp::Convolution` with the canonical `BufferTransfer` pattern (from JUCE ConvolutionDemo)
- **IRLibrary.h** — stock IR metadata registry (8 IRs) + synthetic IR generator (placeholders until real WAVs arrive)
- **Thread safety**: IR loading is deferred to the message thread via `juce::AsyncUpdater`. Audio thread only reads `useIR` (atomic) and calls `bufferTransfer.get()` (try-lock, never blocks). Zero allocation on audio thread.
- **IR_INDEX param**: 0=IIR mode, 1–8=stock IRs, 9+=user slots (require `loadIRFromFile()`)
- **User IR loading**: `SilentRiotProcessor::loadUserIR(file)` delegates to `CabinetSim::loadIRFromFile()`. Wired from `IRLoaderPanel::onUserIRLoaded` (drag-and-drop + file browser)
- `useIR` is only set to `true` AFTER the IR buffer is pushed to `bufferTransfer`
- Convolution runs at base SR (after downsample), not at oversampled rate
- `getTailLengthSeconds()` reports IR tail using the IR's native sample rate

## GUI Architecture

17 custom components in `src/gui/`, all in `namespace sr`:

- **ColourPalette.h** — 28 inline colour constants (dark theme), incl. the `knobBody`/`knobFace`/`knobTick` ramp. Text tiers hold ≥4.5:1 against `panel`; `muted` (3.5:1) is decoration only, never a glyph
- **FontData.h/.cpp** — Embedded Space Mono + DM Sans fonts via BinaryData, plus the `Fonts::Sizes` type scale (micro 9 / small 10 / body 11 / value 12.5 / title 14 / hero 22). No bare numeric font sizes anywhere in `src/gui/` — tune type from that one block. Note these are JUCE font *heights* (ascent+descent), which render ~25% smaller than the same number in CSS px
- **LookAndFeel.h/.cpp** — `SilentRiotLookAndFeel`. Owns `drawRotarySlider` — the arc knob paint routine (five uniform ticks, value arc, body + face circles, indicator triangle inside the face). Every dimension is a ratio of `D = jmin(w,h)`, so one routine covers all knob sizes and `setScale` does not touch the graphic. `kRotaryStartAngle`/`kRotaryEndAngle` (225°→495°) define the 270° sweep — a slider painted with this LookAndFeel **must** call `setRotaryParameters()` with them, or its ticks won't line up with its value
- **NotchKnob.h/.cpp** — Rotary knob shell: owns the slider, reserves the readout + label strip and centres a square knob in what's left, paints the value readout and parameter label. The knob face itself is painted by the LookAndFeel. **No focus indicator is drawn** — the knobs stay in the tab order and respond to arrow/PageUp keys, but focus is deliberately invisible (removed 2026-08-16 by request); don't reintroduce a ring without asking. Name is historical — the knob was a segmented arc with a notch pointer before the arc design. `setScale()` scales only the text strip (each metric floored — text stops being readable long before the knob does); `preferredHeight` (112px) is the room a scale-1.0 knob needs, and laying one out shorter clips the name. Optional `setValueFormatter()` overrides the default 0–10 readout (used for DELAY TIME's ms and the `%` knobs)
- **VUMeter.h/.cpp** — 24-segment LED bar, 60Hz timer, green/yellow/red zones
- **ToggleSwitch.h/.cpp** — toggle wrapping `juce::ToggleButton` for APVTS attachment, in two styles: `Style::Switch` (iOS-style pill + thumb, used by the amp row) and `Style::Led` (a flat lit bar, used by the effects cards). **Always lay one out against `ToggleSwitch::minHeight(style)`** — 34px for Switch, 21px for Led. It paints its label at the top and its track/lamp *below*, and JUCE clips paint() to the component's own bounds, so a toggle given only enough height for its label loses the track entirely: the control renders as a dead text label while its clicks keep working and the parameter keeps changing. That silent failure is what made the effects toggles look broken (fixed 2026-08-16); `EffectsPanel` now static_asserts its card metrics against `minHeight`
- **HeaderBar.h/.cpp** — Brand + PresetSelector + gear icon
- **TunerBar.h/.cpp** — Always-visible live tuner strip (note name + octave, cents meter, freq readout). Owns a `PitchDetector`, pulls DI windows from the processor's capture ring on the shared 30Hz timer
- **FooterBar.h/.cpp** — OS factor, latency, sample rate, bit depth
- **SectionBar.h/.cpp** — Clickable expand/collapse bar with chevron + hint text
- **AdvancedPanel.h/.cpp** — PSU supply rails (teal gradient bars) + engine state readout
- **PresetSelector.h/.cpp** — `[◂ | name | ▸]` cyclic preset browser
- **SegmentedSelector.h/.cpp** — Row of mutually-exclusive buttons
- **SettingsPanel.h/.cpp** — Overlay (340px) with UI preferences + About section
- **IRLoaderPanel.h/.cpp** — Scrollable IR list, waveform display, drag-and-drop (category tabs per UI spec §5.7 not yet implemented — user-IR-only today)
- **IRWaveformDisplay.h/.cpp** — Waveform plot with envelope fill
- **EffectsPanel.h/.cpp** — 2×2 card grid (UTILITY / BOOST / DELAY / REVERB). `panelHeight` (352) is driven by the DELAY card, which is the tightest: it carries an extra sync-division row *and* a full `NotchKnob::preferredHeight` knob row. Shrink it and DELAY's knob names are the first thing to disappear

**Window strategy**: The window is sized to its content height (`kWindowW` 700 × `getContentHeight()`, times the UI scale) on every platform. `applyShapeMask()` — the name is historical — runs each animation frame and calls `setSize()` whenever the animated panel heights change; the size guard makes the steady state a no-op. The old approach (a max-height window clipped with the X11 Shape Extension) is gone: the compositor used the true window geometry for move constraints, so a window taller than the screen got pinned to the top edge and could not be dragged. Sizing to content does **not** clamp to the display work area, so tall panel combinations can still exceed the screen. On Linux the editor still keeps one cached X11 connection (`getX11Display()`) for `setWindowBackground()` and the compositor bypass.

**Live tuner**: `src/dsp/analysis/PitchDetector.h/.cpp` (pure C++, MPM/NSDF) tracks the DI fundamental. The audio thread writes raw channel-0 input into a lock-free ring (`SilentRiotProcessor::pushTunerSamples`) **before** the power-off early-return, so tuning works with the amp silenced; `TunerBar` pulls the latest window via `readLatestTunerSamples` on the editor timer and runs detection off the audio thread. Detection ≥ ±5¢ accuracy on open strings (verified in `pitch_detector_tests.cpp`).

**Editor timer** (30Hz): Polls atomic level meters (real input/output RMS + grid current), DSP readout for advanced panel, footer stats, and refreshes the live tuner. The input/output level meters now read **post-trim** — after the INPUT/OUTPUT gain-staging knobs are applied. Drives animated section expand/collapse (28px/frame linear steps). SectionBar chevrons animate at 300ms, gear icon at 400ms, toggle thumb at ~200ms.

**Keyboard**: Arrow keys ±0.1 display (±0.01 internal), PageUp/Down ±1.0 display (±0.1 internal) via `NotchKnob::KnobSlider` override. Tab order: GAIN→SHAPE→VOLUME→7W→CAB→PWR.

**UI state persistence**: Panel open states, theme, meter ballistics, knob mode, and UI scale stored as properties on `apvts.state` ValueTree — automatically saved/restored with plugin state.

## DSP Implementation Details

- **Triode model**: Koren with iterative load-line solve. Standard 12AX7 parameters (mu=100, ex=1.4, Kg1=1060, Kp=600, Kvb=300 — matches Guitarix/industry reference). `TriodeState` caches converged plate voltage between samples → 4 iterations sufficient (was 8). Stateless overload available for tests.
- **Pentode model**: Koren pentode equations (mu=19.5, ex=1.35, Kg1=`EL84_KG1` 210.5, Kp=84, Kvb=300) — screen grid acts as virtual anode in the Koren formula. Softplus cutoff, tanh IP clamp, plate saturation knee, screen redistribution. Kg1 was 820 until USE-59 (3.9× too little current); see the datasheet pin under DSP Audit Status.
- **LTP solver**: Asymmetric plate loads (100K/82K). Root-finds the shared cathode node — secant seeded from the previous sample's `vCathode`, bisection over `[0, vSupply]` as the fallback, tolerance 1e-5 — with 4 inner plate iterations per residual evaluation. Caches plate voltages A/B and `vCathode` between samples; `reset()` restores both to their `init()` values. See `docs/handoff-ltp-convergence.md`.
- **Oversample switching**: Crossfade state machine (FadingOut → Reconfiguring → FadingIn), 128-sample fade at base SR. PSU frozen during transition to prevent voltage recovery pop.
- **Sag model**: Cathode resistor physics — `biasOffset = -(Itotal - Iquiescent) × R_cathode`. Fed with raw `|ipA| + |ipB|` before OT scaling. Cold clamp is -0.01 (-3V) — keeps effective bias from going past -12.6V (cutoff is -10V); prevents deep Class C signal loss at high volume.
- **Stereo widener** (`StereoWidth.h`): final-stage widener knob (`width` param, autonomous — no toggle; 0=off/dry, 1=wide, defaults off), runs LAST on the finished stereo bus (after delay/reverb). Additive: Mid/Side scale (sideGain 1→MAX) plus a Haas+all-pass decorrelator injected into the SIDE only (0→MAX), with a slow LFO-modulated fractional read tap that micro-detunes the side voice ~±5 cents (doubler shimmer). `L+R = 2·Mid` stays invariant at any setting → perfect mono compatibility, and width appears even on the dual-mono dry amp (NeuralDSP-doubler style). Never narrows below the input. Stereo-only (no-op on a mono bus); skipped entirely (bit-exact dry) when parked at 0 (`isActive()`). Width value one-pole smoothed (50ms). Lives in the EFFECTS panel's **UTILITY** card, which also hosts the noise gate and the INPUT/OUTPUT level trims — all four knobs are autonomous (gate and width self-gate at 0, INPUT/OUTPUT are unity at centre), no ON/OFF toggle.
- **Shape control**: Orange Shape-inspired 3-band EQ on differential signal only (preserves common-mode even harmonics from PI asymmetry). Three biquad filters: low shelf (250Hz, 0→+4dB), mid bell (800Hz Q=1.2, +5→-9dB), high shelf (3.5kHz, -3→+4dB). Gains interpolate linearly with knob position. At noon: mild scoop (+2dB low, -2dB mid, +0.5dB high).
- **Shape coefficient threshold**: Skip sin/cos/sqrt filter recalc when smoothed shape value hasn't changed by >0.001 (~99% of samples).
- **Speaker coupling excursion**: Impedance deviation scales with bass-weighted cone excursion (X ∝ V/f²). `tanh(excursion * XCUR_SENSITIVITY)` models Xmax suspension stiffening — low excursion keeps impedance nominal, high excursion allows full deviation. Double-gated with existing `modBlend` for clean off-ramp at silence.

## DSP Audit Status (v0.3.4)

All filter frequencies now match spec v2.13:
- Shape: 3-band EQ — low shelf 250Hz, mid bell 800Hz Q=1.2, high shelf 3.5kHz (replaces Vox-cut tone)
- Gain loading LP: 5kHz
- Cab presence: 2.5kHz peak, Q=1.5, +3dB
- Cab rolloff: 5.5kHz LP, Q=0.5
- Cab notch: 400Hz, Q=2.0
- PI coupling HP: 16Hz (was already correct)
- All preamp filters verified correct

Koren tube parameters verified against Guitarix and standard datasheets. The EL84
is additionally pinned to its **datasheet operating point** (Va=250V, Vg2=250V,
Vg1=−7.3V → Ia=48 mA, Ig2=5.5 mA) by tests in `pentode_tests.cpp` — `EL84_KG1`
and `EL84_IP_MAX_N` are coupled through the tanh knee, so re-solve if either moves.

**Ear-tuning knobs, in the order they bite:**
- `GRID_CLAMP_KNEE_N = 6V` — how many volts above threshold a conducting grid may
  sit. **This is the primary gain-staging control** — it decides how much drive
  reaches the next stage. Reach for this before the gain taper.
- `GAIN_KNOB_HEADROOM = 0.8` — fraction of the drive range the knob can reach.
  Knob 10 delivers what knob 8 used to. Raise toward 1.0 to return the top end.
- `VOLUME_KNOB_HEADROOM = 0.21` — **this is the give/touch-sensitivity control.**
  The master volume *is* the power-stage drive control, and past ~knob 4 on the
  old law the EL84 grids sat in permanent conduction, where the diode clamp
  rather than the tube set the level (36 dB in → 2.4 dB out). 0.21 keeps knob 10
  below `GRID_CONDUCT_THRESH_EL84_N`. Raise toward 1.0 for a louder, more
  compressed, less dynamic amp — and re-measure D1 **at knob 10**, which moves
  fast with it.
- `OUTPUT_MAKEUP_GAIN = 3.5` — recovers ~11 dB of the ~20 dB `VOLUME_KNOB_HEADROOM`
  costs. Applied post-cab, pre-tanh, so it does NOT disturb the OT-saturation
  calibration. **Set it together with `VOLUME_KNOB_HEADROOM`:** they are two
  limiters in series, and make-up gain does not restore give, it relocates the
  compression from the grid clamp onto the output tanh. Headroom 0.30 + makeup
  2.5 read D1 1.98 at the gate's volume 0.70 but **0.98 at knob 10**.
  **Calibrate against a real DI, not the D1 gate signal** — `note:110:0.5` is
  ~9 dB hotter than a nominal −18 dBFS DI, so tuning to it leaves the amp quiet
  in actual use (makeup 2.5 gave only −17.0 LUFS at gain 10 / volume 10, with
  6.3 dB of peak headroom unused). 3.5 lands at −14.2 LUFS. Above ~4 the tanh
  becomes the dominant limiter and D1 at knob 10 falls out of band.
- `OT_CURRENT_SCALE = 0.009` — output level trim. Not a pure derivation: the
  IP_MAX tanh knee and plate saturation are nonlinear, so it is set empirically.
- `OT_SAT_DRIVE = 190` — OT flux saturation. Scales *inversely* with signal level,
  so it must be retuned whenever plate currents or `OT_CURRENT_SCALE` move. Too
  high crushes the low end first (`flux ∝ 1/f`) and kills palm-mute thump —
  measure with `tools/sr_thump.py`, not by ear.
- `outputGainCompensation = 1.0` — final headroom into the output tanh.
- `V1B_RSOURCE_MAX_REAL = 150KΩ` — code-only clamp not in spec; may limit Miller
  darkening at low gain (consider removing if low-gain tones sound too bright)

## Important Pitfalls (from spec)

- Bias values are NEGATIVE — use `+` not `-` when applying (e.g. `vGrid + vBias`)
- `triodeWaveshaper` gain emerges from Koren model (mu, Rp, rp) — do NOT scale output by stageGain
- `reportPreampLevel()` takes SIGNAL level, NOT current
- ParamSmoother runs per-sample at oversampled rate (NOT block rate) — init with effectiveSR
- `sag.update()` must be called with raw plate current BEFORE `OT_CURRENT_SCALE` multiplication
- OT saturation: `OT_SAT_DRIVE=190` (was 400 — retuned with `tools/sr_thump.py` after the current-scale recalibration), `OT_SAT_DC_FREQ=20Hz` — DC block prevents integrator blowup from asymmetric tube DC
- LTP plate loads are asymmetric: `PI_RPLATE_A_N` (100K) / `PI_RPLATE_B_N` (82K)
- Speaker coupling uses a complementary 3-way OnePole crossover (`bass + mid + hf == powerOut` always) for impedance-weighted reflection — NOT a `makeBandPass`/`makePeakEQ` extraction (see `SpeakerCoupling::process`)
- `softConductionCurve` biasFactor: `PENTODE_BIAS_15W_N / vBias_n` (NOT the inverse — 7W must get wider overlap)
- PSU droop uses impedance-based model (I×R voltage drops through SOURCE_IMPEDANCE_N, R_DROP1_N, R_DROP2_N) — droop depth emerges from physics, not from percentage constants
- Cathode bypass is a **high-pass gain shelf** (not a lowpass): `x -= lp.process(x) * CATHODE_BYPASS_DEPTH`. Full gain above 4.2 Hz, reduced gain (~0.29×) below.
- IR loading must NEVER happen on the audio thread — always use `AsyncUpdater` to defer to message thread
- `bufferTransfer.set()` (blocking SpinLock) is message-thread only; `bufferTransfer.get()` (try-lock) is audio-thread only
- ToggleSwitch uses `onStateChange` internally for thumb animation — external callbacks must use `onClick` (not `onStateChange`) to avoid breaking the animation timer
- Gain pot: taper **Gang A only** (`audioTaper()`, Spec §3.3); **Gang B is LINEAR**. The signal passes through both gangs, so the law the player feels is the *product*. Tapering neither leaves the LTP 4× too hot at noon (gain collapse on attack, dip/bloom); tapering *both* gives x⁴, which puts noon at −24 dB, squeezes all usable drive into dial 7–10, and drives V1b/PI grid conduction into blocking collapse ("drowning" when pushed). The user knob is additionally remapped through `gainDriveTaper()` (cubic × `GAIN_KNOB_HEADROOM`) before the gangs. Pinned by the combined-law tests in `signal_chain_tests.cpp`
- Grid conduction is a **diode clamp, not a resistive divider**. `vLoaded = threshold + knee·log1p(vAbove/knee)` — transparent below the knee, logarithmic above. The old `Rg/(Rg+Rs)` divider could only ever *attenuate*: at the PI that ratio is 0.23, so a ~520 V swing still delivered a **121 V grid** on a 12AX7 biased at −1.5 V, which left the whole power section overdriven at every knob position and flattened the amp's dynamics
- Grid conduction thresholds track **|bias|**, because a grid conducts once it approaches the *cathode*. 12AX7 at −1.5 V → 0.5 V. `GRID_CONDUCT_THRESH_EL84_N` must stay near the EL84's −9.6 V bias (8.6 V), NOT 0 — at 0 every positive excursion of amplified noise counts as conduction, ratcheting the coupling-cap charge to its rail **on silence** and driving a ~17.7 Hz motorboating oscillation at gain ≥ 9 with nothing plugged in. Detect with `tools/sr_idle.py`
- LTP solver **root-finds** the cathode node (secant + bisection fallback); it is NOT a relaxation. Do not "simplify" it back to a damped fixed-point — that version did not converge, it limit-cycled into a Nyquist-rate square wave at constant input. `alpha = 1.0` is a trap: it reports zero residual because it kills the stage. Inner plate iterations must stay at 4 (3 oscillates). See `docs/handoff-ltp-convergence.md`
- Speaker coupling `SPK_GAINMOD_TAU_MS` must be ≥15ms — shorter values cause per-cycle AM modulation (audible as LFO-like filtering)
- `GRID_CHARGE_RATE` must stay low (≤0.003) and `MAX_BIAS_SHIFT_N` ≤ 1.5V — higher values cause PI grid charge saturation → LTP pumping at gain≥5. The EL84 charge sits **railed at this clamp across the whole playing range**, which looks exactly like the "pinned dynamic" bug fixed four times elsewhere in this file — it is not. The rail is **load-bearing**: it absorbs an overdrive that should not reach the grid, and raising it to −6 V produces blocking collapse (output dead at −58 dBFS, D1 reads a meaningless 39.94 dB). Fix the drive, not the clamp — see `VOLUME_KNOB_HEADROOM`
- The master volume is the **power-stage drive control**, not an output level control — it sits directly in front of the EL84 grids. Above `GRID_CONDUCT_THRESH_EL84_N` (8.6 V of grid drive) the diode clamp rather than the tube sets the level, and the amp stops responding to playing dynamics. Sag and PSU droop are both driven by *average plate current*, so once the grids clamp, that current goes flat (+1.4% under a full-crank note) and every downstream dynamic starves at once. If sag/give measures low, check plate-current swing **before** touching any sag constant — a starved dynamic and a railed one read identically at the gate
- INPUT/OUTPUT level trims are gain staging, NOT tone controls: unity at knob centre, and `processBlock` skips the multiply entirely while a trim sits at unity so the dry path stays bit-identical at defaults. Do not "simplify" them into always-on gain stages. They read through `sanitizeUnitParamOr(v, 0.5f)`, not `sanitizeUnitParam` — a 0.0 fallback would mean −12 dB in and silence out. New APVTS parameters must be **appended** to `createParameterLayout()`; inserting mid-list shifts positional host automation lanes
