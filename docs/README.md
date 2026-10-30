# Documentation

Start here to find the right document, and to know whether you can trust it.

## Which documents are current

- **`dev/`** and **`manual/`** are **maintained**. They describe the code as
  it is. If one disagrees with the code, the doc is wrong; fix it in the same PR.
- **`design/`** and **`records/`** are **dated and frozen**. They record what was
  designed or measured at the time, and are not updated as the code moves on.
- **`plans/`** are working plans; each carries its own status.

When two sources disagree, trust them in this order: **the code** > `CLAUDE.md` >
`records/handoffs/` > `design/`. Never change code to match a design spec.

## Developer

| Doc | What it covers | Status |
|---|---|---|
| [`dev/building.md`](dev/building.md) | Building on Linux and Windows, CMake options, where the plugin lands | maintained |
| [`dev/testing.md`](dev/testing.md) | `verify.sh`, the three test targets, the processor test harness | maintained |
| [`dev/architecture.md`](dev/architecture.md) | Voltage domain, signal chain, source layout, threading, state | maintained |
| [`dev/tone-lock.md`](dev/tone-lock.md) | The frozen sound (`tone-v1`): what the tone lock test guards and how the sound may change | maintained |
| [`dev/tone-signoff.md`](dev/tone-signoff.md) | The tone sign-off protocol: references, gates, how "tone is frozen" is decided | maintained (2026-06; gates in `tools/signoff_gates.json`) |
| [`../tools/README.md`](../tools/README.md) | The sign-off render and analysis tools | maintained |
| [`../CLAUDE.md`](../CLAUDE.md) | Detailed engineering notes and pitfalls (written for coding agents, useful to anyone) | maintained |

## User manual

| Doc | What it covers | Status |
|---|---|---|
| [`manual/README.md`](manual/README.md) | Overview, quick start, keyboard, signal flow | maintained |
| [`manual/controls.md`](manual/controls.md) | GAIN, SHAPE, VOLUME, 15W/7W, CAB, PWR, oversampling | maintained |
| [`manual/effects.md`](manual/effects.md) | UTILITY, BOOST, DELAY, REVERB | maintained |
| [`manual/cabinets-and-irs.md`](manual/cabinets-and-irs.md) | Built-in cab vs IRs, formats, level matching, loader limits | maintained |
| [`manual/presets-and-settings.md`](manual/presets-and-settings.md) | Presets, settings panel, saved state | maintained |
| [`manual/tuner.md`](manual/tuner.md) | The live tuner | maintained |

## Design background

| Doc | What it covers | Status |
|---|---|---|
| [`design/dsp-model-spec-v2.13.md`](design/dsp-model-spec-v2.13.md) | Original DSP design rationale: circuit, voltage domain, numerical hygiene | frozen 2026-03; **superseded** where it disagrees with the code (see its banner) |
| [`design/ui-spec.md`](design/ui-spec.md) | GUI layout, components, colours, typography | partly current; `src/gui/` wins |
| [`design/ui-prototype.jsx`](design/ui-prototype.jsx) | Early React visual mock-up | frozen 2026-06; out of date (old knob, no tuner or effects) |

## Records

Dated defect investigations and reviews. Each one is a measurement you can re-run.

| Doc | What it is about |
|---|---|
| [`records/handoffs/handoff-power-section-calibration.md`](records/handoffs/handoff-power-section-calibration.md) | Power-section calibration (USE-59): gain law, EL84 current, grid clamp, sag starvation |
| [`records/handoffs/handoff-ltp-convergence.md`](records/handoffs/handoff-ltp-convergence.md) | The phase-inverter solver limit cycle and its root-finding replacement |
| [`records/handoffs/handoff-LTP-vcathode-reset.md`](records/handoffs/handoff-LTP-vcathode-reset.md) | LTP `reset()` not restoring the cathode node (resolved) |
| [`records/handoffs/handoff-IR-limiter-aliasing.md`](records/handoffs/handoff-IR-limiter-aliasing.md) | Aliasing in the IR-path output limiter (fixed with a dedicated oversampler) |
| [`records/handoffs/handoff-power-section-fixes.md`](records/handoffs/handoff-power-section-fixes.md) | Measurements for the 2026-10-23 power-section fixes plan |
| [`records/handoffs/handoff-power-section-fixes-probe.cpp`](records/handoffs/handoff-power-section-fixes-probe.cpp) | The probe program behind those measurements |
| [`records/handoffs/handoff-ir-path-sound.md`](records/handoffs/handoff-ir-path-sound.md) | Measurements for the IR-path sound fixes (level match, mode fades) |
| [`records/reviews/review-dsp-2026-10-15.md`](records/reviews/review-dsp-2026-10-15.md) | Full DSP code review, 2026-10-15 (findings B1–B8, S1–S8) |
| [`records/reviews/review-dsp-2026-10-15-probe.cpp`](records/reviews/review-dsp-2026-10-15-probe.cpp) | The probe program behind that review |
| [`records/reviews/review-rt-stress-2026-10-29.md`](records/reviews/review-rt-stress-2026-10-29.md) | Real-time stress review, 2026-10-29 (findings RT-1–RT-6): allocations, locks, deadlines, NaN recovery |
| [`records/roadmap-2026-06-audit.md`](records/roadmap-2026-06-audit.md) | June 2026 engineering audit roadmap (archived; the live plan is [`../ROADMAP.md`](../ROADMAP.md)) |

## Plans

| Plan | Status |
|---|---|
| [`plans/2026-08-02-io-level-trims.md`](plans/2026-08-02-io-level-trims.md) (spec: [`plans/specs/2026-08-02-io-level-trims-design.md`](plans/specs/2026-08-02-io-level-trims-design.md)) | done |
| [`plans/2026-10-23-power-section-fixes.md`](plans/2026-10-23-power-section-fixes.md) | in progress (Tasks 0–1 done) |
| [`plans/2026-10-26-ir-path-sound-fixes.md`](plans/2026-10-26-ir-path-sound-fixes.md) | done |
| [`plans/2026-10-28-open-source-docs-prep.md`](plans/2026-10-28-open-source-docs-prep.md) | in progress |
| [`plans/2026-10-30-rt-safety-fixes.md`](plans/2026-10-30-rt-safety-fixes.md) | not started (fixes RT-1…RT-6) |

Current project status and the next steps are in [`../.agents/STATE.md`](../.agents/STATE.md);
the product roadmap is [`../ROADMAP.md`](../ROADMAP.md).
