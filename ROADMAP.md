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

- [x] **[H] Add a `pluginval` CI job.** _(PR #4)_ Validates the shipped Linux VST3
  (strictness 8, in-process, under Xvfb) — the standard JUCE release gate for state
  save/restore, parameter automation, threading, and fuzzing.
- [x] **[H] Add ASan/UBSan CI job** for the test target. _(PR #4)_ Builds the test targets
  with `-fsanitize=address,undefined -fno-sanitize-recover=all` and runs ctest.
  invariant. Would catch the data races above under TSan.
- [~] **[H] Packaging & signing pipeline.** _(PR #4, partial)_ Git-tag-driven release
  automation added (`release.yml`): builds Release on Linux + Windows, packages versioned
  archives with SHA256 sums, publishes a GitHub Release on `v*` tags. **Still open:**
  OS installers (`.pkg`/NSIS/`.deb`), Windows Authenticode, macOS notarization — all need
  maintainer-provided certs/secrets (placeholder slots are in the workflow).
- [x] **[M] Run tests on Windows** (was Linux-only) and **run CI on `develop` PRs**.
  _(PR #4)_ Added a `windows-latest` test job; `pull_request` now triggers on `main` and
  `develop`.
- [x] **[M] Add FetchContent/build caching.** _(PR #4)_ ccache on every compile job + a
  persistent `FETCHCONTENT_BASE_DIR` cache so JUCE/Catch2 aren't re-cloned each run.
- [x] **[M] Validate the Release artifact, not a separate Debug build.** _(PR #4)_ The
  test job now configures Release; pluginval runs on the actual built VST3; the Standalone
  is uploaded; all uploads use `if-no-files-found: error`.
- [x] **[M] Fix `install-deps.sh` ↔ CI drift** — local script omitted `libxext-dev`,
  which `CMakeLists.txt:159` hard-requires (`X11::Xext`); added so a clean local build
  matches CI. _(Done alongside P0 — it blocked the local verification build.)_
- [x] **[L] Harden CI** _(PR #4)_: Windows "Verify VST3" step now locates MSVC via
  `vswhere` (no hardcoded VS edition path); added `timeout-minutes` on every job, a
  `concurrency` group with cancel-in-progress, and deduped the apt list into a composite
  action (`.github/actions/setup-linux-deps`).

## P4 — Reach, quality bar & tech-debt cleanup

- [x] **[M] Decide macOS / AU / LV2 scope.** **Decision (2026-06-19): stay VST3 +
  Standalone for now; macOS / AU / LV2 deferred.** macOS VST3 + AU need a macOS CI runner
  and Apple signing/notarization tooling; revisit once those are available.
- [ ] **[M] Audit the Linux X11 Shape-Extension window strategy for the plugin case.**
  `XOpenDisplay(nullptr)` opens a _second_ X11 connection per `applyShapeMask`/
  `setWindowBackground` call (`PluginEditor.cpp:254,285,303`) rather than reusing the
  peer's display — fragile when a VST3 is embedded in a host's own X11 connection. Fine
  for standalone; risky for the plugin.
- [~] **[M] Centralize layout & DSP magic numbers.** **DSP side done** _(PR #5)_: cab IIR
  coefficients, sag-clamp, OT LP Q, and grid-charge taus are now named constants in
  `Constants.h`. **Still open:** GUI layout is hardcoded with paint()/resized() positions
  hand-synced in parallel ladders (`SettingsPanel.cpp`, `PluginEditor.cpp:428-479`), and
  the duplicate/contradictory panel-height constants (`SettingsPanel` `maxH=600` vs layout
  `420`).
- [x] **[M] Extract the duplicated coefficient-recalc block** _(PR #5)_ → single
  `SilentRiotProcessor::recalcRateDependentCoeffs()` shared by `prepareToPlay` and the
  oversample-switch commit path.
- [~] **[M] Add a golden-file / null-test** regression for rendered output. _(PR #5,
  partial)_ Added `tests/regression_tests.cpp`: a chain-render guard asserting finite +
  limiter-bounded + deterministic + stable-under-drive (the less-brittle alternative to a
  strict golden file, which would fight the ongoing ear-tuning). **Still open:** a true
  null/golden against the real `processBlock` (needs a plugin-host test target). _Note:_
  the reconstructed chain shows a small idle DC + slow limit-cycle at max gain — likely a
  harness artifact (omits PSU/sag, comp=1.5); worth a check against real `processBlock`.
- [x] **[M] Unit-test `OversampleSwitcher` and `ParamSmoother`** _(PR #5)_ — +6 / +5
  cases covering the deferred crossfade state machine + fade envelope and the one-pole
  smoother's convergence/time-constant behaviour.
- [~] **[L] Add `.clang-format`, `.clang-tidy`, `.editorconfig`** _(PR #5, partial)_ —
  config files added, tuned to the existing hand-aligned style. **Still open:** the CI
  lint/format check + a baseline reformat decision (the codebase uses manual column
  alignment, so a blanket reformat would be invasive — needs maintainer buy-in).
- [~] **[L] Remove dead constants/code** _(PR #5, partial)_ — removed the 8 referenced
  nowhere: `V_THERMAL`, `R_470`, `R_820`, `EL84_GAIN_BASE`, `TRIODE_BIAS_N`, `SPK_RE_N`,
  `SPK_FS`, `segSpanDeg`. **Kept** `R_1K5/R_68K/R_500K/R_1M`: still exercised by the
  VoltageScale resistance-reference-table tests, so removing them means deleting passing
  spec-characterization tests — left for a separate call.

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
