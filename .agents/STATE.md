# State

_Updated 2026-10-26_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPLv3, going open source with optional donations
  (no paid tier). `bash scripts/verify.sh` passes: static + Release build + 308/308 ctest (2026-10-26).
- `COMPETITIVE_ROADMAP.md` is the live plan (open-source 1.0; Phase C = installers + support link);
  `ROADMAP.md` archived; DSP spec v2.13 background only; JSX prototype out of date.
- History rewritten 2026-10-26: every commit authored `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>`,
  AI trailers stripped. Commit with that identity (the cloud env sets `aidevblock@gmail.com`; override it).
- Power-section fixes planned in `docs/superpowers/plans/2026-10-23-power-section-fixes.md`, none built.
- Tone sign-off open: E3 (MUST-PASS) diagnosed not fixed; A1/B1 need re-ratifying.

## Next
1. Owner: delete every remote branch except `main`, `docs/dsp-review`, `fix/ir-restore-and-samples`,
   `feat/neural-hybrid-blend-sketch`. The others hold pre-rewrite history (old emails, AI trailers);
   the cloud proxy refuses branch deletion. Re-clone any local copy (hashes changed).
2. Owner: make the repo public (Settings → General → Danger Zone).
3. Owner: pick a donation platform (GitHub Sponsors / Ko-fi); then add `.github/FUNDING.yml`, a
   README "Support" section and an About-panel link (roadmap Phase C).
4. Owner: GitHub Actions fails every run since 2026-10-09 (~3–5 s, no logs); likely billing.
   Recheck once public (free standard runners). Until then verify locally.
5. Owner: pick the 7W option (power plan Task 5). Not blocking Tasks 0–2.
6. Power plan Task 0 (baseline: verify, build `sr_render`, record `signoff.py`/thump/idle/bloom),
   then Task 1 (stale comment), Task 2 (conduction-scaled PSU/sag currents).
7. Owner: confirm the E3 plan, "repoint the gate, then fix the engine" (proposed, not agreed).
8. E3 step 1: give `alias_crank` (`tools/signoff_gates.json`) a real bright IR (today a skipped
   placeholder, so E3 runs the IIR path); move whole-engine aliasing to a report-only gate.
9. E3 step 2: measure harmonic fall-off per nonlinearity; smooth the sharpest corner
   (`TriodeWaveshaper.cpp:28`, `GridConduction.cpp:39`, `OTSaturation.h:43`); coordinate with power Task 4.

## Decisions
- Open source + optional donations (2026-10-26): no audience to sell to yet, and GPL source makes a
  license gate moot. Never gate features, nag, or add a trial.
- DSP spec v2.13 is background: several designs were root-caused as bugs in USE-59. Code wins.
- PSU's one-sample current lag is intended (spec §3.11).
- `verify.sh` builds into `build-verify/`; `bootstrap.sh` omits webkit (`JUCE_WEB_BROWSER=0`).
- sr_render is not built by CI or verify (`-DSR_BUILD_TOOLS=ON`); build it when touching tools/.

## Known issues
- Old commits stay reachable via closed PRs' `refs/pull/*` (GitHub-owned); only GitHub Support or a fresh repo removes them.
- E3 = −37.9 dBc (≤ −85): engine aliasing, not the IR limiter; 8× OS only reaches −43.5.
- `signoff.py` on `d2315b4`: A1 −31.6 LUFS, B1 0.99%, E1 −70.4 dBFS, E3 −37.9 fail; other 8 pass.
- EL84s have no plate load line (Vp = PSU B+), so the plate knee never engages (power Task 4).
- ~0.5 s startup thump while the PSU settles; pollutes E1's idle floor (unverified).
