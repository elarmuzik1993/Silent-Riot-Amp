# Tone lock

Silent Riot's sound is **frozen at `tone-v1`**: the owner signed it off by ear on
2026-10-29, at commit `059b209`. A test guards that sound, so no code change can move it
by accident. This page says what is locked, how the check works, and the only way the
sound may change.

## What is locked

The **steady-state** output of the amp, the cabinet and the effects:

| Group | Settings rendered |
|---|---|
| Presets | the five factory presets |
| Amp knobs | GAIN 0 and 10, SHAPE 0 and 10, VOLUME 2 and 10 |
| Switches | 7W, CAB off, a loaded IR (`tests/fixtures/ir/neutral.wav`) |
| Effects | BOOST, DELAY, REVERB, the noise gate on a quiet DI, WIDTH 10, INPUT up with OUTPUT down |
| Engine | 44.1 kHz and 96 kHz, 2x and 8x oversampling |

Every case plays 3 s of a real guitar DI (`tests/di_sample_82hz.raw`) at 48 kHz, or an
E major chord at the other sample rates, with every other parameter at its default.

**Not locked:** the first 0.5 s of each render, where the amp settles from idle, and
transitions such as switching cab mode, power, oversampling or presets. Fixing a click or
a dip there is allowed, as long as the steady sound still matches.

## How the check works

`tests/tone_lock_tests.cpp` renders each case through the real `processBlock` and reduces
it to a **fingerprint** (`tests/tone_lock.h`): the level of 32 bands from 40 Hz to 16 kHz
in every 2048-sample frame, per channel, plus the overall RMS. It compares that with the
reference in `tests/tone/golden/<case>.srtl`. A case fails if any band of any frame moves
by more than **0.1 dB** or the RMS by more than **0.02 dB**. Bands more than 100 dB below
full scale count as silent.

The fingerprint ignores phase on purpose: it pins what you hear (level, tone, distortion
and how they move with your playing) and tolerates float rounding between compilers.
Two more cases keep the check honest. One renders the same case twice and requires an
identical result. The other requires that small changes **fail**: OUTPUT nudged up by
0.12 dB, and SHAPE moved by 0.02.

The lock runs in `SilentRiotProcessorTests`, so in `verify.sh` and ctest (about 20 s).

```bash
ctest --test-dir build -R "Tone lock" --output-on-failure
```

A failure names the case, the worst band (frame, channel, band, old and new level) and the
RMS change.

## Changing the sound on purpose

The references move only when the owner wants the sound to change:

1. **The owner approves the change** after an A/B: render before and after with
   `sr_render` (or play both builds) at the settings the change affects.
2. **Rewrite the references** from the approved build:
   ```bash
   SR_TONE_LOCK_WRITE=1 build/tests/SilentRiotProcessorTests "[tone-lock]"
   ```
   Commit them in the same PR as the change and say in the PR that the owner approved it.
3. **Tag the merge commit** with the next voicing, `tone-v2`, `tone-v3` and so on, and
   record it in the changelog's **Changed** section.

Never rewrite the references to make a failing test pass without that approval. A failing
tone lock means the change moves the sound: find out why.

**Already approved in principle:** the E3 aliasing fix (whole-engine aliasing on the IIR
path, `docs/dev/tone-signoff.md` gate E3). It still needs the owner's A/B before its new
references land. The amp re-calibration that was planned before the freeze (raising
`VOLUME_KNOB_HEADROOM`, D1/A1/B1 re-ratification, power-section Tasks 2–5) is cancelled.

## The frozen voicing (tone-v1)

The tuning constants the voicing rests on, from `src/utils/Constants.h` at `059b209`:

| Constant | Value |
|---|---|
| `GRID_CLAMP_KNEE_N` | 6 V |
| `GAIN_KNOB_HEADROOM` | 0.8 |
| `VOLUME_KNOB_HEADROOM` | 0.21 |
| `OUTPUT_MAKEUP_GAIN` | 3.5 |
| `OT_CURRENT_SCALE` | 0.009 |
| `OT_SAT_DRIVE` | 190 |
| `EL84_KG1` | 210.5 |
| `GRID_CONDUCT_THRESH_EL84_N` | 8.6 V |
| `GRID_CHARGE_RATE` | 0.003 |
| `MAX_BIAS_SHIFT_N` | −1.5 V |
| `V1B_RSOURCE_MAX_REAL` | 150 kΩ |
| `XCUR_SENSITIVITY` | 2.0 |

The sign-off gates were not all passing at the freeze (A1 −31.6 LUFS, B1 0.99 %,
E3 −37.9 dBc). The owner froze the sound by ear: A1 and B1 are accepted as they are, and E3
stays open behind an A/B (above).

## Known limit

The references were recorded with MSVC on Windows. Linux/GCC builds have not been checked
against them yet; if they differ by more than the tolerance there, the fix is per-platform
tolerance, not new references.
