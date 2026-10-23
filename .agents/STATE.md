<!-- Hand-off between sessions on any machine, local or cloud. AGENTS.md imports it, so every
     session starts with it. The hand-off rewrites it; it is not a log, and history belongs in git.
     Keep it under ~40 lines; the SessionStart hook warns past 60. -->
# State

_Updated 2026-10-23_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone). `bash scripts/verify.sh` passes: static checks +
  Release build + 308/308 ctest. Cloud sessions provision via `.agents/bootstrap.sh`.
- Two work lists, nothing fixed yet, **one combined order below**:
  - DSP review `docs/review-dsp-2026-10-15.md`: B1–B8 blocking, S1–S8 suggestions, measured and
    re-checked. Line refs against `b8e20a9`; still valid (no src/tests change since).
  - Power-section plan `docs/superpowers/plans/2026-10-23-power-section-fixes.md`, Tasks 0–5.
    Written without the review; its *Coordination with the DSP review* section maps the overlap.
- Docs: DSP spec v2.13 is background only; `COMPETITIVE_ROADMAP.md` is the live plan.

## Next (combined order; one PR per step, each with a test)
1. Owner: GitHub Actions is down (every run since 2026-10-09 fails in ~3–5 s, no logs). Check
   Actions billing / spending limit. Until fixed, verify locally.
2. Phase 1, settled tone unchanged at 44.1k/4×:
   a. Plan Task 0 (baseline: `signoff.py`, thump, idle, bloom) + Task 1 (stale comment).
   b. Review B5 + B4: apply the saved OVERSAMPLE mode at prepare; latency report per active path.
   c. B1 + S1: seed filters/PSU/sag from a solved idle point (clears E1); sanitize + seed smoothers.
   d. B7 (grid-charge rate × 176400/effectiveSR), then B6 + B8 (owner: delay self-oscillation?).
3. Phase 2, changes the tone, then ONE re-calibration and an owner listening check:
   Plan Task 2 **together with** B2 (one idle fixed-point solve, shared with B1's seeding; the
   existing idle test is tautological, replace it with one that runs the real PSU loop), B3,
   Task 3, Task 4 with S8 (bracketed solves, not the 0.5-damped plate loop), Task 5 (owner picks
   the 7W option). Then re-tune D1/A1/B1 and the P-* positions once.
4. E3 last (owner: confirm "repoint the gate, then fix the engine"). Step 1: give `alias_crank`
   (`tools/signoff_gates.json`) a real bright IR (today a skipped placeholder, so E3 runs the IIR
   path). Step 2 alongside S8: harmonic fall-off per nonlinearity (`TriodeWaveshaper.cpp:28`,
   `GridConduction.cpp:39`, `OTSaturation.h:43`; Task 4 adds a plate knee).
5. Phase 3, batched: S2 + S4, S3 + S6, S5 + S7 (owner: raise S7's 1000 ms limit or show clamp?).
6. Owner: delete stale remote branches (keep `main`, `claude/neural-learning-path-slider-kt8dy7`;
   check `fix/effects-toggle-rendering-use-60` first); adopt the kit's *Rules* in AGENTS.md?

## Decisions
- DSP spec v2.13 is background, not authoritative; code, CLAUDE.md and handoff docs win.
- Fix order: tone-neutral fixes first, tone-changing ones together, so the amp is re-calibrated
  once. B1 seeds the solved idle point, not hard-coded values, so B2/Task 2 can't break it.
- PSU's one-sample current lag is intended (spec §3.11); not a power-section defect.
- `verify.sh` builds into `build-verify/` so it never clobbers `build/` from `scripts/build.sh`.
- `bootstrap.sh` omits `libwebkit2gtk-4.1-dev` (`JUCE_WEB_BROWSER=0`; verify passes without it).
- sr_render is not built by CI or verify (`-DSR_BUILD_TOOLS=ON`); build it when touching tools/.

## Known issues
- `signoff.py` on `d2315b4`: A1 −31.6 LUFS (band −17.5…−14.5), B1 0.99% (floor 1.0%), E1
  −70.4 dBFS (≤ −90), E3 −37.9 dBc (≤ −85, MUST-PASS); the other 8 pass. Valid only at 44.1k/4× (B7).
- E1 is the startup burst (B1: preamp filter reset + PSU reset), not a noise floor; settled, the
  amp idles at −269 dBFS. `CLAUDE.md`'s *Next* line still blames the PSU alone; fix with B1.
- E3 is engine aliasing (16th harmonic of 11 kHz folding at 176.4k); 8× OS only reaches −43.5.
- Sag idles warm: +0.77 V (15W) / +1.03 V (7W), reference taken at nominal rails (B2).
- EL84s have no plate load line (Vp = PSU B+), so the plate knee never engages (Task 4).
- Settings → About paints a hard-coded "Licensed — Full Version" (`SettingsPanel.cpp:162`).
