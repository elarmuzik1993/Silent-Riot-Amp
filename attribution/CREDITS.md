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

Silent Riot is a **model of a publicly documented amplifier architecture**, not a
fictional design. Its reference is the Orange Tiny Terror: two 12AX7 preamp stages, a
long-tail-pair phase inverter, a push-pull EL84 power section, GAIN / SHAPE / VOLUME controls
and a 15W/7W power switch. The design spec (`docs/design/dsp-model-spec-v2.13.md`) names the
Tiny Terror throughout for that reason, and the SHAPE control is modelled on the style of
Orange's Shape control. Some of the topology is common to the Vox AC15 family, which is also
referenced.

This is a tribute to and study of that circuit, not a copy of any product. The circuit
architecture and electrical behaviour are facts and ideas, which copyright does not protect.
What is original here is the implementation:

- all source code, written from scratch (nothing taken from Orange, Vox or any other
  manufacturer's software or firmware);
- the normalized voltage-domain framework, the signal-chain structure and the numerical
  methods (the bracketed LTP root-finder, the grid-conduction diode clamp, the
  impedance-based PSU droop, the cathode-resistor sag model, the output-transformer
  saturation and the speaker-coupling crossover);
- the GUI, effects, cabinet/IR handling and test tooling.

### Where the component values come from

No Orange schematic, service manual or measurement of a real Tiny Terror was used. The
values in `Constants.h` and the design spec were written from general knowledge of common
tube-amp circuits and then tuned by ear. They fall into two groups:

- **Textbook and datasheet values:** 12AX7 µ and r<sub>p</sub> (RCA/Mullard/GE datasheets), EL84
  operating points (Mullard and Philips data), 100 kΩ preamp plate loads, 1 MΩ grid leaks,
  25 µF cathode bypass, 0.1 µF coupling caps, the 12AX7 self-bias point.
- **Educated guesses for this amp category, unverified against any real unit:** the 1.2 kΩ
  phase-inverter tail resistor, the 100 kΩ / 82 kΩ asymmetric LTP plate loads, the 4.7 kΩ and
  22 kΩ supply droppers, the 470 Ω screen resistor and the 300 V B+. These may differ from a
  real Tiny Terror, and Silent Riot makes no claim that they match one.

No manufacturer code, firmware, audio samples or copyrighted manual text or artwork is
included. Silent Riot is not a clone of, and makes no claim to match the sound of, any
commercial product.

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

### Images

`resources/textures/brick-wall.jpg` (the cab's wall) and `resources/textures/faceplate-steel.jpg`
(the amp faceplate's steel layer) are photographs from [Pexels](https://www.pexels.com), used
under the [Pexels License](https://www.pexels.com/license/): free to use and modify, with no
attribution required, but not redistributable on their own as stock photos. They are **not**
covered by the GPL; they are embedded in the plugin as artwork. The renders in the plugin
paint over them (tint, wear, glow).

### Test fixtures

`tests/di_sample_82hz.raw` is a guitar DI recording made by the author, and covered by the
project's licence.

`tests/fixtures/ir/{neutral,dark,bright}.wav` are synthetic impulse responses generated by
`tools/make_ir_fixtures.py` (Butterworth band-passes, one with an RBJ peaking EQ). They are
original to this project and covered by its licence. They are not recordings of any real
cabinet.

---

## Trademarks

Vox, AC15, AC30, Orange, Tiny Terror, Marshall, Fender and Celestion are trademarks of their respective owners.
They are named in this project only to describe a style or circuit family. Silent Riot is
not affiliated with, endorsed by or sponsored by any of them.

---

## Summary

| Item | Status |
|---|---|
| DSP source code | Original; designed, directed, reviewed and owned by Boris Miscenco, AI-assisted implementation ([`AI_USAGE.md`](AI_USAGE.md)) |
| Signal chain implementation | Original; structure follows the documented architecture above |
| Tube model equations | Public domain (Koren, published openly) |
| Tube parameters | Public domain (datasheet values) |
| Filter algorithms | Standard DSP textbook knowledge |
| Circuit topology | Not original: modelled on the publicly documented Orange Tiny Terror architecture (see above) |
| Code from Guitarix | None |
| Code from SwankyAmp | None |
| Code from ChowDSP / NAM | None |
| Code from any other project | None |
