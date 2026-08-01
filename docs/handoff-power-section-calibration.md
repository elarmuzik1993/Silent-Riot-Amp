# Handoff — power-section calibration (USE-59)

Record of the defects found while root-causing a set of listening-test
complaints, what fixed them, and what is still open. Every claim here is a
measurement, not an impression — the point of writing it down is so the next
person can re-run them.

Companion: `handoff-ltp-convergence.md` covers the solver rewrite in detail.

## The complaints, and what each turned out to be

| reported | actual cause |
|---|---|
| "gain knob not smooth or gradual" | pot law was x⁴, not x² |
| "drowning when I push too hard" | LTP hard-clipped on 58% of samples |
| "sounds weird above 3" | sag offset pinned at its cold clamp from knob 3 up |
| "palm mutes underpowered, missing thump" | OT saturation crushing the low band |
| "rumbling low noise even when not playing" | 17.7 Hz motorboating from the EL84 grid threshold |

None of these were guessable from the symptom. Each needed a measurement.

## Defects

### 1. Gain pot law was x⁴

`audioTaper()` (x²) was applied at **both** gangs of the dual-gang pot. The signal
passes through both, so the laws multiply. Noon sat at −24 dB against the spec's
own `audioTaper(0.5) = 0.25` (−12 dB) invariant.

Effect: dial 0→5 covered 6% of the gain range and 7→10 covered 76%. Half the
sweep did nothing, then everything arrived at once.

The existing tests only asserted `audioTaper()` in isolation, which is why a full
suite passed with the combined law wrong. `signal_chain_tests.cpp` now pins the
**combined** law.

### 2. LTP solver did not converge

A period-2 limit cycle rather than a fixed point: at a constant 1.5 V input `piA`
alternated between 0.933 and 0.292 *every sample*. See the companion doc.

### 3. Sag reference desynchronised from the tube model

`SAG_QUIESCENT_I_N` was 96 — the **datasheet** figure (2 × 48 mA) — while the
model actually idled at 24 mA. So `excessI` went negative, `biasOffset()` went
*positive*, and it pinned at the warm clamp. A cathode-biased stage can only ever
go **colder** than idle; sitting on the warm rail was proof the constant was wrong.

Now derived from the model via `pentodeQuiescentTotal()`, and it follows the
15W/7W bias switch. The old tests couldn't catch this because they compared the
constant against itself.

### 4. EL84 drew 3.9× too little current

At the datasheet point (Va=250V, Vg2=250V, Vg1=−7.3V) the model produced
**13.5 mA** where the datasheet says **48 mA**.

Consequence: the pair idled at 24 mA instead of ~90 mA, so the sag cold clamp was
reached at only **1.25× idle current** — any real playing railed it instantly,
freezing the bias at −12.6 V from knob 3 upward.

| constant | was | now |
|---|---|---|
| `EL84_KG1` | 820 | **210.5** |
| `EL84_IP_MAX_N` | 65 mA | **95 mA** |
| `SCREEN_CURRENT_RATIO` | 0.25 | **0.115** |

**`EL84_KG1` and `EL84_IP_MAX_N` are coupled** — the tanh knee compresses the
result, so solving Kg1 against the raw Koren current gives the wrong answer. Set
IP_MAX first, then solve Kg1 through the full pipeline. Pinned by
`pentode_tests.cpp`.

### 5. Grid conduction never clamped

It was a fixed resistive divider:

```
vLoaded = threshold + vAbove * Rg/(Rg+Rs)
```

which can only **attenuate**. At the PI that ratio is 0.23, so a ~520 V swing
still delivered a **121 V grid** on a 12AX7 biased at −1.5 V. That is why the amp
was overdriven at *every* knob position and had near-zero dynamic response.

A real grid-cathode junction is a diode — current rises exponentially, so the
source impedance pins the grid a few volts above threshold. Inverting an
exponential gives a logarithm:

```cpp
vLoaded = threshold + knee * log1p(vAbove / knee);
```

Transparent below the knee (slope exactly 1 at `vAbove = 0`, so C¹-continuous
with the non-conducting branch), logarithmic above. The knee scales per stage by
`Rg/(Rg+Rs)`, preserving the per-stage character the divider had.

### 6. EL84 grid threshold was 0 V

A grid conducts once it approaches the **cathode**, so the threshold belongs near
`|bias|`. The 12AX7 stages follow that (−1.5 V bias → 0.5 V). The EL84 is biased
at −9.6 V but its threshold was **0 V** — about 8.6 V too low.

So every positive excursion of amplified noise counted as grid conduction, and
the coupling-cap charge integrator (100 ms tau, no lower bound) ratcheted to its
`MAX_BIAS_SHIFT_N` rail **with nothing plugged in**. That railed charge shifted
the bias → moved plate current → drooped the supply → moved the operating point.

Measured on silence at gain 10:

| | before | after |
|---|---|---|
| settled output | **−20.3 dBFS @ 17.7 Hz**, 94.9% below 120 Hz | **−279.8 dBFS** |
| EL84 grid charge | 1.50 V (railed) | 0.00 V |
| PSU swing | 3.5 V | 0.0 V |

Found by freezing each feedback loop in turn: killing **either** the PSU **or**
the grid charge silenced it, while sag, speaker coupling and OT saturation did
nothing — which pinned it to their coupling rather than leaving it to guesswork.

### 7. Shape EQ reset to noon on oversample change

`FilterBank::recalcAll` ends with `updateShape(0.5f)`. The oversample commit calls
it without invalidating `lastShapeCoeffVal`, so the per-sample guard saw "shape
hasn't moved" and never restored the knob. Shape silently sat at noon until
touched.

## Result

All three tone sign-off THD bands pass together for the first time:

| | at the start | now | target |
|---|---|---|---|
| clean | 21.56% | **1.03%** | 1–5% |
| edge | 30.95% | **9.44%** | 6–12% |
| crank | 28.55% | **30.1%** | 18–35% |

Note the starting figures were non-monotonic — "clean" measured inside the crank
band. Peak ceiling holds at −1.31 dBFS; DC offset passes; idle is silent.

## Gain knob range

`GAIN_KNOB_HEADROOM = 0.8` remaps the knob's full sweep onto the lower 80% of the
drive curve, so knob 10 delivers what knob 8 used to. The old top end was past
where the chain runs out of headroom — it added compression, not usable drive.
The full 270° arc now covers the range that sounds good.

Verified as a pure mapping change: with the sign-off points rescaled by 1/0.8 the
gates returned **identical** numbers (1.034 / 9.44 / 30.135, peak −1.31), i.e. the
tone at equivalent operating points is unchanged.

## Still open

- **E3 aliasing −34.7 dBc** vs ≤ −85 (MUST-PASS). Improved from −8.8 but not
  fixed. Note the gate references an `OPTIONAL_BRIGHT_IR` that is not supplied,
  so confirm it exercises the intended path before treating it as a hard number.
- **D1 sag depth 0.38 dB** vs 1.5–4.0. The EL84 grid charge rails at
  `MAX_BIAS_SHIFT_N` (−1.5 V) while the sag offset rails at +1.5 V — two pinned
  mechanisms cancelling, so neither is a dynamic. **This is the next structural
  item**, and it is the same failure shape as defects 3 and 6.
- **B4 even-harmonic warmth** (H2 ≥ H3 at edge).
- **Startup thump**: ~0.5 s at −17 dBFS on silence at gain 10 while the PSU
  settles from initial conditions. Audible on plugin load. `E1`'s "idle noise
  floor −47 dBFS" is this transient being averaged in, *not* steady noise — the
  settled level is −279.8 dBFS.

## Lesson worth keeping

Four of these seven defects are the same shape: **a dynamic pinned against its
limiter stops being a dynamic**. Sag warm rail, sag cold rail, grid-charge rail,
LTP plate clamp. When a "dynamic" constant sits at its clamp across most of the
control range, that is the bug — not the clamp value.
