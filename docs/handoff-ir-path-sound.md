# Handoff — IR-path sound fixes

Record for `docs/superpowers/plans/2026-10-26-ir-path-sound-fixes.md`. Every number here is a
measurement you can re-run; each task appends its own section below with the same tables.

## Task 0 — baseline (2026-10-27, branch `docs/ir-path-sound-plan` at `b576323`; `src/`, `tools/`
and `tests/` identical to `main` at `495c256`)

`bash scripts/verify.sh`: pass, 372/372. `sr_render` built per `AGENTS.md`
(`build/tools/sr_render_artefacts/Release/sr_render`). Unless a table says otherwise: 4× OS,
block 512, `--warmup-ms 250`, signal `note:220:0.1:2.0`, levels are ch0.

### Fixtures (`tools/make_ir_fixtures.py` → `tests/fixtures/ir/`)

48 kHz, 8192 samples, mono, 32-bit float WAV, peak-normalised to 1.0. "4th-order" is scipy's
`butter(4, …, btype="bandpass")`. The generator is deterministic (two runs, identical md5).

| File | Response |
|---|---|
| `neutral.wav` | Butterworth band-pass 80 Hz – 5 kHz |
| `dark.wav` | Butterworth band-pass 80 Hz – 3 kHz |
| `bright.wav` | Butterworth band-pass 80 Hz – 8 kHz, then RBJ peaking +4 dB, Q 1.0, at 3 kHz |

All three load through `loadUserIR()` (they pass `isValidUserIRFormat()` and `isLoadableIR()`);
the IR-mode renders below differ from the built-in cab, so the IR is in use.

### Sign-off gates (`tools/signoff.py`, 44.1 kHz, built-in cab)

| ID | Render | Value | Target | Status |
|---|---|---|---|---|
| A2 (M) | thd220_crank | −6.03 dBFS peak | ≤ −1.0 | PASS |
| A4 (M) | thd220_clean | 0.0 dB L/R | ≤ 0.1 | PASS |
| A1 | thd220_clean | −31.58 LUFS | −17.5…−14.5 | FAIL |
| B1 | thd220_clean | 0.991 % THD | 1.0…5.0 | FAIL |
| B2 | thd220_edge | 7.857 % THD | 6.0…12.0 | PASS |
| B3 | thd220_crank | 20.498 % THD | 18.0…35.0 | PASS |
| B4 | thd220_edge | H2 ≥ H3 | true | PASS |
| D5 (M) | note_crank | −10.94 dBFS at 300 ms | ≥ −60 | PASS |
| D1 | note_crank | 2.72 dB sag depth | 1.5…4.0 | PASS |
| E1 | idle_crank | −200.0 dBFS | ≤ −90 | PASS |
| E2 | idle_crank | −268.93 dBFS DC | ≤ −80 | PASS |
| E3 (M) | alias_crank | −37.89 dBc | ≤ −85 | FAIL |

12 gates, 3 fail, 1 MUST-PASS fail (E3) — the same set as `.agents/STATE.md`. E3's bright IR is
still `OPTIONAL_BRIGHT_IR`, so `alias_crank` renders with the built-in cab.

### Level: IR vs built-in cab (`tools/sr_irpath.py`)

RMS from 0.5 s on, dBFS. Brackets: IR minus built-in.

| Point | Rate | Built-in | neutral | dark | bright |
|---|---|---|---|---|---|
| P-CLEAN | 44.1k | −38.81 | −49.68 (−10.87) | −47.51 (−8.70) | −53.40 (−14.59) |
| P-CLEAN | 48k | −38.81 | −49.43 (−10.62) | −47.18 (−8.38) | −53.31 (−14.50) |
| P-CLEAN | 96k | −38.84 | −46.35 (−7.51) | −44.15 (−5.32) | −50.14 (−11.31) |
| P-EDGE | 44.1k | −26.71 | −37.56 (−10.85) | −35.39 (−8.68) | −41.28 (−14.57) |
| P-EDGE | 48k | −26.72 | −37.32 (−10.61) | −35.08 (−8.36) | −41.20 (−14.48) |
| P-EDGE | 96k | −26.73 | −34.23 (−7.49) | −32.03 (−5.30) | −38.02 (−11.29) |
| P-CRANK | 44.1k | −11.04 | −21.43 (−10.38) | −19.28 (−8.24) | −25.11 (−14.07) |
| P-CRANK | 48k | −11.05 | −21.19 (−10.14) | −18.97 (−7.92) | −25.02 (−13.97) |
| P-CRANK | 96k | −11.07 | −18.14 (−7.07) | −16.00 (−4.93) | −21.87 (−10.80) |

Rate dependence, level at the rate minus level at 48 kHz (dB):

| Cab | 44.1k (CLEAN / EDGE / CRANK) | 96k (CLEAN / EDGE / CRANK) |
|---|---|---|
| built-in | −0.00 / +0.01 / +0.00 | −0.03 / −0.02 / −0.02 |
| neutral | −0.25 / −0.24 / −0.24 | +3.08 / +3.09 / +3.05 |
| dark | −0.32 / −0.32 / −0.31 | +3.03 / +3.04 / +2.97 |
| bright | −0.09 / −0.09 / −0.09 | +3.16 / +3.17 / +3.15 |

### Startup (`sr_irpath.py --startup`: P-EDGE, 44.1 kHz, block 512, `--warmup-ms 0`, neutral)

Mean-square energy above 7 kHz (8th-order Butterworth high-pass over the whole render, then
10 ms windows), dB. The absolute level is low because the OT low-pass at 7 kHz sits in front of
both cabs; only the differences matter.

| t (ms) | IR | CAB off | Built-in | IR − CAB off |
|---|---|---|---|---|
| 0 | −115.3 | −115.3 | −124.8 | −0.0 |
| 10 | −113.6 | −113.5 | −123.8 | −0.0 |
| 20 | −114.4 | −114.1 | −123.8 | −0.3 |
| 30 | −116.3 | −114.0 | −124.2 | −2.3 |
| 40 | −118.2 | −113.1 | −123.5 | −5.0 |
| 50 | −121.6 | −112.9 | −122.9 | −8.7 |
| 60 | −127.3 | −112.3 | −122.2 | −15.1 |
| 70 | −132.1 | −113.4 | −123.3 | −18.6 |
| 80 | −140.7 | −112.1 | −122.4 | −28.6 |
| 90–140 | −139.9…−141.1 | −112.0…−113.1 | | −27.8…−28.6 |
| settled 0.5–1.5 s | −140.7 | −112.6 | −122.6 | −28.1 |

For the first ~20–30 ms IR mode carries CAB-off >7 kHz energy (within 0.3 dB); it reaches
its settled level at ~80 ms, 28 dB below CAB off.

### Repeatability (`sr_irpath.py --repeat 6 --block 64 512 2048`: P-EDGE, 44.1 kHz, warmup 0, neutral)

One unloaded reference render, then 6 renders with 4 busy-loop processes on the 4 cores.

| Block | Bit-identical | First difference (ms) | Max \|diff\| |
|---|---|---|---|
| 64 | 0/6 | 7.30, 11.66, 11.66, 14.56, 16.01, 26.17 | 0.004 – 0.027 |
| 512 | 1/6 | 11.66 (×5) | 0.0145 |
| 2048 | 6/6 | — | 0 |

A second loaded run at block 512 gave 0/6. Without any load, 6 renders at block 512 were not
identical either: 4 of 5 differed from the first one from 23.27 ms. Every first difference falls
at frame `k × block + 2`, i.e. on a block boundary: the swap from JUCE's pass-through to the real
engine lands at whichever block the background thread has finished by.

### Against the plan's findings table

| Finding | Plan | Measured here | Agrees? |
|---|---|---|---|
| 1, IR ~11 dB quiet | neutral −10.9 dB (`sr_render`) | neutral −10.87 dB at P-CLEAN/44.1k, −10.85 at P-EDGE, −10.38 at P-CRANK; dark −8.7, bright −14.6 | Yes for neutral. The plan's −53.5 vs −42.6 (gain 0.3) and −14.8 vs −4.9 (full crank) are other knob positions and are not re-measured here. The gap depends on the IR: 6 dB between dark and bright |
| 2, rate dependence | +3.1 dB at 96k vs 48k; built-in 0.01 dB | +3.05…+3.17 dB for all fixtures and points; built-in ≤ 0.03 dB. Also −0.1…−0.3 dB at 44.1k | Yes |
| 3, first IR plays un-cabbed | first ~20 ms at CAB-off >7 kHz energy, 27 dB above settled by ~80 ms | 0–20 ms within 0.3 dB of CAB off; settled 28 dB below it at 80 ms | Yes |
| 5, wall-clock swap | on `495c256` 6 loaded renders matched an unloaded one; swap moves with block size | at block 512 1/6 and 0/6 matched, and unloaded renders also differ; 64 never matched; 2048 always matched | **No** for "matched": on this machine IR renders at block 512 are not repeatable even unloaded. Agrees that it depends on block size and that repeatability isn't guaranteed. Task 1's verify step is the cure either way |

Findings 4 and 6 have no Task 0 measurement.

### Built-in-cab baseline for later tasks

Every `signoff_gates.json` render, exactly as `signoff.py` runs it (44.1 kHz), plus the built-in
cab `note:220:0.1:2.0` at P-CLEAN/P-EDGE/P-CRANK × 44.1/48/96 kHz: 19 `.f32` renders, ~16 MB,
not committed. Saved at Task 0 in the session scratchpad (`…/scratchpad/baseline/`); a later
session without that directory re-creates it by checking out `b576323`, building `sr_render`
and running `--save-baseline`.

```bash
SR=build/tools/sr_render_artefacts/Release/sr_render
python3 tools/sr_irpath.py --render-bin $SR --save-baseline <dir>   # once, before the task
python3 tools/sr_irpath.py --render-bin $SR --diff-baseline <dir>   # after; exit 1 on any difference
```

At Task 0, `--diff-baseline` against a fresh save: 19/19 bit-identical, and the saved signoff
renders are byte-identical to `signoff.py`'s own.

## Task 1 — install the active IR synchronously in `prepareToPlay` (2026-10-27)

`CabinetSim::installIRNow()` hands the IR to JUCE and then calls `Convolution::prepare()` on the
same thread. JUCE 7.0.12 (`juce_Convolution.cpp`, `Convolution::Impl::prepare`) does exactly what
the plan says: `messageQueue->pimpl->popAll()` runs every queued load on the calling thread
(under `popMutex`, so it waits out a load the background thread is in the middle of), then
`engineQueue->prepare(spec)` rebuilds the engine from that IR and the new spec, and the engine is
moved straight into `currentEngine` with `previousEngine = nullptr` and the crossfade mixer reset:
no pass-through and no crossfade. A pending `BufferTransfer` is cleared first
(`BufferTransfer::clear()`); otherwise the next `processBlock` would queue the same IR again and
crossfade to it at a wall-clock time. `applyUserIRIndex()` / `applyIRIndexParam()` take
`IRLoad::Now` / `IRLoad::Async`; only `prepareToPlay` passes `Now` (state restore can run beside
`processBlock`, so it stays `Async`). `sr_render` now runs one startup order for both cab modes:
`--ir` is loaded (`loadUserIR()` needs nothing prepared), then `prepareToPlay`, then the knobs.
It exits with an error when the IR does not load (it used to render the built-in cab silently).

No mechanism differed from the plan. One JUCE detail matters for anyone writing the next test:
`ConvolutionEngineFactory` keeps the last IR it built, so a *re*-prepare of a convolution that
has already installed an IR rebuilds that IR, not a pass-through. Finding 3 only shows on the
first IR a convolution sees, e.g. a project restored into a fresh instance.

Tests (written first, failing before the change): `installIRNow plays the IR on the very first
block` (10 kHz through a 64-tap moving average, one block, no sleeps: −0.004 dB before,
≤ −30 dB required and met), `installIRNow drops a pending async load instead of replaying it`
(0.46 s of blocks with sleeps after a queued unit impulse: −0.004 dB before; with the
`clear()` call removed it reads −18.1 dB and fails, so it pins the clear), and
`Processor: a session restored before prepare plays its IR from the first block` (>7 kHz energy,
IR minus CAB off: first block 0.0 dB before, −57.8 dB after, settled −53.1 dB).
`user_ir_state_tests.cpp` also runs the past-the-list and index-0 cases through `IRLoad::Now`.
`bash scripts/verify.sh`: pass, 375/375 (290 / 72 / 13).

### Startup (`--startup`, same settings as Task 0)

| t (ms) | IR before | IR after | CAB off | IR − CAB off before | after |
|---|---|---|---|---|---|
| 0 | −115.3 | −141.6 | −115.3 | −0.0 | −26.3 |
| 10 | −113.6 | −141.9 | −113.5 | −0.0 | −28.3 |
| 20 | −114.4 | −141.2 | −114.1 | −0.3 | −27.1 |
| 30 | −116.3 | −142.0 | −114.0 | −2.3 | −28.0 |
| 40 | −118.2 | −141.9 | −113.1 | −5.0 | −28.8 |
| 50 | −121.6 | −140.9 | −112.9 | −8.7 | −28.0 |
| 60 | −127.3 | −140.2 | −112.3 | −15.1 | −27.9 |
| 70 | −132.1 | −141.0 | −113.4 | −18.6 | −27.6 |
| 80–140 | −139.9…−141.1 | −139.9…−141.1 | −112.0…−113.1 | −27.8…−28.6 | −27.8…−28.6 |
| settled 0.5–1.5 s | −140.7 | −140.7 | −112.6 | −28.1 | −28.1 |

No CAB-off burst: IR mode sits at its settled level from the first window. (The first window
reads 2 dB above the rest because the analysis high-pass starts on the note's onset.)

### Repeatability (`--repeat 6 --block 64 512 2048`, 4 busy loops on 4 cores)

| Block | Before: bit-identical | After: bit-identical |
|---|---|---|
| 64 | 0/6 | 6/6 |
| 512 | 1/6 (0/6 on a second run; unloaded runs differed too) | 6/6 |
| 2048 | 6/6 | 6/6 |

No second cause turned up: with the IR installed before the first block, nothing in the render
depends on JUCE's background thread any more.

### Built-in cab and level table

`--diff-baseline` against Task 0's saved renders: **19/19 bit-identical** (every sign-off render
plus the nine built-in level renders), so the sign-off gates are unchanged.

The default level table (RMS from 0.5 s, `--warmup-ms 250`) is identical to Task 0's, and so are
the renders behind it: all 27 IR renders are bit-identical to Task 0's from the first sample of
the measured region (JUCE's swap finished inside the 250 ms warm-up then). With `--warmup-ms 0`
(the `--repeat` renders, P-EDGE, neutral), the new renders differ from Task 0's reference only in
the first 64–100 ms (64 ms at block 512, 68 ms at 64, 100 ms at 2048), the old pass-through and
crossfade, and are bit-identical after it.
