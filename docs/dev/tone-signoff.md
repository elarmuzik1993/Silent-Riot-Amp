# Silent Riot — Tone Sign-Off Protocol (Phase A)

_Created: 2026-06-24 · Branch: `feature/competitive-1.0-fixes` · Expands `ROADMAP.md` Phase A_

This is the concrete, reproducible procedure that decides **when the tone is frozen**.
It exists so "ear-tuning is done" stops being a feeling and becomes a **signed measurement
against named references**. Nothing downstream in the roadmap (real cabs, presets, neural
model) starts until this protocol passes and the constants are locked.

> **Honesty note on the numbers below.** The absolute targets are **calibration targets**,
> not invented absolutes. They are ratified by the **Calibration Pass** (§5): you measure
> the current frozen-candidate build once, sanity-check each number against this table,
> adjust any that are unrealistic for _this_ circuit, then those become the frozen gates.
> The _structure_, _method_, and _tolerances_ are fixed; a handful of center values get
> pinned on first measurement.

---

## 1. The voicing under test

Silent Riot is an **EL84 push-pull, Class A/AB, 15W/7W** amp with an Orange-Shape-inspired
3-band EQ. Its reference family is therefore the **British EL84 chime/grind** voicing, not
American 6L6 scooped tones. All references are chosen from that family.

---

## 2. Named reference targets

Three tiers, used for three different jobs.

### 2.1 Real-amp references (the tonal anchor)
The voicing is signed off _in the family of_ these amps (not as a bit-exact clone — the
bet is "a great EL84 amp," not "a capture"):

| Ref | Amp | Role in the test |
|---|---|---|
| **R1** | Vox AC15C1 (12AX7 preamp, EL84 PP, ~15W) | Clean chime + edge-of-breakup anchor |
| **R2** | Vox AC30 (EL84 PP, ~30W) | Loud clean headroom + top-end character |
| **R3** | Orange Rocker 15 / OR15 (EL84, Shape-style EQ) | Mid-forward crunch + the Shape control's target sweep |
| **R4** | Orange Tiny Terror (EL84, 7W/15W switch) | The **7W mode** reference — Class-A compression feel |

**How to obtain reference audio (pick one, in order of rigor):**
1. **Re-amp captures (best):** record the four amps with a DI split, same DI feeding amp
   and plugin. Mic: SM57 + R121 on a real cab, or a load-box + a known commercial IR so
   the comparison is cab-matched.
2. **Commercial profile/IR packs** of these amps with a documented signal path.
3. **Published, signal-path-documented demos** as a last resort (uncontrolled mic/room —
   use for gestalt only, not for FR matching).

### 2.2 DI corpus (the test signals)
Sign-off uses a **fixed, version-controlled DI set** so results are comparable run-to-run.
Start from the asset already in the repo and grow it:

| DI | Content | Source |
|---|---|---|
| **D0** | Low-E (82 Hz) sustained note | `tests/di_sample_82hz.raw` (already in repo; used by `di_chain_trace_test.cpp`) |
| **D1** | Clean single-coil arpeggio (chord detail, Vox chime test) | record / re-amp DI |
| **D2** | Humbucker open power chords (crunch + low-end tightness) | record / re-amp DI |
| **D3** | Hard-picked single-note lead (sag/bloom + sustain) | record / re-amp DI |
| **D4** | Dynamic passage: soft→hard picking on the same phrase (touch sensitivity) | record / re-amp DI |
| **D5** | Calibration signals: 82 Hz / 220 Hz / 1 kHz sines, log sweep 20 Hz–20 kHz, pink noise | generated |

All DIs are **normalized to −18 dBFS RMS** before the plugin so gain-staging is identical
across signals and across runs. Commit them under `tests/tone/` with a checksum manifest.

### 2.3 Internal golden reference (the drift guard)
Once frozen, the **plugin's own rendered output** of D0–D5 at the canonical settings (§3)
becomes the golden reference for the regression test (Phase A's "golden-test lock"). Any
later code change that shifts these renders beyond tolerance fails CI.

---

## 3. Test matrix (settings × signals)

Parameters are internal `0..1` (UI shows `0..10`). Audio taper applies, so these are the
_displayed_ knob positions.

**Canonical operating points** (the gain-staging ladder):

| Point | Gain | Volume | Shape | 7W | Purpose |
|---|---|---|---|---|---|
| **P-CLEAN** | 3 | 5 | noon (5) | off | headroom, chime, noise floor |
| **P-EDGE** | 5 | 5 | noon (5) | off | edge-of-breakup — the money zone for this amp |
| **P-CRANK** | 8 | 7 | noon (5) | off | full grind, dynamics, aliasing stress |
| **P-7W** | 6 | 7 | noon (5) | **on** | Class-A compression / 7W feel (vs R4) |
| **P-SHAPE-CCW** | 5 | 5 | 0 | off | Shape warm/mid-forward extreme |
| **P-SHAPE-CW** | 5 | 5 | 10 | off | Shape scooped/modern extreme |

**Coverage rule:** every objective gate in §4 is measured at **P-CLEAN, P-EDGE, P-CRANK**
minimum; Shape gates add the two Shape extremes; sag/dynamics gates add P-7W. Cab on (real
IR once Phase B lands; until then the IIR cab path) unless a gate says "DI/no-cab."

---

## 4. Objective acceptance gates

Each gate: **metric · target · tolerance · method · status**. Status starts `TBM`
(to-be-measured); the Calibration Pass fills it in and ratifies the target.

### 4.A Output level & gain staging
Input DI normalized to −18 dBFS RMS.

| ID | Metric | Target | Tol | Method | Status |
|---|---|---|---|---|---|
| A1 | Output integrated loudness, P-CLEAN | −16 LUFS-I | ±1.5 LU | render D1, ITU-R BS.1770 | TBM |
| A2 | Peak ceiling, all points | ≤ −1.0 dBFS | hard | tanh limiter must hold; no inter-sample > 0 dBFS at 4× | TBM |
| A3 | Clean→crank loudness rise (P-CLEAN→P-CRANK) | +6 dB | ±2 dB | musically usable range, not a cliff | TBM |
| A4 | Channel match (L vs R) | 0 dB | ±0.1 dB | mono-to-stereo copy must be bit-identical | **must pass** |

### 4.B Harmonic structure (the "tube" character)
Sine probes (220 Hz A3 and 82 Hz E2), DI/no-cab to isolate the amp.

| ID | Metric | Target | Tol | Method | Status |
|---|---|---|---|---|---|
| B1 | THD, P-CLEAN @ 220 Hz | 1–5 % | band | FFT, sum H2..H10 / fundamental | TBM |
| B2 | THD, P-EDGE | 6–12 % | band | as above | TBM |
| B3 | THD, P-CRANK | 18–35 % | band | as above | TBM |
| B4 | **H2 ≥ H3 at P-CLEAN/P-EDGE** | even-dominant | — | the PI-asymmetry even-harmonic preservation must be audible as warmth | TBM |
| B5 | H3 rises with drive (P-EDGE→P-CRANK) | monotonic ↑ | — | push-pull odd-harmonic grind at crank | TBM |
| B6 | No non-harmonic spurs > −60 dBc | pass | hard | catches limit-cycle / instability tones | TBM |

### 4.C Frequency response & Shape control
Log sweep / pink noise, 1/12-oct smoothed, 80 Hz–8 kHz.

| ID | Metric | Target | Tol | Method | Status |
|---|---|---|---|---|---|
| C1 | FR match to reference (R1/R3, cab-matched) | ≤ ±2 dB | per band | overlay plugin vs ref curve | TBM |
| C2 | Cab notch present | dip @ 400 Hz | ±50 Hz | spectrum | TBM |
| C3 | Presence peak present | bump @ 2.5 kHz | ±300 Hz | spectrum | TBM |
| C4 | HF rolloff | −3 dB by 5.5–7 kHz | — | OT LP + cab rolloff | TBM |
| C5 | **Shape sweep monotonic** CCW→CW at 800 Hz | +5 dB → −9 dB, monotonic | ±1 dB at endpoints | mid-bell delta vs knob | TBM |
| C6 | Shape at noon | mild scoop (≈ +2/−2/+0.5 dB) | ±1 dB | per CLAUDE.md spec | TBM |

### 4.D Dynamics, sag & touch
The differentiator for "feel." Transient/envelope analysis.

| ID | Metric | Target | Tol | Method | Status |
|---|---|---|---|---|---|
| D1 | Sag depth, P-CRANK (attack peak vs 300 ms sustain) | 1.5–4 dB | band | D3 transient envelope | TBM |
| D2 | Sag depth, P-7W | > P-CRANK by ≥ 1 dB | — | 7W must compress more (Class-A) | TBM |
| D3 | Sag recovery time constant | 80–250 ms | band | envelope fit after note release | TBM |
| D4 | Touch sensitivity (D4 soft vs hard) | output ΔdB ≥ 0.7 × input ΔdB at P-EDGE | — | amp must not flatten dynamics | TBM |
| D5 | No signal collapse at high vol+gain | output present | hard | the bias-clamp fix (PR #1) guard — sustain never drops to silence | **must pass** |

### 4.E Noise, stability & artifacts
The defects Phase A must close.

| ID | Metric | Target | Tol | Method | Status |
|---|---|---|---|---|---|
| E1 | Idle noise floor (no input, P-CRANK) | ≤ −90 dBFS | hard | render silence 5 s | TBM |
| E2 | Idle DC offset / limit cycle | ≤ −80 dBFS, no sustained tone | hard | resolves the audit's "idle DC + limit cycle" note against **real** `processBlock` | TBM |
| E3 | **IR-path aliasing parity** (11 kHz @ P-CRANK through a bright IR) | ≤ −85 dBc inharmonic | hard | the PR #7/#9 fix target — IR path must match the IIR path's ≤ −85 dBc (was −28 dBc) | **must pass** |
| E4 | Sample-rate-change stability (44.1→48→96 mid-note) | no NaN, no pop > −40 dBFS | hard | exercises the PR #8 LTP-reset fix | **must pass** |
| E5 | Oversample-switch transition | click ≤ −60 dBFS | hard | crossfade state machine | TBM |
| E6 | Reported latency = oversampler latency | exact | hard | PDC correctness via pluginval | **must pass** |

---

## 5. Calibration Pass (ratify the targets — do once, first)

Before the protocol can gate anything, establish the baseline:

1. Build the current frozen-candidate (Release) with the parked constants
   (`outputGainCompensation`, `V1B_RSOURCE_MAX_REAL`, `XCUR_SENSITIVITY`, Shape ranges).
2. Render D0–D5 at every canonical point via the harness (§6).
3. Fill every `TBM` with the measured value. For each, decide: **is the target realistic
   for this circuit?** Adjust center values that are physically wrong (keep tolerances).
4. Lock the table. From here, the numbers are the **frozen gates**.

This is what makes the protocol honest: the gates are anchored to one real measurement of
the actual plugin, not to guesses.

---

## 6. Measurement harness (reproducible, CI-able)

Build on what already exists rather than from scratch:

- **Existing:** `tests/di_chain_trace_test.cpp` and `tests/level_trace_test.cpp` already
  load `di_sample_82hz.raw` and trace levels through the chain; `tests/regression_tests.cpp`
  already asserts finite/bounded/deterministic output.
- **Add a render tool** — a small target (`tools/sr_render` or a Catch2-driven offline
  render) that runs the **real `processBlock`** (not a reconstructed chain — the audit
  flagged the reconstructed harness as omitting PSU/sag) over a DI/test WAV at a given
  parameter set and writes a WAV.
- **Add an analyzer** — `juce::dsp::FFT` (already available) or a Python/`scipy` script:
  THD, harmonic table, LUFS (BS.1770), 1/12-oct spectrum, envelope/sag fit, dBc spurs.
- **Wire the gates** — each §4 gate becomes an assertion in a `[tone-signoff]` Catch2 tag,
  run in CI. The frozen renders of D0–D5 become the **golden files** for the drift guard
  (Phase A's golden-test lock — same harness, two jobs).

Output of a run: a one-page report (metric · measured · target · pass/fail) + the rendered
WAVs, archived as a CI artifact.

---

## 7. Subjective sign-off (what numbers can't catch)

Objective gates prove the amp is _correct and clean_; they don't prove it's _good_. Final
sign-off adds a blind panel:

- **Panel:** ≥ 3 listeners, at least one guitarist who plays this style.
- **Material:** D1–D4 rendered through Silent Riot vs the matched real-amp reference
  (R1/R3/R4), loudness-matched to ±0.5 LU (critical — louder always "wins").
- **Preference test (not ABX-clone):** blind A/B, "which is more convincing as a real EL84
  amp?" 12 trials/listener.
- **Pass bar:** Silent Riot chosen ≥ **45%** of trials across the panel (statistical parity
  with the real amp) **and** no listener reports a disqualifying artifact (fizz, buzz,
  unnatural compression). The bet is parity-or-better with a real boutique EL84, blind.

---

## 8. Freeze procedure & ledger

When all §4 **must-pass** gates pass, the rest are within tolerance, and §7 clears:

1. **Tag the constants.** Record the final values of every tuning constant in a
   `## Frozen Voicing v1.0` block in the DSP spec, with the date.
2. **Record the evidence.** Commit the §6 report + the golden renders + the DI manifest
   checksums. The renders ARE the contract.
3. **Lock CI.** The `[tone-signoff]` + golden-file jobs go required-to-merge on `main`.
4. **Unblock downstream.** Phase B (cabs), curated presets, and the parked neural model
   may now train/tune against a target that cannot move without breaking CI.

A tone change after freeze is a deliberate, versioned event (bump the voicing version,
re-run the panel, re-bless the goldens) — never an accidental drift.

---

### Quick reference — what "done" means

> Tone is frozen when: every **must-pass** gate (A4, D5, E3, E4, E6) passes against the
> **real** `processBlock`; all other §4 gates sit inside tolerance on the ratified table;
> the blind panel reaches parity with a real EL84; and the golden renders are committed
> and CI-locked. Until then, Phase A is not complete and nothing downstream starts.
