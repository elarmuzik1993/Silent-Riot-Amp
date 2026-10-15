<!-- Hand-off between sessions on any machine, local or cloud. AGENTS.md imports it, so every
     session starts with it. The hand-off rewrites it; it is not a log, and history belongs in git.
     Keep it under ~40 lines; the SessionStart hook warns past 60. -->
# State

_Updated 2026-10-15_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone). `bash scripts/verify.sh` passes: static checks +
  Release build + 308/308 ctest. Cloud sessions provision via `.agents/bootstrap.sh`.
- DSP review in `docs/review-dsp-2026-10-15.md`: 8 blocking (B1–B8), 8 suggestions (S1–S8), each
  measured and independently re-checked, refs against `b8e20a9`. Nothing fixed yet. It carries a
  *Suggested fix order*; its probe (`docs/review-dsp-2026-10-15-probe.cpp`) reproduces B1–B3.
- Tone sign-off open: E3 (MUST-PASS) diagnosed, not fixed; A1/B1 need re-ratifying.

## Next
1. Owner: GitHub Actions is down: every run since 2026-10-09 fails in ~5 s with no logs (jobs
   never start). Check Actions billing / spending limit. Until then, verify locally.
2. Review fix order, phase 1 step 1: B5 + B4. Set `OversampleSwitcher::currentMode` from the
   OVERSAMPLE parameter in `prepareToPlay` and reset its transition state; make the latency report
   match the active path (pad the IIR path or report per path; round, don't truncate).
3. Then B1 + S1 (clears E1), B7, B6 + B8; then B2 + B3 and one re-calibration pass (D1/A1/B1,
   P-* positions), then S8 with E3 step 2; S-items batched last. One PR per step, with a test.
   Owner decides B8 (allow self-oscillation?), approves B2/B3 by ear, picks S7's fix.
4. Owner: confirm the E3 plan, "repoint the gate, then fix the engine" (proposed, not agreed).
   Step 1: give `alias_crank` (`tools/signoff_gates.json`) a real bright IR; today it is the
   `OPTIONAL_BRIGHT_IR` placeholder that `signoff.py` skips, so E3 measures the IIR path.
   Step 2 (with S8): measure each nonlinearity's harmonic fall-off; suspects are the plate clamp
   (`TriodeWaveshaper.cpp:28`), the grid clamp (`GridConduction.cpp:39`), the OT flux clamp.
5. Owner: delete or mark done `docs/superpowers/plans/2026-08-02-io-level-trims.md`; tick the
   done items in `COMPETITIVE_ROADMAP.md` Phase A; delete stale remote branches (keep `main` and
   `claude/neural-learning-path-slider-kt8dy7`; check `fix/effects-toggle-rendering-use-60` first);
   decide whether AGENTS.md adopts the kit template's *Rules* section.

## Decisions
- `verify.sh` builds into `build-verify/` so it never clobbers `build/` from `scripts/build.sh`.
- `bootstrap.sh` omits `libwebkit2gtk-4.1-dev` (`JUCE_WEB_BROWSER=0`; verify passes without it).
- sr_render is not built by CI or verify (`-DSR_BUILD_TOOLS=ON`); build it when touching tools/.
- Fix order: settled-tone-neutral fixes first, tone-changing ones (B2, B3) together, so the amp is
  re-calibrated once. B1 seeds the solved idle point, not hard-coded values, so B2 can't break it.

## Known issues
- E3 −37.9 dBc (≤ −85, MUST-PASS) at P-CRANK 44.1k/4×: engine aliasing (16th harmonic of 11 kHz
  folding at 176.4k). Oversampling alone cannot reach −85 (8× gives −43.5).
- `signoff.py` on `d2315b4`: A1 −31.6 LUFS (band −17.5…−14.5), B1 0.99% (floor 1.0%), E1
  −70.4 dBFS (≤ −90), E3 −37.9; the other 8 gates pass.
- E1 is the startup transient (review B1), not a noise floor: settled, the amp idles at −269
  dBFS. Fixing only the preamp filter reset or only the PSU reset leaves E1 near −68 dBFS.
- Sign-off numbers hold only at 44.1 kHz / 4×: grid-charge blocking scales with the rate (B7).
- `CLAUDE.md`'s *Next* line still blames the startup thump on the PSU alone; correct it with B1.
