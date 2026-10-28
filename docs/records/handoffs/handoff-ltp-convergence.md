# LTP solver convergence — RESOLVED

**Status:** fixed. `solveLTP()` now root-finds the cathode node instead of
relaxing toward it. Kept as a record of the defect, the measurements, and the
dead ends — several plausible "fixes" are actively wrong.

**Where:** `src/dsp/power/LTPSolver.cpp`.

## The defect

The old solver ran a damped fixed-point iteration and **did not converge**. At a
*constant* input it settled into a period-2 limit cycle, synthesising a
full-scale Nyquist-rate square wave out of DC.

Constant 1.5 V on the PI grid — an ordinary level, not an extreme:

| sample | piA | piB | vCathode |
|---|---|---|---|
| 2 | 0.933308 | 0.916758 | 0.005029 |
| 3 | 0.292157 | 0.870049 | 0.003834 |
| 4 | 0.933308 | 0.916758 | 0.005029 |

`piA` alternated by **0.64 — 64% of full scale — every sample, forever.**

Residual peak-to-peak of `piA` at constant input (0 = converged):

| PI grid input | 0.6 V | 1.5 V | 3.0 V | 6.0 V | 15 V |
|---|---|---|---|---|---|
| old | 0.271 | 0.641 | 0.927 | 0.918 | 0.023 |
| **new** | **0** | **0** | **0** | **0** | **0** |

Worst through the *middle* of the usable range. The oversampler's decimation
filter removed the Nyquist component, but the pentode, soft conduction, OT
saturation and output `tanh` sit *before* it and intermodulated that content back
into the audio band, where nothing downstream can remove it.

## The fix

The shared cathode satisfies `vC = (ipA + ipB) * tailR`. Raising `vC` lowers both
grid-cathode voltages, so both plate currents fall — meaning

```
f(vC) = (ipA(vC) + ipB(vC)) * tailR - vC
```

is **strictly decreasing with exactly one root**, and `f(0) > 0`,
`f(vSupply) < 0`. So `[0, vSupply]` always brackets the root and a bracketed
method cannot diverge.

Implementation:

- **Outer:** secant seeded from the previous sample's `vCathode` (it moves slowly,
  so this usually lands in 1–2 steps), with **bisection fallback** whenever the
  secant leaves `[0, vSupply]` or stalls. Tolerance `1e-5` normalised = 3 mV.
- **Inner:** each triode's own load line solved with 4 damped (0.5) iterations —
  the same scheme `triodeWaveshaper()` already uses. The seed must be *identical
  for every evaluation within a sample*, or `f()` becomes path-dependent and the
  outer solve stalls.

Measured: **1.42 residual evaluations per sample.**

## Dead ends — do not retry these

**`alpha = 1.0` (undamped relaxation).** Looks perfect: residual drops to exactly
0 at every level, from as few as 3 iterations. **It is wrong.** `vCathode`
overshoots and runs away to **2.9 normalised (870 V)**, far above the 280 V
supply, cutting both triodes off. `piA` then sits pinned at the supply rail for
*every* input including idle — the stage is dead. The residual metric reported
"converged" because a dead stage is perfectly stable. Any convergence test must
also assert the operating point is physical and still differential.

**Damping the plate update.** `triodeWaveshaper()` damps its plate step at 0.5
while the old LTP took a full step, which looked like the obvious culprit. Swept
plate damping × alpha × iteration count: **every damped combination still
oscillated.** Closest was pd=0.35/alpha=0.35/12 iterations at 0.0014 residual —
still not a fixed point. Relaxation is the wrong method here, not a mistuned one.

**Blaming the softplus plate bound.** Residuals were identical for the original
hard `std::clamp` and the softplus knee (0.270842 / 0.641151 at 0.6 V / 1.5 V),
so the limit cycle pre-dated it.

**Inner iteration count 3.** Non-monotonic: 2 works (1e-8 residual), **3
oscillates (0.084)**, 4/6/8 are exactly 0. 4 is the smallest value safely inside
the converged region — do not "optimise" it down to 3.

**Tolerance looser than 1e-5.** 1e-4 gets to 1.05 evals/sample but visibly shifts
the operating point (piA 0.83403 vs 0.83384, swing −0.12866 vs −0.12800).

## Effect

Full chain, DI peak 0.30, 15W, volume 10:

| GAIN | out RMS before | out RMS after | plate bound engaged before → after |
|---|---|---|---|
| 2 | 0.179 | 0.404 | 2.7% → 0% |
| 4 | 0.172 | 0.505 | 21.1% → 0% |
| 6 | 0.204 | 0.573 | 28.1% → 0% |
| 7 | 0.460 | 0.580 | 39.8% → 0% |
| 8 | 0.599 | 0.582 | 50.9% → 0% |
| 10 | 0.595 | 0.579 | 58.5% → 0% |

The +7 dB discontinuity between GAIN 6 and 7 is gone, and the plate no longer
hits its lower bound at all — the converged solver finds the true operating point
instead of being flung to the rails.

## Cost

The solver is now the most expensive thing in the amp loop.

| OS | amp DSP, % of one core | budget @ 64 samples / 48 kHz |
|---|---|---|
| 2× | 19.7 | 0.26 / 1.33 ms |
| 4× | 41.4 | 0.55 / 1.33 ms |
| 8× | 75.6 | 1.01 / 1.33 ms |

4× (the default) is fine. **8× is now marginal** — it was ~38% before. Excludes
oversampler filters, effects, GUI and host overhead, so 8× at a 64-sample buffer
is not advisable. Raise the buffer or stay at 4×.

## Tests

`tests/ltp_tests.cpp`, tagged `[ltp][regression]`:

- constant input settles to a fixed point (the acceptance test — was hidden and
  failing, now runs and passes)
- the solved cathode satisfies its own node equation and re-solving is a true
  fixed point
- the stage still inverts **differentially** and is not cut off at idle — this is
  what catches the `alpha = 1.0` trap

## Still open

The PI grid is driven to **126 V peak at GAIN 10** (12AX7 grid, −1.5 V bias).
`applyGridConduction` is a resistive divider that only attenuates to ~23% at full
conduction and never clamps, whereas real grid conduction is a diode that pins
the grid near the cathode. Separate gain-staging question.

Related: with the sag reference fixed, `biasOffset()` now pins at the **cold**
clamp (−3 V) from GAIN 4 upward. Correct direction, but still a railed dynamic —
worth revisiting once PI drive is sane.
