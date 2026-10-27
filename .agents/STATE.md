# State

_Updated 2026-10-27_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone). `bash scripts/verify.sh` passes: static checks +
  Release build + 375/375 ctest (290 pure C++ / 72 JUCE / 13 `SilentRiotProcessorTests`, which
  drives the real processor via `tests/processor_harness.h`).
- Work list: DSP review `docs/review-dsp-2026-10-15.md` (line refs against `b8e20a9`). Fixed: B1, B4–B8,
  S1–S7. Open: B2, B3 (tone-changing), S8. IRs: load rejects unsafe (NaN/Inf/energy overflow) and
  inaudible (a channel all below −80 dBFS) files; restore rejects only unsafe ones.
- Power plan `docs/superpowers/plans/2026-10-23-power-section-fixes.md`: Tasks 0–1 done; baseline and
  grid-conduction corners in `docs/handoff-power-section-fixes.md` (probe `… -probe.cpp`, `grid` mode).
  EL84s stay class A and their grids never conduct at any corner tried (hottest 5.91 V vs 8.6 V);
  the PI charge rails from ~gain 8.
- IR plan `docs/superpowers/plans/2026-10-26-ir-path-sound-fixes.md` (against `495c256`): none built.

## Next (one PR per step, each with a test)
1. Owner: delay FEEDBACK default now decays 9 dB/repeat (was 3.4); keep, or remap the knob?
2. Owner: GRID I reads 0 in normal play (EL84 grids never conduct). Keep, or read the PI grid?
3. IR plan Tasks 0–1 (tone-neutral), then 2–4. Independent of Phase 2: Task 2 matches IR mode to
   the built-in cab's filters, so a later re-calibration carries over. Task 0's `bright.wav` can
   replace E3's placeholder IR.
4. Phase 2 (changes the tone; owner listening check, then ONE re-calibration of D1/A1/B1/P-*):
   plan Task 2 with B2 (replace the tautological idle test; update the pinned numbers in
   `idle_operating_point_tests.cpp`), then B3. Measure whether the PI rail is load-bearing first.
5. EL84s leaving class A (owner leans yes: cranked volume should break up the power tubes;
   not yet confirmed). After B2/B3: raise `VOLUME_KNOB_HEADROOM` so knob 10 reaches the EL84
   grids and check pick dynamics survive (D1 at knob 10), then plan Tasks 3–4 (Task 4 with S8)
   and 5, then the one re-calibration. Each step ends in an owner listening check.
6. E3 last (owner: confirm "repoint the gate, then fix the engine"); its bright IR is a placeholder.
7. Owner: GitHub Actions fails every run since 2026-10-09 (check billing); delete stale branches.

## Decisions
- DSP spec v2.13 is background only; code, CLAUDE.md, handoffs win. `COMPETITIVE_ROADMAP.md` is live.
- Tone-neutral fixes first, tone-changing ones together, so the amp is re-calibrated once.
- Delay must never self-oscillate (owner). Sync ceiling 2000 ms, TIME knob stays 20–1000 ms (owner).
- Restore is laxer than load on purpose: refusing a silent IR would drop it and every later slot.
- Latency: exact integer report, constant across cab modes; the IIR path is padded to match.
- PSU's one-sample current lag is intended (spec §3.11).
- IR mode is level-matched to the built-in cab (owner, IR plan D1): existing projects with an IR
  get ~10 dB louder. The 8192-sample IR cap stays; truncated tails get a fade (D2).
- `verify.sh` builds into `build-verify/`; sr_render is not built by CI or verify.

## Known issues
- `signoff.py` (44.1k/4×): A1 −31.6 LUFS (band −17.5…−14.5), B1 0.99% (floor 1.0%), E3 −37.9 dBc
  (≤ −85, MUST-PASS) fail; the other 9 pass (E1 −200 dBFS). Other rates within 0.03 dB up to 384k
  effective; above that −0.2…+1.2 dB, cause unknown (not grid charge).
- Sag idles warm (+0.77 V 15W / +1.03 V 7W, B2) and never goes cold under drive.
- Live 15W↔7W switch settles audibly (the idle point moves); a prepare in 7W is silent.
- `sr_bloom.py` gain 0.625 rebounds ~4 dB at 25 ms; `sr_thump.py` gain 1.0: louder in, quieter out.
- A stereo IR with a dead channel 1 is refused on load even for mono-bus users (owner may prefer
  keeping channel 0). `readUserIRFromFile` has no 3-channel-file test (only `CabinetSim`'s path).
- Settings → About paints a hard-coded "Licensed — Full Version" (`SettingsPanel.cpp:162`).
- IR mode: ~11 dB quieter than the built-in cab, +3 dB per doubling of host rate, un-cabbed for
  the first ~20 ms after `prepare` (settled by ~80 ms), hard mode switches. IR plan Tasks 1–3.
- IR loader panel, not planned: the list can't scroll past 8 (`IRLoaderPanel.cpp:116`), `.ir` is
  accepted but unreadable while AIFF/FLAC are refused, no remove or way back to the built-in cab,
  and it never refreshes after automation or a state load.
