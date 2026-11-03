# State

_Updated 2026-11-02 (repo public; v0.4.0 ready to tag; sanitizers job covers the processor tests)_

## Now
- Silent Riot v0.4.0 (JUCE 7 VST3/Standalone, Windows + Linux), GPL-3.0-or-later, free; public on GitHub.
  No tags or releases exist yet. CHANGELOG `[0.4.0] - 2026-11-02` (with Known issues); README Install
  points at `releases/latest`.
- Tone frozen at `tone-v1` (to be tagged on e56ab9f, the tone-lock merge), guarded by `tests/tone_lock_tests.cpp`.
- Presets removed (amp opens on defaults); save/load/curated presets are #72.
- CI runs again since the repo went public. Linux + Windows builds, pluginval and Windows tests pass;
  the Linux tone lock now passes (silent floor −65 dB, fix/tone-lock-linux). Sanitizers build all 3 test targets under
  ASan/UBSan, skip the tone lock, and excuse only JUCE's libjpeg (`tests/sanitizers/ubsan.supp`).
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>` via exported
  `GIT_AUTHOR_*`/`GIT_COMMITTER_*`. Cloud sessions can't push tags, create labels or dispatch workflows.

## Next
1. Owner, local clone: `git tag -a tone-v1 e56ab9f`, `git tag -a v0.4.0` on `main`, `git push origin tone-v1 v0.4.0`.
   `release.yml` then builds and publishes Linux .tar.gz + Windows .zip. If the run fails, read its logs.
2. When the release has both downloads: post the Discussions announcement (draft was given in chat:
   what it is, features, getting started, known issues #43/#45, how to help); enable Discussions first.
3. Owner settings: create the `tone` label; private vulnerability reporting; "Keep my email addresses
   private"; delete stale branches (keep `main`, `feat/neural-hybrid-blend-sketch`).
4. Fix #43 (IR LOADER panel stale after automation/state load) and #45 (window not clamped to screen).
5. Make `tests/rt_stress.cpp` build on MSVC (`_aligned_malloc`), so full verify runs on Windows.
6. Later: donation platform (`docs/plans/2026-10-28-open-source-docs-prep.md` step 9); installers (#46/#47);
   macOS (#69); replace the two Pexels textures with own/CC0 photos; E3 aliasing fix → owner A/B → `tone-v2`.

## Decisions
- Tone frozen (amp + cab + effects, steady state); glitch fixes allowed while the lock passes. Never
  regenerate references without the owner's A/B.
- Free forever: never gate features, nag, or add a trial. No AI attribution anywhere (AGENTS.md).
- #43/#45 ship in 0.4.0 as documented known issues rather than block the first release.
- Sanitizer suppressions are for third-party code only; ours must stay clean.
- Code/CLAUDE.md/docs/dev/handoffs beat DSP spec v2.13. Delay never self-oscillates; IRs level-matched.

## Known issues
- Tone lock: references come from MSVC; GCC differs only below −70 dB, so the silent floor is −65 dB on all platforms. `verify.sh` on Linux: 513/513.
- Full `verify.sh` can't build on Windows (`rt_stress.cpp`): use `--quick` + the three ctest targets.
- `signoff.py`: A1/B1 fail but accepted (frozen); E3 −37.9 dBc open.
- Projects using an IR play louder (+5.4…+13.1 dB) since the IR level match (CHANGELOG).
- GUI: small low-contrast text; no screen clamp (#45). Builds are unsigned (SmartScreen warning).
- 39 old commits carry a non-noreply email; email privacy only affects new ones.
