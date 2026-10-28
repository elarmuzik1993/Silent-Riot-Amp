# Silent Riot Amp

**Silent Riot** is a JUCE 7 VST3/Standalone amp simulator plugin by Monolit Beatz.
It models an original fictional amplifier design built around ECC83/12AX7 preamp tubes
and EL84 push-pull power tubes, with three amp controls (Gain, Shape, Volume), a
switchable 15W/7W power mode, a built-in effects section and a live tuner.

## Licensing

This project is licensed under the GNU General Public License v3.0 or later. See `LICENSE`.

## Copyright

Copyright © 2026 Boris Miscenco (Monolit Beatz). Licensed under the GNU GPL v3 or later; see `LICENSE`.

## Originality

All DSP source code is original ai-assisted work designed by Boris Miscenco. The tube models use the
publicly documented Koren equations with standard datasheet parameters. No code has
been copied from any third-party project (Guitarix, SwankyAmp, ChowDSP, NAM, or
others). See `CREDITS.md` for the full originality statement and attribution details.

## Architecture

- **Preamp**: Two cascaded ECC83/12AX7 triode stages with dual-gang gain pot
- **Phase inverter**: Long-tail pair (ECC83) with asymmetric plate loads
- **Shape**: Post-PI Orange-inspired 3-band EQ (low shelf 250Hz, mid bell 800Hz, high shelf 3.5kHz) — warm/mid-forward to scooped/modern
- **Power amp**: Push-pull EL84 pair, cathode biased, 15W/7W switchable
- **Dynamics**: PSU droop model, cathode sag envelope, grid conduction
- **Cabinet**: IIR cab sim (presence/rolloff/notch) or user IR convolution
- **Effects** (EFFECTS panel): noise gate, boost (level/tight/bright), tempo-syncable delay, spring reverb, stereo widener, and INPUT/OUTPUT level trims
- **Tuner**: always-visible live tuner on the DI input, works with the amp muted

## Build

```bash
./scripts/install-deps.sh   # Ubuntu deps (one-time)
./scripts/build.sh          # Debug build
./scripts/build.sh release  # Release build
```

## Key Documents

| File | Purpose |
|---|---|
| `docs/Silent Riot DSP Model Spec v2.13.md` | Original DSP design rationale — background only; superseded where it disagrees with the code (see its status banner) |
| `docs/SilentRiot_UI_Spec.md` | GUI layout, components, colours |
| `CREDITS.md` | Originality statement, attribution, third-party licenses |
| `LICENSE` | GNU GPL v3.0 |

