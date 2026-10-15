# DSP code review — 2026-10-15

**Status:** open. Nothing in this review has been fixed yet; it is the work list
for the fixes. Line references are against `main` at `b8e20a9`.

**Scope:** `src/dsp/**`, `src/utils/**`, and the signal chain in
`src/PluginProcessor.cpp` (`prepareToPlay`, `resetDSPState`,
`recalcRateDependentCoeffs`, `processBlock`, state restore).

**Verdict:** request changes. B1–B3 change what the amp sounds like; B4–B6 are
what a user notices in a DAW session.

## How the findings were checked

- Two review passes (a reviewer read and an automated correctness pass), then
  every finding re-checked by an independent skeptic told to refute it.
- The headline numbers come from measurement, not reading:
  - **Amp-loop probe** — [`review-dsp-2026-10-15-probe.cpp`](review-dsp-2026-10-15-probe.cpp)
    replicates `processBlock`'s per-sample amp loop with the repo's own DSP
    classes, minus the oversampling filters (input is fed at the oversampled
    rate directly). Build and modes are in its header comment.
  - **`sr_render`** — the real `processBlock`, for the startup burst (B1).
  - **JUCE 7.0.12 `dsp::Oversampling`** compiled standalone, for the latency
    numbers in B4.

## Summary

| ID | Severity | Finding | Where |
|---|---|---|---|
| B1 | Blocking | Burst on silence after every `prepareToPlay`; it is also the whole E1 failure | `PluginProcessor.cpp:187` |
| B2 | Blocking | Sag reference taken at nominal supply voltages, so the bias idles warm | `PluginProcessor.cpp:406` |
| B3 | Blocking | V1b Miller cutoff swings every cycle; its gain-pot dependence is dead code | `PluginProcessor.cpp:551`, `:565` |
| B4 | Blocking | Reported latency is ~3.6 samples too high in the default IIR-cab mode | `PluginProcessor.cpp:255` |
| B5 | Blocking | Saved oversample setting ignored at prepare | `OversampleSwitcher.cpp:13` |
| B6 | Blocking | Delay / reverb / IR freeze while off and replay stale audio when re-enabled | `PluginProcessor.cpp:687` |
| S1 | Suggestion | GAIN/TONE/VOLUME/OVERSAMPLE skip `ParamSanitize`; NaN on OVERSAMPLE indexes out of bounds | `PluginProcessor.cpp:348`, `:504` |
| S2 | Suggestion | Restoring state with an out-of-range IR index can leave the previous IR playing | `PluginProcessor.cpp:996` |
| S3 | Suggestion | GRID I readout stores a voltage and converts it as a current (~7.8× low) | `GridConduction.cpp:44` |
| S4 | Suggestion | User IR samples never checked for NaN/Inf | `UserIRState.cpp:81` |
| S5 | Suggestion | Reverb SIZE jumps the loop length in whole samples per block (zipper) | `SpringReverb.h:86` |
| S6 | Suggestion | Tuner mute freezes the VU meters | `PluginProcessor.cpp:397` |
| S7 | Suggestion | Tempo-synced delay silently clamps at 1000 ms | `TempoSync.h:47` |

---

## Blocking

### B1 — Burst on silence after every `prepareToPlay`

**Where:** `resetDSPState()`, `src/PluginProcessor.cpp:187-228`;
`PowerSupplyModel::reset()`, `src/dsp/power/PowerSupplyModel.cpp:17-24`.

**What happens.** `resetDSPState()` zeroes every preamp filter
(`FilterBank::resetAll`) and the triode plate cache (`TriodeState::vPlate = -1`).
The first samples therefore see each triode's plate DC — about 150 V — as a step,
and the zeroed coupling high-passes pass it straight through. Separately, the
PSU restarts at its nominal 300 V although the model idles at ~282 V, which adds
a slower tail as the rails and the sag envelope settle.

**Evidence** (probe, 44.1 kHz, 4×, silence, peak per 10 ms window):

| Run | 0–10 ms | 10–20 | 20–30 | … | 90–100 ms |
|---|---|---|---|---|---|
| As shipped (gain 0.6, vol 0.5, 15W) | −8.9 dBFS | −17.8 | −17.8 | | −51.7 |
| PSU frozen at nominal, sag off | −9.2 | −17.8 | −17.8 | | −50.2 |
| Preamp settled first | −79.8 | −90.2 | −101.2 | | −161.4 |

So the initial burst comes from the preamp, not from the PSU.

**E1 is this transient.** The E1 gate renders 5 s of silence at P-CRANK after
`sr_render`'s 250 ms warm-up and gets −70.4 dBFS (target ≤ −90). The probe
reproduces it: −70.1 dBFS RMS after a 250 ms warm-up, with the last 3 s at
digital silence. The preamp tail alone leaves −72.2 dBFS, the PSU settling alone
−67.8 dBFS, and with the whole chain settled for 1 s first the output is digital
silence. The amp has no idle noise floor to speak of; E1 is measuring how long
the startup transient lasts.

**Why it matters.** Every prepare — opening a session, changing sample rate or
buffer size, many hosts' offline bounce — starts with a thump of up to −9 dBFS.
STATE.md and CLAUDE.md attribute the thump to the PSU, which would send a fix to
the wrong place.

**Fix direction.** Start the state at its idle operating point instead of zero:
seed `TriodeState::vPlate` with the stage's quiescent plate voltage, set the
cathode-bypass and coupling-filter states to match that DC, and start the PSU
nodes and the sag envelope at their idle fixed point (see B2). A short silent
warm-up inside `prepareToPlay` is the blunt alternative. Then re-run E1 and E2.

### B2 — Sag reference is taken at nominal voltages, so the bias idles warm

**Where:** `src/PluginProcessor.cpp:404-408`; the PSU idle droop in
`src/dsp/power/PowerSupplyModel.cpp:33-53`.

**What happens.** The sag reference is `pentodeQuiescentTotal(bias,
V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N)` — the idle current at 300 V / 280 V. But
at idle the PSU model droops through `SOURCE_IMPEDANCE_N` and `R_SCREEN_N`, so
the tubes actually idle on lower rails and draw less than the reference.
`SagEnvelope::biasOffset()` then rests positive, i.e. warm.

**Evidence** (probe, after 3 s of silence):

| Mode | B+ | Screen | Idle plate current | Reference | `biasOffset()` |
|---|---|---|---|---|---|
| 15W | 282.2 V | 258.0 V | 79.75 mA | 87.50 mA | **+0.77 V** |
| 7W | 274.4 V | 248.3 V | 114.64 mA | 124.95 mA | **+1.02 V** |

This breaks the invariant documented at `PentodeWaveshaper.h:40-47` and
`SagEnvelope.h:15-20` (`biasOffset()` must be ≤ 0). The regression test at
`tests/psu_sag_tests.cpp:230` still passes because it feeds the nominal voltages
straight into the sag envelope instead of running the PSU loop — the same
self-consistency trap that test's own comment warns about.

**Why it matters.** The tubes idle hotter than designed, and the first ~8 mA
(15W) / ~10 mA (7W) of extra current under drive only cancels the warm offset
instead of producing sag.

**Fix direction.** Compute the reference at the PSU's settled idle voltages —
iterate the PSU + pentode fixed point once when the bias changes. Add a test
that runs the real PSU loop on silence and asserts `biasOffset() <= 0`.
**This moves the calibration:** re-measure D1 and A1 afterwards.

### B3 — The V1b Miller cutoff swings every cycle; its gain-pot dependence is dead

**Where:** `src/PluginProcessor.cpp:551` (V1a) and `:565-567` (V1b);
`src/dsp/filters/DynamicMillerLP.h:29-35`.

**What happens.** The Miller gain estimate is `|out| / |in|`, sample by sample.
It explodes near every input zero crossing and collapses toward 0 where the
stage clips, so the Miller capacitance — and with it the low-pass cutoff — is
modulated at twice the note frequency. The 1 ms smoothing on the cutoff does not
remove it.

**Evidence** (probe, 110 Hz sine, V1b smoothed cutoff over the second half of a
1 s run; design value with the nominal gain of 60 is 10.1 kHz):

| Input amplitude | Gain knob | Cutoff min / mean / max | Gain estimate max |
|---|---|---|---|
| 0.125 (≈ −18 dBFS) | 0.3 | 14.1 / 15.7 / 24.9 kHz | 3.3e4 |
| 0.125 | 0.6 | 14.0 / 15.8 / 25.6 kHz | 7.2e4 |
| 0.5 | 1.0 | 8.2 / 57.5 / 97.1 kHz | 1.0e6 |

So the stage is brighter than designed and its tone is modulated every cycle.

Two related facts:

- **V1b's source resistance never moves.** `rSource_real = min(38.5k +
  (1 − gainA)·500k, 150k)`. `gainDriveTaper()` tops out at 0.52, so `gainA`
  (its square) never exceeds 0.27 and the unclamped value never drops below
  403 kΩ. The 150 kΩ clamp (`V1B_RSOURCE_MAX_REAL`) is engaged at every knob
  position, including 0, so the gain-pot term is dead code.
- **V1a's Miller filter is inaudible.** With a 10 kΩ source its cutoff sits at
  237–906 kHz, far above Nyquist at any oversampling factor, so only V1b matters.

**Fix direction.** Use a slow estimate of stage gain (e.g. the ratio of
smoothed RMS levels, or simply the nominal `StageGains` value) instead of an
instantaneous ratio, and clamp it. Decide whether the gain-pot dependence should
exist; if so, re-derive `V1B_RSOURCE_MAX_REAL` against the new gain taper.

### B4 — Reported latency is too high in the default IIR-cab mode

**Where:** `src/PluginProcessor.cpp:255-262`.

**What happens.** The IR-path limiter oversampler's latency is always added to
`setLatencySamples`, so that latency stays constant across cab modes. But in
IIR-cab mode — the default, `IR_INDEX = 0` — the signal never passes through
that oversampler, and nothing delays the IIR path to match.

**Evidence** (JUCE 7.0.12 polyphase-IIR `dsp::Oversampling`):

| Factor | `getLatencyInSamples()` | Reported as `int` |
|---|---|---|
| 2× | 3.137 | 3 |
| 4× | 4.433 | 4 |
| 8× | 4.951 | 4 |

At the default 4× the plugin reports 4 + 4 = 8 samples while the IIR path is
~4.4 samples late, so the host shifts the amp ~3.6 samples too early (~75 µs at
48 kHz). Blended 50/50 with a parallel DI or dry track that comb-filters, with
the first notch near 6.7 kHz. The `static_cast<int>` also truncates (8× reports
4 for 4.95).

**Fix direction.** Either delay the IIR path by the limiter oversampler's
latency so the constant report is true, or report only the active path's latency.
Round instead of truncating, or use `Oversampling::setUsingIntegerLatency(true)`.

### B5 — The saved oversample setting is ignored at prepare

**Where:** `src/dsp/OversampleSwitcher.cpp:13`; `src/PluginProcessor.cpp:269`,
`:279`, `:504-507`.

**What happens.** `OversampleSwitcher::init()` always sets `currentMode = 1`
(4×), and `prepareToPlay` never applies the saved `OVERSAMPLE` parameter. A
session saved at 2× or 8× therefore reports the 4× latency at prepare. Its first
block then fades out (128 samples, silence for the rest of the block), commits
the switch on the next block and fades back in, changing the latency again from
the audio thread. `init()` also leaves `state`, `fadeCounter` and `pendingMode`
untouched, so a prepare that arrives mid-transition inherits a half-finished
switch.

**Fix direction.** In `prepareToPlay`, set `currentMode` from the parameter
(clamped to 0..2) and reset the transition state before
`recalcRateDependentCoeffs()`.

### B6 — Delay, reverb and IR freeze while off and replay stale audio when re-enabled

**Where:** `src/PluginProcessor.cpp:687-713`; `src/dsp/cabinet/CabinetSim.cpp:28-29`.

**What happens.** With `DELAY_ON` / `REVERB_ON` off, `delay.process()` /
`reverb.process()` are simply not called. The delay line, its loop filters and
LFO phases, and the spring tanks keep whatever they held. Switching the effect
back on replays it: with the delay at high feedback, up to about a second of the
previous phrase's repeats. The widener is the only stage that clears its tail
on the off→on edge (`PluginProcessor.cpp:721-729`). The IR convolution behaves
the same way when the cab is switched off, and switching between the IR and IIR
paths is an instant, un-crossfaded change.

**Fix direction.** On each effect's off→on edge, `reset()` it and ramp its mix
from 0 — the pattern `widenerWasActive` already uses — or keep processing it
with the wet level ramped to 0 if tails should ring out naturally.

---

## Suggestions

### S1 — GAIN/TONE/VOLUME/OVERSAMPLE skip `ParamSanitize`

**Where:** `src/PluginProcessor.cpp:348-350`, `:504`;
`src/dsp/OversampleSwitcher.cpp:24-34`.

The effects parameters are sanitized because "a misbehaving host could report
NaN" (`ParamSanitize.h`); the four core amp parameters are read raw. One NaN on
GAIN, TONE or VOLUME locks `ParamSmoother` at NaN forever
(`current = target + (current − target)·decay` never clears), poisons the
triode, PSU and filter state, and the final `isfinite` scrub turns that into
permanent silence until the next prepare. A NaN on OVERSAMPLE is worse:
`static_cast<int>(NaN)` is undefined behaviour (INT_MIN on x86), and
`requestModeChange` / `commitSwitch` use it to index `oversamplers` with no
bounds check — a likely crash. Fix: route all four through
`sanitizeUnitParamOr(...)` and clamp the oversample index to 0..2.

### S2 — State restore can leave the previous session's IR playing

**Where:** `src/PluginProcessor.cpp:989-1004`.

`loadUserIRsFromState()` reloads only when the restored `IR_INDEX` is within the
restored IR list; otherwise it neither disables the IR nor updates
`cachedIRIndex`. If the restored index equals the cached one — e.g. the instance
had IR 5 active and the new state has `irIndex = 5` but only three IRs, or
parsing stopped early at an invalid entry — `processBlock` sees no change and
the old IR keeps convolving. Fix: in the out-of-range case call
`cabSim.setIRIndex(0)` (or trigger the async update) explicitly.

### S3 — GRID I readout is ~7.8× too low

**Where:** `src/dsp/tubes/GridConduction.cpp:44-45`; `src/PluginProcessor.cpp:829`.

`lastGridCurrent` stores `vGrid − vLoaded`, a normalized *voltage*, and
`getDSPReadout()` converts it with `VS::toAmps()` as if it were a normalized
current. In this domain `I_n = V_n / R_n`, so the readout is low by the source
resistance in normalized units — 38.5 kΩ / 300 kΩ = 0.128 for the EL84 stages,
i.e. ~7.8×. Fix: divide by `model.sourceResistance` when storing it.

### S4 — User IR samples are never checked for NaN/Inf

**Where:** `src/dsp/cabinet/UserIRState.cpp:78-82`; `src/PluginProcessor.cpp:945`.

Restored IRs come from untrusted project state; their dimensions are validated
but the samples are not, and `loadUserIR` reads files the same way. A non-finite
sample makes the normalized convolution output NaN, which the final scrub turns
into silence for as long as that IR is selected. Fix: reject (or zero) entries
with non-finite samples on both paths.

### S5 — Reverb SIZE zippers while it is turned

**Where:** `src/dsp/effects/SpringReverb.h:86-89`; `src/dsp/effects/Reverb.cpp:39-44`.

`setLength()` snaps the loop to a new integer length, and SIZE is re-applied
from the raw knob every block it moves by more than 1e-4. The read position
therefore jumps by several samples per block inside a feedback loop of gain up
to 0.935 — clicks while sweeping. The LFO already reads at a fractional delay,
so smoothing a float loop length would remove it.

### S6 — Tuner mute freezes the VU meters

**Where:** `src/PluginProcessor.cpp:397`; `src/PluginEditor.cpp:518-528`.

The tuner-mute early return skips the meter stores, and the editor only zeroes
the meters when POWER is off, so with tuner mute engaged they hold their last
level while the plugin outputs silence.

### S7 — Tempo-synced delay silently clamps at 1000 ms

**Where:** `src/dsp/effects/TempoSync.h:46-47`; `DELAY_TIME_MAX_MS`,
`src/utils/Constants.h:334`.

Synced times are clamped to 1000 ms, so 1/2 notes below 120 BPM, dotted 1/4
below 90 BPM and 1/4 below 60 BPM play off-grid with no indication. Either raise
the ceiling (the delay line is sized from it in `Delay::prepare`) or show that
the sync is clamped.

---

## Read with no findings

Read in full without turning up a defect — not proven correct, but not where
the next review should start:

- `LTPSolver.cpp` (secant seeded from the previous sample, bisection fallback
  over `[0, vSupply]`).
- `OnePole.h`: `setFreqFast` skips pre-warping, but its pole `−(g−1)/(g+1)` stays
  inside the unit circle for every `g > 0`, so it is stable at any cutoff.
- `Biquad.h` (RBJ forms); `SpeakerCoupling`'s crossover is exactly complementary
  (`bass + mid + hf == powerOut`).
- `OTSaturation` at small signal reduces to a ~20 Hz high-pass; the flux clamp
  is a safety rail.
- IR thread handoff (`BufferTransfer`, atomic `useIR` / `cachedIRIndex`) and the
  tuner ring's release/acquire publish.
- `NoiseGate`, `Boost`, `StereoWidth` (mono sum `L + R = 2·Mid` preserved),
  `softClip`.

## Consequences for STATE.md

- The startup thump's first burst is the preamp, not the PSU (B1).
- E1's −70.4 dBFS is the startup transient outlasting the 250 ms warm-up; once
  settled the amp idles at digital silence. Fixing B1 should be checked against
  E1 before E1's target or the warm-up is touched.
