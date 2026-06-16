# Silent Riot — Engineering Roadmap

_Audit date: 2026-06-16 · Version at audit: v0.3.4 · Branch: `claude/plugin-audit-roadmap-0f43q6`_

This roadmap is the output of a full read-only audit of the codebase (DSP core, GUI,
build/test/CI). It captures the gap between what the project _claims_ (CLAUDE.md /
README) and what the code _does_, then sequences the work to get to a shippable 1.0.

Findings are tagged **[H]/[M]/[L]** (High/Medium/Low) and reference `file:line`.

---

## Executive summary

The DSP engine is mature and well-tested (190 tests, CI green on `main`). The biggest
risks are **not** in the audio math — they are in:

1. **Missing source-of-truth docs.** `docs/` (DSP spec v2.13, UI spec, JSX prototype)
   and `CREDITS.md` are referenced everywhere in CLAUDE.md/README but **do not exist in
   the repo**. The "authoritative source for all signal-chain math" is absent.
2. **A real-time correctness gap.** `prepareToPlay` never resets DSP state; `reset()`
   methods exist but are dead code.
3. **Cross-thread data races** on the GUI telemetry path and `cachedIRIndex`.
4. **UI features that are wired to nothing** — the entire Settings panel (theme, meter
   ballistics, knob mode, peak-hold, tooltips) saves/restores state but no code consumes
   it; stock IRs are not browsable; "TEMP ~420°C" is a fabricated readout.
5. **No release engineering** — no pluginval, no sanitizers, no packaging/signing, no
   macOS build, VST3 + Standalone only.

Recommended sequencing: **P0 correctness → P1 docs/spec → P2 finish-the-UI →
P3 release engineering → P4 reach/polish.**

---

## P0 — Correctness & safety (do first; these are latent bugs) — ✅ DONE

- [x] **[H] Reset DSP state in `prepareToPlay`.** Added `reset()` to the structs that
  lacked one (`PowerSupplyModel`, `SagEnvelope`, `GridConductionModel`, `TriodeState`,
  `LTPModel`) and a `SilentRiotProcessor::resetDSPState()` helper that clears all filter
  state, integrators, cached plate voltages, and grid charge; called at the end of
  `prepareToPlay`. Regression tests added for PSU/Sag reset.
- [x] **[H] Fix the `getDSPReadout()` data race.** PSU/sag/bias/osFactor are now published
  once per block as `std::atomic` snapshots from the audio thread (`*ForGUI` members);
  `getDSPReadout()` reads only those atomics — no torn reads of live DSP state.
- [x] **[M] Make `cachedIRIndex` atomic.** Now `std::atomic<int>` shared between the
  message and audio threads.
- [x] **[M] Fix/document the stock-IR load path.** `handleAsyncUpdate` now falls back to
  IIR mode for any out-of-range index (no more stale-IR state), and the user-IR-only
  index mapping is documented inline (stock-IR wiring deferred to the "ship real IRs"
  item).
- [x] **[M] Clamp/guard the OT saturation integrator.** Added a `std::isfinite` reset +
  hard clamp to `OT_SAT_FLUX_MAX` on the flux integrator, with a regression test for a
  sustained-DC blow-up. (`OTSaturation.h`)
- [x] **[L] Add a final NaN/Inf guard** on both output paths (per-sample IIR path and
  post-convolution IR path).

## P1 — Restore the source-of-truth documentation

- [x] **[H] Recover the missing `docs/`.** Root cause found: commit `cad9dff`
  ("Add project agent instructions") accidentally deleted them alongside removing the
  Obsidian-vault startup note. Recovered byte-for-byte from `cad9dff^`:
  `docs/Silent Riot DSP Model Spec v2.13.md` (the authoritative signal-chain math),
  `docs/SilentRiot_UI_Spec.md`, and `docs/SilentRiot_Final.jsx`.
- [x] **[H] Restore `CREDITS.md`.** Recovered from `cad9dff^` (originality statement +
  third-party license attribution; referenced by README.md and CLAUDE.md).
- [ ] **[M] Reconcile CLAUDE.md against the code.** Several claims are now stale:
  speaker coupling uses a complementary OnePole crossover, **not** `makeBandPass` (the
  pitfall note is outdated, `SpeakerCoupling.cpp:23-26`); README still describes a
  "Vox-style cut" tone control (replaced by the 3-band Shape EQ); IRLoaderPanel "Category
  tabs" described but not implemented.

## P2 — Finish the half-built UI — mostly ✅

- [x] **[H] Wire up the Settings panel.** All controls now consume their state and
  persist via the ValueTree:
  - Theme **DARK/LIGHT** — `ColourPalette.h` converted to runtime-swappable palette
    globals + `setTheme()`; editor applies it and repaints. (Light values are sensible
    but not yet visually fine-tuned — flagged below.)
  - Meter ballistics FAST/MED/SLOW — `VUMeter::setBallistics()` sets the smoothing factor.
  - Knob mode LINEAR/ROTARY — `NotchKnob::setKnobMode()` switches slider drag style.
  - Peak hold — `VUMeter::setPeakHold()` + held-peak segment marker.
  - Tooltips — editor owns a `TooltipWindow` gated by the toggle; controls have tooltip
    text. `ToggleSwitch` is now a `SettableTooltipClient`.
- [x] **[M] Restore PresetSelector display from the parameter on editor open.**
- [x] **[M] Remove the fabricated "TEMP ~420°C" readout** (dropped the `tempC` field +
  Engine-state row).
- [x] **[L] Fix the dead dashed-border in IRLoaderPanel** (distinct source/dest paths).
- [x] **[L] Fix stale version string** — Settings "About" now uses `JucePlugin_VersionString`
  and shows a clean Product/Version/Format/Developer grid.
- [ ] **[M] Surface stock IRs in the IR loader.** Still user-IR-only; the 8 stock IRs
  aren't browsable, and "Category tabs" (ALL/STOCK/ORANGE/3RD PARTY/USER per UI spec
  §5.7) aren't implemented. **Deferred** — tied to the parked "ship real cabinet IRs"
  item (stock IRs are synthetic placeholders today).
- [x] **[M] Keyboard & accessibility coverage** (main interactive surface). All the
  previously click-only widgets are now keyboard-operable, focus-ringed, and expose an
  `AccessibilityHandler` with the right role: PresetSelector (←/→, comboBox),
  SectionBars (Space/Return, button), the gear (new focusable `GearButton`, button),
  ToggleSwitch (Space/Return, toggleButton with checked state), SegmentedSelector (←/→,
  comboBox). Fixed a latent bug — tab order was set on the **hidden** toggle buttons, so
  the 7W/CAB/PWR switches weren't actually keyboard-reachable; the `ToggleSwitch` is now
  the focusable element. Explicit tab order: preset → gain → shape → volume → 7W → CAB →
  PWR → IR → ADV. _Remaining (deferred):_ IRLoaderPanel's user-IR **list/drop-zone**
  keyboard navigation and the Settings **close-X** (closable via the gear) are still
  pointer-only.
- [x] **[M] _(newly surfaced)_ Presets now recall values.** Added a factory-preset table
  (`sr::Presets::table`, single source of truth — `PresetSelector` reads its names from
  it) and `SilentRiotProcessor::applyPreset()`, which sets GAIN/SHAPE/VOLUME/7W/CAB +
  PRESET_IDX. Applied on explicit selection only (not on state restore, so user tweaks
  saved over a preset survive). The 5 presets have starter values — worth ear-tuning.

## P3 — Release engineering (gate for any public 1.0)

- [ ] **[H] Add a `pluginval` CI job.** No automated VST3 validation today — the
  standard JUCE release gate for state save/restore, parameter automation, threading, and
  fuzzing. Especially valuable given the async IR-loading threading.
- [ ] **[H] Add ASan/UBSan CI job** for the test target. None today; high-value for a
  DSP plugin with manual buffer management and a "no allocation in processBlock"
  invariant. Would catch the data races above under TSan.
- [ ] **[H] Packaging & signing pipeline.** CI uploads raw `.vst3`/`.exe` dirs only — no
  installer (`.pkg`/NSIS/`.deb`), no Windows Authenticode, no macOS notarization, no
  git-tag-driven release automation.
- [ ] **[M] Run tests on Windows** (currently Linux-only) and **run CI on `develop` PRs**
  (workflow only triggers `pull_request` on `main`).
- [ ] **[M] Add FetchContent/build caching.** JUCE 7.0.12 + Catch2 are cloned fresh on
  every run of all 3 jobs — slow and network-flaky.
- [ ] **[M] Validate the Release artifact, not a separate Debug build.** Tests run
  against a Debug build; the shipped Release binary is never test-validated. Also: Linux
  Standalone is built but never uploaded; all artifacts use `if-no-files-found: warn`,
  masking broken packaging.
- [x] **[M] Fix `install-deps.sh` ↔ CI drift** — local script omitted `libxext-dev`,
  which `CMakeLists.txt:159` hard-requires (`X11::Xext`); added so a clean local build
  matches CI. _(Done alongside P0 — it blocked the local verification build.)_
- [ ] **[L] Harden CI**: pin/parameterize the hardcoded VS 2022 _Enterprise_ path in the
  Windows "Verify VST3" step, add `timeout-minutes`, add a `concurrency` group, dedupe
  the apt list copy-pasted between the `build-linux` and `test` jobs.

## P4 — Reach, quality bar & tech-debt cleanup

- [ ] **[M] Decide macOS / AU / LV2 scope.** `FORMATS VST3 Standalone` only; no macOS in
  CI (so no AU and no macOS VST3). This is the biggest reach limiter.
- [ ] **[M] Audit the Linux X11 Shape-Extension window strategy for the plugin case.**
  `XOpenDisplay(nullptr)` opens a _second_ X11 connection per `applyShapeMask`/
  `setWindowBackground` call (`PluginEditor.cpp:254,285,303`) rather than reusing the
  peer's display — fragile when a VST3 is embedded in a host's own X11 connection. Fine
  for standalone; risky for the plugin.
- [ ] **[M] Centralize layout & DSP magic numbers.** GUI layout is entirely hardcoded
  with paint() and resized() positions hand-synced in parallel ladders
  (`SettingsPanel.cpp`, `PluginEditor.cpp:428-479`); duplicate/contradictory panel-height
  constants (`SettingsPanel` declares `maxH=600` but layout hardcodes `420`). On the DSP
  side, cab IIR coefficients (`FilterBank.cpp:17-19`), sag clamps (`SagEnvelope.h:39`),
  and grid-charge taus live as inline literals despite the "all constants in Constants.h"
  convention.
- [ ] **[M] Extract the duplicated coefficient-recalc block** copy-pasted between
  `prepareToPlay` and the oversample-switch commit path (`PluginProcessor.cpp:111-145` vs
  `:216-234`) into one helper — silent-drift risk.
- [ ] **[M] Add a golden-file / null-test** regression for rendered output. A
  `di_sample_82hz.raw` fixture already exists; ongoing "ear-tuning" silently changes
  output with nothing guarding against unintended regressions.
- [ ] **[M] Unit-test `OversampleSwitcher` and `ParamSmoother`** — both flagged
  pop/timing-sensitive in CLAUDE.md, both currently untested.
- [ ] **[L] Add `.clang-format`, `.clang-tidy`, `.editorconfig`** and a CI lint/format
  check. None exist.
- [ ] **[L] Remove dead constants/code:** `V_THERMAL`, `TRIODE_BIAS_N`, `SPK_RE_N`,
  `SPK_FS`, `EL84_GAIN_BASE`, `R_470/R_820/R_500K/R_1M/R_68K/R_1K5`
  (`Constants.h`/`VoltageScale.h`); the unused `segSpanDeg` knob constant (`NotchKnob.h`).

## Parked — explicit "later" items (tracked, not scheduled)

- [ ] **Finalize ear-tuning** of `outputGainCompensation` (1.0), `V1B_RSOURCE_MAX_REAL`
  (150KΩ), `XCUR_SENSITIVITY` (2.0), Shape EQ gain ranges, and dynamics feel —
  per CLAUDE.md "Next" step. **Blocks the neural model.**
- [ ] **Ship real cabinet IRs.** All 8 stock IRs are synthetic placeholders
  (`IRLibrary.h` `generateSyntheticIR`, "until real WAVs arrive").
- [ ] **Neural model.** On hold until the DSP target is frozen (retraining churn).

---

### Status snapshot at audit

- **Tests:** 190 total (177 pure C++ + 13 JUCE) — count verified accurate; ~18 of the
  177 are diagnostic/trace rather than assertion-based.
- **CI:** green on `main`; Linux + Windows build, tests Linux-only/Debug-only.
- **No open issues / no open PRs** on the repo at audit time.
