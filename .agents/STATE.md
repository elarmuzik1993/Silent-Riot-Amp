# State

_Updated 2026-10-30 (real-time stress harness, its findings, and the plan to fix them)_

## Now
- **Tone frozen at `tone-v1`** (059b209): `tests/tone_lock_tests.cpp` guards it (`docs/dev/tone-lock.md`).
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPL-3.0-or-later, free + donations. No tagged release.
  `verify.sh`: 440/445 ctest; the 5 failures are **the tone lock on Linux/GCC** — see Next 6.
- GUI, IR loader and the standalone input selector are all in (`CLAUDE.md` § GUI Architecture); the
  docs are open-source ready and indexed by `docs/README.md`.
- **Real-time stress harness** `tests/rt_stress.cpp` (target `SilentRiotRTStress`, built by verify,
  not a ctest, exit 1 on a violated contract) measures audio-thread allocations, locks and deadlines
  on the real processor over 12 scenarios — `docs/dev/testing.md` explains it. It found RT-1…RT-6
  (`docs/records/reviews/review-rt-stress-2026-10-29.md`); **none is fixed** — see Next 1.
- Contributor issues #37–#48 are open (GUI bugs, IR loader, installers, README screenshot). Commit
  as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>`: export
  `GIT_AUTHOR_EMAIL`/`GIT_COMMITTER_EMAIL`; the cloud env's values beat `git -c`.

## Next
1. Work `docs/plans/2026-10-30-rt-safety-fixes.md`, one task per commit, in its order — Task 1 first
   (RT-1, CRITICAL: one NaN input sample silences the amp for the session). Its owner decisions are
   settled; each task's gate is a `sr_render` byte-diff, not the tone lock, and none may change tone.
2. With owner OK: tag the tone-lock merge commit `tone-v1` and push the tag (`docs/dev/tone-lock.md`).
3. Owner, before going public: enable private vulnerability reporting; create the `tone` label;
   confirm `tests/di_sample_82hz.raw` is self-recorded; delete stale branches (keep `main` and
   `feat/neural-hybrid-blend-sketch`). Then go public, then pick a donation platform (docs plan 9).
4. Owner: delay FEEDBACK default decays 9 dB/repeat — keep or remap? GRID I reads 0 — keep or read PI?
   Faceplate lamp/weave/"POWER"/jewel? Cab view: white tolex in the light theme, dim while CAB off?
5. E3 (the one tone change still allowed): repoint the gate to `tests/fixtures/ir/bright.wav`, fix the
   engine aliasing, owner A/Bs it, then new references + `tone-v2`. Take the deferred latency
   decision (D1 in the RT plan) with it.
6. Tone lock on Linux/GCC **fails** (measured 2026-10-29, GCC 13 Release): all 5 cases on bands
   between −84 and −100 dB, worst 4.04 dB, RMS delta 3.8e-05 dB — a cross-compiler noise-floor
   difference, not a tone change. Owner: widen the tolerance under ~−80 dB, or keep per-toolchain
   references; until then verify.sh cannot pass on Linux. CI has also failed since 2026-10-09.

## Decisions
- Tone frozen at `tone-v1` (amp + cab + effects, steady state). Re-calibration (power Tasks 2–5,
  `VOLUME_KNOB_HEADROOM`) on hold; glitch and safety fixes allowed while the lock passes.
- RT fixes (2026-10-30, reasoning in the plan): defer the latency report to a message-thread timer
  rather than make latency constant across oversample factors (that shifts 2× by 2 samples — wait
  for `tone-v2`); keep `IR_INDEX` automatable; scrub non-finite input, no panic re-seed for now.
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
- RT-1…RT-6 all open: a NaN/Inf input sample silences the amp for the session; `setLatencySamples`
  and `triggerAsyncUpdate` run on the audio thread; a tuner-ring race; an over-sized host block
  segfaults; 8× is over budget above 48 kHz on a 2.1 GHz core.
- 13 commits on `main` and the docs plan's first commit carry the env email, not the noreply one.
