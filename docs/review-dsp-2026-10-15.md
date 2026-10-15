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
  every finding handed to an independent skeptic told to refute it. The
  corrections they found are folded in; where a claim was not independently
  re-checked, its section says so.
- The headline numbers come from measurement, not reading:
  - **Amp-loop probe** — [`review-dsp-2026-10-15-probe.cpp`](review-dsp-2026-10-15-probe.cpp)
    replicates `processBlock`'s per-sample amp loop with the repo's own DSP
    classes, minus the oversampling filters (input is fed at the oversampled
    rate directly). Build, modes and caveats are in its header comment. Used for
    B2 and B3.
  - **`sr_render`** and a probe around the real `processBlock` (snapshot and
    restore of processor state around `prepareToPlay`) for B1.
  - **JUCE 7.0.12** compiled standalone: `dsp::Oversampling` and
    `dsp::Convolution` latency for B4; the VST3 wrapper's latency path read for
    B5.

## Summary

| ID | Severity | Finding | Where |
|---|---|---|---|
| B1 | Blocking | Burst on silence after every `prepareToPlay` (preamp filters and PSU reset out of equilibrium); it is the whole E1 failure | `PluginProcessor.cpp:192`, `:197` |
| B2 | Blocking | Sag reference taken at nominal supply voltages, so the bias idles warm | `PluginProcessor.cpp:406` |
| B3 | Blocking | V1b Miller cutoff swings every cycle; its gain-pot dependence is dead code | `PluginProcessor.cpp:551`, `:565` |
| B4 | Blocking | Reported latency is ~3.6 samples too high whenever the IR path is inactive (the default) | `PluginProcessor.cpp:255` |
| B5 | Blocking | Saved oversample setting ignored at prepare: dropout on the first blocks, wrong latency at 2× | `OversampleSwitcher.cpp:13` |
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

**Where:** `resetDSPState()`, `src/PluginProcessor.cpp:187-228` — specifically
`filterBank.resetAll()` (`:192`) and `psu.reset()` (`:197`).

**What happens.** Two resets each leave the amp out of equilibrium:

1. `FilterBank::resetAll()` zeroes the V1a/V1b cathode-bypass and coupling
   high-pass state while each 12AX7 plate sits at ~194 V DC (203.5 V on the
   nominal 250 V rail). The first samples see that DC as a step and the zeroed
   high-passes pass it on. V1b's 7 Hz coupling high-pass alone reproduces most of
   it.
2. `PowerSupplyModel::reset()` puts every supply node back at nominal, but the
   model idles lower (B+ 282.2 V, screen 258.0 V, PI 262.3 V, preamp 232.4 V).
   The preamp rail then falls 250 → 232 V, which moves every preamp operating
   point at once.

Resetting `TriodeState` (the plate-voltage cache) is not a cause.

**Evidence** — real `processBlock` (44.1 kHz, 4×, 512-sample blocks, silence),
peak in the first 10 ms:

| Case | Default knobs | P-CRANK |
|---|---|---|
| Fresh instance (`sr_render --warmup-ms 0`) | −22.5 dBFS¹ | −7.3 dBFS (at 30–40 ms) |
| Re-prepare of a settled instance | −9.1 | −5.0 |
| Re-prepare, only the preamp filters reset | −9.4 | −5.6 |
| Re-prepare, only PSU + sag reset | −19.5 | −6.5 |
| Re-prepare, only `TriodeState` reset | −118.8 | −79.0 |

¹ Still −21.2 dBFS at 20–40 ms and −50.7 at 90–100 ms. A fresh instance is
quieter than a re-prepare because the GAIN/VOLUME/TONE `ParamSmoother`s start at
0 and ramp in (5 ms); `resetDSPState()` leaves them alone, so a re-prepare runs
at full knob values. The re-prepare rows come from a probe that snapshots and
restores processor state around `prepareToPlay`; the fresh-instance row
reproduces with `sr_render --out /tmp/b1 --signal silence:5.25 --warmup-ms 0`.

**E1 is this transient.** The E1 gate (5 s of silence at P-CRANK after
`sr_render`'s 250 ms warm-up) reads −70.42 dBFS against a target of ≤ −90. The
same render with `--warmup-ms 1000` reads −200 dBFS — `measure_noise`'s floor;
the true RMS is −268.9 dBFS, a constant ~4e-14 residual. Each cause alone fails
E1: the preamp-filter reset alone leaves −68.5 to −69.5 dBFS, the PSU reset alone
−67.1 to −67.8 dBFS.

**Why it matters.** Every prepare — opening a session, changing sample rate or
buffer size, many hosts' offline bounce — starts with a thump, up to −5 dBFS at
high gain. STATE.md attributes it to the PSU alone; fixing only the PSU (or only
the filters) leaves E1 near −68 dBFS.

**Fix direction.** Start at the idle operating point instead of zero: seed the
cathode-bypass and coupling-filter states with each stage's quiescent DC, and
start the PSU nodes and the sag envelope at their idle fixed point (B2 needs that
fixed point anyway). A few hundred ms of silent warm-up inside `prepareToPlay`
is the blunt alternative. Decide separately whether the smoother fade-in on a
fresh instance is wanted. Then re-run E1 and E2 at the default 250 ms warm-up.

### B2 — Sag reference is taken at nominal voltages, so the bias idles warm

**Where:** `src/PluginProcessor.cpp:404-408`; the PSU idle droop in
`src/dsp/power/PowerSupplyModel.cpp:33-53`.

**What happens.** The sag reference is `pentodeQuiescentTotal(bias,
V_POWER_NOMINAL_N, V_SCREEN_NOMINAL_N)` — the idle current at 300 V / 280 V. But
at idle the PSU droops through `SOURCE_IMPEDANCE_N` (200 Ω) and `R_SCREEN_N`
(470 Ω), so the tubes idle on lower rails and draw less than the reference. The
pentode current follows the screen voltage (the Koren model treats the screen as
the virtual anode), so `SagEnvelope::biasOffset()` rests positive, i.e. warm.

**Evidence** — the idle fixed point, solved by hand from `Constants.h` and
matched by the probe after 3 s of silence (probe values in brackets; it reads
~6 mV low because the float envelope stalls just above the plate current):

| Mode | B+ | Screen | Idle plate current | Reference | `biasOffset()` |
|---|---|---|---|---|---|
| 15W | 282.2 V | 257.9 V | 79.75 mA | 87.50 mA | **+0.774 V** (+0.768) |
| 7W | 274.4 V | 248.2 V | 114.65 mA | 124.95 mA | **+1.030 V** (+1.024) |

Both sit inside the +1.5 V warm clamp: a resting warm offset, not a railed one.
This breaks the invariant stated at `PentodeWaveshaper.h:44-45` and
`tests/psu_sag_tests.cpp:228-229` (`biasOffset()` must be ≤ 0). The regression
test `tests/psu_sag_tests.cpp:231-244` still passes because it feeds the
nominal-supply reference current itself back into the envelope
(`sag.update(idle)`); it never evaluates the pentode at the drooped voltages the
PSU loop produces. The sibling sag tests have the same shape, and no test runs
the PSU loop and checks the sign.

**Why it matters.** The tubes idle hotter than designed, and the first ~8 mA
(15W) / ~10 mA (7W) of extra current under drive only cancels the warm offset
instead of producing sag.

**Fix direction.** Compute the reference at the PSU's settled idle voltages —
iterate the PSU + pentode fixed point once when the bias changes. Add a test
that runs the real PSU loop on silence and asserts `biasOffset() <= 0`.
**This moves the calibration:** re-measure D1 and A1 afterwards.

### B3 — The V1b Miller cutoff swings every cycle; its gain-pot dependence is dead

**Where:** `src/PluginProcessor.cpp:551-552` (V1a), `:565-568` (V1b);
`src/dsp/filters/DynamicMillerLP.h:29-35` (1 ms cutoff smoothing,
`MILLER_FC_SMOOTH_MS`).

**What happens.** The Miller gain estimate is `|out| / |in|`, sample by sample.
It spikes toward infinity at every *input* zero crossing and collapses toward 0
at every *output* zero crossing — the coupling high-pass and cathode shelf shift
the output a few degrees from the input — even on clean settings where V1b does
not clip. At heavy drive it also collapses on the clipped peaks (≈ 4) and
inflates in the grid-clamped half-cycle. The cutoff therefore swings at twice
the note frequency; the collapse side drives it up, and the 1 ms smoothing does
not remove it.

**Evidence** (probe, 110 Hz sine, V1b smoothed cutoff over the second half of a
1 s run, the same swing in every cycle):

| Input amplitude | Gain knob | Cutoff min / mean / max | Gain estimate max |
|---|---|---|---|
| 0.125 (≈ −18 dBFS) | 0.3 | 14.1 / 15.7 / 24.9 kHz | 3.3e4 |
| 0.125 | 0.6 | 14.0 / 15.8 / 25.6 kHz | 7.2e4 |
| 0.5 | 1.0 | 8.2 / 57.5 / 97.1 kHz | 1.0e6 |

At clean settings the stage's real small-signal gain is ~42.5, which puts a
steady cutoff at ~14.0 kHz — the floor in the table. The swing above that floor
is the estimator. The 10.1 kHz "design" value assumes `StageGains::V1B_GAIN = 60`
(`Constants.h:15`), which this Koren stage never reaches.

Two related facts:

- **V1b's source resistance never moves.** `rSource_real = min(38.5k +
  (1 − gainA)·500k, 150k)`. `gainDriveTaper()` (`TriodeWaveshaper.h:67-70`) tops
  out at 0.52, so `gainA` (its square) never exceeds 0.2704 and the unclamped
  value never drops below 403 kΩ. The 150 kΩ clamp (`V1B_RSOURCE_MAX_REAL`) is
  engaged at every knob position, including 0; it would only release with
  `GAIN_KNOB_HEADROOM` ≳ 0.96. The gain-pot term is dead code.
- **V1a's Miller filter is inaudible.** With a 10 kΩ source its cutoff parameter
  is 205 kHz–1.3 MHz. `OnePoleLP::setFreqFast` (`OnePole.h:26-32`) skips
  pre-warping, so the realised cutoff lands below Nyquist (40–43.5 kHz at
  2×/44.1 kHz), but its effect at 20 kHz is at most −0.06 dB.

**Fix direction.** Use a slow estimate of stage gain (e.g. the ratio of
smoothed RMS levels) or a fixed per-stage value, clamped to a sane range, and
re-derive `StageGains::V1B_GAIN` from the stage's real small-signal gain. Decide
whether the gain-pot dependence should exist; if so, re-derive
`V1B_RSOURCE_MAX_REAL` against the current gain taper.

### B4 — Reported latency is too high in the default IIR-cab mode

**Where:** `src/PluginProcessor.cpp:255-262`; the limiter oversampler only runs
inside `if (irMode)` at `:655-674`.

**What happens.** The IR-path limiter oversampler's latency is always added to
`setLatencySamples`. That is deliberate — the comment at `:255-258` and
`docs/handoff-IR-limiter-aliasing.md:143-144` choose a constant latency across
cab modes — but nothing delays the IIR path to match. So whenever the IR path is
not active (IIR cab, the default `IR_INDEX = 0`, or the cab switched off) the
report is too high.

**Evidence** (JUCE 7.0.12 polyphase-IIR `dsp::Oversampling`, compiled
standalone; `useIntegerLatency` defaults to false):

| Factor | `getLatencyInSamples()` | Reported as `int` | Reported total | Real IIR-path delay |
|---|---|---|---|---|
| 2× | 3.137 | 3 | 7 | 3.14 |
| 4× | 4.433 | 4 | 8 | 4.43 |
| 8× | 4.951 | 4 | 8 | 4.95 |

At the default 4× the host shifts the amp ~3.6 samples too early (~74 µs at
48 kHz). For a linear path blended 50/50 with a delay-compensated copy (a null
test, or a parallel blend) the first comb notch lands at ≈ 6.9–7.0 kHz at 48 kHz
(≈ 6.4 kHz at 44.1 kHz) — slightly above the naive fs / (2 · 3.57) because the
polyphase IIR's phase delay rises with frequency. In IR mode the report is
roughly right but truncated: 8 reported against a measured 8.8 (4×) and 9.3 (8×);
the default `juce::dsp::Convolution` is zero-latency.

**Fix direction.** Keep the constant report and delay the IIR path by the
limiter oversampler's latency, or report only the active path's latency. Round
instead of truncating, or use `Oversampling::setUsingIntegerLatency(true)`.

### B5 — The saved oversample setting is ignored at prepare

**Where:** `src/dsp/OversampleSwitcher.cpp:13`; `src/PluginProcessor.cpp:269`,
`:279`, `:493-511`, `:652`.

**What happens.** `OversampleSwitcher::init()` always sets `currentMode = 1`
(4×) and never resets `state`, `fadeCounter` or `pendingMode`; nothing applies
the saved `OVERSAMPLE` parameter at prepare (its only reader is `processBlock`).
A session saved at 2× or 8× therefore starts every prepare at 4× and switches
on its first blocks:

- **Blocks > 128 samples** (e.g. 512): block 1 fades out over 128 samples and is
  silent for the rest; block 2 commits the switch and fades back in.
- **Blocks ≤ 128 samples:** the fade-out spans ⌈128/N⌉ blocks, then one fully
  silent block, then the commit and fade-in (64-sample blocks: blocks 1–2 fade
  out, block 3 silent, blocks 4–5 fade in).

The PSU is frozen throughout, which also delays B1's settling.

The latency report is visibly wrong only at **2×**: prepare reports 8, the
commit block then calls `setLatencySamples(7)` from the audio thread. (At 8×
the truncated value is also 8, so nothing changes.) JUCE 7.0.12's VST3 wrapper
defers the resulting `restartComponent(kLatencyChanged)` to the message thread,
but a host that follows the VST3 rule and deactivates/reactivates on that flag
re-runs `prepareToPlay`, which resets to 4× again — a 2× session could then cycle
fade-out → silence → fade-in on every reactivation (not reproduced in a real
host).

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

- The startup thump has two causes, not one: the preamp filter reset (dominant
  at default knobs) and the PSU reset to nominal. The triode plate cache is not
  a cause (B1).
- E1's −70.4 dBFS is that transient outlasting the 250 ms warm-up; settled, the
  amp idles at −269 dBFS. Fix both causes before touching E1's target or the
  warm-up length.
