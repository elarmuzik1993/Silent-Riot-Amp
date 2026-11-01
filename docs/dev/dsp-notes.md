# DSP implementation details and audit status

_Moved verbatim out of `CLAUDE.md` on 2026-11-01 to keep that file short. Same rules, same text; `CLAUDE.md` links here._

## DSP Implementation Details


- **Triode model**: Koren with iterative load-line solve. Standard 12AX7 parameters (mu=100, ex=1.4, Kg1=1060, Kp=600, Kvb=300 — matches Guitarix/industry reference). `TriodeState` caches converged plate voltage between samples → 4 iterations sufficient (was 8). Stateless overload available for tests.
- **Pentode model**: Koren pentode equations (mu=19.5, ex=1.35, Kg1=`EL84_KG1` 210.5, Kp=84, Kvb=300) — screen grid acts as virtual anode in the Koren formula. Softplus cutoff, tanh IP clamp, plate saturation knee, screen redistribution. Kg1 was 820 until USE-59 (3.9× too little current); see the datasheet pin under DSP Audit Status.
- **LTP solver**: Asymmetric plate loads (100K/82K). Root-finds the shared cathode node — secant seeded from the previous sample's `vCathode`, bisection over `[0, vSupply]` as the fallback, tolerance 1e-5 — with 4 inner plate iterations per residual evaluation. Caches plate voltages A/B and `vCathode` between samples; `reset()` restores both to their `init()` values. See `docs/records/handoffs/handoff-ltp-convergence.md`.
- **Oversample switching**: Crossfade state machine (FadingOut → Reconfiguring → FadingIn), 128-sample fade at base SR. PSU frozen during transition to prevent voltage recovery pop. `prepareToPlay` starts the switcher in the saved OVERSAMPLE mode with no transition pending (review B5) — never hard-code 4× there, or 2×/8× sessions drop out on their first blocks.
- **Latency**: every oversampler is built by `makeIntegerLatencyOversampler()` (JUCE integer latency, a Thiran all-pass rounds it up), so the report is exact: main + IR-limiter = 10 / 12 / 12 samples at 2× / 4× / 8×. The limiter's latency is reported in both cab modes, so the IIR path is delayed to match by `iirPathAlign` (`IntegerDelay`, review B4). Change either oversampler and both the report and the delay follow automatically; don't truncate a fractional latency again.
- **Sag model**: Cathode resistor physics — `biasOffset = -(Itotal - Iquiescent) × R_cathode`. Fed with raw `|ipA| + |ipB|` before OT scaling. Cold clamp is -0.01 (-3V) — keeps effective bias from going past -12.6V (cutoff is -10V); prevents deep Class C signal loss at high volume.
- **Stereo widener** (`StereoWidth.h`): final-stage widener knob (`width` param, autonomous — no toggle; 0=off/dry, 1=wide, defaults off), runs LAST on the finished stereo bus (after delay/reverb). Additive: Mid/Side scale (sideGain 1→MAX) plus a Haas+all-pass decorrelator injected into the SIDE only (0→MAX), with a slow LFO-modulated fractional read tap that micro-detunes the side voice ~±5 cents (doubler shimmer). `L+R = 2·Mid` stays invariant at any setting → perfect mono compatibility, and width appears even on the dual-mono dry amp (NeuralDSP-doubler style). Never narrows below the input. Stereo-only (no-op on a mono bus); skipped entirely (bit-exact dry) when parked at 0 (`isActive()`). Width value one-pole smoothed (50ms). Its knob sits under OUTPUT beside the OUT meter; the noise gate's THRESH and the INPUT trim mirror them beside the IN meter — all four knobs are autonomous (gate and width self-gate at 0, INPUT/OUTPUT are unity at centre), no ON/OFF toggle.
- **Shape control**: Orange Shape-inspired 3-band EQ on differential signal only (preserves common-mode even harmonics from PI asymmetry). Three biquad filters: low shelf (250Hz, 0→+4dB), mid bell (800Hz Q=1.2, +5→-9dB), high shelf (3.5kHz, -3→+4dB). Gains interpolate linearly with knob position. At noon: mild scoop (+2dB low, -2dB mid, +0.5dB high).
- **Shape coefficient threshold**: Skip sin/cos/sqrt filter recalc when smoothed shape value hasn't changed by >0.001 (~99% of samples).
- **Speaker coupling excursion**: Impedance deviation scales with bass-weighted cone excursion (X ∝ V/f²). `tanh(excursion * XCUR_SENSITIVITY)` models Xmax suspension stiffening — low excursion keeps impedance nominal, high excursion allows full deviation. Double-gated with existing `modBlend` for clean off-ramp at silence.

## DSP Audit Status (v0.3.4)


All filter frequencies now match spec v2.13:
- Shape: 3-band EQ — low shelf 250Hz, mid bell 800Hz Q=1.2, high shelf 3.5kHz (replaces Vox-cut tone)
- Gain loading LP: 5kHz
- Cab presence: 2.5kHz peak, Q=1.5, +3dB
- Cab rolloff: 5.5kHz LP, Q=0.5
- Cab notch: 400Hz, Q=2.0
- PI coupling HP: 16Hz (was already correct)
- All preamp filters verified correct

Koren tube parameters verified against Guitarix and standard datasheets. The EL84
is additionally pinned to its **datasheet operating point** (Va=250V, Vg2=250V,
Vg1=−7.3V → Ia=48 mA, Ig2=5.5 mA) by tests in `pentode_tests.cpp` — `EL84_KG1`
and `EL84_IP_MAX_N` are coupled through the tanh knee, so re-solve if either moves.

**Ear-tuning knobs, in the order they bite:**
- `GRID_CLAMP_KNEE_N = 6V` — how many volts above threshold a conducting grid may
  sit. **This is the primary gain-staging control** — it decides how much drive
  reaches the next stage. Reach for this before the gain taper.
- `GAIN_KNOB_HEADROOM = 0.8` — fraction of the drive range the knob can reach.
  Knob 10 delivers what knob 8 used to. Raise toward 1.0 to return the top end.
- `VOLUME_KNOB_HEADROOM = 0.21` — **this is the give/touch-sensitivity control.**
  The master volume *is* the power-stage drive control, and past ~knob 4 on the
  old law the EL84 grids sat in permanent conduction, where the diode clamp
  rather than the tube set the level (36 dB in → 2.4 dB out). 0.21 keeps knob 10
  below `GRID_CONDUCT_THRESH_EL84_N`. Raise toward 1.0 for a louder, more
  compressed, less dynamic amp — and re-measure D1 **at knob 10**, which moves
  fast with it.
- `OUTPUT_MAKEUP_GAIN = 3.5` — recovers ~11 dB of the ~20 dB `VOLUME_KNOB_HEADROOM`
  costs. Applied post-cab, pre-tanh, so it does NOT disturb the OT-saturation
  calibration. **Set it together with `VOLUME_KNOB_HEADROOM`:** they are two
  limiters in series, and make-up gain does not restore give, it relocates the
  compression from the grid clamp onto the output tanh. Headroom 0.30 + makeup
  2.5 read D1 1.98 at the gate's volume 0.70 but **0.98 at knob 10**.
  **Calibrate against a real DI, not the D1 gate signal** — `note:110:0.5` is
  ~9 dB hotter than a nominal −18 dBFS DI, so tuning to it leaves the amp quiet
  in actual use (makeup 2.5 gave only −17.0 LUFS at gain 10 / volume 10, with
  6.3 dB of peak headroom unused). 3.5 lands at −14.2 LUFS. Above ~4 the tanh
  becomes the dominant limiter and D1 at knob 10 falls out of band.
- `OT_CURRENT_SCALE = 0.009` — output level trim. Not a pure derivation: the
  IP_MAX tanh knee and plate saturation are nonlinear, so it is set empirically.
- `OT_SAT_DRIVE = 190` — OT flux saturation. Scales *inversely* with signal level,
  so it must be retuned whenever plate currents or `OT_CURRENT_SCALE` move. Too
  high crushes the low end first (`flux ∝ 1/f`) and kills palm-mute thump —
  measure with `tools/sr_thump.py`, not by ear.
- `outputGainCompensation = 1.0` — final headroom into the output tanh.
- `V1B_RSOURCE_MAX_REAL = 150KΩ` — code-only clamp not in spec; may limit Miller
  darkening at low gain (consider removing if low-gain tones sound too bright)
