# State

_Updated 2026-10-28_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPLv3, open source + optional donations. `bash
  scripts/verify.sh` passes: 408/408 ctest (308 pure C++ / 81 JUCE / 19 processor).
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>`; the cloud env's
  `GIT_AUTHOR_EMAIL`/`GIT_COMMITTER_EMAIL` beat `git -c`, so export both to override.
- DSP review `docs/records/reviews/review-dsp-2026-10-15.md`: B1, B4–B8, S1–S7 fixed; B2, B3 (tone-changing), S8 open.
- Power plan `docs/plans/2026-10-23-power-section-fixes.md`: Tasks 0–1 done (handoff in
  `docs/records/handoffs/`). EL84 grids never conduct (5.91 V vs 8.6 V); PI charge rails ≥ gain 8.
- IR plan `docs/plans/2026-10-26-ir-path-sound-fixes.md` done (`docs/records/handoffs/
  handoff-ir-path-sound.md`). **Projects that use an IR play louder** (+5.4…+13.1 dB; CHANGELOG).
- Docs plan `docs/plans/2026-10-28-open-source-docs-prep.md` 1–8 done; `verify.sh` also checks SPDX
  headers, repo links (`scripts/check_links.py` + allowlist), `sr_editor_snapshot`'s sources.

## Next (one PR per step, each with a test)
1. Owner: delete stale remote branches (all but `main`, `feat/neural-hybrid-blend-sketch`; the
   proxy refuses). Create the `tone` issue label. Make the repo public; pick a
   donation platform, then docs plan step 9: `.github/FUNDING.yml`, README "Support" link and an
   About-panel link (roadmap Phase C).
2. Owner: delay FEEDBACK default decays 9 dB/repeat (was 3.4): keep or remap? GRID I reads 0
   in normal play: keep, or read the PI grid?
3. Phase 2 (tone-changing; owner listening check, then ONE re-calibration of D1/A1/B1/P-*):
   power Task 2 with B2 (replace the tautological idle test; re-pin `idle_operating_point_tests.cpp`), then B3. First measure whether the PI rail is load-bearing.
4. EL84s leaving class A (owner leans yes): after B2/B3 raise `VOLUME_KNOB_HEADROOM`, check D1
   at knob 10, then power Tasks 3–5 (Task 4 with S8; owner picks the 7W option for Task 5).
5. E3 last (owner: confirm "repoint the gate, then fix the engine"); repoint it to
   `tests/fixtures/ir/bright.wav` (`spur_crank_ir` already renders it, −23.1 dBc).
6. IR loader UI plan (see Known issues).
7. Owner: GitHub Actions fails every run since 2026-10-09; recheck once public (free runners).

## Decisions
- Open source + optional donations (2026-10-26): never gate features, nag, or add a trial.
- DSP spec v2.13 is background only; code, CLAUDE.md, handoffs win. `ROADMAP.md` is live.
- Tone-neutral fixes first, tone-changing ones together, so the amp is re-calibrated once.
- Delay never self-oscillates; sync ceiling 2000 ms, TIME knob 20–1000 ms (owner).
- Licence GPL-3.0-or-later; `docs/dev/` + `docs/manual/` track the code, `design/` + `records/` frozen.
- Latency is an exact integer, constant across cab modes; PSU's one-sample current lag is intended.
- IR mode matches the built-in cab's level (owner D1); the 8192 cap stays (D2); state keeps raw IRs;
  restore is laxer than load (refusing a silent IR would drop it and every later slot).
- `verify.sh` builds into `build-verify/`; sr_render is not built by CI or verify.

## Known issues
- 13 commits on `main` (#33, #35) and the docs plan's first commit carry the env identity; old
  pre-rewrite commits stay reachable via closed PRs' `refs/pull/*` (only GitHub Support fixes it).
- `signoff.py` (44.1k/4×) fails A1 −31.6 LUFS, B1 0.99%, E3 −37.9 dBc (MUST-PASS); 9 pass.
  IR gates are report-only. Rates above 384k effective drift −0.2…+1.2 dB (cause unknown).
- Sag idles warm (B2) and never goes cold; a live 15W↔7W switch settles audibly. `sr_bloom.py`
  gain 0.625 rebounds ~4 dB at 25 ms; `sr_thump.py` gain 1.0: louder in, quieter out.
- IR path: cab-mode switches dip (~6–95 ms by block size); a new IR starts on the old one until
  JUCE's crossfade; bright IRs 1.2–1.5 dB under the built-in cab; the 8192 cap loses long rings;
  a stereo IR with a dead channel 1 is refused even on a mono bus (details in the IR handoff).
- IR loader panel: no scroll past 8 (`IRLoaderPanel.cpp:118`), `.ir` accepted but unreadable,
  AIFF/FLAC refused, no remove or way back to the built-in cab, no refresh after automation or
  state load; `IR_INDEX` is positional. Wheel scroll truncates `deltaY*3` to 0 rows (likely cause).
- GUI (found writing the manual): tooltip em dashes in `PluginEditor.cpp` aren't UTF-8; ◂ ▸ ♭ ♯
  render as boxes (not in the bundled fonts); a fresh instance shows "Classic Crunch" on default
  knobs and host `presetIndex` changes don't load a preset; no on-screen oversample control.
