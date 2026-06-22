# Handoff — IR-path output limiter is not oversampled (aliasing)

**Component:** `SilentRiotProcessor::processBlock` — output stage
**Status:** confirmed, measured. Not yet fixed.
**Severity:** Low–Moderate, *conditional* (IR mode + cranked output + bright IR only).
**Audience:** DSP maintainer picking up the "DSP audit / sound polish" branch.

---

## 1. Summary

The plugin has two output topologies that share an identical final stage —
`output *= outputGainCompensation; output = tanh(output)` — but run it at two
different sample rates:

| Path | Where the `tanh` limiter runs | Anti-aliased? |
|------|-------------------------------|---------------|
| **IIR cab** (`IR_INDEX == 0`) | inside the per-sample oversampled loop, **before** `processSamplesDown` | ✅ yes — the downsampler's decimation FIR removes the harmonics `tanh` generates above base-Nyquist |
| **IR convolution** (`IR_INDEX > 0`) | in a separate loop at **base SR**, *after* `processSamplesDown` and after convolution | ❌ no — harmonics above base-Nyquist fold straight back into the audio band |

`tanh` is a nonlinearity; when it compresses it generates odd harmonics. In the
IIR path those harmonics are created at the oversampled rate and then filtered
out on the way down. In the IR path they are created at base SR with nothing
above to catch them, so they alias.

This also **deviates from the spec**, which places the limiter inside the
oversampled loop (`docs/Silent Riot DSP Model Spec v2.13.md:1293`,
`finalOut = std::tanh(finalOut);  // gentle limiter`).

---

## 2. Exact code locations

`src/PluginProcessor.cpp`:

- **IIR-path limiter (correct, oversampled)** — lines 376–381, inside the
  `for (i …) osBlock` loop:
  ```cpp
  float finalOut = cabOut;
  if (!irMode) {
      finalOut *= outputGainCompensation;
      finalOut = std::tanh(finalOut);      // runs at effectiveSR
  }
  ```
- **Downsample** — line 393: `oversampler->processSamplesDown(outputBlock);`
- **IR-path limiter (aliasing, base SR)** — lines 398–414:
  ```cpp
  if (irMode) {
      cabSim.processBlock(outputBlock);    // convolution at base SR
      for (ch …) for (i …) {
          samples[i] *= outputGainCompensation;
          samples[i] = std::tanh(samples[i]);   // runs at base SR ← aliases
          if (!std::isfinite(samples[i])) samples[i] = 0.0f;
      }
  }
  ```

Note the convolution itself (`CabinetSim::processBlock`,
`src/dsp/cabinet/CabinetSim.cpp:25`) is **linear**, so running *it* at base SR
is fine — a linear filter generates no new frequencies and cannot alias. The
problem is purely the `tanh` that follows it.

---

## 3. Measurement

Standalone harness (not committed; reproducible — see §6) comparing the two
behaviors on identical input: base-SR `tanh` (IR path) vs. 8× oversampled
`tanh` + decimation (IIR path / spec). `baseSR = 48 kHz`, Nyquist = 24 kHz.
Numbers are the level of the worst inharmonic **alias tone**, in dB relative to
the fundamental (dBc).

| Input | Drive into limiter | Worst alias (IR path) | Same tone (oversampled) | Penalty |
|-------|--------------------|------------------------|--------------------------|---------|
| 7 kHz sine | 0.5 (gentle) | −66 dBc | −90 dBc | 24 dB |
| 7 kHz sine | 1.0 (cranked) | **−45 dBc** | −90 dBc | 45 dB |
| 7 kHz sine | 2.0 (hot) | **−28 dBc** | −87 dBc | 59 dB |
| 5 kHz sine | 2.0 | −41 dBc | −85 dBc | 44 dB |
| 3.3k+4.7k mix | 2.0 | −56 dBc | −56 dBc | ~0 dB |

Reading the table:

- **The defect only bites when both conditions hold:** (a) the limiter is
  actually compressing (drive ≳ 1.0), and (b) there is real energy in the
  upper treble (≈5–7 kHz) feeding it. A 7 kHz tone's 5th/7th/9th harmonics
  (35/49/63 kHz) fold to 13 kHz / 1 kHz / 15 kHz — inharmonic, hence audible.
- At **drive 1.0** (a cranked patch — CLAUDE.md notes driven output ≈ −0.1 dBFS),
  the alias sits at **−45 dBc**: subtle but can add a thin "fizz/harshness" on
  bright high-gain tones.
- At **drive 2.0** (hot output / loud IR) it reaches **−28 dBc** — that is
  plainly audible as an inharmonic whistle.
- The **3.3k+4.7k mix shows ~0 dB penalty**: its intermodulation products land
  below Nyquist, so nothing folds. Aliasing needs harmonics/IMD products to
  exceed 24 kHz, which requires fairly high input frequencies driven hard.
- At **drive 0.5** everything is ≤ −66 dBc → inaudible. If the IR path never
  drives the limiter hard, the bug is dormant.

**Why it matters for the roadmap:** the neural-model capture (CLAUDE.md "Neural
model: on hold") would, if captured through an IR, bake this aliasing into the
training target. Worth fixing *before* any capture done through the IR path.

---

## 4. Realism caveats (don't over-rate it)

- Convolution loads IRs with `Normalise::yes`
  (`CabinetSim.cpp:38`), so the IR doesn't add gross gain — drive into the
  limiter ≈ the OT/cab output level, not an IR-inflated one.
- Real guitar cab IRs roll off hard above ~5 kHz, which limits how much
  5–7 kHz energy reaches the limiter. A *bright/modern* IR plus fizzy
  high-gain content is the realistic worst case; a dark vintage IR barely
  triggers it.
- The IIR path is unaffected and stays clean (measured −85…−92 dBc).

Net: this is a real correctness gap and an IIR-vs-IR inconsistency, but it is
**not** a constant "the amp sounds broken" problem. It is exactly the kind of
quiet defect that is annoying to diagnose later, and it is cheap to close.

---

## 5. Fix options

Ordering note: the IIR path runs **cab → limiter**, and the `tanh` is a final
*safety* limiter (catches overs), so it should stay **after** the cab/IR.
That rules out "just move `tanh` before convolution."

**Option A — run convolution at the oversampled rate.**
Move the whole cab/IR + scale + limiter into the oversampled loop for both
paths. Fully spec-compliant, both paths identical.
*Cost:* convolution at 4–8× SR ≈ 4–8× its current CPU, plus the IR must be
resampled to the oversampled rate. Likely too expensive. ❌ not recommended.

**Option B — surgical: oversample only the IR-path limiter.** ✅ recommended
After `cabSim.processBlock(outputBlock)`, wrap *only* the
`scale + tanh` in a small dedicated oversampler (2× or 4× is plenty — see
penalty table; even 2× turns −28 dBc into roughly −70 dBc). Use a low-latency
polyphase IIR oversampler to match the main engine's latency behavior, or a
short half-band FIR.
*Cost:* tiny — it oversamples one `tanh`, not the convolution.
*Preserves:* exact tone (same `tanh` curve), cab→limiter ordering, IIR path
untouched.
*Watch:* the extra oversampler adds latency → must be added to
`setLatencySamples` and only for the IR path (or always, for consistency).

**Option C — unify both paths behind one post-downsample, oversampled limiter.**
Remove `tanh` from the per-sample loop; after downsample (and after convolution
in IR mode) run a single oversampled limiter stage for both paths.
*Pro:* removes the IIR/IR inconsistency at the source, one code path.
*Con:* touches the IIR path you're currently happy with → more regression risk
against a tone you like. Defer unless you want the cleanup.

**Option D — gentler/true-peak limiter (tone change).**
A softer knee or higher threshold that rarely hard-compresses would cut the
generated harmonics and thus the aliasing. ❌ changes the output coloration
that's now part of the sound — not appropriate for a "polish, don't change the
tone" pass.

**Recommendation:** **Option B.** Smallest change, no tone change, no risk to
the IIR path, directly closes the gap. 2× oversampling on the limiter is
sufficient; 4× makes it bullet-proof.

---

## 6. Verification / test plan

1. **Reproduce the measurement** (standalone, no JUCE needed): feed a 7 kHz
   sine at drive 1.0 and 2.0 through base-SR `tanh` and through
   oversampled-`tanh`+decimate; Goertzel at 1 kHz / 13 kHz / 15 kHz; confirm
   the IR-path values match §3 (≈ −45 / −28 dBc) and the oversampled values are
   ≤ −85 dBc.
2. **After fixing (Option B):** rerun — the IR-path alias tones should drop to
   the oversampled column (≤ −70 dBc at 2× OS, ≤ −85 dBc at 4×).
3. **Parity test:** with a *minimum-phase, flat-ish* IR, IIR mode and IR mode
   driven identically should produce near-identical high-frequency alias
   floors. Before the fix they differ by 40–60 dB at 5–7 kHz; after, they
   should match within a few dB.
4. **Latency check:** confirm `getLatencySamples()` reflects any oversampler
   added in the IR path, and that switching IR↔IIR doesn't pop (it already
   crossfades oversample switches, but the limiter oversampler is a new
   latency source).
5. **Null/regression:** IIR-path output must be bit-identical before/after
   (Option B does not touch it).

---

## 7. One-line takeaway

The IR-path safety limiter runs un-oversampled, so a cranked, treble-heavy
signal through a bright IR folds `tanh` harmonics back into the band at up to
−28 dBc. Fix = oversample just that one `tanh` (Option B, 2–4×). Cheap, no tone
change, IIR path untouched — and worth doing before any neural capture through
an IR.
