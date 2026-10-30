# State

_Updated 2026-10-30 (panel glow planned; tone lock measured on Linux/GCC)_

## Now
- **Tone frozen at `tone-v1`** (059b209): `tests/tone_lock_tests.cpp` guards it (`docs/dev/tone-lock.md`).
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPL-3.0-or-later, free + optional donations. No tagged
  release. `bash scripts/verify.sh`: static checks pass, **444/449 ctest on Linux/GCC** — the 5
  tone-lock cases fail on float noise, not tone (Known issues). The references were written on MSVC.
- Docs are open-source ready (README, `docs/manual/`, `docs/dev/`, CONTRIBUTING/CoC/SECURITY, issue
  forms, CHANGELOG); `docs/README.md` indexes them. `dev/` + `manual/` track the code; `design/` +
  `records/` are frozen. `verify.sh --quick` (also CI) checks SPDX, links, tool source lists, UI text
  — `check_links.py` scans **tracked files only**, so a new doc's dangling paths surface only once it
  is committed; `scripts/linkcheck-allow.txt` carries paths a plan has yet to create.
- `AI_USAGE.md` is the AI addendum (scope, workflow, human authorship, IP, warranty limitation);
  CREDITS/README/CONTRIBUTING link it. Keep it true if the way AI is used changes.
- GUI: amp faceplate with satin pointer knobs and satin-black bat toggles on orange glass rings;
  EFFECTS cards share that style (jewel ON toggles). SYNC turns DELAY's TIME into the division knob
  (`DelayTimeAttachment`). IR LOADER opens on a drawn 2x12 cab (`src/gui/CabinetView.*`) with arrows
  cycling the built-in cab + every IR, and a CAB | LIBRARY switch for the list (`irView` property);
  it sits directly under the amp (section order IR LOADER, EFFECTS, ADVANCED).
- Standalone: Settings (gear) has an AUDIO INPUT list (`src/gui/InputSelector.*`) opening the
  interface with one input active, so Input 2 of a pair plays (the amp reads channel 0 only). Saved
  with the device setup, not presets; hidden in VST3. Owner confirmed Input 2 plays on a KA6
  (`docs/plans/2026-10-29-standalone-input-select.md`).
- **Panel glow is planned, not built** (`docs/plans/2026-10-29-reactive-panel-glow.md`): the faceplate
  piping bead lights with playing and the pilot lamp's bloom breathes, costing no window height.
  Brightness from an envelope on output level, hue from sag. GUI-only but for one read-only block
  peak in `processBlock`; the plan sets out why no task in it can reach the sound.
- Contributor issues #37–#48 are open (GUI bugs, IR loader, installers, README screenshot).
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>`: export
  `GIT_AUTHOR_EMAIL`/`GIT_COMMITTER_EMAIL`; the cloud env's values beat `git -c`. Public contact
  email (docs only, never commits): borismishchenkodev@gmail.com.

## Next
1. Panel glow Task 0: measure sag travel and pick the hue carrier before any paint code. B2 may
   leave hue dead; the plan names the fallback (hue from compression, using existing atomics).
2. Owner: the tone lock's Linux/GCC divergence — a noise-floor gate (skip bands under ~−80 dB), or a
   looser per-band tolerance? It blocks CI going green once the repo is public.
3. With owner OK: tag the tone-lock merge commit `tone-v1` and push the tag (`docs/dev/tone-lock.md`).
4. Owner, before going public: enable private vulnerability reporting; create the `tone` label;
   confirm `tests/di_sample_82hz.raw` is self-recorded (credit it in CREDITS.md); delete stale
   branches (keep `main`, `feat/neural-hybrid-blend-sketch`). Then make the repo public.
5. Owner picks a donation platform → `docs/plans/2026-10-28-open-source-docs-prep.md` step 9.
6. Owner: delay FEEDBACK default decays 9 dB/repeat — keep or remap? GRID I reads 0 — keep, or read
   PI? Faceplate: lamp colour, weave strength, keep the "POWER" label, jewel size/colour?
7. E3 (the one tone change still allowed): repoint the gate to `tests/fixtures/ir/bright.wav`, fix
   the engine aliasing, owner A/Bs it, then new references + `tone-v2`.
8. GitHub Actions has failed every run since 2026-10-09; recheck once public (free runners).
9. Owner, cab view: the light theme keeps white tolex around the black grille — OK? Should the cab
   dim while CAB is off?

## Decisions
- Tone frozen at `tone-v1` (amp + cab + effects, steady state). Re-calibration (power Tasks 2–5,
  `VOLUME_KNOB_HEADROOM`) on hold, owner may revisit; glitch fixes allowed while the lock passes.
- Panel glow goes on the faceplate piping + lamp: zero window height, and it reuses the hardware
  metaphor. VU rails, an under-amp strip and a window perimeter were weighed and rejected.
- Never gate features, nag, or add a trial (open source + donations).
- Code, CLAUDE.md and handoffs beat the DSP spec v2.13; `ROADMAP.md` is the live plan.
- Tone-neutral fixes first; tone-changing ones together, so the amp is re-calibrated once.
- Delay never self-oscillates; sync ceiling 2000 ms, TIME knob 20–1000 ms. SYNC reuses the TIME knob
  for divisions by swapping parameters in the GUI, so the parameter layout stays.
- IRs match the built-in cab's level; 8192 cap stays; state keeps raw IRs; restore is laxer than load.
- Latency is an exact integer, constant across cab modes.

## Known issues
- **Tone lock fails 5/449 on Linux/GCC** (cases 394–398): worst band deltas 0.48 dB
  (`preset-3-full-drive`, a band at −99 dB) and 1.01 dB (`preset-5-heavy-sag`, −96 dB) against a
  0.1 dB per-band tolerance, while RMS agrees to 3.8e−05 dB. Float noise in numerically silent bands,
  not a tone change — 43 of 45 assertions pass in that case.
- `signoff.py` (44.1k/4×): A1 −31.6 LUFS and B1 0.99% fail but are accepted (frozen); E3 −37.9 dBc open.
- Sag idles warm (B2), never goes cold; a live 15W↔7W switch settles audibly.
- **Projects that use an IR play louder** (+5.4…+13.1 dB) since the IR level match; in CHANGELOG.
- IR path: cab-mode switches dip (~6–95 ms by block size); bright IRs sit 1.2–1.5 dB under the
  built-in cab; long IRs lose their ring at the 8192 cap.
- GUI: small low-contrast text (section titles, footer); with EFFECTS (304 px) + IR LOADER open the
  window is 922 px tall and does not clamp to the screen.
- 13 commits on `main` and the docs plan's first commit carry the env email, not the noreply one.
