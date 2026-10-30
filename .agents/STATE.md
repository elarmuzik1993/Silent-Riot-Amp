# State

_Updated 2026-10-30 (RT stress harness + findings; fix plan written, nothing fixed)_

## Now
- **Tone frozen at `tone-v1`** (059b209): `tests/tone_lock_tests.cpp` guards it (`docs/dev/tone-lock.md`).
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPL-3.0-or-later, free + donations. No tagged release.
  `verify.sh`: 440/445 ctest; the 5 failures are **the tone lock on Linux/GCC** — see Next 7.
- Docs are open-source ready and indexed by `docs/README.md`; `dev/` + `manual/` track the code,
  `design/` + `records/` are frozen. `verify.sh --quick` (also CI) checks SPDX, links, UI text.
- GUI: amp faceplate, satin pointer knobs, black bat toggles on orange glass rings; EFFECTS cards
  share it (jewel ON). DELAY: SYNC/PP toggles left of TIME, SYNC turns TIME into the division knob.
- IR LOADER opens on a drawn 2x12 cab (`src/gui/CabinetView.*`) whose arrows cycle the built-in cab
  + every IR; CAB | LIBRARY brings back the list (`irView`). Section order: IR LOADER, EFFECTS, ADV.
- Standalone: Settings has an AUDIO INPUT list (`src/gui/InputSelector.*`) opening the interface with
  one input active, so Input 2 of a pair plays; saved with the device setup, hidden in VST3. Owner
  confirmed on a KA6. Plan: `docs/plans/2026-10-29-standalone-input-select.md`.
- RT stress harness `tests/rt_stress.cpp` (`SilentRiotRTStress`, built by verify, not a ctest) measures
  audio-thread allocations, locks and deadlines. Findings RT-1…RT-6:
  `docs/records/reviews/review-rt-stress-2026-10-29.md`; none fixed yet (PR #57).
- Contributor issues #37–#48 are open (GUI bugs, IR loader, installers, README screenshot).
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>`: export
  `GIT_AUTHOR_EMAIL`/`GIT_COMMITTER_EMAIL`; the cloud env's values beat `git -c`.

## Next
1. Work `docs/plans/2026-10-30-rt-safety-fixes.md`: Task 1 scrubs non-finite input (RT-1, CRITICAL),
   2 chunks over-sized blocks (RT-5), 3 moves the latency report and the IR-index pickup to a
   message-thread timer (RT-2+RT-3), 4 makes the tuner ring atomic. Owner: decide D1 and D2 first.
2. With owner OK: tag the tone-lock merge commit `tone-v1` and push the tag (`docs/dev/tone-lock.md`).
3. Owner, before going public: enable private vulnerability reporting; create the `tone` label;
   confirm `tests/di_sample_82hz.raw` is self-recorded; delete stale branches (keep `main` and
   `feat/neural-hybrid-blend-sketch`). Then go public.
4. Owner picks a donation platform → docs plan step 9 (`docs/plans/2026-10-28-open-source-docs-prep.md`).
5. Owner: delay FEEDBACK default decays 9 dB/repeat — keep or remap? GRID I reads 0 — keep or read PI?
   Faceplate: lamp colour, weave strength, keep "POWER", jewel size/colour?
6. E3 (the one tone change still allowed): repoint the gate to `tests/fixtures/ir/bright.wav`, fix the
   engine aliasing, owner A/Bs it, then new references + `tone-v2`.
7. Tone lock on Linux/GCC **fails** (measured 2026-10-29, GCC 13 Release): all 5 cases on bands
   between −84 and −100 dB, worst 4.04 dB, RMS delta 3.8e-05 dB — a cross-compiler noise-floor
   difference, not a tone change. Owner: widen the tolerance under ~−80 dB, or keep per-toolchain
   references; until then verify.sh cannot pass on Linux. CI has also failed since 2026-10-09.
8. Owner, cab view: light theme keeps white tolex around the black grille — OK? Dim the cab off?

## Decisions
- Tone frozen at `tone-v1` (amp + cab + effects, steady state). Re-calibration (power Tasks 2–5,
  `VOLUME_KNOB_HEADROOM`) on hold; glitch and safety fixes allowed while the lock passes.
- Never gate features, nag, or add a trial (open source + donations).
- Code, CLAUDE.md and handoffs beat the DSP spec v2.13; `ROADMAP.md` is the live plan.
- Tone-neutral fixes first; tone-changing ones together, so the amp is re-calibrated once.
- Delay never self-oscillates; sync ceiling 2000 ms, TIME knob 20–1000 ms. SYNC reuses TIME for its
  divisions by swapping parameters in the GUI, so the layout (and old sessions) stay.
- IRs match the built-in cab's level; 8192 cap stays; state keeps raw IRs; restore is laxer than load.
- Latency is an exact integer, constant across cab modes.

## Known issues
- `signoff.py` (44.1k/4×): A1 −31.6 LUFS and B1 0.99% fail, accepted (frozen); E3 −37.9 dBc open.
- Sag idles warm (B2), never goes cold; a live 15W↔7W switch settles audibly.
- **Projects that use an IR play louder** (+5.4…+13.1 dB) since the IR level match; in CHANGELOG.
- IR path: cab-mode switches dip; bright IRs sit 1.2–1.5 dB low; long IRs lose their ring at 8192.
- GUI: small low-contrast text; EFFECTS + IR LOADER open = a 922 px window, not clamped to the screen.
- RT-1…RT-6 all open (the review above, fixes in `docs/plans/2026-10-30-rt-safety-fixes.md`): a
  NaN/Inf input sample silences the amp for the session; `setLatencySamples` and
  `triggerAsyncUpdate` run on the audio thread; a tuner-ring race; an over-sized block segfaults.
- 13 commits on `main` and the docs plan's first commit carry the env email, not the noreply one.
