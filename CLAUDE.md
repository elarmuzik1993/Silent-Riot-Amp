# Silent Riot — CLAUDE.md

@AGENTS.md

## Project Overview

**Silent Riot** (v0.3.4) is a JUCE 7 amp simulator plugin (VST3/Standalone) by Monolit Beatz. Built with C++17, CMake, and Ninja on Ubuntu Linux.

## Key Specs

- **DSP Spec**: `docs/design/dsp-model-spec-v2.13.md` — the original design rationale (circuit, voltage domain, numerical hygiene, why tube feel works). **Not authoritative**: unmaintained since 2026-03, and its constants and several core designs (LTP solver, gain taper, grid conduction, sag, tone stack) are superseded. Where it disagrees with the code, the code, this file and `docs/records/handoffs/handoff-*.md` win. Never "fix" code to match it
- **UI Spec**: `docs/design/ui-spec.md` — layout, components, colours, typography, JUCE painting strategy
- **UI Prototype**: `docs/design/ui-prototype.jsx` — early React visual reference, frozen since 2026-06. **Out of date**: still shows the old notch-pointer knob and has no tuner, EFFECTS panel or level trims. For anything it disagrees on, `src/gui/` and the UI spec win
- **Credits**: `CREDITS.md` — originality statement, Koren equation sourcing, third-party library licenses; confirms no code copied from Guitarix/SwankyAmp/ChowDSP/NAM

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
- **Power-Section Calibration** (done, USE-59): the amp core, root-caused against measurements rather than by ear. See `docs/records/handoffs/handoff-power-section-calibration.md` for the full record and `docs/records/handoffs/handoff-ltp-convergence.md` for the solver.
  - Gain pot law was **x⁴** — `audioTaper()` applied at both gangs, so the laws multiplied. Now Gang A tapers, Gang B is linear
  - **LTP solver did not converge** — a period-2 limit cycle, i.e. a Nyquist-rate square wave synthesised from DC. Replaced with a bracketed root-find
  - Sag reference desynchronised from the tube model → pinned at its warm clamp. Now derived via `pentodeQuiescentTotal()`
  - **EL84 drew 3.9× too little current** vs datasheet. `EL84_KG1` 820 → 210.5, with `EL84_IP_MAX_N` / `SCREEN_CURRENT_RATIO` corrected
  - **Grid conduction never clamped** (resistive divider) → PI grid reached 121 V on a −1.5 V-biased 12AX7. Now a logarithmic diode law
  - **EL84 grid threshold was 0 V** on a −9.6 V-biased tube → coupling-cap charge railed on silence, driving a 17.7 Hz motorboating oscillation at gain ≥ 9
  - Shape EQ silently reset to noon after any oversample change
  - Result: all three tone sign-off THD bands pass together for the first time (clean 1.03%, edge 9.44%, crank 30.1%)
- **Power-Stage Drive / "give"** (done, USE-59): D1 sag depth 0.38 → **2.93 dB** at the gate point and **1.98 dB** at knob 10 (target 1.5–4.0); touch sensitivity 2.4 → **10.8 dB** over a 36 dB input span (14.8:1 → 3.3:1); the volume knob's top half now spreads 12.2 dB of level instead of 1.15 dB. Root cause was *not* `MAX_BIAS_SHIFT_N`: the sag offset never rails, it is **starved** — average plate current was flat (78.4 → 79.5 mA, +1.4%) at every gain and across a 100× input range, so B+ drooped 0.20% where a real amp droops 10–20%. The master volume sits in front of the EL84 grids and the chain hands it 3–5× more drive than a −9.6 V-biased tube can use, so the grids passed `GRID_CONDUCT_THRESH_EL84_N` around knob 4 and the diode clamp set the level from there up. Fixed with `VOLUME_KNOB_HEADROOM` + `volumeDriveTaper()`, mirroring `GAIN_KNOB_HEADROOM`. B4 (even-harmonic warmth) now passes as a side effect. See defect 8 in `docs/records/handoffs/handoff-power-section-calibration.md`
- **IR-Path Sound** (done, `docs/plans/2026-10-26-ir-path-sound-fixes.md`, record `docs/records/handoffs/handoff-ir-path-sound.md`): the active IR installs synchronously in `prepareToPlay`; every IR is level-matched to the built-in cab (was ~11 dB quieter and +3 dB louder at 96 kHz than 48 kHz) — **existing projects with an IR play louder**; every cab-mode switch fades and IR mode never plays JUCE's pass-through; IRs cut at the 8192-sample cap get a 5 ms fade
- **Next**: A1 clean loudness + the P-\* volume sign-off positions need re-ratification (Calibration Pass §5) — the volume knob's meaning changed by design, and A1 failed on the *loud* side before this change and the quiet side after; B1 clean THD 0.99% vs a 1.0% floor; E3 aliasing (−37.9 dBc vs ≤ −85, MUST-PASS) — despite the gate's name this is whole-engine aliasing on the IIR path, not the IR path: the gate's bright IR is still the unsupplied placeholder that `signoff.py` skips (`tests/fixtures/ir/bright.wav` can now replace it; repointing the gate is the E3 step), and the IR-limiter bug it was written for is fixed (`508c842`). See `.agents/STATE.md`
- **Neural model**: On hold until ear-tuning is finalized — training against DSP output that's still changing produces a model that must be retrained after every constant tweak. Finalize the DSP first, then capture and train against a stable target.

## Conventions

- All DSP constants live in `Constants.h` — suffix `_N` = normalized, `_REAL` = real units, no suffix = dimensionless
- `namespace sr` for all DSP code; `using VS = VoltageScale` shorthand
- No dynamic allocation in `processBlock`; all buffers pre-allocated
- `ANTI_DENORMAL` (1e-18f) injected in all IIR filter state
- Parameter smoothing per-sample at oversampled rate (target/tick model in ParamSmoother)
- CMake target is `SilentRiot`; product name is `Silent Riot`
- Tests use Catch2 v3 — one test file per DSP module, three test targets:
  - `SilentRiotTests` — 308 pure C++ test cases (no JUCE dependency)
  - `SilentRiotJuceTests` — 81 JUCE-dependent test cases (cabinet IR, convolution, IR level match, IR readiness, user IR loading and truncation fade, oversample switcher + latency, delay/reverb)
  - `SilentRiotProcessorTests` — 19 test cases on the real `SilentRiotProcessor` (effect-tail wiring, host bypass, IR restore and first-block install, IR level, cab-mode fades, GUI meters). Links the plugin's SharedCode target `SilentRiot`, so there is no source list to keep in step; drive it through `tests/processor_harness.h` (`ProcessorHarness`: prepared processor, `set()` params in natural units, `run()` / `silence()` in host blocks, `Mode::Bypassed`, `loadIR()`). Use it for anything that lives in `processBlock`'s wiring — a unit test of the DSP class it calls cannot see that. Each case takes seconds (4× oversampled amp), so keep runs short

## Cabinet IR Loader Architecture

- **CabinetSim** wraps `juce::dsp::Convolution` with the canonical `BufferTransfer` pattern (from JUCE ConvolutionDemo)
- **IRLibrary.h** — stock IR metadata registry (8 IRs) + synthetic IR generator (placeholders until real WAVs arrive)
- **Thread safety**: IR loading is deferred to the message thread via `juce::AsyncUpdater`. Audio thread only reads `useIR` (atomic) and calls `bufferTransfer.get()` (try-lock, never blocks). Zero allocation on audio thread.
- **IR_INDEX param** (int 0–`MAX_USER_IRS` = 32): 0 = IIR mode, 1..N = user IR `userIRs[IR_INDEX-1]` (added by `loadUserIR()`, restored from state), past N = convolution disabled — see `applyUserIRIndex()` (`UserIRState.h`). The 8 stock IRs in `IRLibrary.h` are **not** reachable from the parameter: only `CabinetSim::setIRIndex(n > 0)` loads them, and only the tests call it with `n > 0`
- **User IR loading**: `SilentRiotProcessor::loadUserIR(file)` reads through `readUserIRFromFile()` (`UserIRState.h`). Two sample predicates live in `CabinetSim.h`. `isSafeToNormaliseIR()` (name historical): no NaN/Inf and each channel's float sum of squares well below `FLT_MAX` (`MAX_IR_CHANNEL_ENERGY`) — a NaN/Inf sample makes the level match's band power non-finite and reaches the convolution, poisoning or silencing IR mode; the energy ceiling dates from JUCE's own normalisation and is kept so the accepted set did not change. `isAudibleOnEveryChannel()`: every channel has a sample at or above `IR_TRIM_THRESHOLD` — JUCE first trims to the span above an **absolute** −80 dBFS (`Trim::yes`, a union over channels), and then plays each IR channel on its own output channel (output *i* ← IR channel min(*i*, last); on a mono bus only IR channel 0 is convolved), so an all-quiet IR plays silence and a dead channel is a silent side — or total silence on a mono bus when it is channel 0. **Length cap**: `MAX_USER_IR_SAMPLES` (8192); a longer file is cut there and its last `IR_TRUNCATION_FADE_MS` (5 ms) faded out with a raised cosine (`fadeTruncatedTail()`, IR plan Task 4) — the one exception to "store the raw audio", since the stored copy is already truncated; a file at or under the cap stays bit-exact, and restore never fades. **Load** (`readUserIRFromFile()`: file browser, drag-and-drop) requires both (`isLoadableIR()`) plus `isValidUserIRFormat()`; the test-only `CabinetSim::loadIRFromFile` checks `isLoadableIR()` but not the format (no sample-rate band). **Restore** requires the format and only the *safety* predicate: a silent IR is harmless (it plays silence, as when it was saved — except a stereo IR with a dead channel 0 saved in a stereo session, which goes fully silent if restored on a mono bus), and refusing it would drop it and every later slot, which the next save deletes. Restore stops at the first invalid entry, a non-`IR` child included, and drops every entry after it, so no later IR shifts onto an earlier IR's IR_INDEX — those indices fall past the list, which disables the convolution (IIR mode). Wired from `IRLoaderPanel::onUserIRLoaded` (drag-and-drop + file browser)
- **IR level match** (`IRLevel.h`, IR plan Task 2): every load path (`loadIRFromBuffer`, `installIRNow`, test-only `loadStockIR`/`loadIRFromFile`) scales the copy it hands to the convolution, off the audio thread, so its 100 Hz–5 kHz band power (power average of |H(f)|² over 69 points, 1/12 octave, at the IR's native rate, channel-averaged, one gain for all channels) equals the built-in cab biquads' over the same band (−0.89 dB full band, designed at a fixed `IR_LEVEL_REF_DESIGN_SR`; an IR below 11.1 kHz is compared over its clamped band). Gain capped at `IR_LEVEL_MAX_GAIN_DB` (40). JUCE gets `Normalise::no`, which keeps the response — and so the level — the same at every host rate. **Never pass `Normalise::yes`**: it scales to 0.125/√energy after resampling, ~11 dB under the built-in cab and +3 dB at 96 kHz vs 48 kHz. `userIRs` and saved state keep the raw IR (pinned by a processor test). JUCE's −80 dBFS trim acts on the scaled copy. An IR with no sample at or above `IR_TRIM_THRESHOLD` is left unscaled (it still plays the silence it was saved as)
- **IR_INDEX → convolution**: always through `applyUserIRIndex()`. An index past the IR list (automation, or a restored state with fewer IRs than its saved index) must *disable* the convolution, not leave the previous IR playing; restore and `prepareToPlay` apply it directly via `applyIRIndexParam()`, because `processBlock` only reacts when the index changes. `prepareToPlay` passes `IRLoad::Now` → `CabinetSim::installIRNow()` (IR plan Task 1): clear any pending `bufferTransfer`, queue the IR, then `Convolution::prepare()`, which runs the queued load on the calling thread and installs the engine with no pass-through or crossfade — so a restored session plays its IR from the first block, independent of JUCE's background thread. Restore stays `IRLoad::Async` (it can run beside `processBlock`)
- `useIR` is only set to `true` AFTER the IR buffer is pushed to `bufferTransfer`
- **Cab-mode switcher** (`CabModeSwitcher.h`, IR plan Task 3): `useIR` and CAB only *request* a mode (Off / built-in / IR); the switcher owns the **active** one, and everything in `processBlock` (in-loop tanh, IR block, `iirPathAlign`, `cabSim.setEnabled`) runs on the active mode. A change fades out (128 samples), commits at gain 0 at a block start — where `enterCabMode()` runs the B6 resets (convolution + limiter oversampler on entering IR, `iirPathAlign` on leaving it) — holds silent while the entered path comes out of reset (`cabFadeInHold()`: the IR's peak + limiter latency, or the alignment delay), then fades in. The gain is applied once, at base rate on the finished cab-path output before delay/reverb; idle it is skipped, so the steady state is bit-identical. IR mode is entered only once `CabinetSim::hasRealIR()`; until then it keeps playing the active mode and `prime()` lets JUCE install the IR on a silent scratch block — so the first IR of a session never plays JUCE's pass-through. `hasRealIR()` is true when `getCurrentIRSize()` is the engine size predicted from JUCE's trim + resample rules (pinned by a test) **or** anything but the pass-through's size — fail-open, since the wait has no timeout — and **not** `> 1`: the pass-through is 2 samples at 96 kHz and 4 at 192 kHz. The first block after `prepareToPlay` and after `tailsStale` switches without a fade (no audible output to fade from). The built-in cab's three biquads run in **every** mode (output used only in built-in), so they are live when it takes over — don't gate them on the mode again, and don't reset them at the commit (the 400 Hz notch rings in)
- Convolution runs at base SR (after downsample), not at oversampled rate
- `getTailLengthSeconds()` reports IR tail using the IR's native sample rate

## GUI Architecture

17 custom components in `src/gui/`, all in `namespace sr`:

- **ColourPalette.h** — 28 inline colour constants (dark theme), incl. the `knobBody`/`knobFace`/`knobTick` ramp. Text tiers hold ≥4.5:1 against `panel`; `muted` (3.5:1) is decoration only, never a glyph
- **FontData.h/.cpp** — Embedded Space Mono + DM Sans fonts via BinaryData, plus the `Fonts::Sizes` type scale (micro 9 / small 10 / body 11 / value 12.5 / title 14 / hero 22). No bare numeric font sizes anywhere in `src/gui/` — tune type from that one block. Note these are JUCE font *heights* (ascent+descent), which render ~25% smaller than the same number in CSS px
- **LookAndFeel.h/.cpp** — `SilentRiotLookAndFeel`. Owns `drawRotarySlider` — the arc knob paint routine (five uniform ticks, value arc, body + face circles, indicator triangle inside the face). Every dimension is a ratio of `D = jmin(w,h)`, so one routine covers all knob sizes and `setScale` does not touch the graphic. A slider with the `kPointerKnobProperty` property (the amp row's GAIN/SHAPE/VOLUME) gets a satin pointer knob instead of the body/face/triangle: a black skirt with a chamfered bar and cream line, rotated point by point so the light stays fixed; ticks and value arc are shared. `kRotaryStartAngle`/`kRotaryEndAngle` (225°→495°) define the 270° sweep — a slider painted with this LookAndFeel **must** call `setRotaryParameters()` with them, or its ticks won't line up with its value
- **NotchKnob.h/.cpp** — Rotary knob shell: owns the slider, reserves the readout + label strip and centres a square knob in what's left, paints the value readout and parameter label. The knob face itself is painted by the LookAndFeel. **No focus indicator is drawn** — the knobs stay in the tab order and respond to arrow/PageUp keys, but focus is deliberately invisible (removed 2026-08-16 by request); don't reintroduce a ring without asking. Name is historical — the knob was a segmented arc with a notch pointer before the arc design. `setScale()` scales only the text strip (each metric floored — text stops being readable long before the knob does); `preferredHeight` (112px) is the room a scale-1.0 knob needs, and laying one out shorter clips the name. Optional `setValueFormatter()` overrides the default 0–10 readout (used for DELAY TIME's ms and the `%` knobs)
- **VUMeter.h/.cpp** — 24-segment LED bar, 60Hz timer, green/yellow/red zones
- **ToggleSwitch.h/.cpp** — toggle wrapping `juce::ToggleButton` for APVTS attachment, in two styles: `Style::Switch` (a short chrome bat toggle with a black ball tip, through a hex nut on a narrow glass ring lit softly in the switch colour, lever up = on; used by the amp row) and `Style::Led` (a flat lit bar, used by the effects cards). **Always lay one out against `ToggleSwitch::minHeight(style)`** — 44px for Switch, 21px for Led. It paints its label at the top and its switch/lamp *below*, and JUCE clips paint() to the component's own bounds, so a toggle given only enough height for its label loses the track entirely: the control renders as a dead text label while its clicks keep working and the parameter keeps changing. That silent failure is what made the effects toggles look broken (fixed 2026-08-16); `EffectsPanel` now static_asserts its card metrics against `minHeight`
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

**Editor timer** (30Hz): Polls atomic level meters (real input/output RMS + grid current), DSP readout for advanced panel, footer stats, and refreshes the live tuner. The input/output level meters now read **post-trim** — after the INPUT/OUTPUT gain-staging knobs are applied. Every path that leaves `processBlock` early publishes its own meter values (review S6): tuner mute → output 0, input still live (the tuner is using it); power off → both 0; host bypass → the dry passthrough on both. Drives animated section expand/collapse (28px/frame linear steps). SectionBar chevrons animate at 300ms, gear icon at 400ms, toggle lever at ~200ms.

**Keyboard**: Arrow keys ±0.1 display (±0.01 internal), PageUp/Down ±1.0 display (±0.1 internal) via `NotchKnob::KnobSlider` override. Tab order: GAIN→SHAPE→VOLUME→7W→CAB→PWR.

**UI state persistence**: Panel open states, theme, meter ballistics, knob mode, and UI scale stored as properties on `apvts.state` ValueTree — automatically saved/restored with plugin state.

## DSP Implementation Details

- **Triode model**: Koren with iterative load-line solve. Standard 12AX7 parameters (mu=100, ex=1.4, Kg1=1060, Kp=600, Kvb=300 — matches Guitarix/industry reference). `TriodeState` caches converged plate voltage between samples → 4 iterations sufficient (was 8). Stateless overload available for tests.
- **Pentode model**: Koren pentode equations (mu=19.5, ex=1.35, Kg1=`EL84_KG1` 210.5, Kp=84, Kvb=300) — screen grid acts as virtual anode in the Koren formula. Softplus cutoff, tanh IP clamp, plate saturation knee, screen redistribution. Kg1 was 820 until USE-59 (3.9× too little current); see the datasheet pin under DSP Audit Status.
- **LTP solver**: Asymmetric plate loads (100K/82K). Root-finds the shared cathode node — secant seeded from the previous sample's `vCathode`, bisection over `[0, vSupply]` as the fallback, tolerance 1e-5 — with 4 inner plate iterations per residual evaluation. Caches plate voltages A/B and `vCathode` between samples; `reset()` restores both to their `init()` values. See `docs/records/handoffs/handoff-ltp-convergence.md`.
- **Oversample switching**: Crossfade state machine (FadingOut → Reconfiguring → FadingIn), 128-sample fade at base SR. PSU frozen during transition to prevent voltage recovery pop. `prepareToPlay` starts the switcher in the saved OVERSAMPLE mode with no transition pending (review B5) — never hard-code 4× there, or 2×/8× sessions drop out on their first blocks.
- **Latency**: every oversampler is built by `makeIntegerLatencyOversampler()` (JUCE integer latency, a Thiran all-pass rounds it up), so the report is exact: main + IR-limiter = 10 / 12 / 12 samples at 2× / 4× / 8×. The limiter's latency is reported in both cab modes, so the IIR path is delayed to match by `iirPathAlign` (`IntegerDelay`, review B4). Change either oversampler and both the report and the delay follow automatically; don't truncate a fractional latency again.
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

## Important Pitfalls

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
- Never hand JUCE's convolution `Normalise::yes`. It scales to 0.125/√energy *after* resampling: ~11 dB under the built-in cab, and +3 dB at 96 kHz vs 48 kHz. The IR level match (`IRLevel.h`) normalises at the IR's native rate and passes `Normalise::no`
- `CabinetSim::installIRNow()` only from `prepareToPlay` (`applyIRIndexParam(IRLoad::Now)`): it calls `Convolution::prepare()`, which must not run beside `processBlock`. Every other caller (state restore, IR_INDEX changes, the loader) uses the async `loadIRFromBuffer()`
- ToggleSwitch uses `onStateChange` internally for the lever animation — external callbacks must use `onClick` (not `onStateChange`) to avoid breaking the animation timer
- Gain pot: taper **Gang A only** (`audioTaper()`, Spec §3.3); **Gang B is LINEAR**. The signal passes through both gangs, so the law the player feels is the *product*. Tapering neither leaves the LTP 4× too hot at noon (gain collapse on attack, dip/bloom); tapering *both* gives x⁴, which puts noon at −24 dB, squeezes all usable drive into dial 7–10, and drives V1b/PI grid conduction into blocking collapse ("drowning" when pushed). The user knob is additionally remapped through `gainDriveTaper()` (cubic × `GAIN_KNOB_HEADROOM`) before the gangs. Pinned by the combined-law tests in `signal_chain_tests.cpp`
- Grid conduction is a **diode clamp, not a resistive divider**. `vLoaded = threshold + knee·log1p(vAbove/knee)` — transparent below the knee, logarithmic above. The old `Rg/(Rg+Rs)` divider could only ever *attenuate*: at the PI that ratio is 0.23, so a ~520 V swing still delivered a **121 V grid** on a 12AX7 biased at −1.5 V, which left the whole power section overdriven at every knob position and flattened the amp's dynamics
- Grid conduction thresholds track **|bias|**, because a grid conducts once it approaches the *cathode*. 12AX7 at −1.5 V → 0.5 V. `GRID_CONDUCT_THRESH_EL84_N` must stay near the EL84's −9.6 V bias (8.6 V), NOT 0 — at 0 every positive excursion of amplified noise counts as conduction, ratcheting the coupling-cap charge to its rail **on silence** and driving a ~17.7 Hz motorboating oscillation at gain ≥ 9 with nothing plugged in. Detect with `tools/sr_idle.py`
- LTP solver **root-finds** the cathode node (secant + bisection fallback); it is NOT a relaxation. Do not "simplify" it back to a damped fixed-point — that version did not converge, it limit-cycled into a Nyquist-rate square wave at constant input. `alpha = 1.0` is a trap: it reports zero residual because it kills the stage. Inner plate iterations must stay at 4 (3 oscillates). See `docs/records/handoffs/handoff-ltp-convergence.md`
- Speaker coupling `SPK_GAINMOD_TAU_MS` must be ≥15ms — shorter values cause per-cycle AM modulation (audible as LFO-like filtering)
- `GRID_CHARGE_RATE` must stay low (≤0.003) and `MAX_BIAS_SHIFT_N` ≤ 1.5V — higher values cause PI grid charge saturation → LTP pumping at gain≥5. The rate is the per-sample increment **at 176.4 kHz effective** (`GRID_CHARGE_REF_SR`); `recalcGridChargeDecay` scales it with the decay so the bias shift is rate-independent (review B7) — never apply `GRID_CHARGE_RATE` directly per sample. `MAX_BIAS_SHIFT_N` is shared by every grid stage. Measured 2026-10-23 at 44.1 kHz / 4× (`docs/records/handoffs/handoff-power-section-fixes.md`, *Grid conduction corners*; re-run with `docs/records/handoffs/handoff-power-section-fixes-probe.cpp` `grid`, cross-checked against the real `processBlock`): the **EL84** charge is **0 at every corner measured** — sines, a band-limited square and a six-note chord. The grids peak at 5.91 V at the hottest corner found (gain 10, volume 10, +12 dB INPUT, BOOST LEVEL and BRIGHT max, SHAPE 10, a 1.6 kHz square at 0.5; the same corner's sine reaches 5.73 V, SHAPE 0 / 800 Hz 4.97 V), 3.3 dB under the 8.6 V threshold. That is a margin at the corners tried, not a proof for every input. It is the **PI** charge that rails: from about gain 8 at a nominal DI (a −18 dBFS-RMS sine: 40–47% of samples; 53–59% at gain 10), and 55–100% under *hot input* — those hot corners, i.e. a 0.5-peak signal (−9 dBFS RMS as a sine) raised a further +12 dB by INPUT and +18 dB by BOOST into gain 10 (110 Hz 55%, chord 61–62%, 800 Hz 80%, 1.6 kHz 100%) — where V1b's rails too (53–92%; 0 at a nominal DI). At default knobs with a nominal DI it stays off the rail (0.21–0.36 V average). "Higher values cause PI grid charge saturation" is about *where* saturation starts, not whether it happens. Two separate observations, not a comparison: in Listening Test Round 2 (rate 0.01, before the gain-law, grid-clamp and EL84 recalibrations) it set in from gain ≥ 5 and pumped; today (rate 0.003) the rail engages from about gain 8. Too much else changed in between to credit the shift to the rate. Before `VOLUME_KNOB_HEADROOM` it was the EL84 charge that sat railed across the playing range, and that rail was **load-bearing**: it absorbed an overdrive that should not have reached the grid, and raising `MAX_BIAS_SHIFT_N` to −6 V then produced blocking collapse (output dead at −58 dBFS, D1 a meaningless 39.94 dB). That is still the lesson for any rail that looks pinned — relax it and measure before calling it the bug; fix the drive, not the clamp. Whether the PI rail is load-bearing today has not been measured
- The master volume is the **power-stage drive control**, not an output level control — it sits directly in front of the EL84 grids. Above `GRID_CONDUCT_THRESH_EL84_N` (8.6 V of grid drive) the diode clamp rather than the tube sets the level, and the amp stops responding to playing dynamics. Sag and PSU droop are both driven by *average plate current*, so once the grids clamp, that current goes flat (+1.4% under a full-crank note) and every downstream dynamic starves at once. If sag/give measures low, check plate-current swing **before** touching any sag constant — a starved dynamic and a railed one read identically at the gate
- `resetDSPState()` starts the amp on its **idle operating point** (`dsp/IdleOperatingPoint.h`, review B1), not on zeroed filters and a nominal supply: each 12AX7 plate idles at ~194 V DC behind a coupling cap and the supply idles ~18 V low, so a zeroed start replayed ~0.5 s of settling as a burst on silence (up to −5 dBFS at crank; it was the whole E1 failure). The power stage is **bisected**, not substituted — the sag loop's per-step gain is > 1, so substituting oscillates. Filters are seeded with `seedDC()`, which also lets the float recurrence come to rest: at 4.2 Hz on 176 kHz its loop gain is ~7e3 and rounding alone parked the 7W cathode bypass 1.3e-4 off, a 4.4 mV step that the gain stages turned into a −49 dBFS blip. **Any new stateful DSP that sits on a DC operating point at idle must be seeded there too**, or the burst comes back
- Effects that are skipped while off (delay, reverb, IR convolution, widener) **freeze** their state, and would replay it when switched back on. `processBlock` clears each on its off→on edge (`Delay::restart()` / `Reverb::restart()` / `cabSim.reset()` + `limiterOversampler->reset()` / `clearTail()`), and `tailsStale` re-arms those edges after power-off, tuner mute, host bypass and the host's `reset()`. On the on→off edge delay and reverb keep running for one 50 ms mix ramp to 0 (`fadeOut()` / `fadedOut()`), then are skipped bit-exact; switched back on mid-fade they never stopped, so the ramp reverses with no restart. A new stateful effect must join that edge logic, and a test in `processor_effects_tests.cpp`. `reset()` does **not** touch the amp — zeroing its filters would replay the startup burst (B1)
- Host bypass is `processBlockBypassed`, not JUCE's default: it passes the input through delayed by `getLatencySamples()` (`bypassDelay`, which `processBlock` keeps primed via `IntegerDelay::write()`, so bypass engages without a gap), keeps feeding the tuner, and sets `tailsStale`. JUCE's default passes the input undelayed and asserts the latency is 0 — this plugin reports 10–12 samples
- The delay loop's small-signal gain is exactly FEEDBACK: the saturator `tanh(k·x)/tanh(k)` has slope k/tanh(k) ≈ 1.9, so the recirculated copy is scaled by `loopNorm = tanh(k)/k` (the heard repeat is not). Without it the delay self-oscillated above feedback 0.53 (review B8); the owner wants it never to
- Delay time has **two ceilings on purpose** (review S7): `DELAY_TIME_KNOB_MAX_MS` (1000) is the TIME knob's range — sessions and automation store the knob's 0..1, so moving it re-times every saved delay — and `DELAY_SYNC_MAX_MS` (2000, a 1/2 note at 60 BPM) is the tempo-sync clamp. `DELAY_LINE_MAX_MS` sizes the delay line and bounds `Delay::setParameters`. Don't merge them back into one constant: the old shared 1000 ms silently cut every slow synced division short
- Reverb SIZE is **ramped, then glided** (review S5). `Reverb::process` hands each block's SIZE to `SpringReverbCore::rampSize`, which moves it linearly across that block, so a turn reaches the tank one block late and otherwise follows the knob at **any host block size**. On top, each spring's loop length and feedback one-pole (`SPRING_SIZE_GLIDE_MS`) toward the whole-sample target, held as an offset that decays to 0 — not a one-pole on the length itself, which stalls in float ~2 samples short at 192 kHz. Don't drop the ramp for the glide alone: that only works while the glide outlasts a block, and at 2048-sample blocks every per-block step settled inside its own block, kicking the read speed (pitch) up to 21% at each boundary — a ~21 Hz warble. A settled glide reads exactly the old integer loop, so a static SIZE is bit-identical; `reset()` (so `Reverb::restart()`) lands a pending ramp and the glide. Snapping the loop per block was a block-rate zipper and clicked on a fast turn
- `GridConductionModel::lastGridDrop` is a **voltage** (vGrid − vLoaded across the source impedance), which is what the coupling-cap charge integrates. The grid *current* is `gridCurrentN(drop, model)` = drop / `sourceResistance`; reading the drop as a current made GRID I 7.8× low (review S3). Convert only in the readout — `GRID_CHARGE_RATE` / `MAX_BIAS_SHIFT_N` are calibrated on the voltage. GRID I is the block peak of both EL84 grids, and with `VOLUME_KNOB_HEADROOM` it reads 0 at every corner measured (sines, a band-limited square and a six-note chord; hottest 5.91 V vs the 8.6 V threshold, a 3.3 dB margin — see the `GRID_CHARGE_RATE` pitfall), so no processor test has a known setting that drives it non-zero
- INPUT/OUTPUT level trims are gain staging, NOT tone controls: unity at knob centre, and `processBlock` skips the multiply entirely while a trim sits at unity so the dry path stays bit-identical at defaults. Do not "simplify" them into always-on gain stages. They read through `sanitizeUnitParamOr(v, 0.5f)`, not `sanitizeUnitParam` — a 0.0 fallback would mean −12 dB in and silence out. New APVTS parameters must be **appended** to `createParameterLayout()`; inserting mid-list shifts positional host automation lanes
