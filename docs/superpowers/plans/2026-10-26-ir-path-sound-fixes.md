# IR-Path Sound Fixes Plan

> Steps use checkbox (`- [ ]`) syntax for tracking. Nothing is done until
> `bash scripts/verify.sh` passes. Tone-affecting tasks also need a sign-off run.

**Goal:** Make a loaded cabinet IR sound like the amp that was calibrated. Today IR mode plays
about 11 dB quieter than the built-in cab, its level moves with the host sample rate, and
switching into it can briefly play the amp with no cab at all.

**Review findings (2026-10-26 cabinet/IR review).** Each was measured with `sr_render`, a
throwaway `CabinetSim` probe or JUCE 7.0.12's source, not estimated:

| # | Finding | Evidence | Task |
|---|---|---|---|
| 1 | IR mode is ~11 dB quieter than the built-in cab | `Normalise::yes` scales to `0.125/√energy`, 18 dB below unit energy. Neutral fixture IR: −53.5 vs −42.6 dBFS at gain 0.3, −14.8 vs −4.9 at full crank | 2 |
| 2 | IR loudness depends on the host rate | JUCE normalises *after* resampling to the session rate. +3.1 dB at 96 kHz vs 48 kHz; the built-in cab moves 0.01 dB | 2 |
| 3 | The first IR after a `prepare` plays un-cabbed | JUCE's convolution starts as a pass-through and builds the engine on a thread that polls every 10 ms. Probe: first block passed a 10 kHz tone unchanged (should be −41.5 dB), then a ~58 ms crossfade | 1, 3 |
| 4 | Mode switches are hard cuts, and IR mode resumes with a stale tail | Built-in cab ↔ IR and CAB on/off swap at a block edge. First block of silence after re-entering IR mode was −47.7 dBFS, not 0 | 3 |
| 5 | Offline IR renders aren't repeatable | `sr_render.cpp:428` calls the load "synchronous". With `--warmup-ms 0` under CPU load, 6 renders differed from the unloaded one between 11.7 and 74 ms | 1 |
| 6 | Long IRs are cut off with no fade | Hard cut at 8192 samples (170 ms at 48 kHz, 43 ms at 192 kHz) | 4 |

**Order:** Task 1 first. It makes every later IR measurement repeatable. Task 2 is the big sound
change. Task 3 needs Task 1's install path. Task 4 is small and independent. **One task per
commit**, sign-off after each.

**Not in this plan:** the IR loader UI bugs (list won't scroll, wrong file types, no remove, no
way back to the built-in cab, panel doesn't refresh), the built-in cab's 400 Hz notch (a tone
decision on the IIR path, not the IR loader), positional `IR_INDEX` and the state-copy issue.
They don't change what an IR sounds like. They need their own plan.

## Owner decisions (decided 2026-10-26)

- **D1, level target (Task 2): match the built-in cab's level over 100 Hz–5 kHz**, so
  `OUTPUT_MAKEUP_GAIN`, the headroom calibration and A1/D1 mean the same thing in both modes.
  Accepted consequence: existing projects that use an IR play ~10 dB louder after the update.
  Rejected: playing each IR at its recorded level plus a user IR-level trim. That needs a new
  parameter and UI, and file levels are arbitrary (some are peak-normalised, some aren't).
- **D2, IR length cap (Task 4): keep 8192 samples and only add a fade.** Rejected: a time-based
  cap (e.g. 250 ms, so up to 48000 samples at 192 kHz). It makes saved projects up to ~6× larger,
  because every user IR is stored inside the project. Older builds would also reject the longer
  IRs, and the loader stops at the first IR it rejects, so every IR after it is dropped too.
  Revisit with real IRs in roadmap Phase B.

## Global constraints

- No allocation or blocking in `processBlock`. Any scratch buffer is allocated in `prepare`.
- `bufferTransfer.set()` and JUCE IR loads stay off the audio thread, except where a step says
  otherwise and explains why it is safe.
- **The built-in cab path must stay bit-identical** in Tasks 1, 2 and 4, and in steady state in
  Task 3. Check it by diffing built-in-cab `sr_render` output before and after each task. Any
  difference is a bug in the task.
- Saved state is unchanged: `userIRs` and the project keep the **raw** IR audio. Normalisation
  is applied when an IR is handed to the convolution, so it can be retuned later without
  touching saved projects. (Task 4's fade is the one stated exception.)
- No new APVTS parameters.
- A new `.cpp` must be listed in `CMakeLists.txt` **and** `tools/CMakeLists.txt` (`verify.sh`
  checks both), plus `tests/CMakeLists.txt` if a test needs it.
- Commits carry no AI attribution (see `AGENTS.md`).

---

## Task 0: Baseline and fixtures

- [ ] `bash scripts/verify.sh` passes (308/308).
- [ ] Add `tools/make_ir_fixtures.py`, which writes three deterministic 48 kHz, 8192-sample mono
      IRs with numpy/scipy. Commit the WAVs under `tests/fixtures/ir/` (~32 KB each):
  - `neutral.wav`: 4th-order Butterworth band-pass, 80 Hz–5 kHz, peak-normalised. This is the
    IR all the numbers above came from.
  - `dark.wav`: 80 Hz–3 kHz.
  - `bright.wav`: 80 Hz–8 kHz plus a +4 dB peak at 3 kHz. This is also the synthetic bright IR
    that E3 step 1 in `.agents/STATE.md` asks for. Repointing the E3 gate stays with the E3 plan.
- [ ] Add `tools/sr_irpath.py`, in the style of `sr_thump.py`. It renders `note:220:0.1:2.0` with
      the built-in cab and with each fixture, at P-CLEAN/P-EDGE/P-CRANK and at 44.1/48/96 kHz,
      then prints RMS from 0.5 s on. Add a `--repeat N` mode that renders IR mode with
      `--warmup-ms 0` N times under a CPU load and reports the first and last differing sample.
- [ ] Build `sr_render`. Record `signoff.py`, `sr_irpath.py` and the CPU-load repeat test in
      `docs/handoff-ir-path-sound.md` (new). Expect the numbers in the findings table.
- [ ] Save built-in-cab renders of every sign-off point (`.f32`). Later tasks diff against them.

## Task 1: Install the active IR synchronously in `prepareToPlay`

**Problem:** `prepareToPlay` hands the IR to `bufferTransfer`, so the first block after
`prepare` runs JUCE's pass-through engine. The real engine then arrives at a wall-clock time
(finding 3 at session load, and finding 5).

**Mechanism:** JUCE's `Convolution::prepare()` first runs any queued loads on the calling thread
(`messageQueue->pimpl->popAll()`), then builds and installs the engine with no crossfade. So
"load, then `prepare()`" installs an IR immediately, and that's safe whenever `processBlock`
isn't running.

**Files:** `src/dsp/cabinet/CabinetSim.{h,cpp}`, `src/PluginProcessor.cpp`, `tools/sr_render.cpp`,
`tests/cabinet_ir_tests.cpp`

- [ ] Write a failing test: `prepare(44100, 512, 2)`, install a 64-tap moving-average IR, then
      process one block of a 10 kHz tone with no sleeps. Assert the output is ≥ 30 dB below the
      input. Today it is 0 dB.
- [ ] Add `BufferTransfer::clear()` and `CabinetSim::installIRNow(AudioBuffer&&, double rate)`.
      Document that it must not run concurrently with `processBlock`, so `prepareToPlay` is the
      only plugin caller. Steps: clear any pending transfer (otherwise the audio thread reloads
      the same IR and starts a second, timing-dependent crossfade), call
      `convolution.loadImpulseResponse(...)`, then `convolution.prepare(spec)` with the spec
      stored by `prepare()`, then set `useIR` and the length fields.
- [ ] `prepareToPlay`: call `installIRNow` instead of `loadIRFromBuffer`.
- [ ] `sr_render`: load `--ir` **before** `prepareToPlay`, so `prepareToPlay` installs it
      synchronously. Fix the "synchronous" comment.
- [ ] Verify: the test passes. `sr_irpath.py --repeat 6` under load gives bit-identical renders.
      Built-in-cab renders are bit-identical. The only IR-mode change is the first ~70 ms.

## Task 2: Level-match the IR path (do our own normalisation)

**Problem:** findings 1 and 2. Both come from `Normalise::yes`. With `Normalise::no`, JUCE
instead scales by `originalRate / sessionRate` after resampling, which keeps an IR's frequency
response the same at every host rate. So we normalise once, at the IR's native rate, and pass
`Normalise::no`.

**Metric:** the power-average of |H(f)|² over 69 log-spaced points (1/12 octave) from 100 Hz to
5 kHz. Log spacing means equal weight per octave. The reference is the same metric for the
built-in cab's three biquads (`cab_presence × cab_rolloff × cab_notch`), which comes to
**−0.90 dB**. The OT low-pass is common to both paths, so it is left out. Check of the metric:
it predicts the neutral fixture at −11.4 dB under JUCE's normalisation, a −10.5 dB gap, and
`sr_render` measured −10.9 dB.

**Files:** new `src/dsp/cabinet/IRLevel.h` (pure C++, header-only), `src/utils/Constants.h`
(`IR_LEVEL_BAND_LO_HZ`, `IR_LEVEL_BAND_HI_HZ`, `IR_LEVEL_POINTS_PER_OCT`),
`src/dsp/cabinet/CabinetSim.cpp`, new `tests/ir_level_tests.cpp` (`SilentRiotTests`),
`tests/cabinet_ir_tests.cpp`, `tests/CMakeLists.txt`

- [x] Decision D1 confirmed by the owner (2026-10-26): match the built-in cab.
- [ ] Write pure tests first (`ir_level_tests.cpp`):
  - [ ] A unit impulse reads 0 dB ± 0.01, and impulse × 2 reads +6.02 dB.
  - [ ] The same analog band-pass, designed and sampled at 48 kHz and at 96 kHz, reads within
        0.2 dB at both rates.
  - [ ] The built-in-cab reference reads −0.90 ± 0.05 dB. This pins it, so a change to any
        `CAB_*` constant shows up here, and with it every IR's level.
  - [ ] An all-zero IR reads as "no level", and the caller leaves it unscaled.
  - [ ] The band's top edge is clamped to 0.45 × rate, so an 8 kHz-rate IR still works.
- [ ] Write JUCE tests (`cabinet_ir_tests.cpp`, using Task 1's `installIRNow`, so no waiting):
      push the neutral fixture through a real `juce::dsp::Convolution` prepared at 44.1 kHz and
      at 96 kHz. Feed it an impulse and measure the resulting IR with the same metric. It must
      be within 0.3 dB of the reference at both rates. Today it is −10.5 dB, and 3 dB apart.
- [ ] Implement: one private `CabinetSim` helper that every load path calls (`loadIRFromBuffer`,
      `installIRNow`, and the dead `loadStockIR`/`loadIRFromFile` for consistency). It measures
      the channel-averaged band power at the IR's native rate, scales **all channels by one
      factor** (keeps the stereo balance), then passes `Normalise::no`. Runs on the message
      thread: about 1M multiply-adds per IR at 8192 samples, a few ms.
- [ ] Measure with `sr_irpath.py`. Target: IR vs built-in cab within ±1.5 dB at all three points
      for all three fixtures, and IR mode within ±0.3 dB across 44.1/48/96 kHz. Record the before
      and after table in the handoff doc.
- [ ] Re-check the IR-path limiter. The output tanh now sees ~11 dB more drive in IR mode, and
      the 4× limiter oversampler (`docs/handoff-IR-limiter-aliasing.md`) was only validated at the
      old, too-quiet level. Add report-only IR variants of A2 (peak ceiling), B3, D1 and D5 to
      `tools/signoff_gates.json` using `neutral.wav`, plus a spur check at P-CRANK with
      `bright.wav`. `signoff.py` must resolve `ir` paths relative to the repo root.
- [ ] Verify, then run the full sign-off. The built-in-cab gates must be bit-identical.
- [ ] **Behaviour change:** existing projects that use an IR will play ~10 dB louder, now
      matching the built-in cab. Put it in the release notes and `.agents/STATE.md`.

## Task 3: Fade every cab-mode switch and never play un-cabbed

**Problem:** finding 4, plus finding 3 at runtime. The first time you pick an IR in a session,
the convolution is still a pass-through, and today `useIR` flips IR mode on immediately.

**Design:** the three effective modes are Off, built-in, and IR. The requested mode is
`!cabOn ? Off : (useIR ? IR : built-in)`. A small state machine, modelled on
`OversampleSwitcher`, owns the **active** mode:

- `Idle`: active == requested, gain 1.
- `WaitingForIR` (only when entering IR mode): keep playing the active mode. Each block, call
  `cabSim.prime()`, which picks up any pending IR and runs the convolution on a pre-allocated
  silent scratch block so JUCE can install its engine. Leave once
  `convolution.getCurrentIRSize() > 1`, meaning a real IR is installed. No timeout: it's better
  to stay on the built-in cab a few ms longer than ever play un-cabbed. Log in debug builds if
  waiting exceeds 1 s.
- `FadingOut`: gain 1 → 0 over 128 base-rate samples, in the old mode.
- Commit at gain 0: switch the active mode and reset the state of the path being entered. For
  IR mode that's `convolution.reset()` (zeroes the stale tail and drops any half-finished JUCE
  crossfade) and `limiterOversampler->reset()`. For the built-in cab it's the three cab biquads.
- `FadingIn`: gain 0 → 1 over 128 samples. A new request during the fade-in fades out from the
  current gain, so the ramp has no step.

IR → IR changes are not mode changes. JUCE's own 50 ms crossfade keeps handling them.

`Convolution::reset()` is safe on the audio thread: it zeroes buffers, resets the crossfade
smoother, and passes the previous engine to JUCE's background queue for deletion. That's the same
call JUCE makes inside its own `process()`.

A true crossfade between the two paths would need the oversampled loop to output both streams,
plus a second downsampler. A short dip is click-free, matches oversample switching, and only
happens on a user action.

**Files:** new `src/dsp/cabinet/CabModeSwitcher.h` (pure C++), `src/dsp/cabinet/CabinetSim.{h,cpp}`
(`prime()`, scratch buffer, `hasRealIR()`), `src/PluginProcessor.{h,cpp}` (use the active mode
everywhere `irMode`/`cabOn` gate processing today: the cab biquads, the in-loop tanh, the IR
block, `cabSim.setEnabled`), new `tests/cab_mode_switcher_tests.cpp`, `tests/cabinet_ir_tests.cpp`,
`tools/sr_render.cpp`

- [ ] Write the state-machine tests first (`SilentRiotTests`):
  - [ ] An unchanged mode gives gain exactly 1 and no commit over 10k samples.
  - [ ] built-in → Off reaches exactly 0 before commit, commits once, and returns to 1. No
        per-sample step is larger than 1/128 at block sizes 1, 7, 64, 512 and 4096.
  - [ ] → IR with the IR not ready: gain stays 1 and no commit. Commit happens only once ready.
  - [ ] A request reversed mid-fade-out never commits the abandoned mode.
  - [ ] A request during fade-in continues from the current gain.
- [ ] Write JUCE tests: after the entry reset, the first block of silence is exactly 0 (today
      −47.7 dBFS). An IR pushed while not in IR mode reaches `hasRealIR()` through `prime()`
      within 1 s (bounded poll loop).
- [ ] Add `--ir-at-ms T` and `--cab-at-ms T` to `sr_render` (load an IR, or toggle CAB, between
      blocks at time T), so the click can be measured in the real `processBlock`.
- [ ] Measure before and after: for built-in → IR, IR → built-in, CAB off → on and CAB on → off
      at P-EDGE, record the largest sample-to-sample step and the energy above 7 kHz in the
      20 ms after T. Target: no step above the steady-state maximum, and no >7 kHz burst on the
      first IR load.
- [ ] Verify. Built-in-cab renders with no switch must be bit-identical.

## Task 4: Fade truncated IRs

**Problem:** finding 6. An IR longer than the cap is cut mid-ring, which adds a small spectral
ripple. Low impact at ≤ 96 kHz, but it costs a few lines.

**Files:** `src/dsp/cabinet/UserIRState.{h,cpp}` (pure helper `fadeTruncatedTail(buffer, rate)`),
`src/PluginProcessor.cpp` (`loadUserIR`), `tests/user_ir_state_tests.cpp`

- [x] Decision D2 confirmed by the owner (2026-10-26): keep 8192 samples.
- [ ] Write the test first: `fadeTruncatedTail` ends the buffer in a monotonic raised-cosine fade
      over its last 5 ms, finishing at ~0, and leaves every earlier sample untouched.
- [ ] Call it in `loadUserIR` only when the file was longer than the cap. This is the one
      exception to "store the raw audio": what gets stored is already the truncated copy, not
      the file, so storing it faded needs no flag on restore. IRs at or under the cap stay bit
      for bit. Projects saved before this change keep their unfaded truncation.
- [ ] Verify.

## Done when

- [ ] Tasks 1–4 merged one per commit, each with `verify.sh` green and a before/after table in
      `docs/handoff-ir-path-sound.md`.
- [ ] With every fixture, IR mode is within ±1.5 dB of the built-in cab and within ±0.3 dB across
      44.1/48/96 kHz. IR renders are repeatable. No mode switch clicks or plays un-cabbed.
- [ ] Every built-in-cab sign-off gate is bit-identical to Task 0. The new IR-variant gates are
      recorded.
- [ ] `CLAUDE.md` updated:
  - Cabinet IR Loader Architecture: fix the stale `IR_INDEX` lines (1–32 are user slots;
    `loadUserIR` doesn't go through `loadIRFromFile`). Add the normalisation, `installIRNow` and
    the mode switcher.
  - Important Pitfalls: never pass `Normalise::yes` (−18 dB and rate-dependent);
    `installIRNow` only when `processBlock` can't run.
  - Signal-chain diagram: add the mode fade to the IR path.
- [ ] `.agents/STATE.md` updated, including the ~10 dB louder IR mode in existing projects.
