# Handoff — power-section fixes

Record for `docs/plans/2026-10-23-power-section-fixes.md`. Every number here is a
measurement you can re-run; each task appends its own section below with the same tables.

Companion: `docs/records/reviews/review-dsp-2026-10-15.md` (DSP review). The combined fix order is in
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

### Power-stage telemetry (`docs/records/handoffs/handoff-power-section-fixes-probe.cpp`)

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

## Grid conduction corners (2026-10-23, `fix/ir-restore-and-samples` at `d3417b1`)

Where the coupling-cap charge sits today, for the `GRID_CHARGE_RATE` / GRID I notes in CLAUDE.md.
Re-run: build the probe as in its header, then `/tmp/sr_power_probe grid` (~30 s). 44.1 kHz / 4×,
IIR cab. Not a task of the plan; no source changed.

**Method.** Each corner starts on the seeded idle point (as `resetDSPState()` leaves it), runs
0.5 s of silence, then 2 s of signal with a 5 ms linear fade-in; statistics cover the 2 s. The
input passes the plugin's base-rate front end (INPUT trim, then the real `Boost` at 44.1 kHz) and a
4× linear-phase windowed-sinc upsampler standing in for JUCE's oversampler.

- **EL84 grid peak**: the larger of the two PI outputs after the master volume — the voltage
  `applyGridConduction()` compares with `GRID_CONDUCT_THRESH_EL84_N` (8.6 V). Margin is
  20·log10(8.6 V / peak).
- **Railed**: % of signal samples whose coupling-cap charge is ≥ 99 % of `MAX_BIAS_SHIFT_N`
  (≥ 1.485 V).
- **Nominal DI**: a sine at −18 dBFS RMS (0.178 peak, the gates' `sine:220:0.18`).
- **HOT**: gain 10, volume 10, INPUT +12 dB, BOOST on with LEVEL (+18 dB) and BRIGHT (+9 dB
  shelf) at max, TIGHT 0. With a 0.5-peak signal that is +21 dBFS RMS into the amp for a sine.
- **Square**: band-limited (odd harmonics below 19.8 kHz), plateau 0.5. **Chord**: open-string
  E chord (E2 A2 D3 G3 B3 E4), equal amplitudes, scaled to 0.5 peak.

Cross-check: 10 corners were also run through the real `processBlock` (`ProcessorHarness`, JUCE's
4× oversampler, 512-sample blocks) with temporary instrumentation of the same quantities
(reverted, not committed; re-checking means adding it back). EL84 conduction was counted by the
probe only.

| # | Corner | Grid peak probe / real | PI railed probe / real | V1b railed probe / real |
|---|---|---|---|---|
| 1–2 | default knobs, sine 110 / 220 @0.178 | 0.26 / 0.26, 0.27 / 0.27 V | 0.0 / 0.0 % | 0.0 / 0.0 % |
| 3 | gain 8 / vol 5, sine 110 @0.178 | 0.37 / 0.37 V | 39.9 / 39.9 % | 0.0 / 0.0 % |
| 4–5 | gain 10 / vol 5, sine 110 / 220 @0.178 | 0.61 / 0.61, 0.58 / 0.58 V | 53.4 / 53.4, 59.4 / 59.4 % | 0.0 / 0.0 % |
| 6 | gain 10 / vol 10, sine 110 @0.178 | 2.46 / 2.46 V | 53.4 / 53.4 % | 0.0 / 0.0 % |
| 7 | HOT, SHAPE 10, 15W, sine 1600 @0.5 | 5.73 / 5.74 V | 100.0 / 100.0 % | 91.8 / 91.7 % |
| 8 | HOT, SHAPE 0, 15W, sine 800 @1.0 | 5.20 / 5.19 V | 82.5 / 82.5 % | 72.1 / 72.0 % |
| 9 | HOT, SHAPE 10, 15W, **square 1600 @0.5** | **5.91 / 5.89 V** | 100.0 / 100.0 % | 84.3 / 85.0 % |
| 10 | HOT, SHAPE 10, 7W, sine 1600 @0.5 | 5.57 / 5.57 V | 100.0 / 100.0 % | 91.9 / 91.9 % |

The largest gaps are 0.02 V on the grid peak and 0.7 points on V1b's railed % (the PI's matched
exactly), both at the headline corner, row 9. The other HOT corners below (the chord, 110 Hz,
800 Hz @0.5, the other squares, the other 7W and SHAPE 0 cells) are from the probe alone.

**EL84 conduction: none.** No sample of any corner below crossed 8.6 V (`conducting` 0.00 % on
every row of the probe output). Hottest: a 1.6 kHz square at the HOT corner with SHAPE 10, 15W,
5.91 V, 3.3 dB under the threshold. That is the margin at the corners tried, not a proof for
every possible input.

### Nominal DI (SHAPE 5, 15W, no trim, no boost)

| Knobs (gain / volume) | Signal | EL84 grid peak | Margin | PI charge avg | PI railed | V1b railed |
|---|---|---|---|---|---|---|
| 6 / 5 (default) | sine 110 | 0.26 V | 30.2 dB | 0.21 V | 0.0 % | 0.0 % |
| 6 / 5 (default) | sine 220 | 0.27 V | 30.0 dB | 0.36 V | 0.0 % | 0.0 % |
| 8 / 5 | sine 110 | 0.37 V | 27.3 dB | 1.44 V | 39.9 % | 0.0 % |
| 8 / 5 | sine 220 | 0.35 V | 27.8 dB | 1.47 V | 47.1 % | 0.0 % |
| 10 / 5 | sine 110 | 0.61 V | 23.0 dB | 1.46 V | 53.4 % | 0.0 % |
| 10 / 5 | sine 220 | 0.58 V | 23.5 dB | 1.48 V | 59.4 % | 0.0 % |
| 10 / 10 | sine 110 | 2.46 V | 10.9 dB | 1.46 V | 53.4 % | 0.0 % |
| 10 / 10 | sine 220 | 2.31 V | 11.4 dB | 1.48 V | 59.4 % | 0.0 % |

### HOT corners — EL84 grid peak (margin to 8.6 V)

| Signal | 15W, SHAPE 0 | 15W, SHAPE 10 | 7W, SHAPE 0 | 7W, SHAPE 10 |
|---|---|---|---|---|
| sine 110 @0.5 | 5.01 V (4.7 dB) | 4.46 V (5.7 dB) | 4.89 V (4.9 dB) | 4.35 V (5.9 dB) |
| sine 800 @0.5 | 4.97 V (4.8 dB) | 5.08 V (4.6 dB) | 4.82 V (5.0 dB) | 4.91 V (4.9 dB) |
| sine 1600 @0.5 | 3.74 V (7.2 dB) | 5.73 V (3.5 dB) | 3.62 V (7.5 dB) | 5.57 V (3.8 dB) |
| sine 800 @1.0 | 5.20 V (4.4 dB) | 5.25 V (4.3 dB) | 5.06 V (4.6 dB) | 5.10 V (4.5 dB) |
| square 110 @0.5 | 4.87 V (4.9 dB) | 4.55 V (5.5 dB) | 4.74 V (5.2 dB) | 4.44 V (5.7 dB) |
| square 1600 @0.5 | 4.01 V (6.6 dB) | **5.91 V (3.3 dB)** | 3.90 V (6.9 dB) | 5.75 V (3.5 dB) |
| chord @0.5 | 4.81 V (5.0 dB) | 4.65 V (5.3 dB) | 4.69 V (5.3 dB) | 4.46 V (5.7 dB) |

### HOT corners — PI and V1b charge railed

Both sit upstream of SHAPE and the master volume, so SHAPE does not change them (identical to
the 0.1 % in every pair); 15W and 7W differ only through the shared supply.

| Signal | PI railed 15W / 7W | V1b railed 15W / 7W | PI charge avg |
|---|---|---|---|
| sine 110 @0.5 | 55.3 / 56.0 % | 61.6 / 61.7 % | 1.47 V |
| sine 800 @0.5 | 79.8 / 79.4 % | 75.9 / 76.1 % | 1.49 V |
| sine 1600 @0.5 | 100.0 / 100.0 % | 91.8 / 91.9 % | 1.50 V |
| sine 800 @1.0 | 82.5 / 82.4 % | 72.1 / 72.1 % | 1.49 V |
| square 110 @0.5 | 56.7–56.8 / 57.3 % | 52.8 / 52.8 % | 1.47 V |
| square 1600 @0.5 | 100.0 / 100.0 % | 84.3 / 84.3 % | 1.50 V |
| chord @0.5 | 61.4 / 62.2 % | 63.3 / 63.2 % | 1.48 V |

**What it says.** The EL84 grids never conduct at any corner tried, so GRID I reads 0 and the
EL84 charge stays 0. The rail that does engage is the PI's: from about gain 8 at a nominal DI,
and 55–100 % of samples under the HOT corners, where V1b's rails too (53–92 %; 0 at a nominal DI).
At default knobs with a nominal DI the PI charge stays off its rail (0.21–0.36 V average).
Whether the PI rail is load-bearing (the way the EL84 rail was before `VOLUME_KNOB_HEADROOM`)
is not measured here.

These replace figures quoted earlier from instrumentation that was not kept (hottest 5.55 V at
the 1.6 kHz sine corner; PI railed 40 % at gain 8 and 57 % at gain 10 at −18 dBFS; 60–100 % hot;
0.02 V at default knobs). Gain 8 agrees (39.9 % at 110 Hz). The rest differ, and the numbers
above are the ones to use: that sine corner measures 5.73 V (5.57 V in 7W) and the hottest
corner found is the square, 5.91 V; gain 10 is 53–59 % (110 / 220 Hz); hot is 55–100 %; the
default-knob PI charge is 0.21–0.36 V.
