# Contributing to Silent Riot

Thanks for your interest. Silent Riot is a one-amp plugin built for depth rather than breadth,
so the most useful contributions are the ones that make that one amp more correct, more stable
or easier to use.

## What helps most

- **Bug reports**: crashes, host problems, glitches, wrong behaviour. Use the *Bug report*
  issue form and include your host, OS, sample rate and buffer size.
- **Tone feedback**: what sounds off, at which settings, against which reference. A short
  **DI clip** (the dry guitar, before the plugin) is worth more than any description, because
  it lets us render exactly what you heard. Use the *Tone feedback* issue form.
- **Focused fixes**: a small PR that fixes one thing and comes with a test.

For larger changes (a new feature, a DSP redesign, a new effect), open an issue first so we can
agree on the approach before you spend time on it. Check [`ROADMAP.md`](ROADMAP.md) for what is
already planned.

## Getting set up

1. Build the plugin: see [`docs/dev/building.md`](docs/dev/building.md).
2. Run the checks: `bash scripts/verify.sh`. This is the **definition of done**: it runs the
   same static checks and test suite as CI. `bash scripts/verify.sh --quick` runs only the
   static checks (no build) and is handy while editing docs.
3. Read [`docs/dev/architecture.md`](docs/dev/architecture.md) for the signal chain and the
   source layout, and [`docs/dev/testing.md`](docs/dev/testing.md) for where tests go.

## Pull requests

- **One change per PR, with a test.** Fixes get a test that fails before the fix; features get
  tests for their behaviour. See [`docs/dev/testing.md`](docs/dev/testing.md) for the three test
  targets and the `ProcessorHarness` for anything wired through `processBlock`.
- `bash scripts/verify.sh` must pass before you ask for review.
- **Branch names** follow `<type>/<work>`: `type` is one of `feat fix refactor perf docs test
  build ci chore`, `work` is short kebab-case (`fix/delay-sync-clamp`).
- **Tone-changing PRs** (anything that changes what the amp sounds like at a given setting) need
  a sign-off run: see [`docs/dev/tone-signoff.md`](docs/dev/tone-signoff.md). Say in the PR
  which gates moved and by how much.
- **New plugin parameters are appended** to `createParameterLayout()`, never inserted mid-list:
  hosts that store automation by position would otherwise shift every saved lane.
- Update the docs your change makes stale (`docs/dev/`, `docs/manual/`, `CLAUDE.md`).

## Code conventions

The short version; [`CLAUDE.md`](CLAUDE.md) holds the full list, including the *Important
Pitfalls* section, which records mistakes that have already cost real debugging time.

- All project code lives in `namespace sr`.
- DSP constants live in `src/utils/Constants.h`. Suffix `_N` = normalized voltage domain
  (300 V B+ = 1.0), `_REAL` = real-world units, no suffix = dimensionless.
- **No dynamic allocation in `processBlock`.** Pre-allocate in `prepareToPlay`.
- Inject `ANTI_DENORMAL` into IIR filter state; smooth parameters per sample at the
  oversampled rate with `ParamSmoother`.
- Bias values are negative: apply them as `vGrid + vBias`.
- GUI type sizes come from `Fonts::Sizes`, colours from `ColourPalette.h`.
- Formatting follows `.clang-format`.
- Every new source file (`.h`, `.cpp`, `.py`, `.sh`) starts with the licence header;
  `verify.sh` checks it:

  ```cpp
  // SPDX-License-Identifier: GPL-3.0-or-later
  // Copyright (C) <year> <your name>
  ```

  You keep the copyright in what you write; contributing it means licensing it under
  GPL-3.0-or-later, the project's licence.

## Commits

- Write clear, imperative commit messages (`fix: clamp delay time to the sync ceiling`).
- **The human author is the sole author.** Do not add AI or bot attribution: no
  `Co-Authored-By:` trailer for an AI tool, no "Generated with ..." footer, no session links.
  This applies to commits, PRs, issues and comments.

## AI-assisted contributions

AI-assisted work is welcome on the same terms as any other: you are responsible for it, it
must pass `verify.sh`, and it follows the commit rule above. [`AGENTS.md`](AGENTS.md) and
[`CLAUDE.md`](CLAUDE.md) are the project instructions for coding agents, and
[`.agents/STATE.md`](.agents/STATE.md) is the current-state note they read first. How the
project itself uses AI, and what you confirm when you contribute AI-assisted work, is in
[`AI_USAGE.md`](AI_USAGE.md).

## Releasing

1. In `CHANGELOG.md`, rename `## [Unreleased]` to `## [x.y.z] - YYYY-MM-DD` and add a fresh,
   empty `## [Unreleased]` above it.
2. Bump `VERSION` in the `project(SilentRiot ...)` call at the top of `CMakeLists.txt`.
3. Merge that, then tag the merge commit `vx.y.z` and push the tag. `release.yml` builds the
   packages and uses the `[x.y.z]` section as the release notes (falling back to
   `[Unreleased]` if the section was not renamed).

## Licence

Silent Riot is licensed under the GNU General Public License v3.0 or later. By submitting a
contribution you agree that it is licensed under the same terms. See [`LICENSE`](LICENSE).

## Conduct

Everyone taking part is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md). Security
problems go through the private process in [`SECURITY.md`](SECURITY.md), not public issues.
