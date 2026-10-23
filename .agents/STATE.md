# State

_Updated 2026-10-23_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone). `bash scripts/verify.sh` passes: static checks +
  Release build + 326/326 ctest. Cloud sessions provision via `.agents/bootstrap.sh`.
- Two work lists, **one combined order below**:
  - DSP review `docs/review-dsp-2026-10-15.md`: B1–B8 blocking, S1–S8 suggestions, measured and
    re-checked. Line refs against `b8e20a9`. **B5 + B4 fixed**: saved OVERSAMPLE mode applied at
    prepare; integer-latency oversamplers + `iirPathAlign` make the report exact (10/12/12).
    **B1 + S1 fixed**: prepare seeds the solved idle point (`dsp/IdleOperatingPoint.h`), silence
    is silent from sample 1 (was up to −5 dBFS); amp knobs sanitized, smoothers seeded.
  - Power-section plan `docs/superpowers/plans/2026-10-23-power-section-fixes.md`, Tasks 0–5.
    Written without the review; its *Coordination with the DSP review* section maps the overlap.
    Tasks 0–1 done: baseline in `docs/handoff-power-section-fixes.md` (+ `-probe.cpp`).

## Next (combined order; one PR per step, each with a test)
1. Owner: GitHub Actions is down (every run since 2026-10-09 fails in ~3–5 s, no logs). Check
   Actions billing / spending limit. Until fixed, verify locally.
2. Owner: should the EL84s ever leave class A? The baseline shows they never do (no cut-off,
   plate knee never approached, B+ moves 0.1 V, even with every knob on 10) because
   `VOLUME_KNOB_HEADROOM` = 0.21 keeps their grids small. Yes → re-balance headroom vs. grid clamp
   before plan Tasks 3–4; no → descope Tasks 3–4 to cleanup, Task 2 reduces to B2.
3. Phase 1, settled tone unchanged at 44.1k/4×:
   a. B7 (grid-charge rate × 176400/effectiveSR), then B6 + B8 (owner: delay self-oscillation?).
4. Phase 2, changes the tone, then ONE re-calibration and an owner listening check:
   Plan Task 2 **with** B2 (one idle fixed-point solve, shared with B1; replace the tautological
   idle test with one running the real PSU loop), B3, Tasks 3–4 per item 2 (Task 4 with S8:
   bracketed solves), Task 5 (owner: 7W option). Then re-tune D1/A1/B1 and P-* once.
5. E3 last (owner: confirm "repoint the gate, then fix the engine"). Step 1: give `alias_crank`
   (`tools/signoff_gates.json`) a real bright IR (today a skipped placeholder, so E3 runs the IIR
   path). Step 2 alongside S8: harmonic fall-off per nonlinearity (`TriodeWaveshaper.cpp:28`,
   `GridConduction.cpp:39`, `OTSaturation.h:43`; Task 4 adds a plate knee).
6. Phase 3, batched: S2 + S4, S3 + S6, S5 + S7 (owner: raise S7's 1000 ms limit or show clamp?).
7. Owner: delete stale remote branches (keep `main`, `claude/neural-learning-path-slider-kt8dy7`,
   check `fix/effects-toggle-rendering-use-60`); adopt the kit's *Rules* in AGENTS.md?

## Decisions
- DSP spec v2.13 is background only; code, CLAUDE.md, handoffs win. `COMPETITIVE_ROADMAP.md` is live.
- Fix order: tone-neutral fixes first, tone-changing ones together, so the amp is re-calibrated
  once. B1 seeds the solved idle point, not hard-coded values, so B2/Task 2 can't break it
  (B2 must update the pinned idle numbers in `idle_operating_point_tests.cpp`).
- PSU's one-sample current lag is intended (spec §3.11); not a power-section defect.
- Latency: exact integer report (Thiran-rounded, +2–4 samples vs. the old truncated 7/8/8) over a
  smaller but wrong one. Constant across cab modes; the IIR path is padded, not the report split.
- `verify.sh` builds into `build-verify/` (never clobbers `build/`). `bootstrap.sh` omits
  `libwebkit2gtk-4.1-dev` (`JUCE_WEB_BROWSER=0`). sr_render is not built by CI or verify.

## Known issues
- `signoff.py` after B1: A1 −31.6 LUFS (band −17.5…−14.5), B1 0.99% (floor 1.0%), E3 −37.9 dBc
  (≤ −85, MUST-PASS) fail; the other 9 pass, E1 now −200 dBFS (was −70.4). Valid only at 44.1k/4× (B7).
- Switching 15W ↔ 7W live still settles audibly (−44 dBFS at gain 10 in `sr_idle.py` via the 7W
  wrapper, which flips the mode after prepare): the idle point moves. A prepare in 7W is silent.
- E3 is engine aliasing (16th harmonic of 11 kHz folding at 176.4k); 8× OS only reaches −43.5.
- Sag idles warm: +0.77 V (15W) / +1.03 V (7W), reference taken at nominal rails (B2), and never
  reaches the cold side: average plate current *falls* slightly under drive.
- `sr_bloom.py` at gain 0.625 rebounds 3.8 dB (15W) / 4.1 dB (7W) at 25 ms; at gain 1.0 louder input
  (0.2 → 0.4) gives quieter output in `sr_thump.py`. Both point at upstream blocking; not root-caused.
- Settings → About paints a hard-coded "Licensed — Full Version" (`SettingsPanel.cpp:162`).
