# State

_Updated 2026-10-26_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone). `bash scripts/verify.sh` passes on `main`: static
  checks + Release build + 308/308 ctest (2026-10-23). Cloud sessions provision via `.agents/bootstrap.sh`.
- Docs: the DSP spec v2.13 is background only (status banner lists superseded designs);
  `ROADMAP.md` is archived; `COMPETITIVE_ROADMAP.md` is the live plan, ticked against the code
  2026-10-23. The JSX prototype is marked out of date.
- Power section reviewed 2026-10-23; fixes planned in
  `docs/superpowers/plans/2026-10-23-power-section-fixes.md`, none built yet.
- Going open source (2026-10-26): GPLv3 + optional donations, no paid tier. History rewritten
  and force-pushed: every commit is `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>`,
  AI trailers stripped. Kept branches: `main`, `docs/dsp-review` (#31),
  `fix/ir-restore-and-samples` (#33), `feat/neural-hybrid-blend-sketch` (ex `claude/neural-learning-path-slider-kt8dy7`).
  Roadmap Phase C is now release readiness (installers, support link), not licensing.
- Tone sign-off still open: E3 (MUST-PASS) diagnosed but not fixed; A1/B1 need re-ratifying.

## Next
1. Owner: delete the 20 stale remote branches. They still hold the pre-rewrite history (old
   emails, AI trailers); the cloud session's proxy refused branch deletion. Anything that is not
   one of the four kept branches above goes.
2. Owner: make the repo public (Settings → General → Danger Zone). Old commits stay reachable
   through closed PRs' `refs/pull/*` (GitHub-owned, read-only); only GitHub Support or a fresh
   repo removes those. Then pick a donation platform (Sponsors / Ko-fi) for roadmap Phase C.
3. Owner: GitHub Actions is down: every run since 2026-10-09 fails in ~3–5 s with no logs (seen
   again 2026-10-23). Likely billing; a public repo gets free standard-runner minutes. Until
   fixed, verify locally.
4. Owner: pick the 7W option in the power-section plan (Task 5: leave / lower B+ / match a
   reference schematic). Not blocking Tasks 0–2.
5. Power-section plan Task 0 (baseline: verify + build `sr_render` + record `signoff.py`, thump,
   idle, bloom), then Task 1 (stale comment) and Task 2 (conduction-scaled PSU/sag currents).
6. Owner: confirm the E3 plan, "repoint the gate, then fix the engine" (proposed, not agreed).
7. E3, step 1: give `alias_crank` in `tools/signoff_gates.json` a real bright IR (today the
   `OPTIONAL_BRIGHT_IR` placeholder, which `signoff.py` skips, so E3 runs the IIR path). Owner
   supplies one, or commit a synthetic bright IR fixture. Move whole-engine aliasing to its own
   report-only gate with a ratified target.
8. E3, step 2: measure harmonic fall-off per nonlinearity; smooth the sharpest corner. Suspects:
   hard plate `std::clamp` (`TriodeWaveshaper.cpp:28`), C1-only grid clamp (`GridConduction.cpp:39`),
   OT flux clamp (`OTSaturation.h:43`). Coordinate with power-plan Task 4 (adds a plate knee).
9. Owner: adopt the kit template's *Rules* section (git, branch naming, secrets) in AGENTS.md?

## Decisions
- Open source (GPLv3) with optional donations, not a paid plugin (2026-10-26): no leverage to
  sell yet, and GPL source makes a license gate moot. Never gate features or add a trial.
- DSP spec v2.13 is background, not authoritative: unmaintained since 2026-03 and several of its
  designs were root-caused as bugs in USE-59. Code, CLAUDE.md and handoff docs win; not kept in sync.
- PSU's one-sample current lag is intended (spec §3.11); not a power-section defect.
- `verify.sh` builds into `build-verify/` so it never clobbers `build/` from `scripts/build.sh`.
- `bootstrap.sh` omits `libwebkit2gtk-4.1-dev`: `JUCE_WEB_BROWSER=0`; verify passes without it.
- sr_render is not built by CI or verify (`-DSR_BUILD_TOOLS=ON`); build it when touching tools/.

## Known issues
- E3 = −37.9 dBc (gate ≤ −85), P-CRANK, 44.1k, 4× OS: engine aliasing (16th harmonic of 11 kHz
  folding at 176.4k), not the IR limiter. 8× OS only reaches −43.5; oversampling alone can't fix it.
- `signoff.py` on `d2315b4`: A1 −31.6 LUFS (band −17.5…−14.5), B1 0.99% (floor 1.0%), E1 −70.4
  dBFS (≤ −90), E3 −37.9; other 8 pass. A1 and the P-* volume positions need re-ratification.
- EL84s have no plate load line (Vp = PSU B+), so the plate knee never engages (power plan Task 4).
- ~0.5 s startup thump while the PSU settles; it pollutes E1's idle noise floor (unverified).
