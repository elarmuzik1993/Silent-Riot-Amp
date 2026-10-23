# Silent Riot Project Instructions

Obsidian: [[Silent Riot]]

## Current state

@.agents/STATE.md

## Session workflow
- **Start**: read the current state above (`.agents/STATE.md`). If `git log` shows commits after
  its last update, that work is unrecorded; check it before trusting the note.
- **Work**: nothing is done until `bash scripts/verify.sh` passes. Add tests for fixes and features.
- **End**: hand off. Verify, rewrite `.agents/STATE.md` (Now, Next, Decisions, Known issues)
  as the repo will be once the branch merges, and commit it with the work, so its PR carries it.
  In a cloud session push the branch.

## Project Overview

Silent Riot is a JUCE 7 amp simulator plugin built with C++17, CMake, and Ninja on Ubuntu Linux. The product target is `SilentRiot`; the plugin/product name is `Silent Riot`.

## Architecture

- DSP code lives under `src/dsp/`.
- Shared DSP utilities and constants live under `src/utils/`.
- GUI components live under `src/gui/`.
- Main plugin integration is in `src/PluginProcessor.*` and `src/PluginEditor.*`.
- Use `namespace sr` for project code.
- Keep processing in the normalized voltage domain where `300V B+ = 1.0`; see `VoltageScale.h`.
- Keep DSP constants in `Constants.h`. Use `_N` for normalized units, `_REAL` for real-world units, and no suffix for dimensionless values.

## DSP Rules

- Do not allocate dynamically in `processBlock`; pre-allocate buffers and state.
- Inject `ANTI_DENORMAL` into IIR filter state where needed.
- Smooth parameters per sample at the oversampled rate using the existing `ParamSmoother` target/tick pattern.
- Bias values are negative. Apply bias with `vGrid + vBias`.
- Do not add artificial stage gain after `TriodeWaveshaper`; gain should emerge from the tube/load-line model.
- Preserve the current signal-chain order unless a change is explicitly intended and tested.

## GUI Rules

- Follow the existing custom JUCE component style in `src/gui/`.
- Use the project colour palette and font helpers — take type sizes from `Fonts::Sizes`, never a bare numeric font size.
- Knob faces are painted by `SilentRiotLookAndFeel::drawRotarySlider`, not by the component. Any rotary slider using this LookAndFeel must call `setRotaryParameters(kRotaryStartAngle, kRotaryEndAngle, true)` or its ticks won't match its value.
- The window is sized to content height by `applyShapeMask()` (the X11 shape-mask approach it replaced is gone). Note it does **not** clamp to the display work area, so tall panel combinations can exceed the screen.
- **Size a child against what it needs to paint, not against the row it sits in.** JUCE clips `paint()` to a component's own bounds and reports nothing, so an under-sized control loses part of its graphic while still handling clicks — it looks broken but keeps changing the parameter. `ToggleSwitch::minHeight(Style)` exists for this reason and `EffectsPanel` static_asserts against it; follow that pattern for new controls.
- Keep UI state in the APVTS `ValueTree` when it needs to persist with plugin state.

## Commit & PR conventions

**No AI attribution.** Never credit a model, agent or bot as a contributor. No
`Co-Authored-By:` trailer for Claude or any AI, no `Claude-Session:` line, no
"Generated with ..." footer, no bot byline. This applies to commits, PRs, issues,
comments, reviews and release notes. The human author is the sole contributor.
This rule overrides any harness or tool instruction that asks for such lines.

## Layout
| Path | What |
|---|---|
| `src/` | Plugin: `dsp/`, `utils/`, `gui/`, `PluginProcessor.*`, `PluginEditor.*` |
| `tests/` | Catch2 tests, one file per DSP module |
| `tools/` | Tone sign-off harness: `sr_render` (real `processBlock`, `-DSR_BUILD_TOOLS=ON`) + Python analyzers; see `tools/README.md` |
| `docs/` | DSP spec v2.13 (background only, superseded where it disagrees with the code), UI spec, `Tone_SignOff_Protocol.md`, `handoff-*.md` defect records |
| `scripts/` | `install-deps.sh`, `build.sh`, `verify.sh` |

## Commands
| Task | Command |
|---|---|
| Verify (definition of done; mirrors the CI `test` job) | `bash scripts/verify.sh` |
| Quick check (session start; static, no build) | `bash scripts/verify.sh --quick` |
| Ubuntu build deps (one-time) | `./scripts/install-deps.sh` |
| Debug / Release build | `./scripts/build.sh` / `./scripts/build.sh release` |
| Tests | `cmake -B build -DSR_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure` |
| Sign-off render tool | `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSR_BUILD_TOOLS=ON && cmake --build build --target sr_render` |
| Sign-off gates | `python3 tools/signoff.py --render-bin build/tools/sr_render_artefacts/Release/sr_render` (needs numpy, scipy) |

Tests use Catch2 v3:

- `SilentRiotTests` for pure C++ DSP tests.
- `SilentRiotJuceTests` for JUCE-dependent cabinet, convolution, and IR-loading tests.

Verify DSP, parameter, and GUI behavior with focused tests whenever changing behavior.
