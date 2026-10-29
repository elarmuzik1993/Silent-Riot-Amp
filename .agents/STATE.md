# State

_Updated 2026-10-29 (tone frozen at tone-v1; RT stress harness)_

## Now
- **Tone frozen at `tone-v1`** (059b209): `tests/tone_lock_tests.cpp` guards it (`docs/dev/tone-lock.md`).
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPL-3.0-or-later, free + optional donations. No tagged
  release. `bash scripts/verify.sh`: 440/445 ctest (312 pure C++ / 81 JUCE / 52 processor). The 5
  failures are **the tone lock on Linux/GCC** against MSVC-written references — see Next 7.
- Docs are open-source ready (README, `docs/manual/`, `docs/dev/`, CONTRIBUTING/CoC/SECURITY, issue
  forms, CHANGELOG); `docs/README.md` indexes them. `dev/` + `manual/` track the code; `design/` +
  `records/` are frozen. `verify.sh --quick` (also CI) checks SPDX, links, tool source lists, UI text.
- GUI: amp faceplate with satin pointer knobs and satin-black bat toggles on orange glass rings.
  EFFECTS cards share that style (jewel ON toggles). DELAY has no division row: SYNC and PP are bat
  toggles stacked left of TIME, and SYNC turns TIME into the division knob (`DelayTimeAttachment`).
- IR LOADER opens on a drawn 2x12 cab (`src/gui/CabinetView.*`: 2:1, plain black caps, orange
  piping, black expanded-metal grille) with arrows that cycle the built-in cab + every IR; a
  CAB | LIBRARY switch brings back the list (`irView` UI property). The IR LOADER sits directly under
  the amp, above EFFECTS (section order IR LOADER, EFFECTS, ADVANCED).
- Standalone: Settings (gear) has an AUDIO INPUT list (`src/gui/InputSelector.*`) that opens the
  interface with one input active, so Input 2 of a pair plays (the amp reads channel 0 only).
  Saved with the device setup, not presets; hidden in VST3. Owner confirmed Input 2 plays on a KA6. Plan: `docs/plans/2026-10-29-standalone-input-select.md`.
- RT stress harness `tests/rt_stress.cpp` (target `SilentRiotRTStress`, built by verify, not a ctest)
  measures audio-thread allocations, locks and deadlines on the real processor. First run and
  findings RT-1…RT-6: `docs/records/reviews/review-rt-stress-2026-10-29.md`. **Nothing is fixed yet.**
- Contributor issues #37–#48 are open (GUI bugs, IR loader, installers, README screenshot).
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>`: export
  `GIT_AUTHOR_EMAIL`/`GIT_COMMITTER_EMAIL`; the cloud env's values beat `git -c`.

## Next
1. RT-1 (CRITICAL): one non-finite input sample silences the amp until the host re-prepares it —
   scrub the input at the top of `processBlock` (tone-neutral, needs a processor test). Then RT-2
   (`setLatencySamples` off the audio thread) and RT-5 (chunk over-sized blocks). RT-3 is an owner
   call: make `IR_INDEX` non-automatable, or poll it from a timer?
2. With owner OK: tag the tone-lock merge commit `tone-v1` and push the tag (`docs/dev/tone-lock.md`).
3. Owner, before going public: enable private vulnerability reporting; create the `tone` label;
   confirm `tests/di_sample_82hz.raw` is self-recorded (credit it in CREDITS.md); delete stale
   branches (keep `main`, `feat/neural-hybrid-blend-sketch`). Then make the repo public.
4. Owner picks a donation platform → docs plan step 9 (`docs/plans/2026-10-28-open-source-docs-prep.md`).
5. Owner: delay FEEDBACK default decays 9 dB/repeat — keep or remap? GRID I reads 0 — keep, or read PI?
   Faceplate: lamp colour, weave strength, keep the "POWER" label, jewel size/colour?
6. E3 (the one tone change still allowed): repoint the gate to `tests/fixtures/ir/bright.wav`, fix
   the engine aliasing, owner A/Bs it, then new references + `tone-v2` (`docs/dev/tone-lock.md`).
7. Tone lock on Linux/GCC: **it fails** (measured 2026-10-29, GCC 13 Release). All 5 cases fail on
   bands between −84 and −100 dB, worst 4.04 dB, while the RMS delta is 3.8e-05 dB — a cross-compiler
   difference at the noise floor, not a tone change. Owner call: widen the tolerance for bands under
   ~−80 dB, or keep per-toolchain references. Until then verify.sh cannot pass on Linux.
   GitHub Actions has also failed every run since 2026-10-09; recheck once public (free runners).
8. Owner, cab view: the light theme keeps white tolex around the black grille — OK? Should the cab
   dim while CAB is off?

## Decisions
- Tone frozen at `tone-v1` (amp + cab + effects, steady state). Re-calibration (power Tasks 2–5,
  `VOLUME_KNOB_HEADROOM`) on hold, owner may revisit; glitch fixes allowed while the lock passes.
- Never gate features, nag, or add a trial (open source + donations).
- Code, CLAUDE.md and handoffs beat the DSP spec v2.13; `ROADMAP.md` is the live plan.
- Tone-neutral fixes first; tone-changing ones together, so the amp is re-calibrated once.
- Delay never self-oscillates; sync ceiling 2000 ms, TIME knob 20–1000 ms. SYNC reuses the TIME knob
  for divisions by swapping parameters in the GUI, so the parameter layout (and old sessions) stay.
- IRs match the built-in cab's level; 8192 cap stays; state keeps raw IRs; restore is laxer than load.
- Latency is an exact integer, constant across cab modes.

## Known issues
- `signoff.py` (44.1k/4×): A1 −31.6 LUFS and B1 0.99% fail but are accepted (frozen); E3 −37.9 dBc open.
- Sag idles warm (B2), never goes cold; a live 15W↔7W switch settles audibly.
- **Projects that use an IR play louder** (+5.4…+13.1 dB) since the IR level match; in CHANGELOG.
- IR path: cab-mode switches dip (~6–95 ms by block size); bright IRs sit 1.2–1.5 dB under the
  built-in cab; long IRs lose their ring at the 8192 cap.
- GUI: small low-contrast text (section titles, footer); with EFFECTS (304 px) + IR LOADER open the
  window is 922 px tall and does not clamp to the screen.
- RT (all open, see the review): one NaN/Inf input sample silences the amp for the session (RT-1);
  `setLatencySamples` (RT-2) and `triggerAsyncUpdate` (RT-3) run on the audio thread; tuner-ring data
  race (RT-4); an over-sized host block segfaults (RT-5); 8× over budget above 48 kHz (RT-6).
- 13 commits on `main` and the docs plan's first commit carry the env email, not the noreply one.
