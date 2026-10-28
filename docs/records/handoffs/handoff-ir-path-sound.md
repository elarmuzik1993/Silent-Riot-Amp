# Handoff — IR-path sound fixes

Record for `docs/plans/2026-10-26-ir-path-sound-fixes.md`. Every number here is a
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

## Task 2 — level-match the IR path (2026-10-27)

**Design.** `src/dsp/cabinet/IRLevel.h` (pure C++, header-only) holds the metric: the power
average of |H(f)|² over log-spaced points, `IR_LEVEL_POINTS_PER_OCT` = 12 per octave, from
`IR_LEVEL_BAND_LO_HZ` = 100 Hz to `IR_LEVEL_BAND_HI_HZ` = 5 kHz (69 points, both edges included;
the top edge is clamped to 0.45 × the IR's rate, so an 8 kHz IR is measured to 3.6 kHz over 64
points). H is a direct DFT per point (double-precision phasor), channel-averaged. An all-zero IR
has no level and is left unscaled.

The reference is the same metric for `cab_presence × cab_rolloff × cab_notch`, designed by the
very `Biquad::makePeakEQ / makeLowPass / makeNotch` calls `FilterBank::recalcAll()` makes, on the
`CAB_*` constants: **−0.888 dB** (the plan's −0.90, rounded). It is designed at a **fixed** rate,
`IR_LEVEL_REF_DESIGN_SR` = 176.4 kHz, not at the IR's native rate: the processor runs these
biquads at the oversampled rate (base × 2/4/8 = 88.2…384 kHz), where they read −0.881…−0.890 dB,
while at a 44.1 kHz IR's rate the bilinear warp would give −0.852 dB, a response the built-in cab
never has; and a reference tied to the session rate would change every IR's gain on an
oversample switch. The test pins −0.90 ± 0.05 dB and checks every OS rate of 44.1/48 kHz is
within 0.02 dB of it.

`CabinetSim::matchBuiltInCabLevel()` (private) runs in every load path — `loadIRFromBuffer`,
`installIRNow`, and the test-only `loadStockIR` / `loadIRFromFile` — on the copy handed to the
convolution: one gain `sqrt(ref / P)` for all channels, `P` the channel-averaged band power at
the IR's native rate (at most the two channels `Stereo::yes` convolves). `queueConvolutionLoad()`
passes `Normalise::no`. **Where it runs:** `loadIRFromBuffer` scales *before*
`bufferTransfer.set()` on the message thread; `processBlock` only picks the scaled buffer up and
queues it, so nothing is added to the audio thread. `installIRNow` scales before queueing, in
`prepareToPlay`. `userIRs` and saved state keep the raw audio (all callers already pass a copy).

Two guards beyond the plan, both in the helper: an IR with no sample at or above
`IR_TRIM_THRESHOLD` is left unscaled, so a silent IR that restore accepts still plays the silence
it was saved as (JUCE's trim cuts it to nothing) instead of being raised into audibility; and the
gain is capped at `IR_LEVEL_MAX_GAIN_DB` = 40 dB, so a wrong or sub-only file with next to
nothing in band is not raised far into the output tanh. Any audible IR's scaled peak stays above
the trim threshold: where the cap binds the gain is > 1; otherwise band power ≤ (N · peak)², so
the scaled peak ≥ sqrt(ref) / N ≥ 0.90 / 8192 = 1.1e-4 at the length cap.

**Low-rate IRs.** The reference is the built-in cab over the *same* band the IR is measured over
(`referenceBandPower(irRate)` = the cab over `bandFor(irRate)`), so a clamped band is compared
like for like. It equals the full-band −0.888 dB from 11.1 kHz up; at 8 kHz (100 Hz–3.6 kHz,
below the cab's 5.5 kHz roll-off) it is −0.717 dB. `fullBand()` names the unclamped band.

**Consequences worth knowing.** JUCE's absolute −80 dBFS trim now acts on the level-matched copy,
so relative to the raw file the cut sits at −80 dB minus the match gain: −62…−71 dB for the
peak-normalised fixtures (turned down 9–18 dB), i.e. slightly more of a quiet onset or tail is
trimmed than before. And on a mono bus only IR channel 0 is convolved while the gain comes from
the channel average, so a stereo IR with unequal channels plays there off the reference by
channel 0's distance from the average (+2.0 dB for a right channel 6 dB down); mono IRs and
equal-channel stereo IRs are exact.

The fixtures are peak-normalised, so their raw band levels are high and the match turns them
*down*: neutral +12.84 dB → gain −13.73 dB, dark +16.83 → −17.72, bright +8.49 → −9.37.

`isSafeToNormaliseIR()` / `MAX_IR_CHANNEL_ENERGY` stay, with rewritten comments. Finding: only
the NaN/Inf half is still load-bearing — a NaN makes the band power NaN, the gain falls back to 1
and the NaN reaches the convolution. The energy ceiling no longer protects anything (the metric
sums in double and scales a huge IR *down*; JUCE's float sum used to overflow to a gain of 0), so
an "overflowing" IR would now play; it is kept so the set of IRs that load and restore is
unchanged. The cabinet test that showed the overflow case dying now shows a NaN sample instead.

**Tests** (written first; the three level tests failed before the change):
- `ir_level_tests.cpp` (`SilentRiotTests`, 9 cases): unit impulse 0 dB, ×2 +6.02 dB; the band is
  69 points; an 80 Hz–5 kHz band-pass designed at 48 and 96 kHz reads −0.561 / −0.574 dB; the
  reference −0.888 dB; all-zero → no level, gain 1; channel averaging, one gain, the 40 dB cap;
  8 kHz IR → 3.6 kHz top edge, 64 points; an 8 kHz IR is matched to the clamped-band reference
  (−0.717 dB, unit impulse gain = that); the cab biquads' own impulse response at 48 kHz needs
  −0.03 dB of gain.
- `cabinet_ir_tests.cpp`: the neutral fixture through a real `juce::dsp::Convolution` via
  `installIRNow`, its played impulse response measured with the same metric — before −11.67 dB
  (44.1 kHz) / −8.32 dB (96 kHz), after **−0.978 / −0.919 dB** (reference −0.888, ≤ 0.3 required);
  and a stereo copy with the right channel 6 dB down keeps its 6.02 dB balance with the channel
  average at −0.886 dB, and the caller's buffer is bit-unchanged after both `installIRNow` and
  `loadIRFromBuffer`. Fixtures are found through a `SOURCE_DIR` compile definition, now also set
  for `SilentRiotJuceTests` and `SilentRiotProcessorTests`.
- `processor_ir_tests.cpp`: neutral fixture vs built-in cab, P-EDGE, 220 Hz sine at 0.1 — before
  IR −10.85 dB, after **−0.19 dB** (±1.5 required); and saved state keeps the raw IR —
  `loadUserIR(neutral.wav)` → `getStateInformation` → `userIRsFromValueTree` is bit-equal to the file.
- Changed: `IRs that can't be level-matched or trimmed silence IR mode` (the sane-IR section now
  uses a 64-tap moving average, whose 1 kHz sidelobe plays a 0.5 sine at 0.173 after the match,
  required inside 0.05…0.3, clear of pass-through's 0.5; overflow section replaced by a NaN one,
  see above). The two Task 1 `installIRNow` tests now require ≤ −20 dB at 10 kHz
  instead of ≤ −30: the 64-tap moving average is raised 4.8 dB by the match and plays 10 kHz at
  −27.7 dB, while a pass-through or a unit impulse plays it at 0 / −0.9 dB.

`bash scripts/verify.sh`: pass, 388/388 (299 / 74 / 15).

Behaviour change, IR mode after minus before (from the table below): neutral fixture +10.3…+10.7 dB at 44.1/48 kHz and +7.0…+7.4 dB at 96 kHz; across the three fixtures +5.4 dB (dark, 96 kHz) to +13.1 dB (bright, 44.1 kHz).

### Level: IR vs built-in cab (`sr_irpath.py`, same settings as Task 0)

IR minus built-in, dB, before (Task 0/1) → after:

| Point | Rate | neutral | dark | bright |
|---|---|---|---|---|
| P-CLEAN | 44.1k | −10.87 → **−0.20** | −8.70 → **+0.39** | −14.59 → **−1.47** |
| P-CLEAN | 48k | −10.62 → **−0.14** | −8.38 → **+0.41** | −14.50 → **−1.41** |
| P-CLEAN | 96k | −7.51 → **−0.13** | −5.32 → **+0.43** | −11.31 → **−1.43** |
| P-EDGE | 44.1k | −10.85 → **−0.19** | −8.68 → **+0.40** | −14.57 → **−1.46** |
| P-EDGE | 48k | −10.61 → **−0.14** | −8.36 → **+0.42** | −14.48 → **−1.40** |
| P-EDGE | 96k | −7.49 → **−0.13** | −5.30 → **+0.43** | −11.29 → **−1.42** |
| P-CRANK | 44.1k | −10.38 → **−0.12** | −8.24 → **+0.41** | −14.07 → **−1.27** |
| P-CRANK | 48k | −10.14 → **−0.07** | −7.92 → **+0.42** | −13.97 → **−1.22** |
| P-CRANK | 96k | −7.07 → **−0.06** | −4.93 → **+0.44** | −10.80 → **−1.24** |

The built-in column is unchanged (−38.81 / −26.71 / −11.04 dBFS at 44.1k, as Task 0). Target
±1.5 dB: **met for all 27 cells**, but bright at P-CLEAN/P-EDGE 44.1k is only 0.03–0.04 dB inside
it. That residue is the metric, not a defect: the match equalises a flat-weighted band average,
and bright's +4 dB at 3 kHz counts there in full while a 220 Hz note's energy sits mostly below
2 kHz, so bright plays the note a little under the built-in cab (and dark, the opposite way, a
little over). The fixtures' spread shrank from 5.9 dB (dark − bright) to 1.9 dB.

Rate dependence, level at the rate minus level at 48 kHz (dB), after (before: ≈ −0.1…−0.3 at
44.1k and +3.0…+3.2 at 96k):

| Cab | 44.1k (CLEAN / EDGE / CRANK) | 96k (CLEAN / EDGE / CRANK) |
|---|---|---|
| built-in | −0.00 / +0.01 / +0.00 | −0.03 / −0.02 / −0.02 |
| neutral | −0.05 / −0.05 / −0.05 | −0.02 / −0.01 / −0.01 |
| dark | −0.02 / −0.01 / −0.02 | −0.01 / −0.00 / −0.00 |
| bright | −0.05 / −0.05 / −0.05 | −0.05 / −0.04 / −0.04 |

Target ±0.3 dB: met, worst 0.05 dB.

### Built-in cab and sign-off

`--diff-baseline` against Task 0's saved renders: **19/19 bit-identical**. The 12 built-in gates
are the Task 0 values exactly (A2 −6.029, A1 −31.583, B1 0.991, B2 7.857, B3 20.498, D5 −10.94,
D1 2.72, E1 −200.0, E2 −268.93, E3 −37.89; same 3 fails, 1 MUST-PASS).

New report-only IR-mode gates (`tools/signoff_gates.json`, target `null` → CALIBRATE; `ir` paths
are now resolved against the repo root by `signoff.py`). "Before" is the Task 1 binary
(`ae8593c`) on the same table:

| Gate | Render | Field | Before | After | Built-in |
|---|---|---|---|---|---|
| A2-IR | thd220_crank_ir (neutral) | peak dBFS | −13.98 | **−4.43** | −6.03 |
| B3-IR | thd220_crank_ir (neutral) | THD % | 23.78 | **21.45** | 20.50 |
| D5-IR | note_crank_ir (neutral) | sustain 300 ms dBFS | −21.92 | **−11.44** | −10.94 |
| D1-IR | note_crank_ir (neutral) | sag depth dB | 3.07 | **2.77** | 2.72 |
| S-IR | spur_crank_ir (bright, 11 kHz sine 0.5) | total spur dBc | −23.14 | **−23.14** | (E3 −37.89) |

The limiter holds: the crank peak stays 3.4 dB under A2's −1 dBFS ceiling with ~7–13 dB more drive
into the output tanh. B3 and D1 *fall* slightly and land next to the built-in values: the output
tanh now compresses IR mode as much as the built-in cab, where before IR mode reached it 10 dB
quieter and skipped that compression. The spur figure is unchanged to 0.01 dB, so the 4× limiter
oversampler adds nothing measurable at the new level; its −23 dBc is the engine's aliasing shaped
by the bright IR (which passes the in-band spurs and cuts the 11 kHz carrier), i.e. E3's problem,
not the limiter's. `alias_crank` (E3) still renders `OPTIONAL_BRIGHT_IR` → built-in cab; `S-IR` is
not E3 and does not replace it.

## Task 3 — fade every cab-mode switch, never play un-cabbed (2026-10-27)

**Design.** `src/dsp/cabinet/CabModeSwitcher.h` (pure C++) owns the *active* cab mode (Off /
built-in / IR); `requestedCabMode(cabOn, useIR)` is only a request. States Idle → (WaitingForIR)
→ FadingOut → Silent → commit → FadingIn → Idle. The gain is an integer level / 128, so every
per-sample step is exactly 1/128 and the fades end on exactly 0 and 1. A request during a fade-in
fades out from the current level; a request reversed before the commit fades back in on the mode
that never stopped playing, without committing; WaitingForIR has no timeout (a debug build logs
once past 1 s). The commit can only be at a block start (the mode decides the whole block: the
in-loop tanh, the cab biquads' use, the IR block, `iirPathAlign`, `cabSim.setEnabled`), so after
the fade-out the rest of that block is silent.

`processBlock` asks once per block: `prime()` while IR is requested but not active, then
`beginBlock(requested, cabSim.hasRealIR())`; on a commit `enterCabMode()` runs #33's resets
(convolution + limiter oversampler on entering IR mode, `iirPathAlign` on leaving it) and
`holdSilence(cabFadeInHold())` keeps the gain at 0 while the entered path comes out of reset.

**Where the gain is applied:** once, at base rate, on the finished cab-path output — after the IR
block (convolution + oversampled limiter) and after `iirPathAlign` on the other branch, before
delay/reverb/widener/OUTPUT trim. It is the one point both paths pass, so nothing from either path
reaches the output un-faded; it is after both limiters, so the dip only ever lowers the level; and
delay/reverb tails ring on through the dip instead of being cut. While Idle `applyGain` touches
nothing, so the steady state is bit-identical. An oversample switch fading at the same moment
multiplies with it (its fade runs earlier, on the downsampled amp): both move ≤ 1/128 per sample,
so the product is a deeper or longer dip, never a step.

**Interactions.** `prepareToPlay` resets the switcher to the requested mode with no fade (an IR
selected there was installed by Task 1, so it is real; were it not, it would start on the built-in
cab and wait). The first block after `prepareToPlay` and the first after `tailsStale` (power-off,
tuner mute, host bypass, `reset()`) switch **without** a fade, running the commit's resets: the
last output was silence (or bypass's dry signal), so there is nothing to fade from, and hosts set
their knobs after prepare (`sr_render` does: without this every CAB-off render started with a
dip). They still never enter an IR that isn't installed. On `tailsStale` with IR mode active the
convolution and limiter are cleared, as `lastIrMode = false` did; `lastIrMode` is gone. IR → IR
loads are not mode changes: JUCE's 50 ms crossfade handles them, as before.

### JUCE facts (7.0.12, `juce_Convolution.cpp`), verified in the source

- `getCurrentIRSize()` is `currentEngine->getIRSize()`, the length of the trimmed, **resampled**
  IR. The pass-through is the factory's initial 1-sample impulse whose `originalSampleRate` is the
  constructor's default 44.1 kHz, resampled to the session rate by `roundToInt(max(1, n·dest/src))`:
  **1 sample at 44.1/48 kHz, 2 at 96 kHz, 4 at 192 kHz.** So the plan's readiness test
  `getCurrentIRSize() > 1` reads the pass-through as a real IR at 96 kHz and up (confirmed: the new
  "fresh convolution" test fails at 96k and 192k with it). `hasRealIR()` instead compares the size
  with the one the queued IR's engine will have, predicted by `engineSizeFor(trimmedLength(...))`, a
  replica of `fixNumChannels` + `trimImpulseResponse` + `resampleImpulseResponse`, pinned against
  the real engine for 5 session × 4 IR rates. It **fails open** (review of 047cc65): ready when
  the size is the predicted one *or* anything but the pass-through's, so a misprediction (a JUCE
  change, rounding, a prepare racing a load) can never leave IR mode unreachable — the switcher
  has no timeout. A test forces a wrong prediction and still reaches ready once the real engine
  is in. Trade-off: any real engine counts, so an IR picked while an older real IR is installed
  enters IR mode on the older IR until JUCE's crossfade to the new one (still cabbed). The
  `queued*` fields behind the prediction are atomics: they are written by the load paths and read
  from `prepare()`, which a host may call off the message thread. Consequence: an IR whose engine is exactly the
  pass-through's size (a silent IR → 1 zero sample, or 1–4 taps) reads as ready at once and hands
  over through JUCE's own crossfade, as before. After an IR → IR load of the same length it reads
  true while the older IR still plays; JUCE's crossfade covers that.
- The real engine is installed only inside `processSamples` (`installPendingEngine` when no
  crossfade is running), which then starts a 50 ms crossfade from the previous engine. Hence
  `prime()`: picks up `bufferTransfer`, runs `convolution.process()` on a silent scratch block
  allocated in `prepare()`. The convolution's state it advances is cleared at the commit.
- `Convolution::reset()` is audio-thread safe: outer `Mixer::reset()`, `CrossoverMixer::reset()`
  (smoother set to 1, not smoothing → only the current engine plays), `currentEngine->reset()`
  (zeroes buffers), and the previous engine is handed to the background queue for deletion
  (`FixedSizeFunction`, no allocation; destroyed in place only if that queue is full). A pending
  engine not yet installed is untouched and installs later with a fresh crossfade — which is why
  the commit waits for `hasRealIR()` rather than relying on the reset.

### Deviations from the plan (all measured)

1. **Readiness** is the predicted engine size, not `> 1` (above).
2. **The built-in cab's biquads run in every mode** (only the built-in mode uses their output), and
   are *not* reset at the commit. With the plan's reset, the restarted biquads (the 400 Hz notch,
   Q 2, rings in) stepped past the steady state through the fade-in: CAB off → on 0.00265 and
   IR → built-in 0.00223 vs ≤ 0.00234 steady. Running them keeps their state live; the built-in
   steady state is unchanged bit for bit (same operations), and in the other modes the output is
   unchanged.
3. **A hold at gain 0 after the commit** (`cabFadeInHold`): entering IR mode, the IR's peak
   position (session-rate samples, from the trim start) + 32, capped at 1024, + the limiter's
   latency (12); leaving IR mode, `iirPathAlign`'s delay (12); Off ↔ built-in, none. Without it a
   reset convolution reached its IR's peak *during* the fade-in, and the cleared `iirPathAlign`
   played 12 zeros then the note mid-cycle: steps of 0.0069–0.0089 (first IR, 3 kHz sinc IR,
   peak at tap 63), 0.0044 (IR CAB off → on), 0.0024 (IR CAB on → off) vs ~0.0022 steady.
4. **The first block after prepare / `tailsStale` switches without a fade** (above).

### Tests

- `cab_mode_switcher_tests.cpp` (`SilentRiotTests`, 9 cases, blocks 1/7/64/512/4096 where the
  plan asks): unchanged mode → gain exactly 1, no commit over 10k samples; built-in → Off reaches
  exactly 0, commits once, returns to 1, step ≤ 1/128; → IR not ready → full gain, no commit until
  ready; never ready → never commits (100k samples); reversed mid-fade-out and reversed at gain 0
  → no commit; a request mid-fade-in fades out from the current gain; `holdSilence` (0/12/300);
  `requestedCabMode`.
- `cabinet_ir_tests.cpp` (3): a fresh convolution has no real IR at 44.1/48/96/192 kHz (and not
  after a queued load); an IR queued outside IR mode becomes real through `prime()` within 1 s
  (bounded poll; 44.1/96 kHz), and then cuts 10 kHz ≤ −20 dB; `hasRealIR()` matches JUCE's engine
  after `installIRNow` and after a re-prepare at another rate (5 × 4 rates); and (+1) with the
  prediction deliberately wrong, the pass-through still reads not ready and the installed IR
  still reads ready (44.1/96 kHz) — with an exact-match-only rule that test fails.
- `processor_ir_tests.cpp` (4), P-EDGE, `sine(220, 0.1)`, switch mid-note after 0.6 s; max
  |x[i] − x[i−1]| from the switch to 20 ms past the dip vs the steady state (0.2 s before, last
  0.15 s), 1 % tolerance for the steady state's own drift:

| Switch | Steady (before / after) | Before Task 3 | After |
|---|---|---|---|
| first IR of the session (async load, 3 kHz sinc IR) | 0.00213 / 0.00227 | 0.141 | 0.00227 |
| IR → built-in (state restore, IR_INDEX 0) | 0.00218 / 0.00213 | 0.0312 | 0.00213 |
| built-in, CAB on → off | 0.00213 / 0.00233 | 0.00423 | 0.00233 |
| built-in, CAB off → on | 0.00234 / 0.00213 | 0.0415 | 0.00213 |
| IR (neutral), CAB on → off | 0.00218 / 0.00233 | 0.0324 | 0.00233 |
| IR (neutral), CAB off → on | 0.00234 / 0.00218 | 0.0657 | 0.00219 |

  First IR, >7 kHz energy per 10 ms window: built-in −123.0 dB, settled IR −163.7 dB. While
  waiting (before the fade-out) the worst window is −122.1 dB, the built-in cab's; in the 20 ms
  from 2 ms past the end of the fade-in **−162.7 dB** (before Task 3, from the load: **−82.9 dB**,
  the pass-through plus the hard cut). IR mode started 25–37 ms after the load (JUCE's background
  build; varies run to run). The 2 ms offset is deliberate: a linear fade's end is a corner in the
  envelope, broadband on its own (−99 dB here, the fade's and not the cab's), and the analysis
  high-pass rings on it ~1 ms. Plus: a cab mode set after prepare, before the first block, is
  bit-identical to one set before prepare.

  `bash scripts/verify.sh`: pass, **405/405** (308 / 78 / 19). #33's B6 tests pass unchanged.

### Measurements

- `--diff-baseline` against Task 0's renders: **19/19 bit-identical**. `signoff.py`: all 17 gates
  identical to Task 2 (12 built-in + 5 IR report-only), and all 13 sign-off renders bit-identical
  to Task 2's.
- Level table: identical to Task 2, all 36 renders (27 IR + 9 built-in) bit-identical.
- `--startup` and `--repeat 6 --block 64 512 2048` against a Task 2 (`e195431`) `sr_render`:
  identical tables, the three startup renders and the three repeat references bit-identical;
  repeat 6/6 at every block size. (Before the first-block rule, the CAB-off startup render
  differed: it opened with a built-in → Off dip.)

**Dip length** at 44.1 kHz: 128 down + the rest of that host block + the hold + 128 up — about
6 ms at block 64, 14.5 ms (built-in ↔ Off) to 17 ms (into the 3 kHz IR) at 512, ~95 ms at 4096.
A true crossfade would need the amp loop to output both paths and a second downsampler; not done.

## Task 4 — fade truncated IRs (2026-10-27)

**Design.** `fadeTruncatedTail(buffer, rate)` (`UserIRState.{h,cpp}`) multiplies the last
`IR_TRUNCATION_FADE_MS` = 5 ms (`Constants.h`; N = round(rate · 5 ms): 221 samples at 44.1 kHz,
240 at 48, 480 at 96, 960 at 192, at most 3840 at the 768 kHz ceiling) of every channel by a
raised cosine, sample i of N by 0.5·(1 + cos(π·(i+1)/N)): strictly falling from just under 1 to
exactly 0 on the last sample. Every sample before the fade is untouched. A buffer shorter than
the fade is faded over its whole length (same curve, N = its length) so it still ends at 0; an
empty one is left alone. `readUserIRFromFile` never hits the short case: it calls the fade only
when the file's `lengthInSamples` exceeded `MAX_USER_IR_SAMPLES`, on the 8192-sample copy, after
the read and before `isLoadableIR()`. A file at or under the cap is kept bit for bit. The faded
copy is what `userIRs` and saved state hold (the plan's stated exception to "store the raw
audio"; the stored copy was already truncated), so restore needs no flag and is unchanged:
projects saved before this keep their unfaded cut.

**Tests** (`user_ir_state_tests.cpp`, `SilentRiotJuceTests`, written first):
- the fade at 44.1/48/96/192 kHz on a 2-channel 8192-sample ringing buffer: every pre-fade sample
  bit-identical; inside it the gain (faded / original) matches the raised cosine to 1e-6, falls
  strictly, ends < 1e-6 and is 0.5 ± 0.02 mid-fade; `IR_TRUNCATION_FADE_MS` pinned at 5;
- a 100-sample buffer at 48 kHz (~2 ms) is faded over its whole length (first gain > 0.99,
  strictly falling, last < 1e-6); 0- and 1-sample buffers;
- `readUserIRFromFile` on a temp 32-bit float WAV of 8192 + 1808 samples: 8192 kept, body
  bit-identical to the file, tail equal to `fadeTruncatedTail` of the cut copy, last sample ~0;
  files of 8192, 8191 and 300 samples: bit-identical to the file, last sample included. With the
  call in `readUserIRFromFile` removed, this case fails.

**Before / after** (scratch probe, not committed: the neutral fixture's band-pass designed at each
rate, generated 1 s long and cut to 8192 samples; a second IR adds a +12 dB, Q 20 resonance at
110 Hz, i.e. a ~60 ms ring, like a cab or room mode). "Step" is the last kept sample, dB re peak;
"dev" is the max |H| deviation from the uncut IR on the 69 metric points, below / above 1 kHz.

| IR | Rate (cap) | Hard cut: step, dev <1k / >1k | Faded: step, dev <1k / >1k |
|---|---|---|---|
| neutral | 44.1k–96k (186–85 ms) | −331…−167 dB, 0.00 / 0.000 | 0, 0.00 / 0.000 |
| neutral | 192k (43 ms) | −100 dB, 0.00 / 0.000 | 0, 0.00 / 0.000 |
| ringing | 44.1k (186 ms) | −84 dB, 1.12 / 0.002 | 0, 1.15 / 0.000 |
| ringing | 48k (171 ms) | −61 dB, 1.22 / 0.011 | 0, 1.22 / 0.000 |
| ringing | 96k (85 ms) | −67 dB, 2.21 / 0.006 | 0, 2.16 / 0.000 |
| ringing | 192k (43 ms) | −50 dB, 2.16 / 0.041 | 0, 2.11 / 0.000 |

What the fade fixes is the cut itself: the step goes to 0 and the broadband ripple it spreads
above 1 kHz (up to 0.04 dB here) goes to 0.000. What it can't fix is the ring that the cap throws
away: the 1–2 dB at the resonance is the 8192-sample cap (decision D2), with or without the fade.
The band level, and so the level match, moves ≤ 0.02 dB between the two. As the plan said: low
impact, a few lines. The fixtures are exactly 8192 samples, so nothing they measure changes.

**Measurements.** `sr_render` rebuilt: `--diff-baseline` against Task 0's renders **19/19
bit-identical**. `bash scripts/verify.sh`: pass, **408/408** (308 / 81 / 19).

## Done when — final check (2026-10-27, after Task 4, `0d619a6`)

Every measurement re-run on the Task 4 `sr_render` (44.1 kHz sign-off, `sr_irpath.py` defaults).

| Target | Measured | Result |
|---|---|---|
| Tasks 1–4 one per commit, `verify.sh` green, before/after table each | `ae8593c`, `e195431`, `4a9db2e`, `0d619a6`; 375 → 388 → 405 → 408/408; a table in each section above | **Met** on the branch; "merged" waits on the PR |
| IR within ±1.5 dB of the built-in cab, every fixture | 27 cells, −1.47…+0.44 dB (bright P-CLEAN 44.1k −1.47, 0.03 dB inside) — identical to Task 2 | **Met** (narrowly for bright) |
| IR within ±0.3 dB across 44.1/48/96 kHz | worst 0.05 dB (built-in itself 0.03) | **Met** |
| IR renders repeatable | `--repeat 6 --block 64 512 2048` under 4 busy loops: 6/6, 6/6, 6/6 bit-identical | **Met** |
| No switch clicks | Task 3 processor tests (6 switches: max step ≤ steady state, in `verify.sh`) | **Met** — a dip, not a crossfade (Known issues) |
| Never plays un-cabbed | `--startup`: IR's >7 kHz energy at its settled level from the first 10 ms window (−130.9 vs settled −130.0 dB; CAB off −115.3); first-IR-of-session processor test −162.7 dB vs settled −163.7 | **Met** |
| Built-in-cab gates bit-identical to Task 0 | `--diff-baseline` 19/19 bit-identical; 12 gates identical (A2 −6.029, A4 0.0, A1 −31.583, B1 0.991, B2 7.857, B3 20.498, B4 true, D5 −10.94, D1 2.72, E1 −200.0, E2 −268.93, E3 −37.89) | **Met** — same 3 fails (A1, B1, E3 MUST-PASS) as Task 0, none from this plan |
| IR-variant gates recorded | A2-IR −4.432 dBFS, B3-IR 21.445 %, D5-IR −11.44 dBFS, D1-IR 2.77 dB, S-IR −23.14 dBc (report-only, unchanged since Task 2) | **Met** |
| `CLAUDE.md` | level match, `installIRNow`, mode switcher, loading paragraph, truncation fade; pitfalls `Normalise::yes` and `installIRNow` only from `prepareToPlay`; both chain diagrams end in the cab-mode fade | **Met** |
| `.agents/STATE.md` | rewritten, incl. the louder-IR behaviour change | **Met** |

`--startup` absolute IR levels are ~10.7 dB above Task 1's table (−130 vs −140.7 dB settled):
that is Task 2's level match, not a burst; IR − CAB off is now −15.6…−18.1 dB in every window.
