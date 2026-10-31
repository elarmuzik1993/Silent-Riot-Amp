# Credits and Originality Statement

## Silent Riot — Amp Simulator Plugin
**Copyright © 2026 Boris Miscenco (Monolit Beatz).**
Licensed under the GNU General Public License v3 or later; see `LICENSE`.

---

## Original Work

The Silent Riot DSP implementation — including all source code, the normalized voltage
domain framework (VoltageScale), the signal chain architecture, and the DSP model
specification — is original work, designed, directed, reviewed and owned by Boris Miscenco,
with AI-assisted implementation. [`AI_USAGE.md`](AI_USAGE.md) says what the AI tools did and
what the author did.

No source code has been copied, adapted, or derived from any third-party project,
including but not limited to Guitarix, SwankyAmp, NAM (Neural Amp Modeler), ChowDSP,
Surge, or any other open-source or commercial amp simulator.

---

## Public Domain Mathematical Knowledge Used

The following well-established, publicly documented mathematical formulations are used
in the DSP model. These are not proprietary; they are part of the shared scientific
and engineering knowledge base.

### Koren Tube Equations
The triode and pentode nonlinear models are based on the **Koren tube model** —
a set of equations published by Norman Koren in the public domain for free use by
the tube amp simulation community. The Koren model is widely referenced in academic
papers, DIY amp forums, and open-source simulators. No implementation code was
taken from any of those sources; only the underlying published equations are used.

**12AX7 / ECC83 parameters used:**
`mu=100, ex=1.4, Kg1=1060, Kp=600, Kvb=300`

These values are derived from publicly available tube datasheets and Norman Koren's
own published reference tables. They are the standard operating-point parameters for
this tube, cross-verified against multiple independent sources (datasheets, published
literature, and open-source implementations) to confirm correctness — not copied
from any of them.

**EL84 parameters used:**
`mu=19.5, ex=1.35, Kg1=820, Kp=84, Kvb=300`

Same sourcing as above. EL84 datasheet values, cross-checked against published
literature.

### Standard DSP Algorithms
The following DSP techniques are textbook knowledge and not owned by any party:

- **One-pole IIR filters** (RC lowpass/highpass via bilinear transform)
- **Biquad filters** in transposed Direct Form II (Audio EQ Cookbook, R. Zölzer)
- **Tanh waveshaping** for soft clipping
- **Exponential envelope followers** (attack/release)
- **Oversampling via polyphase FIR** (JUCE `dsp::Oversampling`)
- **Dispersive all-pass spring-reverb model** — the published parametric-spring
  technique (a cascade of first-order all-pass filters forming a dispersion network
  inside a damped feedback delay loop). This is a documented DSP method in the audio
  literature; the implementation here is original and written from scratch — no
  third-party reverb code was used.
- **Analog/tape-voiced feedback delay** — feedback-path tone shaping (a high-cut
  inside the loop so repeats darken cumulatively, plus a fixed low-cut), a
  dual-rate wow+flutter LFO on the read tap, and soft (tanh) saturation of the
  repeat so each pass compresses/warms like a real tape head or BBD chip.
  Standard, well-documented echo-modelling techniques; the implementation is
  original (built on `juce::dsp::DelayLine` and the project's own one-pole
  filters).
- **Ping-pong stereo delay topology** — two cross-fed delay taps (a mono send
  triggers the first tap, which feeds the second, which feeds back into the
  first) producing a bouncing L/R echo. A standard, widely documented delay
  topology; the implementation here is original.

---

## Circuit Topology Reference

The Silent Riot circuit is an **original fictional amplifier design** created by
Boris Miscenco. It is not a direct model or clone of any specific commercial product.

The topology — cascaded ECC83 preamp stages feeding an EL84 push-pull power section
via a long-tail pair phase inverter — is a general class of amplifier architecture
that has appeared in countless amplifiers since the 1950s. The spec describes this
class as "AC15-inspired," meaning it shares a family resemblance with that
well-documented circuit topology.

No proprietary schematic, trade secret, or copyrighted design document from Vox,
Marshall, Fender, or any other manufacturer was used. The specific component values,
gain staging, dual-gang gain pot arrangement, and post-phase-inverter tone control
placement are the author's own design choices.

The Vox AC15 schematic has been publicly available in service manuals and repair
documentation for over 60 years and is widely published in the amp DIY community.
The general topology it represents — common-cathode triode stages + LTP + EL84
push-pull — is not owned by any party.

---

## Third-Party Libraries

The following third-party libraries are used under their respective licenses:

| Library | License | Use |
|---|---|---|
| [JUCE 7](https://juce.com) | GPL v3 / Commercial | Plugin framework, DSP utilities, GUI |
| [Catch2 v3](https://github.com/catchorg/Catch2) | BSL-1.0 | Unit testing only (not shipped) |

JUCE's `dsp::Oversampling`, `dsp::Convolution`, and filter utilities are used as
infrastructure. The DSP model running inside those utilities is original work.

### Fonts

The font (Regular and Bold) is embedded in the plugin binary (via JUCE BinaryData) and are used under the
SIL Open Font License 1.1. The licence text ships next to the font files.

| Font | Copyright | License | Files |
|---|---|---|---|
| [Oxanium](https://github.com/sevmeyer/oxanium) | Copyright 2019 The Oxanium Project Authors | SIL OFL 1.1 — `resources/fonts/OFL-Oxanium.txt` | `resources/fonts/Oxanium-*.ttf` |

### Test fixtures

`tests/fixtures/ir/{neutral,dark,bright}.wav` are synthetic impulse responses generated by
`tools/make_ir_fixtures.py` (Butterworth band-passes, one with an RBJ peaking EQ). They are
original to this project and covered by its licence. They are not recordings of any real
cabinet.

---

## Trademarks

Vox, AC15, AC30, Orange, Tiny Terror, Marshall and Fender are trademarks of their respective owners.
They are named in this project only to describe a style or circuit family. Silent Riot is
not affiliated with, endorsed by or sponsored by any of them.

---

## Summary

| Item | Status |
|---|---|
| DSP source code | Original; designed, directed, reviewed and owned by Boris Miscenco, AI-assisted implementation ([`AI_USAGE.md`](AI_USAGE.md)) |
| Signal chain architecture | Original design |
| Tube model equations | Public domain (Koren, published openly) |
| Tube parameters | Public domain (datasheet values) |
| Filter algorithms | Standard DSP textbook knowledge |
| Circuit topology | Original fictional design in a common architectural family |
| Code from Guitarix | None |
| Code from SwankyAmp | None |
| Code from ChowDSP / NAM | None |
| Code from any other project | None |
