# State

_Updated 2026-11-02 (presets removed; save/load/curated presets tracked in #72)_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPL-3.0-or-later, free + optional donations; no tagged
  release. **Tone frozen at `tone-v1`** (059b209), guarded by `tests/tone_lock_tests.cpp`.
- ctest 513/513 on Windows/MSVC (2026-11-01, after the compact layout). On Linux/GCC the 5 tone-lock
  cases fail (band deltas ~−88 dB, per PR #66; unverified here).
- Presets: the 5 factory presets, the header browser and `presetIndex` are gone; the amp opens on its
  defaults. Save/load/curated presets: #72. The 5 old settings live on as test-local data in `tone_lock_tests.cpp`.
- GUI: Oxanium font; amp faceplate with satin pointer knobs, bat toggles, worn powder coat + steel photo
  layer, polished dark-gunmetal Phillips corner screws (top right lightly rusted with a short drip, bottom
  left flecked); INPUT/THRESH beside the IN meter, OUTPUT/WIDTH beside the OUT meter (were the UTILITY card);
  EFFECTS one row (BOOST | DELAY | REVERB), titles and ON/SYNC/PP jewels inside each plate, tight-dial knobs;
  20 px section bars; Settings overlay; IR LOADER (LOAD IR at its top left) opens on a drawn, worn 2x12
  cab over a brick wall whose piping glows with the output peak and reddens with compression.
- Docs: `CLAUDE.md` is rules + a map; detail lives in `docs/dev/`.
- RT safety: `SilentRiotRTStress` harness (not a ctest). Contributor issues #37–#48 open.
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>` via exported
  `GIT_AUTHOR_*`/`GIT_COMMITTER_*` (local git config holds another address).

## Next
1. Owner: tag the tone-lock merge commit `tone-v1` and push the tag (`docs/dev/tone-lock.md`).
2. Owner, before going public: confirm source/licence of `resources/textures/brick-wall.jpg` and
   `faceplate-steel.jpg`, credit them in `attribution/CREDITS.md`; confirm `tests/di_sample_82hz.raw` is
   self-recorded; enable private vulnerability reporting; create the `tone` label; delete stale branches
   (keep `main`, `feat/neural-hybrid-blend-sketch`); turn on "Keep my email addresses private".
3. First release, in order: (a) item 2; (b) #41 is moot (browser removed; revisit with #72), fix #43 (IR
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
  Delay never self-oscillates; IRs level-matched to the built-in cab; latency an exact integer.
- Never gate features, nag, or add a trial. Code/CLAUDE.md/docs/dev/handoffs beat DSP spec v2.13.
- Glow: blurred-mask halo, not stacked strokes (they banded); level eased so attacks don't flash.
- Hardware parts (screws, grille, caps) use fixed colours in both themes. Screw look tuned by the owner in
  the standalone: Phillips, darker gunmetal, rust kept light (`ScrewLook` in `renderPlateCache()`).

## Known issues
- Full `verify.sh` can't build on Windows (`rt_stress.cpp`): use `verify.sh --quick` + the three ctest targets.
- GitHub Actions never runs (minutes block) since 2026-10-09; verify locally. IR projects play louder
  (+5.4…+13.1 dB) since the IR level match (CHANGELOG).
- `signoff.py`: A1/B1 fail but accepted (frozen); E3 −37.9 dBc open. Sag idles warm; 15W↔7W settles audibly.
- GUI: small low-contrast text; EFFECTS + IR LOADER open = 716 px, no screen clamp.
- Some commits on `main` carry a non-noreply email, including GitHub web merges (account default email;
  `gh pr merge --author-email` rejects the noreply address). Fix: owner turns on email privacy.
