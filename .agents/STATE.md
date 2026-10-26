# State

_Updated 2026-10-26_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone). `bash scripts/verify.sh` passes on `main`: static
  checks + Release build + 308/308 ctest (2026-10-26). Cloud sessions provision via `.agents/bootstrap.sh`.
- Docs: the DSP spec v2.13 is background only (status banner lists superseded designs);
  `ROADMAP.md` is archived; `COMPETITIVE_ROADMAP.md` is the live plan, ticked against the code
  2026-10-23. The JSX prototype is marked out of date.
- Power section reviewed 2026-10-23; fixes planned in
  `docs/superpowers/plans/2026-10-23-power-section-fixes.md`, none built yet.
- Cabinet/IR reviewed 2026-10-26; IR sound fixes planned in
  `docs/superpowers/plans/2026-10-26-ir-path-sound-fixes.md`, none built yet. The loader-panel
  bugs found in the same review are not planned (see Known issues).
- Tone sign-off still open: E3 (MUST-PASS) diagnosed but not fixed; A1/B1 need re-ratifying.

## Next
1. Owner: GitHub Actions is down: every run since 2026-10-09 fails in ~3–5 s with no logs (seen
   again 2026-10-23). Check Actions billing / spending limit. Until fixed, verify locally.
2. Owner: pick the 7W option in the power-section plan (Task 5: leave / lower B+ / match a
   reference schematic). Not blocking Tasks 0–2.
3. Power-section plan Task 0 (baseline: verify + build `sr_render` + record `signoff.py`, thump,
   idle, bloom), then Task 1 (stale comment) and Task 2 (conduction-scaled PSU/sag currents).
4. Owner: confirm the E3 plan, "repoint the gate, then fix the engine" (proposed, not agreed).
5. E3, step 1: give `alias_crank` in `tools/signoff_gates.json` a real bright IR (today the
   `OPTIONAL_BRIGHT_IR` placeholder, which `signoff.py` skips, so E3 runs the IIR path). Owner
   supplies one, or commit a synthetic bright IR fixture. Move whole-engine aliasing to its own
   report-only gate with a ratified target.
6. E3, step 2: measure harmonic fall-off per nonlinearity; smooth the sharpest corner. Suspects:
   hard plate `std::clamp` (`TriodeWaveshaper.cpp:28`), C1-only grid clamp (`GridConduction.cpp:39`),
   OT flux clamp (`OTSaturation.h:43`). Coordinate with power-plan Task 4 (adds a plate knee).
7. Owner: delete stale remote branches: all but `main` and `claude/neural-learning-path-slider-kt8dy7`
   (unique hybrid-blend sketch). Check `fix/effects-toggle-rendering-use-60` first (unverified
   whether the LED-toggle work superseded it).
8. Owner: adopt the kit template's *Rules* section (git, branch naming, secrets) in AGENTS.md?
9. Owner: IR plan decisions D1 (level target; recommended: match the built-in cab) and D2 (IR
   length cap; recommended: keep 8192 samples, add a fade). D1 blocks IR plan Task 2 only.
10. IR plan Task 0 (fixtures `tests/fixtures/ir/`, `tools/sr_irpath.py`, baseline), then Task 1
   (install the active IR synchronously in `prepareToPlay`). Its `bright.wav` fixture is the
   synthetic bright IR item 5 needs.

## Decisions
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
- Settings → About paints a hard-coded "Licensed — Full Version" (`SettingsPanel.cpp:162`); no licensing exists.
- ~0.5 s startup thump while the PSU settles; it pollutes E1's idle noise floor (unverified).
- IR mode is ~11 dB quieter than the built-in cab (JUCE `Normalise::yes` = 0.125/√energy) and
  ~3 dB louder per doubling of host rate. The first IR after `prepare` plays un-cabbed for ~70 ms;
  mode switches are hard cuts. Measured 2026-10-26; IR plan Tasks 1–3.
- IR loader panel: the list can't scroll (wheel delta truncates to 0, `IRLoaderPanel.cpp:116`),
  so IRs 9–32 are unreachable; `.ir` is accepted but unreadable while AIFF/FLAC are refused; no
  remove and no way back to the built-in cab; the panel never refreshes after automation or a
  state load. Not planned yet.
- CLAUDE.md's `IR_INDEX` lines are stale: 1–32 are user slots (no stock IRs reachable), and
  `loadUserIR` doesn't go through `CabinetSim::loadIRFromFile`.
