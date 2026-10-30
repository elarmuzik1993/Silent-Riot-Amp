# Reactive Panel Glow Plan

**Goal:** Make the amp show that it is working. The faceplate's orange piping bead lights with
playing and the pilot lamp's bloom breathes with it, so a player sees the amp respond to attack
and to how hard the power stage is being driven — without the window growing by a single pixel.

**Architecture:** The signal is already published. `outputLevelForGUI` carries level and
`sagOffsetForGUI` carries the power stage's bias shift; the editor's 30 Hz timer already reads
both. Two glow carriers come off them: **brightness** from a fast-attack / slow-release envelope
on output level, and **hue** from sag depth, running `Colours::accent` (orange, clean) toward
`Colours::red` (deep, sagging). Both colours are already in the palette and already flip with the
theme.

The glow is painted by `AmpFaceplate::paint()`, *after* `paintPlate()` and never inside it:
`paintPlate` is shared with the four EFFECTS cards (`EffectsPanel`), so a glow added there would
light all five panels at once. The lamp keeps `paintLamp`'s existing `bloom` parameter — the
routine is shared with the `Style::Jewel` effect toggles, so nothing about its signature or
defaults moves.

The one piece of new audio-thread work is a **block peak** published alongside the existing RMS.
`outputLevelForGUI` is a block RMS sampled at 30 Hz, so a pick attack inside one host block is
averaged away before the GUI ever sees it — at 64-sample blocks there are ~25 blocks per GUI
frame and only the last one's value survives. A peak with a per-block decay (a hardware peak
meter's ballistics, block-size independent) fixes it for the price of one atomic and one O(n)
scan.

**Tech Stack:** JUCE 7 (`juce_gui_basics`, `juce_graphics`), Catch2 v3, CMake/Ninja.

**Spec:** this file. Placement chosen by the owner from a four-option visual comparison
(2026-10-29): piping + lamp, over VU rails, an under-amp strip and a window perimeter. Signal and
intensity chosen at the same time: level envelope + sag hue, subtle / hardware realism.

## Global Constraints

- **No audio change.** Nothing in this plan alters a sample, and this holds by construction, not
  by care: Tasks 2–4 are GUI only and the tone lock never builds an editor (it drives the real
  `processBlock` through `tests/processor_harness.h`), while Task 1's only audio-thread work is a
  **read-only** scan of a finished buffer — it sits where `outputLevelForGUI` is already published,
  after the last write, with nothing but atomic stores below it. A new named constant in
  `src/utils/Constants.h` is additive; unlike APVTS parameters, nothing there is order-sensitive.
  `tests/tone_lock_tests.cpp` must pass untouched — a failing tone lock means find out why, never
  rewrite the references (`docs/dev/tone-lock.md`).
- **Three places a careless implementation would change the tone anyway**, none of them required
  by this plan: reaching past `meterPeak` into DSP state inside `reset()` (B1 — that replays the
  startup burst); editing neighbouring lines while adding the decay to `prepareToPlay` (B5); and
  injecting an anti-denormal into the audio path to deal with the meter's own decay (Task 1).
  Touch only what each task names.
- No new parameter, no change to presets or saved plugin state.
- No dynamic allocation in `processBlock` (AGENTS.md); the peak scan allocates nothing.
- GUI rules (AGENTS.md): colours from `src/gui/ColourPalette.h`, no bare numeric font sizes, no
  non-ASCII literals. This feature draws no text, so `scripts/check_text.py` is unaffected.
- **No new file under `src/`**: only `src/gui/AmpFaceplate.{h,cpp}`,
  `src/PluginProcessor.{h,cpp}` and `src/PluginEditor.cpp` are touched, so neither the root
  `CMakeLists.txt` nor the two source lists in `tools/CMakeLists.txt` move
  (`scripts/verify.sh` checks 1–2). New **test** files are a different matter: both test targets
  in `tests/CMakeLists.txt` carry an explicit source list, so every new test file is added there
  in the same task that creates it or it is never compiled.
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>`; no AI attribution.
- Branch `feat/reactive-panel-glow`.

## Review Focus

1. **Sag may not travel.** Known issue B2 — sag idles warm and never goes fully cold, and
   `VOLUME_KNOB_HEADROOM` work is on hold. If `biasOffset()` barely leaves 0 across the playing
   range, hue is a dead carrier and the glow is a one-colour level meter. Task 0 measures this
   before any painting code is written, and names the fallback.
2. **The EFFECTS cards must not light.** `paintPlate` is shared. A test renders an
   `EffectsPanel` card and the faceplate at the same drive and asserts only one changed.
3. **Repaint cost, and a cache that can go stale.** The faceplate does not repaint on a timer
   today. At 30 Hz the grille weave alone is ~314 line draws per frame, so the static plate is
   cached into a `juce::Image` and only the glow and lamp are redrawn. The trap: `applySettings()`
   mutates the `Colours` globals and calls one top-level `repaint()` — **no component is told the
   theme changed**. A cache keyed only on size would keep serving the old theme's plate after a
   theme switch. It must carry a theme token and rebuild on mismatch (Task 2).
4. **Power off.** With PWR off the amp is silent and both meters read 0, so the glow must be
   fully dark and the lamp unlit — no residual bead colour.
5. **Light theme.** A bloom needs a dark ground to spill into; over the light theme's white panel
   it reads as a smudge. Light mode gets a tinted bead *fill* instead of a halo (Task 4).
6. **Every early-return path.** Review S6: `processBlock` has three exits that publish meters
   (normal end-of-block, power-off / tuner-mute, and `processBlockBypassed`). A new atomic that
   only one of them writes leaves the glow frozen at its last value.
7. **Nothing here is verified by comparing pixels.** `tests/` has no image-comparison helper, and
   every GUI test in the repo asserts on state (`tests/gui_ir_loader_tests.cpp`,
   `tests/gui_input_selector_tests.cpp`). The snapshot tools (`tools/sr_editor_snapshot.cpp`) write
   PNGs for the manual, live behind `-DSR_BUILD_TOOLS=ON`, and are not part of the `test` job
   `scripts/verify.sh` runs. So the glow's colour maths is pulled out into a **pure function** and
   tested there; the painting itself is held to structural facts (which routine draws it, whether it
   runs at all) rather than to a reference image. Building a pixel-diff harness is a bigger job than
   this feature and is not in scope.

---

### Task 0: Measure what sag actually does (no production code)

Decides whether hue has a carrier. Nothing else starts until this is answered.

**Files:**
- Create: `docs/records/handoffs/handoff-sag-travel-probe.cpp` — a standalone probe, **kept**, the
  way `docs/records/handoffs/handoff-power-section-fixes-probe.cpp` is kept so its measurement can
  be re-run later (CLAUDE.md cites exactly that for the grid-conduction corners). It is not part of
  any test target, so no `tests/CMakeLists.txt` entry.

**Steps:**
- [x] Drive `ProcessorHarness` through a grid: gain {2, 5, 8, 10} × volume {3, 6, 10}, each with a
      nominal DI (−18 dBFS sine at 110 Hz) and a hot one (0.5 peak), 15W and 7W.
- [x] For each cell record min / mean / max of `sagOffsetForGUI` over a 2 s run, and the implied
      `sagPercent` (`biasOffset / 0.01`).
- [x] Report the **travel**: how much of the range between 0 and `SAG_BIAS_CLAMP_COLD_N` (−0.01,
      i.e. −3 V) sag actually uses, and whether it ever sits positive (the warm clamp).

**Decision gate** — the question is not what fraction of the clamp sag uses, it is whether the
resulting colour moves enough to see. Map the measured span onto the accent→red hue ramp (24° → 0°)
and ask:

- The span covers **≥ ~15° of hue** between a soft phrase and a hard one at normal settings →
  **hue runs off sag** as designed. Normalise
  `heat = jlimit(0, 1, -biasOffset / -SAG_BIAS_CLAMP_COLD_N)`, then rescale to the *measured*
  span so the full ramp is reachable in play rather than only at the clamp.
- It does not → **fall back to compression**: `heat` from how far output level has fallen behind
  input level relative to their clean ratio. Both atomics already exist, it shows the same musical
  thing (the amp giving), and it needs no new DSP. Record the switch in this file.

**Verification:** numbers written into this task, and the carrier named, before Task 2 begins.

#### Result (2026-10-30)

Probe: `docs/records/handoffs/handoff-sag-travel-probe.cpp` (real `processBlock` via
`ProcessorHarness`, 44.1 kHz, 512-sample blocks, 4x, built-in cab, effects off; atomics read after
every host block; statistics skip the first 0.5 s of each 2 s cell for parameter smoothing and
onset). Re-run instructions are in its header. `sagPercent` below is `biasOffset / -0.01 * 100`,
so **negative means warm** (positive offset).

**Sag never leaves the warm side.** Idle sits at **+0.00258** (15W, +0.77 V, -25.8 %) and
**+0.00342** (7W, +1.03 V, -34.2 %), independent of the knobs. Under play it only relaxes back
toward 0; nothing in the grid, the sweep or the plucks ever reaches 0, let alone the cold clamp.

| Grid cell (sag mean, 2 s sine) | 15W DI | 15W hot | 7W DI | 7W hot |
|---|---|---|---|---|
| gain 2, vol 3 | +0.002578 | +0.002571 | +0.003420 | +0.003416 |
| gain 5, vol 6 | +0.002476 | +0.002480 | +0.003335 | +0.003327 |
| gain 8, vol 6 | +0.002424 | +0.002259 | +0.003297 | +0.003151 |
| gain 8, vol 10 | +0.002129 | +0.001641 | +0.003057 | +0.002673 |
| gain 10, vol 10 | +0.001246 | +0.001359 | +0.002384 | +0.002508 |
| **min seen in any cell** | **+0.001141** | | **+0.002280** | |

| Soft vs hard (15W, 110 Hz sine) | -30 dBFS RMS | -6 dBFS RMS | sag travel | hue, rescaled to cranked span |
|---|---|---|---|---|
| gain 6, vol 5 (defaults) | -25.5 % | -24.8 % | 0.7 pt (2 mV) | ~1 deg |
| gain 5, vol 6 | -25.6 % | -25.0 % | 0.6 pt | ~1 deg |
| gain 8, vol 6 | -24.2 % | -22.2 % | 2.0 pt | ~3 deg |
| gain 10, vol 10 | -20.3 % | -11.5 % | 8.8 pt | ~13 deg |
| Pluck (tau 0.35 s), deepest point, -24 vs -6 dBFS peak: defaults | -25.4 % | -24.4 % | 1.0 pt | ~1.5 deg |
| same, gain 10 / vol 10 | -18.8 % | -9.5 % | 9.3 pt | ~14 deg |

- **Travel:** the largest excursion measured is **0.0016** (idle +0.00258 to +0.00099 on a hard
  pluck at gain 10 / vol 10), **16 %** of the 0..cold-clamp distance, and all of it on the warm
  side. The warm clamp (+0.005) is never reached either.
- **Hue span:** under the plan's linear map (`-biasOffset / 0.01`, clamped at 0) every reading is
  0, so **0 deg** everywhere. Rescaled so the cranked span fills the 24 deg ramp, soft vs hard at
  normal settings moves **1-3 deg**, and the sweep is not even monotonic (at defaults -18 dBFS
  sags deeper than -6). The 15W/7W idle offset (8.4 pt) is larger than any normal-setting playing
  difference, so a rescaled hue would mostly show the 7W switch.

**Carrier: compression** (the gate's fallback; the switch is recorded here as the gate requires).
Sag is a dead carrier at normal settings: 2 mV of travel between a soft and a hard phrase, below
the noise of the sweep's own non-monotonicity, and only usable at full crank. This is B2 (sag idles
warm) seen from the GUI side; `VOLUME_KNOB_HEADROOM` keeps the EL84 grids out of conduction, so the
average plate current, and with it sag, barely moves. Compression, by contrast, separates soft and
hard playing by 11-21 dB at every setting measured, from the same two atomics the editor already
reads (`inputLevelForGUI`, `outputLevelForGUI`; both block RMS of channel 0, post-trim; note the
input meter sits **before** the gate/BOOST, so BOOST reads as extra gain, and delay/reverb tails add
output). Hue therefore runs off `heat` from compression, not from `sagOffsetForGUI`.

Compression in dB, `20*log10(cleanRatio / ratio)`, `ratio = outRms / inRms`, clean = the -36 dBFS
reading:

| out/in RMS, 15W | -36 | -30 | -24 | -18 | -12 | -6 dBFS RMS |
|---|---|---|---|---|---|---|
| gain 6, vol 5 | 0.569 | 0.575 | 0.583 | 0.515 | 0.159 | 0.144 |
| compression dB | 0.0 | -0.1 | -0.2 | 0.9 | 11.1 | 11.9 |
| gain 5, vol 6 | 0.455 | 0.460 | 0.467 | 0.477 | 0.388 | 0.127 |
| compression dB | 0.0 | -0.1 | -0.2 | -0.4 | 1.4 | 11.1 |
| gain 8, vol 6 | 3.551 | 3.068 | 0.970 | 0.902 | 0.737 | 0.461 |
| compression dB | 0.0 | 1.3 | 11.3 | 11.9 | 13.7 | 17.7 |
| gain 10, vol 10 | 14.39 | 12.41 | 8.70 | 5.22 | 2.53 | 1.24 |
| compression dB | 0.0 | 1.3 | 4.4 | 8.8 | 15.1 | 21.3 |

**Clean ratio** (out/in at -48 dBFS RMS, 15W; 7W is 5-8 % lower throughout). It spans five
decades and is ~0 at gain 0 or volume 0, so it cannot be a constant:

| gain \ vol | 2 | 4 | 6 | 8 | 10 |
|---|---|---|---|---|---|
| 2 | 0.0075 | 0.0299 | 0.0673 | 0.1197 | 0.1871 |
| 4 | 0.0292 | 0.1169 | 0.2632 | 0.4688 | 0.7337 |
| 6 | 0.0886 | 0.3549 | 0.8010 | 1.4291 | 2.2433 |
| 8 | 0.3709 | 1.4926 | 3.3866 | 6.0953 | 9.6805 |
| 10 | 1.8229 | 7.5650 | 17.75 | 32.93 | 54.07 |

The full grid (min/mean/max sag and in/out RMS for all 48 cells, 7W included) is what the probe
prints; the cells above are a subset.

**Normalisation for Task 2** (the pure function's contract; constants to name there):
`ref` is a running clean ratio, the peak-hold of `ratio` over frames with `inRms >= dB(-50)`,
released at 0.5 dB/s and re-learnt (reset) whenever GAIN, VOLUME, 7W, OUTPUT trim, CAB mode or an
effect's ON changes; it may be seeded from the table above (bilinear in dB) so a hard first phrase
does not read clean. Then `compDb = 20*log10(ref / ratio)` and
`heat = jlimit(0, 1, (compDb - 1 dB) / 11 dB)`: the 1 dB knee absorbs the +-1 dB wobble of clean
play, and full heat at 12 dB is where a hard phrase lands at default knobs. Below the input gate,
heat targets 0. Compression is lumpy (at defaults it jumps 10 dB between -18 and -12 dBFS), so
`heat` goes through the glow's own slow envelope before it reaches the hue.

Side observation, not in scope: at defaults the output RMS *falls* as the input rises from -18 to
-12 dBFS (0.065 to 0.040), and at gain 8 / vol 6 the ratio drops 10 dB between -30 and -24 dBFS.
Both look like the PI grid-charge rail (CLAUDE.md, `GRID_CHARGE_RATE`) engaging; worth a look when
the tone lock is next opened, not here.

---

### Task 1: Publish an output block peak

**Files:**
- Modify: `src/PluginProcessor.h` (one atomic + one audio-thread member)
- Modify: `src/PluginProcessor.cpp` (three publish sites)
- Test: `tests/processor_meter_tests.cpp`

**Interfaces:**
- Produces: `std::atomic<float> outputPeakForGUI { 0.0f }` — peak of channel 0, post-trim, with a
  release decay applied per block.
- Private audio-thread state: `float meterPeak = 0.0f`, plus the per-block decay derived in
  `prepareToPlay` from `METER_PEAK_RELEASE_MS` and the host block duration, so the ballistics do
  not change with block size.

**Steps:**
- [x] Add `METER_PEAK_RELEASE_MS` (250) to `src/utils/Constants.h`, following the established
      `_MS` time suffix (`SAG_ATTACK_MS`, `TAU_POWER_MS`).
- [x] In `processBlock`, beside the existing `outputLevelForGUI.store(channelRms(...))`, scan
      channel 0 for `std::abs` max (guarding non-finite samples, as the soft-clip loop does), fold
      it in as `meterPeak = max(blockPeak, meterPeak * decay)` and store. The existing conditional
      soft-clip loop runs only when an effect is live, so this needs its own pass.
- [x] Publish on the other two exits: `0.0f` under power-off and under tuner mute (the output is
      silent); the delayed dry passthrough's own peak in `processBlockBypassed`.
- [x] Clear `meterPeak` in `prepareToPlay` and in `reset()`. This is metering state, not DSP
      state — the rule that `reset()` must not touch the amp (B1, the startup burst) still holds.
- [x] Flush `meterPeak` to exactly 0 below a small threshold. Decaying toward zero across silence
      drives it denormal, and denormals are slow on some CPUs. Flush the **meter's own value**;
      never reach for `ANTI_DENORMAL` here, which belongs in IIR filter state inside the audio
      path and would be a tone change.

**Verification:**
- [x] A single-sample spike in an otherwise silent block raises `outputPeakForGUI` where
      `outputLevelForGUI` (RMS) stays near 0.
- [x] The decay is block-size independent: the same signal at 64 and 2048 samples per block
      reaches the same peak and releases at the same rate within tolerance.
- [x] Power off → 0. Tuner mute → 0. Host bypass → the passthrough's peak, not 0 and not stale.
- [x] `tests/tone_lock_tests.cpp` passes unchanged.

---

### Task 2: Paint the glow on the faceplate

**Files:**
- Modify: `src/gui/AmpFaceplate.h`, `src/gui/AmpFaceplate.cpp`
- Test: `tests/gui_faceplate_glow_tests.cpp` (new, in `SilentRiotProcessorTests`)

**Interfaces:**
- Produces: `juce::Colour sr::glowBeadColour(float level, float heat)` — **pure**, declared in
  `AmpFaceplate.h`, taking no `Graphics` and no component state. It carries the whole colour
  decision (the accent→red ramp, alpha from level, and Task 4's light-theme branch), so it is
  unit-testable without a GUI.
- Produces: `void AmpFaceplate::setDrive(float level, float heat)` — stores the two values and
  repaints only when either moves past a small epsilon, so a silent amp is idle.
- `paintPlate` and `paintLamp` keep their current signatures and behaviour.

**Steps:**
- [ ] Cache the static plate (gradient, weave, piping, screws) into a `juce::Image` blitted at the
      top of `paint()`. Without this the weave's ~314 line draws run 30 times a second. Key the
      cache on **both** the component size and a theme token (`Colours::panel.getARGB()` is enough —
      every theme redefines it) and rebuild when either differs: there is no theme-change callback,
      only a blanket `repaint()`, so a size-only key serves a stale plate after a theme switch.
- [ ] After the blit, draw the bead glow: three `drawRoundedRectangle` strokes on the piping's
      path at widths 1 / 2.5 / 5 px and alphas `a` / `0.4a` / `0.15a`, all **inside** the
      component bounds (JUCE clips `paint()`, and there is no cheap blur primitive). `a` scales
      with `level`; keep the ceiling low enough that the bead never outshines the value readouts.
- [ ] Lamp: keep `brightness` at `powered ? 1.0f : 0.0f` so "lit = powered" stays unambiguous, and
      modulate only `bloom` with `level`. The lamp's own colour stays `Colours::red`.
- [ ] With `powered == false`, skip the glow entirely.
- [ ] Update the class comment in `AmpFaceplate.h`: it opens "Pure decoration", which stops being
      true once the panel carries live signal state.

**Verification** (`tests/glow_colour_tests.cpp`, added to `SilentRiotTests` in
`tests/CMakeLists.txt` — pure C++, no GUI):
- [ ] `glowBeadColour(0, h)` is fully transparent for every `h`: a silent amp adds nothing.
- [ ] Rising `level` raises alpha monotonically and never past the agreed ceiling.
- [ ] `glowBeadColour(1, 0)` and `glowBeadColour(1, 1)` differ **in hue**, not only in alpha, and
      their hues match `Colours::accent` and `Colours::red` at the ends of the ramp.
- [ ] `heat` outside 0..1 and non-finite inputs clamp rather than producing a garbage colour.
- [ ] Structural, by inspection and held by Review Focus 2: `paintPlate` gains no glow argument and
      no new call site, so `EffectsPanel` cannot light.
- [ ] Manual check with `tools/sr_editor_snapshot` in both themes, attached to the PR. This is a
      look, not an assertion — no reference PNG is committed.

---

### Task 3: Wire the editor timer

**Files:**
- Modify: `src/PluginEditor.cpp` (timer callback; `faceplate.setPowered` already lives there)
- Test: `tests/gui_faceplate_glow_tests.cpp`

**Steps:**
- [ ] In the 30 Hz timer, beside the existing `vuIn` / `vuOut` updates, read `outputPeakForGUI`
      and the sag carrier chosen in Task 0.
- [ ] Ease for display: jump straight to a rising peak (the audio-side decay already provides
      release), and one-pole the hue so it drifts rather than flickers. Frame-rate independent,
      since the timer is nominal 30 Hz and hosts do stall it.
- [ ] Power off → `setDrive(0, 0)`, matching the branch that already zeroes the VU meters.

**Verification:**
- [ ] Driving a `ProcessorHarness` with a loud block and ticking the editor timer raises the
      faceplate's drive; silence lets it fall back.
- [ ] Power off zeroes it on the first tick.

---

### Task 4: Light theme treatment

**Files:**
- Modify: `src/gui/AmpFaceplate.cpp`

**Steps:**
- [ ] In the light palette, replace the additive halo with a tinted bead: raise the piping stroke's
      own saturation and alpha with `level` instead of stacking outer strokes, so the signal reads
      as the bead *colouring up* rather than as a grey smudge on white.
- [ ] Branch inside `glowBeadColour()` on the palette's own luminance, not on a stored theme
      index, so a future theme inherits the right treatment automatically — and so the branch is
      covered by the Task 2 unit tests rather than needing a render.

**Verification** (extends `tests/glow_colour_tests.cpp`):
- [ ] Switching the palette with `Colours::setTheme()` changes which branch `glowBeadColour()`
      takes, and both branches return a visible colour at full drive.
- [ ] Computed contrast of `Colours::text` against the lit bead colour still clears 4.5:1 in both
      themes at full drive, so the readouts stay legible (this is arithmetic on the palette, not a
      render).
- [ ] Manual look in both themes via `tools/sr_editor_snapshot`, attached to the PR.

---

### Task 5: Verify, record, hand off

**Files:**
- Modify: `.agents/STATE.md`, `docs/README.md` (plans index), `CHANGELOG.md` (Unreleased → Added)
- Keep: `docs/records/handoffs/handoff-sag-travel-probe.cpp` and its measurement write-up — the
  repo keeps probes so their numbers can be re-run, it is not test-target code, and Task 0's
  result is the evidence for whichever hue carrier shipped.

**Steps:**
- [ ] `bash scripts/verify.sh` green, tone lock included.
- [ ] Move this plan's row in the `docs/README.md` index from `planned` to `done`.
- [ ] CHANGELOG entry under Unreleased → Added, naming it as a visual-only change.
- [ ] Rewrite `.agents/STATE.md` as the repo will be once the branch merges.

**Verification:** `bash scripts/verify.sh` passes; `git diff` reviewed before commit.

---

## Deliberately not in scope

- **No settings toggle.** The owner chose subtle hardware realism over a user-switchable effect.
  If the glow turns out to divide opinion, `SettingsPanel` already has the pattern (`peakHold` is
  a `ToggleSwitch` + a persisted `apvts.state` property) and it is a small follow-up.
- **No VU meter change.** The rails option was considered and not chosen; `VUMeter` keeps its RMS
  feed and its own 60 Hz timer.
- **No new strip or perimeter component.** Both were rejected for costing window height or for
  reading as case lighting rather than studio hardware.
