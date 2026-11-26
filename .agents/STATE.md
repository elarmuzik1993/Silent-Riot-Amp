# State

_Updated 2026-11-26 (test infra: GCC 15 CI fix, full verify on Windows, Windows CI stops on failure)_

## Now
- Silent Riot v0.4.0 (JUCE 7 VST3/Standalone, Windows + Linux), GPL-3.0-or-later, free; public on GitHub.
  Release `v0.4.0` is published with Linux .tar.gz and Windows .zip (+ .sha256); `tone-v1` tags the
  frozen tone (e56ab9f). Release body is CHANGELOG `[0.4.0]`; README Install points at `releases/latest`.
- Tone frozen at `tone-v1` (`tests/tone_lock_tests.cpp`); silent floor −60 dB on all platforms (MSVC refs).
- CI `ubuntu-latest` is Ubuntu 26.04 (since 2026-11-06): GCC 15 defaults to `-march=x86-64-v3`, so
  Linux CI code uses FMA. Tests hold with and without FMA. `build.yml` actions are on Node 24 majors.
- `verify.sh` runs in full on Windows (Git Bash, Visual Studio generator, like `test-windows`) and uses
  the first Python 3 that runs. `SilentRiotRTStress` builds on MSVC (no lock interposition there).
- Presets removed (amp opens on defaults); save/load/curated presets are #72.
- Sanitizers build all 3 test targets (ASan/UBSan), skip the tone lock, excuse only JUCE's libjpeg.
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>` via exported
  `GIT_AUTHOR_*`/`GIT_COMMITTER_*`. Cloud sessions can't push tags, create labels or dispatch
  workflows; the branch-guard hook rejects `git push origin <tag>`: the owner pushes tags.

## Next
1. Before the next release (owner's call): `release.yml` builds Linux on `ubuntu-latest`, so its package
   would need an x86-64-v3 CPU and Ubuntu 26.04's glibc. Pin it (and `build-linux`) to `ubuntu-24.04`
   and/or set a `-march` baseline in CMake; move its actions to Node 24 majors; dry-run it.
2. Owner settings: create the `tone` label; private vulnerability reporting; "Keep my email addresses
   private"; delete stale branches (keep `main`, `feat/neural-hybrid-blend-sketch`).
3. Fix #43 (IR LOADER panel stale after automation/state load) and #45 (window not clamped to screen).
4. Footer shows "1x OS" until the first audio block: `osFactorForGUI` starts at 1
   (`src/PluginProcessor.h`) but the default is 4x. Initialise it to 4 or set it in `prepareToPlay`.
5. Later: donation platform (`docs/plans/2026-10-28-open-source-docs-prep.md` step 9); installers (#46/#47);
   macOS (#69); replace the two Pexels textures with own/CC0 photos; E3 aliasing fix → owner A/B → `tone-v2`.

## Decisions
- Tone frozen (amp + cab + effects, steady state); glitch fixes allowed while the lock passes. Never
  regenerate references without the owner's A/B. Tone-lock floor −60 dB instead of per-platform references.
- Free forever: never gate features, nag, or add a trial. No AI attribution anywhere (AGENTS.md).
- Float tests assert a bound with a stated margin over FMA rounding, never an exact zero across call sites.
- Sanitizer suppressions are for third-party code only; ours must stay clean.
- Code/CLAUDE.md/docs/dev/handoffs beat DSP spec v2.13. Delay never self-oscillates; IRs level-matched.

## Known issues
- `signoff.py`: A1/B1 fail but accepted (frozen); E3 −37.9 dBc open.
- Projects using an IR play louder (+5.4…+13.1 dB) since the IR level match (CHANGELOG).
- GUI: small low-contrast text; no screen clamp (#45); footer "1x OS" until audio runs. Unsigned builds.
- Old commits (incl. 00b2cca) carry a non-noreply email; email privacy only affects new ones.
- The squash commit for the tone-lock fix is titled "−65 dB" but the code is −60 dB.
