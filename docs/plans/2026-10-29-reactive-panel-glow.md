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

- **No audio change.** Nothing in this plan alters a sample. `tests/tone_lock_tests.cpp` must pass
  untouched — a failing tone lock means find out why, never rewrite the references
  (`docs/dev/tone-lock.md`).
- No new parameter, no change to presets or saved plugin state.
- No dynamic allocation in `processBlock` (AGENTS.md); the peak scan allocates nothing.
- GUI rules (AGENTS.md): colours from `src/gui/ColourPalette.h`, no bare numeric font sizes, no
  non-ASCII literals. This feature draws no text, so `scripts/check_text.py` is unaffected.
- No new `.cpp`: only `src/gui/AmpFaceplate.{h,cpp}`, `src/PluginProcessor.{h,cpp}` and
  `src/PluginEditor.cpp` are touched, so neither `CMakeLists.txt` nor the two source lists in
  `tools/CMakeLists.txt` move (`scripts/verify.sh` checks 1–2).
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>`; no AI attribution.
- Branch `feat/reactive-panel-glow`.

## Review Focus

1. **Sag may not travel.** Known issue B2 — sag idles warm and never goes fully cold, and
   `VOLUME_KNOB_HEADROOM` work is on hold. If `biasOffset()` barely leaves 0 across the playing
   range, hue is a dead carrier and the glow is a one-colour level meter. Task 0 measures this
   before any painting code is written, and names the fallback.
2. **The EFFECTS cards must not light.** `paintPlate` is shared. A test renders an
   `EffectsPanel` card and the faceplate at the same drive and asserts only one changed.
3. **Repaint cost.** The faceplate does not repaint on a timer today. At 30 Hz the grille weave
   alone is ~314 line draws per frame. The static plate is cached into a `juce::Image`; only the
   glow and lamp are redrawn per frame.
4. **Power off.** With PWR off the amp is silent and both meters read 0, so the glow must be
   fully dark and the lamp unlit — no residual bead colour.
5. **Light theme.** A bloom needs a dark ground to spill into; over the light theme's white panel
   it reads as a smudge. Light mode gets a tinted bead *fill* instead of a halo (Task 4).
6. **Every early-return path.** Review S6: `processBlock` has three exits that publish meters
   (normal end-of-block, power-off / tuner-mute, and `processBlockBypassed`). A new atomic that
   only one of them writes leaves the glow frozen at its last value.

---

### Task 0: Measure what sag actually does (no production code)

Decides whether hue has a carrier. Nothing else starts until this is answered.

**Files:**
- Create: `tests/sag_travel_probe.cpp` (temporary, `SilentRiotProcessorTests`) — or extend
  `tools/` if a render-based sweep is easier; either way it is removed before the branch merges.

**Steps:**
- [ ] Drive `ProcessorHarness` through a grid: gain {2, 5, 8, 10} × volume {3, 6, 10}, each with a
      nominal DI (−18 dBFS sine at 110 Hz) and a hot one (0.5 peak), 15W and 7W.
- [ ] For each cell record min / mean / max of `sagOffsetForGUI` over a 2 s run, and the implied
      `sagPercent` (`biasOffset / 0.01`).
- [ ] Report the **travel**: how much of the range between 0 and `SAG_BIAS_CLAMP_COLD_N` (−0.01,
      i.e. −3 V) sag actually uses, and whether it ever sits positive (the warm clamp).

**Decision gate:**
- Travel ≥ ~25% of the cold range across the grid → **hue runs off sag** as designed. Normalise
  `heat = jlimit(0, 1, -biasOffset / -SAG_BIAS_CLAMP_COLD_N)` and set the usable span from the
  measured numbers rather than the clamp.
- Travel is negligible → **fall back to compression**: `heat` from how far output level has fallen
  behind input level relative to their clean ratio. Both atomics already exist, it shows the same
  musical thing (the amp giving), and it needs no new DSP. Record the switch in this file.

**Verification:** numbers written into this task, and the carrier named, before Task 2 begins.

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
- [ ] Add `METER_PEAK_RELEASE_MS` (250) to `src/utils/Constants.h` — dimensionless-time, so no
      `_N` / `_REAL` suffix.
- [ ] In `processBlock`, beside the existing `outputLevelForGUI.store(channelRms(...))`, scan
      channel 0 for `std::abs` max (guarding non-finite samples, as the soft-clip loop does), fold
      it in as `meterPeak = max(blockPeak, meterPeak * decay)` and store. The existing conditional
      soft-clip loop runs only when an effect is live, so this needs its own pass.
- [ ] Publish on the other two exits: `0.0f` under power-off and under tuner mute (the output is
      silent); the delayed dry passthrough's own peak in `processBlockBypassed`.
- [ ] Reset `meterPeak` in `prepareToPlay` and in `reset()`.

**Verification:**
- [ ] A single-sample spike in an otherwise silent block raises `outputPeakForGUI` where
      `outputLevelForGUI` (RMS) stays near 0.
- [ ] The decay is block-size independent: the same signal at 64 and 2048 samples per block
      reaches the same peak and releases at the same rate within tolerance.
- [ ] Power off → 0. Tuner mute → 0. Host bypass → the passthrough's peak, not 0 and not stale.
- [ ] `tests/tone_lock_tests.cpp` passes unchanged.

---

### Task 2: Paint the glow on the faceplate

**Files:**
- Modify: `src/gui/AmpFaceplate.h`, `src/gui/AmpFaceplate.cpp`
- Test: `tests/gui_faceplate_glow_tests.cpp` (new, in `SilentRiotProcessorTests`)

**Interfaces:**
- Produces: `void AmpFaceplate::setDrive(float level, float heat)` — `level` 0..1 sets brightness,
  `heat` 0..1 runs the bead colour from `Colours::accent` to `Colours::red`. Repaints only when
  either moves past a small epsilon, so a silent amp is idle.
- `paintPlate` and `paintLamp` keep their current signatures and behaviour.

**Steps:**
- [ ] Cache the static plate (gradient, weave, piping, screws) into a `juce::Image`, rebuilt on
      resize and on theme change, blitted at the top of `paint()`. Without this the weave's ~314
      line draws run 30 times a second.
- [ ] After the blit, draw the bead glow: three `drawRoundedRectangle` strokes on the piping's
      path at widths 1 / 2.5 / 5 px and alphas `a` / `0.4a` / `0.15a`, all **inside** the
      component bounds (JUCE clips `paint()`, and there is no cheap blur primitive). `a` scales
      with `level`; keep the ceiling low enough that the bead never outshines the value readouts.
- [ ] Lamp: keep `brightness` at `powered ? 1.0f : 0.0f` so "lit = powered" stays unambiguous, and
      modulate only `bloom` with `level`. The lamp's own colour stays `Colours::red`.
- [ ] With `powered == false`, skip the glow entirely.

**Verification:**
- [ ] `setDrive(0, 0)` on a powered faceplate renders identically to today's faceplate (pixel
      compare against a reference render).
- [ ] `setDrive(1, 0)` and `setDrive(1, 1)` differ, and differ in hue, not only in alpha.
- [ ] An `EffectsPanel` card rendered at the same moment is pixel-identical to its undriven self —
      proof the shared `paintPlate` was not touched (Review Focus 2).
- [ ] Unpowered + `setDrive(1, 1)` is identical to unpowered + `setDrive(0, 0)`.

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
- [ ] Pick the split off the palette rather than a stored theme index, so a future theme inherits
      the right branch automatically.

**Verification:**
- [ ] Rendered in both themes at `setDrive(1, 1)`, the bead is clearly lit in each and neither
      shows a halo over a pale panel.
- [ ] Contrast of the GAIN / SHAPE / VOLUME readouts against the panel is unchanged in both themes
      at full drive (the palette's text tiers must still clear 4.5:1).

---

### Task 5: Verify, record, hand off

**Files:**
- Modify: `.agents/STATE.md`, `docs/README.md` (plans index), `CHANGELOG.md` (Unreleased → Added)
- Delete: `tests/sag_travel_probe.cpp` (Task 0's temporary probe)

**Steps:**
- [ ] `bash scripts/verify.sh` green, tone lock included.
- [ ] Add this plan to the `docs/README.md` index table with its status.
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
