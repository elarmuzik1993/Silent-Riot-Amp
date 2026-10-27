# Power-Section Fixes Plan

> Steps use checkbox (`- [ ]`) syntax for tracking. Nothing is done until
> `bash scripts/verify.sh` passes. Tone-affecting tasks also need a sign-off run.

**Goal:** Fix the issues found in the 2026-10-23 power-section review (PI → Shape → master
volume → EL84 pair → PSU and sag → OT) without breaking the USE-59 calibration more than the
fixes themselves require.

**Review findings.** Each one was checked against spec v2.13 for *intent*. The spec is background
only (see its status banner), so it explains why a behaviour exists, never what the code must do:

| # | Finding | Verdict | Task |
|---|---|---|---|
| 1 | EL84s have no plate load line: `pentodeWaveshaper` gets the PSU's slow B+, so the 40 V plate knee and screen redistribution never engage | **Real gap.** The spec (§4.2) says "Vp drop → reduced swing before hard clip" and that the amp should compress "from BOTH ends", and today only the screen end works | 4 |
| 2a | `condA/condB` scale the output, but the PSU and sag are fed the unscaled `ipA`/`ipB` | **Real inconsistency.** Cut-off current still loads the supply and the cathode | 2 |
| 2b | `softConductionCurve` puts cutoff at *signal* −10 V (≈ −19.6 V of real grid), not where the Koren softplus actually cuts off | **Needs measuring** before changing. The spec asks for this function (§3.8); the question is only where it is anchored | 3 |
| 3 | PSU runs one sample late | **Not a bug.** Spec §3.11 asks for it ("physically correct and avoids circular dependency"). No change | — |
| 4 | Stale numbers in the `softConductionCurve` comment (−0.9 / −0.96, ratio 1.067) | Real, cosmetic | 1 |
| 5 | 7W mode only moves the bias (−9.6 → −7.0 V, hotter); B+ is unchanged | **Matches spec §3.8** ("~96% bias in 7W mode"). But the spec checklist (line 4776) also says "changes bias points **+ supply behavior**", and the supply half was never built | 5 (owner decision) |

**Order:** cheapest and least tone-changing first, so each later measurement starts from a clean
baseline. Tasks 2–4 each change the sound; do **one per commit**, and re-run sign-off after each.

## Coordination with the DSP review

This plan was written without `docs/review-dsp-2026-10-15.md`, which covers the same code. The
combined order lives in `.agents/STATE.md`. Where they meet:

- **Sequencing.** Tasks 0 and 1 belong to the review's phase 1 (settled tone unchanged). Tasks 2–5
  belong to its phase 2, after B5+B4, B1+S1, B7 and B6+B8, and share its **single**
  re-calibration pass instead of re-tuning after each task.
- **Task 0.** E1's −70.4 dBFS is the startup burst (review B1), not a noise floor. All sign-off
  numbers hold only at 44.1 kHz / 4× (B7). Record the baseline there and note both.
- **Task 2 = review B2.** Do them as one change. The sag reference is taken at *nominal* rails, so
  at the PSU's settled idle the bias rests +0.77 V (15W) / +1.03 V (7W) warm. Adding `cond` alone
  corrects ~2% of a ~9% error. The "pair idles" test Task 2 extends is tautological (it feeds the
  reference back into itself): replace it with one that runs the real PSU loop on silence and
  asserts `biasOffset() ≤ 0`. Solve the idle fixed point once (with `cond`) and share it with
  B1's start-up seeding.
- **Task 4 and S8.** S8 shows the 0.5-damped plate iteration (triode, and the LTP's inner loop)
  diverges under extreme input. Task 4's load-line solve must be bracketed as specified here, and
  should not reuse that inner loop. Fix S8 in the same pass.
- **Task 4 recalibration** is the review's phase-2 re-calibration; do it once, at the end.

## Global constraints

- All DSP constants in `Constants.h` (`_N` / `_REAL` / dimensionless). `namespace sr`.
- No allocation in `processBlock`. Bias is negative: `vGrid + vBias`.
- No new APVTS parameters in this plan. If Task 5 adds one, **append** it.
- `tests/di_chain_trace_test.cpp` and `tests/level_trace_test.cpp` re-implement the power stage
  by hand. Any change to the stage must be mirrored there, or those traces silently stop
  describing the real amp.
- Do not touch `MAX_BIAS_SHIFT_N`, `GRID_CHARGE_RATE` or the EL84 grid threshold to "fix" a
  measurement. CLAUDE.md explains why each is load-bearing.
- Commits carry no AI attribution (see `AGENTS.md`).

---

## Task 0: Baseline

Every later task is judged against these numbers, so record them before touching code.

> **Done 2026-10-23.** Results in `docs/handoff-power-section-fixes.md`; the diagnostic is
> `docs/handoff-power-section-fixes-probe.cpp`. **Read its last section before Tasks 2–4:** the
> EL84s run class A at every sign-off point and at every knob on 10 (no cut-off, plate knee
> never approached, B+ moves 0.1 V), so Tasks 3–4 are inaudible at today's
> `VOLUME_KNOB_HEADROOM`, and idle `cond` is 0.975 (15W) / 0.923 (7W), not ≈ 0.98. Whether the
> power stage should ever leave class A is an owner decision that now gates Tasks 3–4.

- [x] `bash scripts/verify.sh` passes (308/308).
- [x] Build `sr_render` (`-DSR_BUILD_TOOLS=ON`, Release) and run
      `python3 tools/signoff.py --render-bin …/sr_render`. Save the full gate table to
      `docs/handoff-power-section-fixes.md` (new). Expect the 2026-10-14 figures in
      `.agents/STATE.md`: A1 −31.6 LUFS, B1 0.99 %, E1 −70.4 dBFS, E3 −37.9 dBc, other 8 pass.
- [x] Also record `tools/sr_thump.py`, `tools/sr_idle.py`, `tools/sr_bloom.py` at P-CLEAN,
      P-EDGE and P-CRANK, in 15W and 7W.
- [x] Add a temporary diagnostic test (or `sr_render` debug output) that logs, per preset:
      peak and average `ipA`/`ipB`, `condA`/`condB` minimum, `sag.biasOffset()` range, and
      B+ minimum. Tasks 2–4 are each checked against these.

## Task 1: Fix the stale comment (no sound change)

**Files:** `src/dsp/tubes/PentodeWaveshaper.cpp` (`softConductionCurve`)

- [x] Replace the −0.9 / −0.96 / 1.067 example with the real values: bias −0.032 (15W) /
      −0.023 (7W), `biasFactor` = −9.6/−7.0 ≈ 1.37, `overlapWidth` = 5 V × `biasFactor`.
- [x] Verify: build passes and output is bit-identical (comments only).

## Task 2: Feed the PSU and sag the current that actually flows

**Problem:** `PluginProcessor.cpp:604–614` computes
`powerOut = ipA·condA − ipB·condB`, then reports `|ipA| + |ipB|` (and the unscaled screen
currents) to the PSU and sag. A tube the conduction curve has turned off still loads the supply
and still lifts the cathode.

**Files:** `src/PluginProcessor.cpp`, both trace tests, `tests/psu_sag_tests.cpp` or
`tests/signal_chain_tests.cpp`

- [ ] Write a failing test: drive one tube deep into the conduction curve's cutoff
      (`cond` < 0.05) and assert that the reported plate current is about `ipA·condA + ipB·condB`,
      not `ipA + ipB`. Easiest route: pull the "pentode pair" step into a small pure function
      (e.g. `PushPullOutput evalPushPull(piA, piB, bias, vPlate, vScreen)` in
      `PentodeWaveshaper.h`) that returns `powerOut`, `totalPlate` and `totalScreen`. The
      processor and both trace tests then call that one function instead of copying the math.
- [ ] Change the totals to the scaled currents: `ipA·condA + ipB·condB`, and the same for screen.
- [ ] Re-check the idle reference. `pentodeQuiescentTotal()` must apply the same `cond` factor
      at idle (about 0.98 today), or sag idles slightly off zero, which is the desync defect
      USE-59 fixed. Extend the existing "pair idles at a realistic cathode current" test so idle
      `biasOffset()` stays about 0.
- [ ] Verify, then run sign-off. Expect a small change: sag and droop get *slightly less* current
      at crank, so D1 sag depth may drop a little. If D1 leaves its 1.5–4.0 dB band, stop and
      report. Do not retune sag constants in this task.

## Task 3: Re-anchor or remove the second cutoff (measure first)

**Problem:** the Koren softplus already models cutoff around vg1 ≈ −10 V. `softConductionCurve`
adds a tanh gate at *signal* −10 V, which is real grid ≈ −19.6 V (15W) / −17 V (7W). Today it is
≈ 0.98 at idle, so it mostly acts as a mild extra squash on deep negative swings. It is not
creating the soft crossover the spec describes.

**Files:** `src/dsp/tubes/PentodeWaveshaper.cpp`, `tests/pentode_tests.cpp`

- [ ] Measure first: plot each tube's `ip·cond` against vg1 for both modes, together with the
      Koren-only curve. Record where each reaches 10 % and 1 % of idle current. Put the plot or
      table in the handoff doc.
- [ ] Decide from the data (state the choice in the commit message):
  - **(a)** Koren's softplus already gives a smooth, bias-dependent crossover → delete
    `softConductionCurve` (cond = 1) and let the tube model carry it. This is the simplest path,
    and the spec's intent (smooth overlap, wider in 7W) is met by physics.
  - **(b)** The Koren tail is too abrupt or too long to sound right → keep the function but
    anchor it on **total** vg1 (`vGrid + vBias`) at the measured Koren cutoff, with width set from
    the bias as the spec intends.
- [ ] Keep or rewrite the five `[conduction]` tests so they pin the chosen behaviour,
      especially "7W has wider overlap than 15W", which is the spec's stated reason for the curve.
- [ ] Verify, then run sign-off. Watch B4 (even-harmonic warmth), the crank THD band and E3.
      Crossover shape changes odd harmonics, which affects aliasing.

## Task 4: Give the EL84s a plate load line (largest task)

**Problem:** each tube's plate voltage should swing with its own current across the output
transformer: `Vp = B+ ∓ v_primary`. Today Vp is pinned to B+ (300 V), so
`plateLimit = 1 − exp(−300/40) ≈ 0.9994` all the time. The plate-side clipping and screen-current
rise that spec §4.2 describes never happen. Plate clipping is only imitated by the 95 mA tanh
ceiling and the OT saturation.

**Model (class-A push-pull, transformer-coupled):** with `Raa` = plate-to-plate primary load,
each half-primary sees the difference current:

```
v   = (ipA − ipB) · Raa / 4
VpA = B+ − v
VpB = B+ + v
```

At 95 mA of difference current and Raa = 8 kΩ, v ≈ 190 V, so VpA gets down to about 110 V and
nears the knee on the biggest peaks. That is the behaviour we want.

**Files:** `src/dsp/tubes/PentodeWaveshaper.*` (the Task 2 helper), `src/utils/Constants.h`,
`src/PluginProcessor.*` (state), both trace tests, `tests/pentode_tests.cpp`, new
`tests/power_loadline_tests.cpp`

- [ ] **Confirm `Raa`.** `OT_NOMINAL_LOAD_N` is 8 kΩ. Check spec §3.9 to see whether that figure
      is plate-to-plate. Add `OT_RAA_N` with a comment giving the source. Do not reuse the
      speaker-coupling constant by accident.
- [ ] **Decide how speaker coupling interacts.** The reflected load (8–35 kΩ) is today applied
      as a gain change *after* the tubes. Step 1 uses a fixed `Raa` and leaves speaker coupling
      as it is. Folding `zEffective` into the load line is a possible follow-up only.
- [ ] **Solve it, don't relax it.** `ip` depends on Vp (through the knee) and Vp depends on `ip`,
      which is the same trap as the LTP (`docs/handoff-ltp-convergence.md`). Above the knee
      dIp/dVp is tiny, so a plain fixed-point step contracts strongly. Near the knee it may not.
      Use a 1-D solve on `v` with a bracket: `v ∈ [−B+, B+]`, residual
      `f(v) = (ipA(B+−v) − ipB(B+ + v))·Raa/4 − v`, which decreases strictly in `v`. Use secant
      seeded from the previous sample, with bisection as fallback, like `solveLTP`. Keep the
      converged `v` in a small state struct next to `ltpModel`, and reset it in `reset()`.
- [ ] **Tests first** (new `power_loadline_tests.cpp`):
  - [ ] Constant input → constant output, with no period-2 alternation (the LTP regression,
        written for this solver).
  - [ ] Residual below tolerance at idle, at crank and at the knee, in both modes.
  - [ ] With the load line on, the plate knee engages at crank (`plateLimit` < 0.95 on peaks),
        and the screen-to-plate current ratio rises above `SCREEN_CURRENT_RATIO` on those peaks.
  - [ ] Small signal: output within 0.1 dB of today's, because the knee is not engaged and the
        change must be invisible there.
  - [ ] The datasheet pin in `pentode_tests.cpp` must not move, since it evaluates at a fixed
        Va.
- [ ] **Recalibrate**, in the CLAUDE.md order: `OT_SAT_DRIVE` (with `tools/sr_thump.py`), then
      `OT_CURRENT_SCALE`, then `VOLUME_KNOB_HEADROOM` + `OUTPUT_MAKEUP_GAIN` **together**. Plate
      clipping adds a third limiter in series with the grid clamp and output tanh, so re-measure
      D1 **at knob 10**, not only at the gate's 0.70.
- [ ] **Cost check:** the stage now runs a root-find per sample at the oversampled rate. Measure
      `processBlock` time before and after at 8× OS. If it is over +15 %, loosen the tolerance
      first (the LTP's 1e-5 is a good starting point).
- [ ] Verify, then run full sign-off plus thump, idle and bloom. Report every gate that moved.
      E3 must not get worse. If it does, a softer knee (`V_PLATE_KNEE_N`) is the first thing to
      try.
- [ ] Write the defect record into `docs/handoff-power-section-fixes.md` in the style of
      `handoff-power-section-calibration.md`, then update CLAUDE.md (signal chain, Pentode model,
      Important Pitfalls) and `.agents/STATE.md`.

## Task 5: 7W mode supply behaviour (owner decision before any code)

Code and spec agree that 7W = hotter bias. What is missing is the "+ supply behavior" in the spec
checklist. **Owner chooses one:**

- **(a) Leave as is.** Mark the checklist item done-by-design in the spec.
- **(b) Add a lower B+ in 7W** (e.g. ~250 V), so 7W gets less plate headroom and more sag, as
  well as the hotter bias. Small code change: per-mode `V_*_NOMINAL_N` in `PowerSupplyModel`,
  and re-run `sag.setQuiescent(pentodeQuiescentTotal(…))` with the new B+. After Task 4 this
  matters much more, because B+ will actually set where the plate clips. **Do it after Task 4**,
  then calibrate 7W A1 loudness separately.
- **(c) Match a specific reference amp's circuit.** The owner supplies the schematic or
  measurement. Do not build from memory.

## Done when

- [ ] Tasks 1–4 merged one per commit, each with `verify.sh` green and a sign-off table in the
      handoff doc.
- [ ] Task 5 decided and, if (b) or (c), built and calibrated.
- [ ] No gate that passed at Task 0 fails at the end, unless the owner signs off on the change.
- [ ] CLAUDE.md and `.agents/STATE.md` match the code. The spec is not updated (it is background
      only), unless a superseded design is added to its status banner.
