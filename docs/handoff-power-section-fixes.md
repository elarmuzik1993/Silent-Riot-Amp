# Handoff — power-section fixes

Record for `docs/superpowers/plans/2026-10-23-power-section-fixes.md`. Every number here is a
measurement you can re-run; each task appends its own section below with the same tables.

Companion: `review-dsp-2026-10-15.md` (DSP review). The combined fix order is in
`.agents/STATE.md`; the plan's *Coordination with the DSP review* section maps the overlap.

## Task 0 — baseline (2026-10-23, `main` at `9970ddd`, src unchanged since `b8e20a9`)

`bash scripts/verify.sh`: pass, 308/308. All renders at 44.1 kHz / 4× OS, the only rate the
calibration holds at (review B7).

### Sign-off gates (`tools/signoff.py`, real `processBlock`)

| ID | Render | Value | Target | Status |
|---|---|---|---|---|
| A2 (M) | thd220_crank | −6.0 dBFS peak | ≤ −1.0 | PASS |
| A4 (M) | thd220_clean | 0.0 dB L/R | ≤ 0.1 | PASS |
| A1 | thd220_clean | −31.59 LUFS | −17.5…−14.5 | FAIL |
| B1 | thd220_clean | 0.99 % THD | 1.0…5.0 | FAIL |
| B2 | thd220_edge | 7.86 % THD | 6.0…12.0 | PASS |
| B3 | thd220_crank | 20.52 % THD | 18.0…35.0 | PASS |
| B4 | thd220_edge | H2 ≥ H3 | true | PASS |
| D5 (M) | note_crank | −10.95 dBFS at 300 ms | ≥ −60 | PASS |
| D1 | note_crank | 2.77 dB sag depth | 1.5…4.0 | PASS |
| E1 | idle_crank | −70.42 dBFS | ≤ −90 | FAIL |
| E2 | idle_crank | −99.29 dBFS DC | ≤ −80 | PASS |
| E3 (M) | alias_crank | −37.89 dBc | ≤ −85 | FAIL |

12 gates, 4 fail, 1 MUST-PASS fail. Matches `.agents/STATE.md` (A1, B1, E1, E3). E1 is the
start-up burst (review B1), not a noise floor: `sr_idle.py` below settles to −268.9 dBFS.

Note: CLAUDE.md quotes edge 9.44 % / crank 30.1 % THD and D1 2.93 dB from the USE-59
calibration. Today's 7.86 % / 20.52 % / 2.77 dB are the numbers to compare against.

### `tools/sr_idle.py` (silence, 8 s, volume 0.7)

| Mode | Gain | First 0.5 s | Settled RMS | Verdict |
|---|---|---|---|---|
| 15W | 0.375 / 0.625 / 1.0 | −61.2 / −57.4 / −41.5 dBFS | −268.9 | quiet |
| 7W | 0.375 / 0.625 / 1.0 | −61.1 / −57.8 / −41.8 dBFS | −268.9 | quiet |

No self-oscillation in either mode. The first-0.5 s figure is the B1 burst.

### `tools/sr_bloom.py` (note 110 Hz @ 0.6, volume 0.7)

| Mode | Gain | Peak | Max rebound | At | Verdict |
|---|---|---|---|---|---|
| 15W | 0.375 | −13.9 dBFS | 0.82 dB | 773 ms | slight |
| 15W | 0.625 | −8.6 | **3.80 dB** | 25 ms | **blooms back** |
| 15W | 1.0 | −3.7 | 0.93 dB | 20 ms | slight |
| 7W | 0.375 | −14.3 | 0.88 dB | 1856 ms | slight |
| 7W | 0.625 | −8.6 | **4.12 dB** | 25 ms | **blooms back** |
| 7W | 1.0 | −3.9 | 1.04 dB | 20 ms | slight |

The edge-gain rebound at 25 ms is pre-existing. Not root-caused here; it is too early for sag
(the probe below shows sag barely moves) and is more likely grid-charge recovery in the preamp.

### `tools/sr_thump.py` (compression slope, volume 0.7; 1.00 = linear)

| Mode | Gain | Low band (82–165 Hz) | Mid band (440–880 Hz) | Low-vs-mid deficit |
|---|---|---|---|---|
| 15W | 0.375 | 1.03 | 0.98 | −0.05 |
| 15W | 0.625 | 0.34 | 0.25 | −0.09 |
| 15W | 1.0 | 0.24 | 0.13 | −0.11 |
| 7W | 0.375 | 1.03 | 0.98 | −0.04 |
| 7W | 0.625 | 0.34 | 0.25 | −0.08 |
| 7W | 1.0 | 0.26 | 0.10 | −0.15 |

No OT-saturation low-end loss in either mode. Observation, not root-caused: at gain 1.0 every
band's output **falls** going from input 0.20 to 0.40 (e.g. 110 Hz −5.8 → −7.6 dBFS), and at
0.625 the mids dip from 0.10 to 0.20. Louder playing giving a quieter output points at blocking
(grid-charge build-up) somewhere upstream of the power stage. Worth a look alongside review B7.

7W was measured by wrapping `sr_render` to append `--7w 1`; the tools themselves have no 7W
option and fix volume at 0.7.

### Power-stage telemetry (`docs/handoff-power-section-fixes-probe.cpp`)

Amp loop copied from the review probe (no oversampling filters, input at the oversampled rate),
1 s silent warm-up, then the gate's own signals. Currents per tube in mA; `diff` is the peak of
`ipA·condA − ipB·condB`, the current the OT sees. Full output: run the probe.

| Point | Signal | ipA min/avg/pk | ipB min/avg/pk | diff pk | Σip / Σip·cond | cond min | Sag offset | B+ min |
|---|---|---|---|---|---|---|---|---|
| P-CLEAN | silence | 39.9 / 39.9 / 39.9 | 39.9 / 39.9 / 39.9 | 0.0 | 79.8 / 77.8 | 0.975 | +0.77 V | 282.2 V |
| P-CLEAN | note 110 @0.5 | 37.8 / 39.7 / 41.4 | 39.1 / 39.7 / 40.5 | 2.7 | 79.4 / 77.4 | 0.973 | +0.74…+0.77 | 282.2 |
| P-EDGE | note 110 @0.5 | 35.4 / 39.7 / 42.4 | 38.6 / 39.7 / 41.1 | 5.8 | 79.4 / 77.5 | 0.970 | +0.72…+0.77 | 282.1 |
| P-CRANK | sine 220 @0.18 | 26.6 / 38.8 / 47.1 | 35.6 / 38.6 / 43.9 | 16.9 | 77.4 / 75.6 | 0.952 | +0.56…+0.78 | 282.2 |
| P-CRANK | note 110 @0.5 | 25.0 / 38.9 / 50.4 | 34.4 / 38.8 / 43.9 | 19.1 | 77.7 / 75.9 | 0.950 | +0.52…+0.79 | 282.1 |
| 7W (any) | silence | 57.3 / 57.3 / 57.3 | 57.3 / 57.3 / 57.3 | 0.0 | 114.6 / 105.9 | 0.923 | +1.02 V | 274.4 V |
| 7W-CRANK | note 110 @0.5 | 41.9 / 56.3 / 66.6 | 52.4 / 56.4 / 61.0 | 19.7 | 112.7 / 104.5 | 0.884 | +0.82…+1.05 | 274.4 |
| P-7W | note 110 @0.5 | 48.8 / 56.7 / 62.3 | 54.8 / 56.7 / 59.6 | 10.9 | 113.4 / 105.0 | 0.904 | +0.93…+1.03 | 274.4 |
| all at 10, 15W | note 110 @0.5 | 13.3 / 38.1 / 61.0 | 29.0 / 37.7 / 48.1 | 35.0 | 75.7 / 74.1 | 0.899 | +0.25…+0.81 | 282.1 |
| all at 10, 7W | note 110 @0.5 | 26.8 / 55.1 / 74.9 | 47.4 / 55.6 / 64.7 | 38.7 | 110.7 / 103.1 | 0.822 | +0.64…+1.07 | 274.4 |

No point puts any sample below cond 0.05.

### What the baseline means for the plan

The EL84 pair runs **class A, small-signal, at every sign-off point and at every knob on 10.**
`VOLUME_KNOB_HEADROOM = 0.21` holds the EL84 grids so far below conduction that neither tube
gets near cut-off or the plate knee. Consequences, task by task:

- **Task 2** (feed PSU/sag `ip·cond`): the correction is an almost **constant** scale, not a
  cut-off effect: 2.5 % at 15W, **7.6 % at 7W** (the plan assumed idle `cond` ≈ 0.98; in 7W
  with today's warm sag offset it is 0.923). Folded with B2 as planned, it shifts the idle
  reference rather than the dynamics.
- **Sag never reaches the cold side.** The offset stays warm (+0.25 V minimum, all knobs at 10)
  because the reference is taken at nominal rails (review B2) *and* average plate current
  **falls** slightly under drive (79.8 → 75.7 mA at 15W), so there is no excess current to
  push it cold. B2 alone will not create sag; with B2 fixed the offset will rest near 0 and
  barely move. With only ~0.3 V of offset swing at P-CRANK, most of D1's 2.77 dB likely comes
  from elsewhere (grid-charge blocking is the first suspect); not measured here.
- **B+ moves 0.1 V** under playing. PSU droop is effectively absent at every setting.
- **Task 3** (second cut-off): `cond` never leaves 0.82…0.975, so today the function is a
  mild signal-dependent gain (a few % even-order shaping), not a crossover. Removing it
  (option a) mostly changes level by ~2.5 % (15W) / ~8 % (7W).
- **Task 4** (plate load line): with Raa = 8 kΩ, the largest difference current measured
  (38.7 mA) swings each plate by `38.7 mA × 8 kΩ / 4 ≈ 77 V`, so Vp never drops below
  ~197 V. The 40 V knee is never approached. Task 4 is inaudible at the current volume
  headroom; it only matters if the owner wants the power stage to be driven harder.

**Owner question raised by the baseline:** should the power stage ever leave class A? A real
cranked EL84 amp drives its output tubes into cut-off and plate clipping, which is where the
plan's Tasks 3–4 (and much of the "power-tube feel") live. Today that is prevented by design
(`VOLUME_KNOB_HEADROOM`, chosen so the grid clamp can't flatten dynamics). If yes, the task
is to re-balance headroom against the grid clamp first, and Tasks 3–4 become meaningful after
that. If no, Tasks 3–4 can be descoped to a cleanup (delete `softConductionCurve`, skip the
load line) and Task 2 reduces to the B2 fix.
