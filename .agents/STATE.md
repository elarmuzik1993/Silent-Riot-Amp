# State

_Updated 2026-10-30 (Settings redesign, Oxanium font)_

## Now
- **Tone frozen at `tone-v1`** (059b209): `tests/tone_lock_tests.cpp` guards it (`docs/dev/tone-lock.md`).
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPL-3.0-or-later, free + optional donations. No tagged
  release. ctest 450 (312 pure C++ / 81 JUCE / 57 processor). On Linux/GCC 445 pass and the 5 tone-lock
  cases fail, also at 857023b (before any GUI change): band deltas at −84…−100 dB, RMS ≤ 0.0002 dB.
- Docs open-source ready (`docs/README.md` index; `design/`+`records/` frozen; `AI_USAGE.md` kept true).
  `verify.sh --quick` checks SPDX, links, tool lists, UI text.
- UI font is **Oxanium** (Regular + Bold, OFL) via `Fonts::ui()`; replaced Space Mono + unused DM Sans.
- GUI: amp faceplate with satin pointer knobs and satin-black bat toggles on orange glass rings.
  EFFECTS cards share that style (jewel ON toggles). DELAY has no division row: SYNC and PP are bat
  toggles stacked left of TIME, and SYNC turns TIME into the division knob (`DelayTimeAttachment`).
- IR LOADER (under the amp, above EFFECTS) opens on a drawn 2x12 cab (`src/gui/CabinetView.*`) whose
  arrows cycle the built-in cab + every IR; CAB | LIBRARY brings back the list (`irView`).
- Standalone: Settings (gear) has an AUDIO INPUT list (`src/gui/InputSelector.*`) that opens the
  interface with one input active, so Input 2 of a pair plays (the amp reads channel 0 only).
  Saved with the device setup, not presets; hidden in VST3. Confirmed on a KA6.
- Settings overlay redesigned (`docs/plans/2026-10-30-settings-panel-layout.md`): 560 px, two columns
  + ABOUT, jewel toggles, scales with the UI, centred over a dimmed backdrop, ✕/Esc/outside-click close.
- **Reactive panel glow** (`docs/plans/2026-10-29-reactive-panel-glow.md`): faceplate piping lights with
  the output peak (`outputPeakForGUI`), reddens with compression; the lamp's bloom breathes. Code:
  `GlowDriveFollower`, `glowBeadColour()` in `src/gui/AmpFaceplate.h`; editor timer drives it.
- Contributor issues #37–#48 are open (GUI bugs, IR loader, installers, README screenshot).
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>`: export
  `GIT_AUTHOR_EMAIL`/`GIT_COMMITTER_EMAIL`; the cloud env's values beat `git -c`. Public contact
  email (docs only, never commits): borismishchenkodev@gmail.com.

## Next
1. With owner OK: tag the tone-lock merge commit `tone-v1` and push the tag (`docs/dev/tone-lock.md`).
2. Owner, before going public: enable private vulnerability reporting; create the `tone` label;
   confirm `tests/di_sample_82hz.raw` is self-recorded (credit it in CREDITS.md); delete stale
   branches (keep `main`, `feat/neural-hybrid-blend-sketch`). Then make the repo public.
3. Owner picks a donation platform → step 9 of `docs/plans/2026-10-28-open-source-docs-prep.md`.
4. Owner: delay FEEDBACK default decays 9 dB/repeat — keep or remap? GRID I reads 0 — keep, or read PI?
   Faceplate: lamp colour, weave strength, keep the "POWER" label, jewel size/colour?
5. E3 (the one tone change still allowed): repoint the gate to `tests/fixtures/ir/bright.wav`, fix
   the engine aliasing, owner A/Bs it, then new references + `tone-v2` (`docs/dev/tone-lock.md`).
6. GitHub Actions has failed every run since 2026-10-09; recheck once public (free runners). The
   tone lock fails on Linux/GCC (references are from MSVC; see Now): owner decides between a
   per-platform reference set or a noise floor below which bands are not compared.
7. Owner, cab view: light theme keeps white tolex around the black grille — OK? Dim the cab when CAB is off?
8. Owner: review Settings redesign + Oxanium on the docs/settings-panel-plan branch, then its PR.
   Re-shoot the manual's `resources/images/` (still Space Mono) with `sr_editor_snapshot`.

## Decisions
- Tone frozen at `tone-v1` (amp + cab + effects, steady state). Re-calibration (power Tasks 2–5,
  `VOLUME_KNOB_HEADROOM`) on hold, owner may revisit; glitch fixes allowed while the lock passes.
- Never gate features, nag, or add a trial. Code/CLAUDE.md/handoffs beat DSP spec v2.13; `ROADMAP.md` is the plan.
- Tone-neutral fixes first; tone-changing ones together, so the amp is re-calibrated once.
- Delay never self-oscillates; sync ceiling 2000 ms, TIME 20–1000 ms; SYNC swaps TIME's parameter in the GUI.
- IRs match the built-in cab's level; 8192 cap stays; state keeps raw IRs; restore is laxer than load.
- Latency is an exact integer, constant across cab modes.

## Known issues
- `signoff.py` (44.1k/4×): A1 −31.6 LUFS and B1 0.99% fail but are accepted (frozen); E3 −37.9 dBc open.
- Sag idles warm (B2), never goes cold; a live 15W↔7W switch settles audibly.
- **Projects that use an IR play louder** (+5.4…+13.1 dB) since the IR level match; in CHANGELOG.
- IR path: cab-mode switches dip ~6–95 ms; bright IRs 1.2–1.5 dB under the cab; 8192 cap cuts rings.
- GUI: small low-contrast text (section titles, footer); EFFECTS + IR LOADER open = 922 px, no screen clamp.
- 13 commits on `main` and the docs plan's first commit carry the env email, not the noreply one.
