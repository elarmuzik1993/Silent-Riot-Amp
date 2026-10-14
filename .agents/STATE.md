<!-- Hand-off between sessions on any machine, local or cloud. AGENTS.md imports it, so every
     session starts with it. The hand-off rewrites it; it is not a log, and history belongs in git.
     Keep it under ~40 lines; the SessionStart hook warns past 60. -->
# State

_Updated 2026-10-14_

## Now
- Repo kit (STATE.md, verify.sh, bootstrap.sh, AGENTS.md sections) on `chore/repo-kit`, awaiting review.
- E3 aliasing diagnosed, not fixed (see Known issues). Proposed: repoint the gate, then fix the engine (owner to confirm).

## Next
1. Review and merge `chore/repo-kit`.
2. E3, step 1: give `alias_crank` in `tools/signoff_gates.json` a real bright IR (today it is the
   `OPTIONAL_BRIGHT_IR` placeholder, which `signoff.py` skips, so E3 runs the IIR path). Gate IR vs
   IIR parity; move whole-engine aliasing to its own report-only gate with a ratified target.
3. E3, step 2: measure harmonic fall-off of each nonlinearity in isolation; smooth the sharpest
   corner. Suspects: hard plate `std::clamp` (`TriodeWaveshaper.cpp:28`), C1-only grid clamp
   (`GridConduction.cpp:39`), OT flux clamp (`OTSaturation.h:43`). Re-measure P-CRANK after.
4. Owner: adopt the kit template's *Rules* section (git, branch naming, secrets) in AGENTS.md?
5. Owner: is `docs/superpowers/plans/2026-08-02-io-level-trims.md` stale? 35 tasks unchecked, but
   the INPUT/OUTPUT trims are in `PluginProcessor.cpp` and CLAUDE.md calls them shipped.
6. CLAUDE.md still describes the X11 shape-mask window; `PluginEditor.cpp:282` says it was replaced.

## Decisions
- `verify.sh` builds into `build-verify/` so it never clobbers `build/` from `scripts/build.sh`.

## Known issues
- E3 = −37.9 dBc (gate ≤ −85, MUST-PASS), reproduced with `sr_render` at P-CRANK, 44.1k, 4× OS.
  It is engine aliasing, not the IR limiter (fixed in `508c842`): spurs on a 400 Hz grid = 16th
  harmonic of 11 kHz folding at 176.4k. 2× OS −4.9, 8× OS −43.5, 48k/4× −29.8. Oversampling alone
  cannot reach −85. The amp's LPs put the 11 kHz reference at −36 dBFS, which inflates the dBc.
- A1 clean loudness and P-* volume positions need re-ratification; B1 clean THD 0.968% vs 1.0%.
- ~0.5 s startup thump while the PSU settles; it pollutes E1's idle noise floor.
