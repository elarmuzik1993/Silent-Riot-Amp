# State

_Updated 2026-10-30 (reactive panel glow built; tone lock measured on Linux/GCC)_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPL-3.0-or-later, free + optional donations, no tagged
  release. **Tone frozen at `tone-v1`** (059b209), guarded by `tests/tone_lock_tests.cpp` (`docs/dev/tone-lock.md`).
- `bash scripts/verify.sh`: static checks pass, **470/475 ctest on Linux/GCC** (312 pure / 81 JUCE /
  82 processor); the 5 failures are the tone lock on float noise (Known issues).
- Docs open-source ready (`docs/README.md` index; `design/` + `records/` frozen). `AI_USAGE.md` is the
  AI addendum — keep it true. `verify.sh --quick` checks SPDX, links (tracked files only), UI text.
- GUI: hardware-style faceplate + EFFECTS cards; IR LOADER under the amp opens on a drawn 2x12 cab;
  standalone Settings has an AUDIO INPUT list.
- **Reactive panel glow** (`docs/plans/2026-10-29-reactive-panel-glow.md`): the faceplate piping lights
  with the output peak (`outputPeakForGUI`) and reddens with compression (sag barely travels:
  `docs/records/handoffs/handoff-sag-travel-probe.cpp`); the lamp's bloom breathes. Code:
  `GlowDriveFollower`, `glowBeadColour()` in `src/gui/AmpFaceplate.h`; editor timer drives it.
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>` (export
  `GIT_AUTHOR_EMAIL`/`GIT_COMMITTER_EMAIL`). Public contact (docs only): borismishchenkodev@gmail.com.

## Next
1. Owner: look at the glow in both themes (`sr_editor_snapshot theme=0|1 drive=1:0|1:1`,
   `tools/README.md`); tune `kGlowAlphaMax*` and the follower constants in `AmpFaceplate.h`.
2. Owner: tone lock on Linux/GCC — noise-floor gate (skip bands under ~−80 dB) or looser tolerance?
   Blocks CI going green once public.
3. With owner OK: tag the tone-lock merge commit `tone-v1` and push the tag.
4. Before going public: private vulnerability reporting, `tone` label, confirm
   `tests/di_sample_82hz.raw` is self-recorded, delete stale branches. Then donation platform
   (`docs/plans/2026-10-28-open-source-docs-prep.md` step 9).
5. E3: repoint the gate to `tests/fixtures/ir/bright.wav`, fix engine aliasing, A/B, `tone-v2`.
6. Owner questions: delay FEEDBACK default; GRID I reads 0; faceplate lamp/weave; cab view light theme.

## Decisions
- Tone frozen (amp + cab + effects); re-calibration on hold; glitch fixes only while the lock passes.
- Glow on the faceplate piping + lamp: zero window height. Hue from compression, not sag (measured).
- Never gate features, nag, or add a trial (open source + donations).
- Code, CLAUDE.md and handoffs beat the DSP spec v2.13; `ROADMAP.md` is the live plan.
- Delay never self-oscillates. IRs match the built-in cab's level. Latency is an exact integer.

## Known issues
- **Tone lock fails 5/475 on Linux/GCC** (394–398): up to 1.01 dB in bands at −96…−99 dB vs 0.1 dB
  tolerance, RMS within 3.8e−05 dB — float noise, not tone. GitHub Actions red since 2026-10-09.
- `signoff.py`: A1/B1 fail but accepted (frozen); E3 −37.9 dBc open. Sag idles warm (B2).
- Projects with an IR play louder (+5.4…+13.1 dB) since the level match; in CHANGELOG.
- Glow shows false heat under host bypass (no bypass flag reaches the editor).
- With EFFECTS + IR LOADER open the window is 922 px and does not clamp to the screen.
