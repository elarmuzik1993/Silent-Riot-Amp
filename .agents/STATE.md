# State

_Updated 2026-10-30 (reactive panel glow built; tone lock measured on Linux/GCC)_

## Now
- **Tone frozen at `tone-v1`** (059b209): `tests/tone_lock_tests.cpp` guards it (`docs/dev/tone-lock.md`).
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPL-3.0-or-later, free + optional donations. No tagged
  release. `bash scripts/verify.sh`: static checks pass, **470/475 ctest on Linux/GCC** — the 5
  tone-lock cases fail on float noise, not tone (Known issues). The references were written on MSVC.
- Docs open-source ready, indexed by `docs/README.md`; `dev/` + `manual/` track the code, `design/` +
  `records/` are frozen. `verify.sh --quick` checks SPDX, links (tracked files only; paths a plan has
  yet to create go in `scripts/linkcheck-allow.txt`), tool source lists, UI text.
- `AI_USAGE.md` is the AI addendum; CREDITS/README/CONTRIBUTING link it. Keep it true.
- GUI: faceplate with satin pointer knobs and bat toggles; EFFECTS cards share the style. SYNC turns
  DELAY's TIME into the division knob. IR LOADER (under the amp) opens on a drawn 2x12 cab with
  arrows cycling cab + IRs, CAB | LIBRARY switch. Standalone Settings has an AUDIO INPUT list.
- **Reactive panel glow** (`docs/plans/2026-10-29-reactive-panel-glow.md`, done): the faceplate
  piping lights with the output peak (`outputPeakForGUI`) and reddens with **compression** (not sag:
  Task 0 measured sag barely travelling, `docs/records/handoffs/handoff-sag-travel-probe.cpp`);
  the lamp's bloom breathes. `GlowDriveFollower` + `glowBeadColour()` in `src/gui/AmpFaceplate.h`.
- Contributor issues #37–#48 are open (GUI bugs, IR loader, installers, README screenshot).
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>` (export `GIT_AUTHOR_EMAIL`/
  `GIT_COMMITTER_EMAIL`). Public contact email (docs only, never commits): borismishchenkodev@gmail.com.

## Next
1. Owner: look at the panel glow in both themes (strength, red at full compression) — tune
   `kGlowAlphaMaxHalo`/`kGlowAlphaMaxBead` and the follower constants in `AmpFaceplate.h`.
2. Owner: the tone lock's Linux/GCC divergence — a noise-floor gate (skip bands under ~−80 dB), or a
   looser per-band tolerance? It blocks CI going green once the repo is public.
3. With owner OK: tag the tone-lock merge commit `tone-v1` and push the tag (`docs/dev/tone-lock.md`).
4. Owner, before going public: private vulnerability reporting; `tone` label; confirm
   `tests/di_sample_82hz.raw` is self-recorded (CREDITS.md); delete stale branches; go public.
5. Owner picks a donation platform → `docs/plans/2026-10-28-open-source-docs-prep.md` step 9.
6. Owner: delay FEEDBACK default (9 dB/repeat)? GRID I reads 0 — keep or read PI? Faceplate lamp/weave/jewel?
7. E3 (the one tone change still allowed): repoint the gate to `tests/fixtures/ir/bright.wav`, fix
   the engine aliasing, owner A/Bs it, then new references + `tone-v2`.
8. GitHub Actions has failed every run since 2026-10-09; recheck once public (free runners).
9. Owner, cab view: white tolex in the light theme OK? Dim the cab while CAB is off?

## Decisions
- Tone frozen at `tone-v1` (amp + cab + effects, steady state). Re-calibration (power Tasks 2–5,
  `VOLUME_KNOB_HEADROOM`) on hold, owner may revisit; glitch fixes allowed while the lock passes.
- Panel glow on the faceplate piping + lamp (zero window height); its hue follows compression.
- Never gate features, nag, or add a trial (open source + donations).
- Code, CLAUDE.md and handoffs beat the DSP spec v2.13; `ROADMAP.md` is the live plan.
- Tone-neutral fixes first; tone-changing ones together, so the amp is re-calibrated once.
- Delay never self-oscillates; sync ceiling 2000 ms, TIME 20–1000 ms; SYNC swaps TIME's parameter in the GUI.
- IRs match the built-in cab's level; 8192 cap stays; state keeps raw IRs; restore is laxer than load.
- Latency is an exact integer, constant across cab modes.

## Known issues
- **Tone lock fails 5/475 on Linux/GCC** (cases 394–398): band deltas up to 1.01 dB in bands at
  −96…−99 dB vs a 0.1 dB tolerance, RMS within 3.8e−05 dB — float noise in silent bands, not tone.
- `signoff.py` (44.1k/4×): A1 −31.6 LUFS and B1 0.99% fail but are accepted (frozen); E3 −37.9 dBc open.
- Sag idles warm (B2), never goes cold; a live 15W↔7W switch settles audibly.
- **Projects that use an IR play louder** (+5.4…+13.1 dB) since the IR level match; in CHANGELOG.
- IR path: cab-mode switches dip (~6–95 ms); bright IRs 1.2–1.5 dB under the built-in cab; 8192 cap.
- GUI: small low-contrast text; EFFECTS + IR LOADER open make the window 922 px, not screen-clamped.
- Glow: under host bypass it can show false heat (out/in ≈ 1 against the amp's learnt ratio).
- 13 commits on `main` and the docs plan's first commit carry the env email, not the noreply one.
