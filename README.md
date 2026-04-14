# Silent Riot Amp

**Silent Riot** is a JUCE 7 VST3/Standalone amp simulator plugin by Monolit Beatz.
It models an original fictional amplifier design built around ECC83/12AX7 preamp tubes
and EL84 push-pull power tubes, with three controls (Gain, Tone, Volume) and a
switchable 15W/7W power mode.

## Licensing

This project is licensed under the GNU General Public License v3.0. See `LICENSE`.

## Copyright

Copyright © 2026 Boris Miscenco (Monolit Beatz). All rights reserved.

## Originality

All DSP source code is original ai-assisted work designed by Boris Miscenco . The tube models use the
publicly documented Koren equations with standard datasheet parameters. No code has
been copied from any third-party project (Guitarix, SwankyAmp, ChowDSP, NAM, or
others). See `CREDITS.md` for the full originality statement and attribution details.

## Architecture

- **Preamp**: Two cascaded ECC83/12AX7 triode stages with dual-gang gain pot
- **Phase inverter**: Long-tail pair (ECC83) with asymmetric plate loads
- **Tone**: Post-PI Vox-style cut control
- **Power amp**: Push-pull EL84 pair, cathode biased, 15W/7W switchable
- **Dynamics**: PSU droop model, cathode sag envelope, grid conduction
- **Cabinet**: IIR cab sim (presence/rolloff/notch) or user IR convolution

## Build

```bash
./scripts/install-deps.sh   # Ubuntu deps (one-time)
./scripts/build.sh          # Debug build
./scripts/build.sh release  # Release build
```

## Key Documents

| File | Purpose |
|---|---|
| `docs/Silent Riot DSP Model Spec v2.13.md` | Authoritative DSP math, signal chain, constants |
| `docs/SilentRiot_UI_Spec.md` | GUI layout, components, colours |
| `CREDITS.md` | Originality statement, attribution, third-party licenses |
| `LICENSE` | GNU GPL v3.0 |

