# State

_Updated 2026-11-26 (test infra fixed; real-time and thread-safety rules enforced by CI and verify.sh)_

## Now
- Silent Riot v0.4.0 (JUCE 7 VST3/Standalone, Windows + Linux), GPL-3.0-or-later, free, public on GitHub;
  release `v0.4.0`: Linux .tar.gz, Windows .zip (+ .sha256), body = CHANGELOG `[0.4.0]`; `tone-v1` = e56ab9f.
- Tone frozen at `tone-v1` (`tests/tone_lock_tests.cpp`); silent floor −60 dB on all platforms (MSVC refs).
- CI `ubuntu-latest` is Ubuntu 26.04 (since 2026-11-06): GCC 15 defaults to `-march=x86-64-v3`, so
  Linux CI code uses FMA. Tests hold with and without FMA. `build.yml` actions are on Node 24 majors.
- `verify.sh` runs in full on Windows (Git Bash, Visual Studio generator, like `test-windows`) and uses
  the first Python 3 that runs. `SilentRiotRTStress` builds on MSVC (no lock interposition there).
- Thread rules are gates: `rt_stress --quick` in `verify.sh` and both CI test jobs (alloc/free/mutex on
  the audio thread), CI `tsan` over the cross-thread scenarios (incl. `hostsave`), verify check 8.
- Presets removed (amp opens on defaults); save/load/curated presets are #72.
- Sanitizers (ASan/UBSan) build all 3 test targets, skip the tone lock, excuse only JUCE's libjpeg.
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>` (exported `GIT_*` vars).
  Cloud sessions can't push tags, create labels or dispatch workflows; only the owner pushes tags.

## Next
1. Before the next release (owner's call): `release.yml` builds Linux on `ubuntu-latest`, so its package
   would need an x86-64-v3 CPU and Ubuntu 26.04's glibc. Pin it (and `build-linux`) to `ubuntu-24.04`
   and/or set a `-march` baseline in CMake; move its actions to Node 24 majors; dry-run it.
2. Owner settings: create the `tone` label; private vulnerability reporting; "Keep my email addresses
   private"; delete stale branches (keep `main`, `feat/neural-hybrid-blend-sketch`).
3. Fix #43 (IR LOADER panel stale after automation/state load) and #45 (window not clamped to screen).
4. Footer shows "1x OS" until audio runs: initialise `osFactorForGUI` to 4 or set it in `prepareToPlay`.
5. Later: donation platform (`docs/plans/2026-10-28-open-source-docs-prep.md` step 9); installers (#46/#47);
   macOS (#69); replace the two Pexels textures with own/CC0 photos; E3 aliasing fix → owner A/B → `tone-v2`.

## Decisions
- Tone frozen (amp + cab + effects, steady state); glitch fixes allowed while the lock passes. Never
  regenerate references without the owner's A/B. Tone-lock floor −60 dB instead of per-platform references.
- Free forever: never gate features, nag, or add a trial. No AI attribution anywhere (AGENTS.md).
- Float tests assert a bound with a stated margin over FMA rounding, never an exact zero across call sites.
- Hosts save/restore from any thread: editor settings live in `setUIProperty` (saved as root properties),
  never the live `apvts.state`; the IR list is read via locked copies. Sanitizer suppressions: third-party only.
- Code/CLAUDE.md/docs/dev/handoffs beat DSP spec v2.13. Delay never self-oscillates; IRs level-matched.

## Known issues
- `signoff.py`: A1/B1 fail but accepted (frozen); E3 −37.9 dBc open.
- Projects using an IR play louder (+5.4…+13.1 dB) since the IR level match (CHANGELOG).
- GUI: small low-contrast text; no screen clamp (#45); footer "1x OS" until audio runs. Unsigned builds.
- Old commits (incl. 00b2cca) carry a non-noreply email; email privacy only affects new ones.
- The squash commit for the tone-lock fix is titled "−65 dB" but the code is −60 dB.
