# State

_Updated 2026-11-01 (compact GUI: UTILITY knobs beside the meters, slim bars, one-row EFFECTS)_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPL-3.0-or-later, free + optional donations; no tagged
  release. **Tone frozen at `tone-v1`** (059b209), guarded by `tests/tone_lock_tests.cpp`.
- ctest 512 (312 pure C++ / 81 JUCE / 119 processor). Windows/MSVC: all passed at 508 (before the
  4 layout tests). Linux/GCC (2026-11-01): all pass but the 5 tone-lock cases (band deltas at ~−88 dB).
- GUI: Oxanium font; amp faceplate with satin pointer knobs, bat toggles, worn powder coat + steel photo
  layer; INPUT/THRESH beside the IN meter and OUTPUT/WIDTH beside the OUT meter (were the UTILITY
  card); EFFECTS is one row, BOOST | DELAY | REVERB, titles on the cards' top edges with the ON/SYNC/PP jewels,
  tight-dial knobs (SYNC turns DELAY TIME into the division knob); 20 px section bars; Settings overlay
  (two columns, standalone AUDIO INPUT list); IR LOADER opens on a drawn, worn 2x12 cab over a brick wall, LOAD IR at its top left.
- **Reactive glow**: the cab's piping lights with the output peak (eased 60 ms up / 120 ms down) and
  reddens with compression; the amp lamp's bloom breathes. Soft halo from a cached blurred mask
  (`CabinetView::glowMask`), owner-approved in the standalone after the stacked-stroke version banded.
- RT safety: `SilentRiotRTStress` harness (not a ctest); review closed by `docs/plans/2026-10-30-rt-safety-fixes.md`.
- Contributor issues #37–#48 open (GUI bugs, IR loader, installers, README screenshot).
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>` via exported
  `GIT_AUTHOR_EMAIL`/`GIT_COMMITTER_EMAIL` (local git config holds another address).

## Next
1. Owner: tag the tone-lock merge commit `tone-v1` and push the tag (`docs/dev/tone-lock.md`).
2. Owner, before going public: confirm the source/licence of `resources/textures/brick-wall.jpg` and
   `faceplate-steel.jpg` and credit them in CREDITS.md; confirm `tests/di_sample_82hz.raw` is
   self-recorded; enable private vulnerability reporting; create the `tone` label; delete stale branches
   (keep `main`, `feat/neural-hybrid-blend-sketch`); turn on GitHub "Keep my email addresses private".
3. Owner picks a donation platform → step 9 of `docs/plans/2026-10-28-open-source-docs-prep.md`.
4. Make `tests/rt_stress.cpp` build on MSVC (`_aligned_malloc`/`_aligned_free`), so full verify runs on Windows.
5. Re-shoot the manual's `resources/images/` (still Space Mono, and the old 2×2 EFFECTS) with `sr_editor_snapshot`.
6. E3: repoint the gate to `tests/fixtures/ir/bright.wav`, fix engine aliasing, owner A/B, `tone-v2`.
7. Owner questions: delay FEEDBACK default (−9 dB/repeat)? GRID I reads 0 — keep or read PI? Lamp colour,
   card weave, jewel size? Cab: light-theme white tolex OK, dim when CAB off, how much damage? Wear on
   EFFECTS knobs? Rename THRESH to GATE now that it sits outside a card? Wall brightness, `kSteelOpacityDark`? Tone lock on Linux: per-platform refs or a noise floor?

## Decisions
- Tone frozen (amp + cab + effects, steady state); re-calibration on hold; glitch fixes allowed while the lock passes.
- Never gate features, nag, or add a trial. Code/CLAUDE.md/handoffs beat DSP spec v2.13.
- Glow: blurred-mask halo, not stacked strokes (they banded); level eased so attacks don't flash.
- Delay never self-oscillates; IRs level-matched to the built-in cab; latency an exact integer.

## Known issues
- Full `verify.sh` can't build on Windows (`rt_stress.cpp`: `posix_memalign`). There: `verify.sh --quick`
  (python3 shim on PATH) + build the three ctest targets in `build-verify` + ctest.
- GitHub Actions jobs fail in seconds with no steps run (minutes block) since 2026-10-09.
- `signoff.py`: A1/B1 fail but accepted (frozen); E3 −37.9 dBc open. Sag idles warm; 15W↔7W settles audibly.
- Projects using an IR play louder (+5.4…+13.1 dB) since the IR level match; in CHANGELOG.
- GUI: small low-contrast text; EFFECTS + IR LOADER open = 716 px (was 922), no screen clamp.
- Some commits on `main` carry a non-noreply email, including GitHub web merges (account default email).
