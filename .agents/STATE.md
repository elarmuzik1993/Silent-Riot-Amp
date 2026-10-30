# State

_Updated 2026-10-30 (real-time safety fixes RT-1…RT-5, RT-6 documented)_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPL-3.0-or-later, free + donations. No tagged release.
- **Tone frozen at `tone-v1`** (059b209), guarded by `tests/tone_lock_tests.cpp` (`docs/dev/tone-lock.md`).
- `verify.sh`: 451/456 ctest (312 pure C++ / 81 JUCE / 63 processor); the 5 failures are the tone
  lock on Linux/GCC (Next 4).
- Real-time stress harness `SilentRiotRTStress` (`docs/dev/testing.md`): built by verify, not a
  ctest, exit 1 on a violated contract; full run reports no findings. Its review
  (`docs/records/reviews/review-rt-stress-2026-10-29.md`) is closed by
  `docs/plans/2026-10-30-rt-safety-fixes.md`: input NaN/Inf scrubbed, over-sized host blocks split,
  latency + `IR_INDEX` published by a 20 Hz message-thread timer, atomic tuner ring, oversampling
  cost in `docs/manual/controls.md`. All bit-identical on the normal path (`sr_render` cmp).
- Contributor issues #37–#48 open. Commit as `Boris Miscenco
  <219343213+elarmuzik1993@users.noreply.github.com>` via `GIT_AUTHOR_EMAIL`/`GIT_COMMITTER_EMAIL`.

## Next
1. With owner OK: tag the tone-lock merge commit `tone-v1` and push the tag (`docs/dev/tone-lock.md`).
2. Owner, before going public: private vulnerability reporting; `tone` label; confirm
   `tests/di_sample_82hz.raw` is self-recorded; delete stale branches (keep `main`,
   `feat/neural-hybrid-blend-sketch`). Then go public and pick a donation platform (docs plan 9).
3. E3, the one tone change still allowed: repoint the gate to `tests/fixtures/ir/bright.wav`, fix
   engine aliasing, owner A/B, new references + `tone-v2`. Take RT plan D1 (constant latency across
   oversample factors) with it.
4. Tone lock fails on Linux/GCC: 5 cases, bands −84…−100 dB only, worst 4.04 dB, RMS 3.8e-05 dB — a
   compiler noise-floor difference. Owner: loosen tolerance below ~−80 dB (suggested) or keep
   per-toolchain references. CI has also failed since 2026-10-09.
5. Owner calls: delay FEEDBACK default (−9 dB/repeat); GRID I reads 0; faceplate lamp/weave/jewel;
   cab view tolex in light theme, dim while CAB off?

## Decisions
- Tone frozen (amp + cab + effects, steady state); re-calibration on hold; glitch/safety fixes allowed
  while the lock passes. Tone-changing fixes are batched so the amp is re-calibrated once.
- Latency report deferred to the timer, not made constant (that moves 2× by 2 samples); `IR_INDEX`
  stays automatable; no panic re-seed (RT plan D1–D3). RT-6 documented, not enforced.
- Open source + donations: never gate features, nag or add a trial.
- Code, CLAUDE.md and handoffs beat DSP spec v2.13; `ROADMAP.md` is the live plan.
- Latency is an exact integer, constant across cab modes. IRs match the built-in cab's level.

## Known issues
- `signoff.py` (44.1k/4×): A1 and B1 fail, accepted (frozen); E3 −37.9 dBc open.
- Sag idles warm (B2); a live 15W↔7W switch settles audibly. IR projects play louder (CHANGELOG).
- IR path: cab-mode switches dip; bright IRs sit 1.2–1.5 dB low; long IRs lose their ring at 8192.
- GUI: small low-contrast text; EFFECTS + IR LOADER open = 922 px window, not clamped to the screen.
- 8× is over budget above 48 kHz (4× above 96 kHz) on a 2.1 GHz core — documented in the manual.
- Destroy the processor on the message thread; timer, restore and prepare share `irStateLock`.
- An engine-internal NaN (never observed) would still silence the amp (RT plan Task 6, deferred).
- 13 commits on `main` carry the env email, not the noreply one.
