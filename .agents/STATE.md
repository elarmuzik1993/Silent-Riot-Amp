# State

_Updated 2026-11-03 (v0.4.0 released; Discussions announcement posted)_

## Now
- Silent Riot v0.4.0 (JUCE 7 VST3/Standalone, Windows + Linux), GPL-3.0-or-later, free; public on GitHub.
  Release `v0.4.0` is published with Linux .tar.gz and Windows .zip (+ .sha256); `tone-v1` tags the
  frozen tone (e56ab9f). Release body is CHANGELOG `[0.4.0]`; README Install points at `releases/latest`.
- Tone frozen at `tone-v1`, guarded by `tests/tone_lock_tests.cpp`. The silent floor is −60 dB on all
  platforms (MSVC references; GCC/CI rounding noise reaches −64 dB). CI is green on all jobs.
- The 0.4.0 announcement is in GitHub Discussions (Announcements).
- Presets removed (amp opens on defaults); save/load/curated presets are #72.
- Sanitizers build all 3 test targets under ASan/UBSan, skip the tone lock, and excuse only JUCE's
  libjpeg (`tests/sanitizers/ubsan.supp`).
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>` via exported
  `GIT_AUTHOR_*`/`GIT_COMMITTER_*`. Cloud sessions can't push tags, create labels or dispatch workflows.
  The branch-guard hook rejects `git push origin <tag>`: the owner pushes tags.

## Next
1. Owner settings: create the `tone` label; private vulnerability reporting; "Keep my email addresses
   private"; delete stale branches (keep `main`, `feat/neural-hybrid-blend-sketch`).
2. Fix #43 (IR LOADER panel stale after automation/state load) and #45 (window not clamped to screen).
3. Footer shows "1x OS" until the first audio block: `osFactorForGUI` starts at 1
   (`src/PluginProcessor.h:169`) but the default is 4x. Initialise it to 4 or set it in `prepareToPlay`.
4. Make `tests/rt_stress.cpp` build on MSVC (`_aligned_malloc`), so full verify runs on Windows.
5. Later: donation platform (`docs/plans/2026-10-28-open-source-docs-prep.md` step 9); installers (#46/#47);
   macOS (#69); replace the two Pexels textures with own/CC0 photos; E3 aliasing fix → owner A/B → `tone-v2`.

## Decisions
- Tone frozen (amp + cab + effects, steady state); glitch fixes allowed while the lock passes. Never
  regenerate references without the owner's A/B. Tone-lock floor −60 dB instead of per-platform references.
- Free forever: never gate features, nag, or add a trial. No AI attribution anywhere (AGENTS.md).
- #43/#45 ship in 0.4.0 as documented known issues rather than block the first release.
- Sanitizer suppressions are for third-party code only; ours must stay clean.
- Code/CLAUDE.md/docs/dev/handoffs beat DSP spec v2.13. Delay never self-oscillates; IRs level-matched.

## Known issues
- Full `verify.sh` can't build on Windows (`rt_stress.cpp`): use `--quick` + the three ctest targets.
- `signoff.py`: A1/B1 fail but accepted (frozen); E3 −37.9 dBc open.
- Projects using an IR play louder (+5.4…+13.1 dB) since the IR level match (CHANGELOG).
- GUI: small low-contrast text; no screen clamp (#45); footer "1x OS" until audio runs. Builds are
  unsigned (SmartScreen warning).
- 39 old commits carry a non-noreply email; email privacy only affects new ones.
- The squash commit for the tone-lock fix is titled "−65 dB" but the code is −60 dB.
- Local clone `main` has diverged from `origin/main` (ahead 123, behind 282): reset it to
  `origin/main` after checking nothing local is unpushed (unverified).
