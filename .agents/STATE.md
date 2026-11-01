# State

_Updated 2026-11-01 (compact GUI layout merged; faceplate screws; release punch list)_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPL-3.0-or-later, free + optional donations; no tagged
  release. **Tone frozen at `tone-v1`** (059b209), guarded by `tests/tone_lock_tests.cpp`.
- ctest 513/513 on Windows/MSVC (2026-11-01, after the compact layout). On Linux/GCC the 5 tone-lock
  cases fail (band deltas ~−88 dB, per PR #66; unverified here).
- GUI: Oxanium font; amp faceplate with satin pointer knobs, bat toggles, worn powder coat + steel photo
  layer, polished dark-gunmetal Phillips corner screws (top right lightly rusted with a short drip, bottom
  left flecked); INPUT/THRESH beside the IN meter, OUTPUT/WIDTH beside the OUT meter (were the UTILITY card);
  EFFECTS one row (BOOST | DELAY | REVERB), titles and ON/SYNC/PP jewels inside each plate, tight-dial knobs;
  20 px section bars; Settings overlay; IR LOADER (LOAD IR at its top left) opens on a drawn, worn 2x12
  cab over a brick wall whose piping glows with the output peak and reddens with compression.
- Docs: `CLAUDE.md` is rules + a map; component/DSP/IR detail lives in `docs/dev/` (gui-architecture,
  dsp-notes, ir-loader, pitfalls, testing).
- RT safety: `SilentRiotRTStress` harness (not a ctest). Contributor issues #37–#48 open.
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>` via exported
  `GIT_AUTHOR_*`/`GIT_COMMITTER_*` (local git config holds another address).

## Next
1. Owner: tag the tone-lock merge commit `tone-v1` and push the tag (`docs/dev/tone-lock.md`).
2. Owner, before going public: confirm source/licence of `resources/textures/brick-wall.jpg` and
   `faceplate-steel.jpg`, credit them in `attribution/CREDITS.md`; confirm `tests/di_sample_82hz.raw` is
   self-recorded; enable private vulnerability reporting; create the `tone` label; delete stale branches
   (keep `main`, `feat/neural-hybrid-blend-sketch`); turn on "Keep my email addresses private".
3. First release, in order: (a) item 2; (b) fix #41 (preset name vs knobs, host preset changes), #43 (IR
   panel stale after automation/state load), #45 (window taller than screen); check and close #38/#40
   (look fixed in code, unverified); (c) item 5 + README hero (#48); (d) bump `CMakeLists.txt` version
   (suggested v0.4.0: IR projects get louder), date the CHANGELOG *Unreleased*; (e) go public: standard
   Actions runners are free on public repos, so the `v*` tag can run `release.yml`; else package locally.
   Installers (#46/#47), macOS (#69), donations, E3 wait until after.
4. Owner picks a donation platform → step 9 of `docs/plans/2026-10-28-open-source-docs-prep.md`.
5. Re-shoot the manual's `resources/images/` (still Space Mono and the old 2×2 EFFECTS) with
   `sr_editor_snapshot`.
6. Make `tests/rt_stress.cpp` build on MSVC (`_aligned_malloc`/`_aligned_free`), so full verify runs on Windows.
7. E3: repoint the gate to `tests/fixtures/ir/bright.wav`, fix engine aliasing, owner A/B, `tone-v2`.
8. Owner questions: delay FEEDBACK default? GRID I reads 0 — keep or read PI? Lamp colour, card weave,
   jewel size? Cab: light-theme tolex, dim when CAB off, damage amount? Wear on EFFECTS knobs? Rename
   THRESH to GATE now that it sits outside a card? Wall brightness, `kSteelOpacityDark`? Tone lock on
   Linux: per-platform refs or a noise floor?

## Decisions
- Tone frozen (amp + cab + effects, steady state); re-calibration on hold; glitch fixes allowed while the lock passes.
- Never gate features, nag, or add a trial. Code/CLAUDE.md/docs/dev/handoffs beat DSP spec v2.13.
- Glow: blurred-mask halo, not stacked strokes (they banded); level eased so attacks don't flash.
- Hardware parts (screws, grille, caps) use fixed colours in both themes. Screw look tuned by the owner in
  the standalone: Phillips, darker gunmetal, rust kept light (`ScrewLook` in `renderPlateCache()`).
- Delay never self-oscillates; IRs level-matched to the built-in cab; latency an exact integer.

## Known issues
- Full `verify.sh` can't build on Windows (`rt_stress.cpp`: `posix_memalign`). There: `verify.sh --quick`
  (python3 shim on PATH, Git Bash path form) + build the three ctest targets in `build-verify` + ctest.
- GitHub Actions jobs never run (minutes block) since 2026-10-09; verify locally.
- `signoff.py`: A1/B1 fail but accepted (frozen); E3 −37.9 dBc open. Sag idles warm; 15W↔7W settles audibly.
- Projects using an IR play louder (+5.4…+13.1 dB) since the IR level match; in CHANGELOG.
- GUI: small low-contrast text; EFFECTS + IR LOADER open = 716 px (was 922), no screen clamp.
- `sr_editor_snapshot scale=` takes the UI-scale *index* (3 = 1.5×), not a factor.
- Some commits on `main` carry a non-noreply email, including GitHub web merges (account default email;
  `gh pr merge --author-email` rejects the noreply address). Fix: owner turns on email privacy.
