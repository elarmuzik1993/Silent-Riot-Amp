# State

_Updated 2026-10-23_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone). `bash scripts/verify.sh` passes: static checks +
  Release build + 349/349 ctest across three targets (pure C++, JUCE, and `SilentRiotProcessorTests`,
  which drives the real processor via `tests/processor_harness.h`).
- Work list: DSP review `docs/review-dsp-2026-10-15.md` (line refs against `b8e20a9`). Fixed:
  B1 (idle-point seeding, `dsp/IdleOperatingPoint.h`), B4+B5 (exact latency 10/12/12, saved OS mode
  at prepare), B6 (effects restart clean, fade out, bypass passthrough), B7 (rate-scaled grid
  charge), B8 (delay loop gain = FEEDBACK), S1 (amp knobs sanitized), S2+S4 (stale/corrupt IRs).
- Power plan `docs/superpowers/plans/2026-10-23-power-section-fixes.md`: Tasks 0–1 done; baseline in
  `docs/handoff-power-section-fixes.md` (EL84s stay class A at every setting).

## Next (one PR per step, each with a test; tone-neutral first)
1. S2+S4 review follow-ups: reject IRs whose per-channel float sum of squares overflows (huge finite
   samples still silence IR mode via JUCE's normalisation); in `applyIRIndexParam` write
   `cachedIRIndex` before clearing `pendingIRIndex`; `break` (not `continue`) on a non-IR child in
   `UserIRState.cpp`; add a processor-harness restore test; fix CLAUDE.md's stale IR_INDEX line.
2. S3 + S6: GRID I readout divides by source resistance (7.8× low); meters zero under tuner mute.
3. S5 + S7: reverb SIZE as a smoothed float loop length; raise the sync clamp to 2000 ms (owner:
   raise, not flag) — delay buffer, both clamps, `tempo_sync_tests`; keep the TIME knob's range.
4. Owner: delay FEEDBACK default now decays 9 dB/repeat (was 3.4); keep, or remap the knob?
5. Phase 2 (changes the tone; owner listening check, then ONE re-calibration of D1/A1/B1/P-*):
   plan Task 2 with B2 (replace the tautological idle test; update the pinned numbers in
   `idle_operating_point_tests.cpp`), then B3.
6. Owner, after B2/B3: should the EL84s leave class A? Gates plan Tasks 3–4 (Task 4 with S8) and 5.
7. E3 last (owner: confirm "repoint the gate, then fix the engine"); its bright IR is a placeholder.
8. Owner: GitHub Actions fails every run since 2026-10-09 (check billing); delete stale branches.

## Decisions
- DSP spec v2.13 is background only; code, CLAUDE.md, handoffs win. `COMPETITIVE_ROADMAP.md` is live.
- Tone-neutral fixes first, tone-changing ones together, so the amp is re-calibrated once.
- Delay must never self-oscillate (owner). Sync clamp goes to 2000 ms (owner).
- Latency: exact integer report, constant across cab modes; the IIR path is padded to match.
- PSU's one-sample current lag is intended (spec §3.11).
- `verify.sh` builds into `build-verify/`; sr_render is not built by CI or verify.

## Known issues
- `signoff.py` (44.1k/4×): A1 −31.6 LUFS (band −17.5…−14.5), B1 0.99% (floor 1.0%), E3 −37.9 dBc
  (≤ −85, MUST-PASS) fail; the other 9 pass (E1 −200 dBFS). Other rates within 0.03 dB up to 384k
  effective; above that −0.2…+1.2 dB, cause unknown (not grid charge).
- Sag idles warm (+0.77 V 15W / +1.03 V 7W, B2) and never goes cold under drive.
- Live 15W↔7W switch settles audibly (the idle point moves); a prepare in 7W is silent.
- `sr_bloom.py` gain 0.625 rebounds ~4 dB at 25 ms; `sr_thump.py` gain 1.0: louder in, quieter out.
- Settings → About paints a hard-coded "Licensed — Full Version" (`SettingsPanel.cpp:162`).
