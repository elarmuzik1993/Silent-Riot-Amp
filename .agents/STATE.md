<!-- Hand-off between sessions on any machine, local or cloud. AGENTS.md imports it, so every
     session starts with it. The hand-off rewrites it; it is not a log, and history belongs in git.
     Keep it under ~40 lines; the SessionStart hook warns past 60. -->
# State

_Updated 2026-10-14_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone). `bash scripts/verify.sh` passes on `main`: static
  checks + Release build + 308/308 ctest. Cloud sessions provision via `.agents/bootstrap.sh`.
- Docs match the code (CLAUDE.md stale-facts pass done). `sr_render` refuses every bad input it
  used to render silently (unknown option, `--in` with `--signal`, out-of-range rate).
- Tone sign-off still open: E3 (MUST-PASS) diagnosed but not fixed; A1/B1 need re-ratifying.

## Next
1. Owner: GitHub Actions is down: every run since 2026-10-09 (main too) fails in ~5 s with no
   logs, i.e. jobs never start. Check Actions billing / spending limit. Until fixed, verify locally.
2. Owner: confirm the E3 plan, "repoint the gate, then fix the engine" (proposed, not agreed).
3. E3, step 1: give `alias_crank` in `tools/signoff_gates.json` a real bright IR (today it is the
   `OPTIONAL_BRIGHT_IR` placeholder, which `signoff.py` skips, so E3 runs the IIR path). The repo
   has no IR WAVs: owner supplies one, or commit a synthetic bright IR as a fixture. Gate IR vs
   IIR parity; move whole-engine aliasing to its own report-only gate with a ratified target.
4. E3, step 2: measure harmonic fall-off of each nonlinearity in isolation; smooth the sharpest
   corner. Suspects: hard plate `std::clamp` (`TriodeWaveshaper.cpp:28`), C1-only grid clamp
   (`GridConduction.cpp:39`), OT flux clamp (`OTSaturation.h:43`). Re-measure P-CRANK after.
5. Docs cleanup (done 2026-10-23, on `docs/power-section-fix-plan`): `ROADMAP.md` archived,
   JSX prototype and io-level-trims plan marked stale/done, `COMPETITIVE_ROADMAP.md` ticked
   against the code. Next for the power section: `docs/superpowers/plans/2026-10-23-power-section-fixes.md`
   (owner to pick the 7W option, Task 5).
6. Owner: delete stale remote branches: all but `main` and `claude/neural-learning-path-slider-kt8dy7`
   (unique hybrid-blend design sketch). `fix/effects-toggle-rendering-use-60` never had a PR; its
   fix looks superseded by the LED-toggle work (unverified), so check it before deleting.
7. Owner: adopt the kit template's *Rules* section (git, branch naming, secrets) in AGENTS.md?

## Decisions
- `verify.sh` builds into `build-verify/` so it never clobbers `build/` from `scripts/build.sh`.
- `bootstrap.sh` omits `libwebkit2gtk-4.1-dev` (in CI's list): `JUCE_WEB_BROWSER=0`, and the full
  verify builds and passes without it.
- sr_render is not built by CI or verify (`-DSR_BUILD_TOOLS=ON`); build it when touching tools/.
- DSP spec v2.13 is background only, not authoritative (2026-10-23): code, CLAUDE.md and handoff
  docs win. It carries a status banner listing the superseded designs; it is not kept in sync.

## Known issues
- E3 = −37.9 dBc (gate ≤ −85, MUST-PASS), reproduced with `sr_render` at P-CRANK, 44.1k, 4× OS.
  It is engine aliasing, not the IR limiter (fixed in `508c842`): spurs on a 400 Hz grid = 16th
  harmonic of 11 kHz folding at 176.4k. 2× OS −4.9, 8× OS −43.5, 48k/4× −29.8. Oversampling alone
  cannot reach −85. The amp's LPs put the 11 kHz reference at −36 dBFS, which inflates the dBc.
- `signoff.py` on `d2315b4` (2026-10-14): A1 −31.6 LUFS (band −17.5…−14.5), B1 0.99% (floor
  1.0%), E1 −70.4 dBFS (≤ −90), E3 −37.9; the other 8 gates pass. The handoff doc's A1 −28.1 /
  E1 −47 are older readings. A1 and the P-* volume positions need re-ratification.
- ~0.5 s startup thump while the PSU settles; it pollutes E1's idle noise floor (unverified here).
