# DSP code review — 2026-10-15

**Status:** open. Nothing in this review has been fixed yet; it is the work list
for the fixes. Line references are against `main` at `b8e20a9`.

**Scope:** `src/dsp/**`, `src/utils/**`, and the signal chain in
`src/PluginProcessor.cpp` (`prepareToPlay`, `resetDSPState`,
`recalcRateDependentCoeffs`, `processBlock`, state restore).

**Verdict:** request changes. B1–B3 and B7 change what the amp sounds like;
B4–B6 and B8 are what a user notices in a DAW session.

## How the findings were checked

- Two review passes (a reviewer read and an automated correctness pass), then
  every finding handed to an independent skeptic told to refute it; all were
  confirmed, most with corrections, which are folded in. A completeness pass
  then looked for what the review missed: B7 and S8 came from it and were each
  confirmed by a separate refuter; B8 turned up while verifying B6 and was
  measured there.
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
| B6 | Blocking | Delay / reverb / IR freeze while off (or muted) and replay stale audio when re-enabled | `PluginProcessor.cpp:687`, `:397` |
| B7 | Blocking | Grid-conduction charge rate ignores the sample rate: 96 kHz / 8× is ~5.7 dB quieter and more blocked | `GridConduction.cpp:46` |
| B8 | Blocking | Delay self-oscillates above feedback ≈ 0.53 (saturator gain 1.90 at low level) | `Delay.cpp:81` |
| S1 | Suggestion | A NaN on GAIN or VOLUME silences the plugin for the rest of the session (not even `prepareToPlay` recovers) | `PluginProcessor.cpp:348` |
| S2 | Suggestion | Restoring state with an out-of-range IR index can leave the previous IR playing | `PluginProcessor.cpp:996` |
| S3 | Suggestion | GRID I readout stores a voltage and converts it as a current (7.8× low) | `GridConduction.cpp:44` |
| S4 | Suggestion | User IR samples never checked for NaN/Inf | `UserIRState.cpp:81` |
| S5 | Suggestion | Reverb SIZE jumps the loop length in whole samples per block (zipper, clicks on fast turns) | `SpringReverb.h:86` |
| S6 | Suggestion | Tuner mute freezes the VU meters, which keep flickering as if live | `PluginProcessor.cpp:397` |
| S7 | Suggestion | Tempo-synced delay silently clamps at 1000 ms | `TempoSync.h:47` |
| S8 | Suggestion | Triode load-line solve diverges under extreme input (from ~+21.6 dBFS at the amp) | `TriodeWaveshaper.cpp:29` |

## Suggested fix order

Fix the bugs that leave the settled sound alone first, then the ones that change
the tone, so the amp is re-calibrated once rather than after every fix. One PR
per step, each with a test.

**Phase 1 — the amp's settled sound at 44.1 kHz / 4× does not change**

1. **B5 + B4** — oversample mode at prepare and the latency report. Same code;
   B1's fix needs the right mode at prepare.
2. **B1 + S1** — startup burst, plus seeding the knob smoothers at prepare.
   Clears E1. Solve the idle operating point once at prepare and seed the filter
   and supply states from it, rather than hard-coding today's values, so the fix
   stays right after B2 moves the idle point.
3. **B7** — grid-charge rate. Scale it so 44.1 kHz / 4× is unchanged; sign-off
   numbers stay put and every other rate starts matching them.
4. **B6 + B8** — stale effect tails and the delay runaway. Independent of the
   amp core.

**Phase 2 — changes the tone, then one re-calibration**

5. **B2 + B3** back to back, then a single pass over D1 / A1 / B1 and the
   pending P-* re-ratification, with a listening check by the owner.
6. **S8**, together with E3 step 2 — both touch the triode solver.
7. **E3** last: its measurement depends on the drive levels phase 2 changes.

**Phase 3 — small items, batched:** S2 + S4 (IR restore and IR samples),
S3 + S6 (readouts), S5 + S7 (reverb zipper, sync clamp).

**Owner decisions**, asked at the start of the step that needs them:

- **B8:** should the delay never self-oscillate, or keep it as an effect at the
  top of the FEEDBACK knob?
- **B2 / B3:** approve the new sound by ear before the sign-off targets are
  re-set.
- **S7:** raise the 1000 ms limit (more delay-line memory) or show when sync is
  clamped?

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

**Where:** `src/PluginProcessor.cpp:687-713` (delay/reverb), `:655-657` (IR
convolution, skipped whenever `irMode` is false), `:397` (power-off / tuner-mute
early return).

**What happens.** When an effect is off its `process()` is simply not called, so
the delay line, loop filters, LFO phases, spring tanks and convolution state
keep whatever they held, and switching back on replays it. The same happens
after POWER off or TUNER MUTE: the early return at `:397` skips all three, so
unmuting after tuning replays the old tail. Nothing clears them:
`delay.reset()` / `reverb.reset()` run only from `prepareToPlay`
(`:216-217`, `:330`); `cabSim.reset()` is never called; there are no parameter
listeners; and the processor does not override `AudioProcessor::reset()`, so a
VST3 host stop (`setProcessing(false)`) clears nothing either. The widener is
the only stage that clears its tail on the off→on edge (`:721-729`).

The smoothers are stale too: Delay's `mixSm`, `feedbackSm` and `delaySamples`
and Reverb's `mixSm` resume from their pre-disable values. Re-enabling has no
fade-in (stale wet at full mix from the first sample); disabling has no fade-out
(wet cut, dry gain steps +3 dB at mix 0.5); a TIME change made while the delay
was off becomes a 50 ms pitch glide of the stale buffer.

**Evidence** (real processor, re-enabled on silence):

| Case | Replay level | Control: `reset()` at re-enable |
|---|---|---|
| Delay, feedback 0.35 (default), time 1000 ms, off for 3 s | −27.2 dBFS (0–1 s), −36.6 (2–3 s), −43.0 (3–5 s) | −272 dBFS |
| Reverb, size 1.0, mix 0.5 | −30.0 dBFS (0–0.1 s), −50.8 (0.5–1 s) | −272 dBFS |
| IR convolution, cab off for 1 s | −52.7 dBFS (0–50 ms), −63.7 (100–200 ms) — exactly the frozen tail | −110.6 dBFS |

The IR replay is bounded by the IR length (≤ 8192 samples). Switching between
the IR and IIR paths is also an instant, un-crossfaded change, and it moves the
output by the limiter oversampler's ~4.4 samples (B4).

**Fix direction.** On each effect's off→on edge (and on leaving power-off /
tuner-mute), `reset()` it and ramp its mix from 0 — the `widenerWasActive`
pattern — or keep processing it with the wet level ramped to 0 if tails should
ring out. Override `AudioProcessor::reset()` to clear the tails.

### B7 — Grid-conduction charge rate is per sample, so tone and level depend on the sample rate

**Where:** `src/dsp/tubes/GridConduction.cpp:46`; the rate-aware decay at
`:62-64`, called from `src/PluginProcessor.cpp:244-248`.

**What happens.** `couplingCapCharge += gridCurrent * GRID_CHARGE_RATE` runs
once per *oversampled* sample with no rate term, while the matching decay is
rate-aware (`exp(-1/(tau·effectiveSR))`). The settled charge — the grid's bias
shift, i.e. how much the stage "blocks" — therefore grows linearly with the
effective sample rate (until the 1.5 V cap), and its attack speeds up. Everything
was calibrated at 44.1 kHz / 4× (`sr_render`'s defaults; `signoff.py` passes
neither flag), so the OVERSAMPLE menu and the host's sample rate change the tone
and level.

**Evidence** (real `processBlock` via `sr_render`, 110 Hz sine at 0.25, gain 1.0,
volume 0.5, output RMS):

| Host rate | 2× | 4× | 8× |
|---|---|---|---|
| 44.1 kHz | −13.38 dBFS | **−13.74** (calibrated) | −14.85 |
| 96 kHz | −13.83 | −14.94 | **−19.43** |

At 192 kHz / 8×: −20.41 dBFS. Level tracks the *effective* rate (44.1k/8× and
88.2k/4× agree to 0.01 dB). In the amp-loop probe the V1b bias shift is
0.134 / 0.270 / 0.549 V at 44.1 kHz 2× / 4× / 8× and 1.242 V at 96 kHz / 8×;
scaling the increment by `176400 / effectiveSR` holds it at 0.270 V everywhere
and keeps output within −13.21 to −13.76 dBFS. At 96 kHz / 8× the effective rate
is 0.0131, comparable to the 0.01 that CLAUDE.md records as having caused PI
pumping (pumping itself not reproduced).

**Why it matters.** A user who raises quality (8×) or works at 96 kHz gets a
darker, more blocked, ~5–6 dB quieter amp, and every sign-off measurement only
holds at 44.1 kHz / 4×.

**Fix direction.** Give `GridConductionModel` a per-model charge rate set next
to the decay in `recalcGridChargeDecay` — `GRID_CHARGE_RATE · 176400 /
effectiveSR` keeps the current calibration. Add a test that the settled bias
shift is the same at 2×/4×/8× and 44.1/96 kHz.

### B8 — The delay self-oscillates above feedback ≈ 0.53

**Where:** `src/dsp/effects/Delay.cpp:81-82`, `:141`; the comments at
`src/utils/Constants.h:335` and `:347-349`.

**What happens.** The feedback saturator is `tanh(k·x) / tanh(k)` with
`k = DELAY_FB_SAT_DRIVE = 1.8`. That is unity at x = 1 (full scale), not at low
level: its small-signal slope is `k / tanh(k) = 1.90`. The loop's small-signal
gain is therefore `feedback × 1.90`, which exceeds 1 for feedback above
1 / 1.90 = 0.526 — despite the comments saying "unity gain at low level" and
that `DELAY_MAX_FEEDBACK = 0.95` keeps it "from self-oscillating".

**Evidence** (Delay class alone, peak 29–30 s after a 0.1 burst): feedback 0.50 →
0.0002, 0.52 → 0.0056, 0.55 → sustains at 0.25, 0.90 → sustains at 1.01. In the
full plugin at 0.9 the repeats grow to and hold at −5.6 dBFS; even a freshly
reset delay grows out of the anti-denormal floor. The existing decay test passes
because it uses feedback 0.5.

**Why it matters.** The top ~45 % of the FEEDBACK knob runs away instead of
giving long repeats. (Self-oscillation can be a feature, but here it contradicts
the design comments and is untested.)

**Fix direction.** Normalise the saturator for unity small-signal gain
(`tanh(k·x) / k`), or fold `tanh(k)/k` into the feedback scaling, then add a
test that repeats decay at `DELAY_MAX_FEEDBACK`.

---

## Suggestions

### S1 — A NaN on GAIN or VOLUME silences the plugin for the rest of the session

**Where:** `src/PluginProcessor.cpp:348-350`; `src/utils/ParamSmoother.h:13-16`.

The effects parameters are sanitized because "a misbehaving host could report
NaN" (`ParamSanitize.h`); GAIN, TONE and VOLUME are read raw, and in JUCE 7.0.12
a host NaN does reach the APVTS raw value (the VST3 wrapper, `jlimit` and
`NormalisableRange` all pass NaN through). Measured on the real processor:

- **GAIN or VOLUME:** one NaN locks `ParamSmoother` at NaN
  (`current = target + (current − target)·decay` never clears), the output scrub
  at `:640` turns the poisoned chain into silence, and **`prepareToPlay` does not
  recover it** — the smoothers are never re-seeded (`init()` only sets the
  decay; nothing calls `setImmediate`). Silent for the life of the instance.
- **TONE:** does not silence; the NaN makes the `> 0.001` test at `:528` always
  false, so the Shape EQ freezes for the life of the instance.
- **OVERSAMPLE:** not reachable. The choice parameter's range clamps and
  rounds, so the raw value is always 0, 1 or 2 (a host NaN silently selects 2× or
  8×). `OversampleSwitcher::requestModeChange` (`OversampleSwitcher.cpp:24-30`)
  still has no bounds check — latent only.

Fix: read the three through `sanitizeUnitParamOr(...)`, and seed the smoothers
with `setImmediate` in `prepareToPlay` (which also settles B1's fresh-instance
ramp question).

### S2 — State restore can leave the previous session's IR playing

**Where:** `src/PluginProcessor.cpp:989-1004`.

`loadUserIRsFromState()` reloads only when the restored `IR_INDEX` is within the
restored IR list; otherwise it neither disables the IR nor updates
`cachedIRIndex`, and `replaceState` fires no async update. If the restored index
equals the cached one, `processBlock` (`:413-418`) sees no change, so the only
code that disables an out-of-range index (`handleAsyncUpdate`, `:53-58`) never
runs. Measured: a live instance with IR 5 active, restored from a state with
`IR_INDEX = 5` and three IRs, keeps convolving the old IR 5 — and still does
after a later `prepareToPlay`, whose reload (`:319-327`) has the same in-range
check. The editor meanwhile shows "IIR Mode". It needs a live instance (preset
recall, A/B, undo) and an out-of-range saved index, which the plugin can produce
itself: `IR_INDEX` is automatable to 32 regardless of the IR count, and
`handleAsyncUpdate` leaves the out-of-range value in place to be saved. Fix: in
the out-of-range case call `cabSim.setIRIndex(0)` explicitly (both here and in
`prepareToPlay`).

### S3 — GRID I readout is ~7.8× too low

**Where:** `src/dsp/tubes/GridConduction.cpp:44-45`; `src/PluginProcessor.cpp:829`.

`lastGridCurrent` stores `vGrid − vLoaded`, a normalized *voltage*, and
`getDSPReadout()` converts it with `VS::toAmps()` as if it were a normalized
current. In this domain `I_n = V_n / R_n`, so the readout is low by the source
resistance in normalized units: 38.5 kΩ / 300 kΩ = 0.128 for the EL84 stages,
i.e. 7.8×. (Example: 20 V above threshold → 15.2 V drop → 0.395 mA real, 0.051 mA
shown.) Fix it in the readout — divide by `sourceResistance` — not in the charge
path, whose `GRID_CHARGE_RATE` / `MAX_BIAS_SHIFT_N` are calibrated on the voltage.
Also, the value published at `:797-800` is each stage's *last* sample of the
block, not a peak as its comment says.

### S4 — User IR samples are never checked for NaN/Inf

**Where:** `src/dsp/cabinet/UserIRState.cpp:78-82`; `src/PluginProcessor.cpp:945`.

Restored IRs come from untrusted project state; their dimensions are validated
but the samples are not, and `loadUserIR` reads float WAVs the same way. A NaN
or ±Inf sample *inside the IR body* survives `Convolution`'s normalisation
(`jmax` drops the NaN sum of squares) and makes IR mode silent (measured: rms
0.0025 → 0). A NaN at the IR's edges is removed by `Trim::yes`. Loading another
IR recovers on x86; on non-Intel builds JUCE's snap-to-zero is a no-op, so the
limiter oversampler's state may stay NaN until the next prepare (code-read).
`prepareToPlay` reloads the bad IR if it is still selected, and a restored bad
IR is re-saved with the project. Fix: reject (or zero) non-finite samples on
both paths.

### S5 — Reverb SIZE zippers while it is turned

**Where:** `src/dsp/effects/SpringReverb.h:86-89`; `src/dsp/effects/Reverb.cpp:39-44`.

`setLength()` snaps the loop to a new integer length, and SIZE is re-applied
from the raw knob every block it moves by more than 1e-4. Measured with the real
`SpringReverbCore` (44.1 kHz, 512-sample blocks): a moderate turn (0.4 of the
range per second) jumps 3–5 samples per block and adds a block-rate (~86 Hz)
zipper ~57 dB below the wet signal — +36 dB of energy above 8 kHz versus a
smoothed length. A fast turn (0 → 1 in 0.25 s) jumps 42 samples per block and
clicks (peaks ≈ −30 dBFS). The LFO already reads at a fractional delay, so a
smoothed float loop length removes it.

### S6 — Tuner mute freezes the meters, which keep looking live

**Where:** `src/PluginProcessor.cpp:397`; `src/PluginEditor.cpp:517-528`.

The power-off / tuner-mute early return skips the meter stores (`:446-459`,
`:783-795`) and the advanced-panel telemetry (`:797-812`); the editor zeroes
the VU meters only when POWER is off. With tuner mute on, both meters hold their
last level — and because `VUMeter` adds ±0.02 jitter, they keep flickering as if
live while the plugin outputs silence.

### S7 — Tempo-synced delay silently clamps at 1000 ms

**Where:** `src/dsp/effects/TempoSync.h:46-47`; `DELAY_TIME_MAX_MS`,
`src/utils/Constants.h:334`.

Every division clamps to 1000 ms below a tempo: 1/2 below 120 BPM, 1/4D below
90, 1/4 below 60, 1/8D below 45, 1/8 below 30, 1/8T below 20, 1/16 below 15
(and 1/16 hits the 20 ms floor above 750 BPM). The clamp is intentional — pinned
by `tests/tempo_sync_tests.cpp:49-60` — but nothing tells the user. Raising it
also means raising the delay buffer (`Delay.cpp:14-16`) and the second clamp
(`Delay.cpp:56`); otherwise, show that the sync is clamped.

### S8 — The triode load-line solve diverges under extreme input

**Where:** `src/dsp/tubes/TriodeWaveshaper.cpp:25-30` (damped step at `:29`).

`triodeWaveshaper` solves `Vp = Vs − Ip·R` with four fixed-point steps damped by
0.5. That iteration is only stable while `R / rp < 3`; a 12AX7 driven positive
toward plate saturation has a much lower `rp`. Instability starts at
vgk ≈ +1.6 V on the 250 V rail and lower when the rail sags (≈ +1.4 V at gain
10). Past it the plate voltage goes chaotic or settles at a wrong constant
(grid +3.5 V: true 24.1 V, output wandering 14.5–54.3 V). The `std::clamp` only
bounds the orbit; smoothing it (the E3 idea) would not fix this. Reachable only
with extreme front-end gain staging — output error is −79 dB up to +18 dBFS at
the amp input, then:

- **Gain 10:** V1b diverges first, from ~+21.6 dBFS (output error −48 dB).
- **Gain 0.3:** V1a from ~+23 dBFS (error −35 dB).

For example, INPUT +12 dB plus BOOST +12 dB into a 0 dBFS-peak DI. This is the
same class of failure as the old LTP solver: the LTP fix replaced only its
outer cathode loop, and its inner plate loop (`LTPSolver.cpp:39-42`) still uses
this scheme, which `LTPSolver.cpp:29-31` and the convergence handoff assert is
stable. Fix: a bracketed or Newton solve (as the LTP got), or a smaller damping
factor derived from `rp`.

---

## Read with no findings

Read in full without turning up a defect — not proven correct, but not where
the next review should start:

- `LTPSolver.cpp`'s outer cathode root-find (secant seeded from the previous
  sample, bisection fallback over `[0, vSupply]`). Its inner plate loop shares
  S8's damped scheme and is safe only because the PI grid stays low (peak
  vgk +0.37 V even at gain 10 / +30 dBFS).
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
- Every sign-off number holds only at 44.1 kHz / 4×: the grid-conduction charge
  scales with the effective sample rate (B7).
