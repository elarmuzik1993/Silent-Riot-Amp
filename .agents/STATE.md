# State

_Updated 2026-10-27_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone). `bash scripts/verify.sh` passes: 408/408 ctest
  (308 pure C++ / 81 JUCE / 19 processor, via `tests/processor_harness.h`).
- DSP review `docs/review-dsp-2026-10-15.md`: B1, B4–B8, S1–S7 fixed; B2, B3 (tone-changing), S8 open.
- Power plan `docs/superpowers/plans/2026-10-23-power-section-fixes.md`: Tasks 0–1 done
  (`docs/handoff-power-section-fixes.md`). EL84 grids never conduct (hottest 5.91 V vs 8.6 V);
  the PI charge rails from ~gain 8.
- IR plan `docs/superpowers/plans/2026-10-26-ir-path-sound-fixes.md` done (record:
  `docs/handoff-ir-path-sound.md`): synchronous IR install in `prepareToPlay`, IRs level-matched
  to the built-in cab (`IRLevel.h`), faded cab-mode switches (`CabModeSwitcher.h`), 5 ms fade on
  IRs cut at 8192 samples. Built-in cab bit-identical.
- **Projects that use an IR play louder** (+5.4…+13.1 dB by IR and rate; neutral ~+10.5 dB at
  44.1/48 kHz). Release-note it.

## Next (one PR per step, each with a test)
1. Owner: delay FEEDBACK default decays 9 dB/repeat (was 3.4); keep, or remap the knob?
2. Owner: GRID I reads 0 in normal play. Keep, or read the PI grid?
3. Phase 2 (tone-changing; owner listening check, then ONE re-calibration of D1/A1/B1/P-*):
   power plan Task 2 with B2 (replace the tautological idle test; re-pin
   `idle_operating_point_tests.cpp`), then B3. First measure whether the PI rail is load-bearing.
4. EL84s leaving class A (owner leans yes, unconfirmed): after B2/B3 raise
   `VOLUME_KNOB_HEADROOM`, check D1 at knob 10, then power plan Tasks 3–5 (Task 4 with S8).
5. E3 last (owner: confirm "repoint the gate, then fix the engine"); repoint it to
   `tests/fixtures/ir/bright.wav` (`spur_crank_ir` already renders it, −23.1 dBc).
6. IR loader UI plan (see Known issues).
7. Owner: GitHub Actions fails every run since 2026-10-09 (check billing); delete stale branches.

## Decisions
- DSP spec v2.13 is background only; code, CLAUDE.md, handoffs win. `COMPETITIVE_ROADMAP.md` is live.
- Tone-neutral fixes first, tone-changing ones together, so the amp is re-calibrated once.
- Delay never self-oscillates; sync ceiling 2000 ms, TIME knob 20–1000 ms (owner).
- IR restore is laxer than load: refusing a silent IR would drop it and every later slot.
- Latency is an exact integer, constant across cab modes; the IIR path is padded to match.
- PSU's one-sample current lag is intended (spec §3.11).
- IR mode matches the built-in cab's level, not the file's (owner D1); the 8192 cap stays (D2);
  saved state keeps the raw IR.
- `verify.sh` builds into `build-verify/`; sr_render is not built by CI or verify.

## Known issues
- `signoff.py` (44.1k/4×) fails A1 −31.6 LUFS, B1 0.99%, E3 −37.9 dBc (MUST-PASS); 9 pass.
  IR gates are report-only. Rates above 384k effective drift −0.2…+1.2 dB (cause unknown).
- Sag idles warm (B2) and never goes cold; a live 15W↔7W switch settles audibly.
- `sr_bloom.py` gain 0.625 rebounds ~4 dB at 25 ms; `sr_thump.py` gain 1.0: louder in, quieter out.
- Cab-mode switches dip rather than crossfade (~6 ms at block 64, ~95 ms at 4096). Readiness
  fails open: an IR picked while an older one is loaded starts on the older one until JUCE's
  crossfade (still cabbed).
- Bright IRs play 1.2–1.5 dB under the built-in cab (band-power match); unequal stereo IRs are
  off the reference on a mono bus. The 8192 cap still loses long rings (fade only removes the step).
- A stereo IR with a dead channel 1 is refused on load even on a mono bus.
- IR loader panel: no scroll past 8 (`IRLoaderPanel.cpp:116`), `.ir` accepted but unreadable,
  AIFF/FLAC refused, no remove or way back to the built-in cab, no refresh after automation or
  state load; `IR_INDEX` is positional.
- Settings → About paints a hard-coded "Licensed — Full Version" (`SettingsPanel.cpp:162`).
