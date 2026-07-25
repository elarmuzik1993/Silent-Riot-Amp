# Design Sketch — Hybrid Blend (Neural Capture ↔ Physics DSP)

**Status:** Concept / brainstorm — *not scheduled, not spec'd for implementation.*
**Target:** A future version (post-1.0 growth feature, per `COMPETITIVE_ROADMAP.md`).
**Audience:** Whoever picks this up after the DSP tone is frozen.
**Relationship to existing docs:** This is a *different* feature from the "neural
model" parked in `ROADMAP.md:187` / `CLAUDE.md:76`. That one distills the DSP into
a cheaper net (efficiency). This one blends a capture of a *real amp* with the DSP
(tone/feel). See §7.

---

## 1. The idea in one line

A single **BLEND** control that morphs the output between a **neural capture of a
real amplifier** (e.g. an Orange Micro Dark) and **Silent Riot's physics DSP** —
one static snapshot on one end, one live interactive model on the other.

```
                    ┌──────────────────────────┐
   guitar DI ───┬──▶│  Neural capture (static) │──┐
                │   └──────────────────────────┘  │
                │                              equal-power
                │                               crossfade ──▶ shared cab ─▶ out
                │   ┌──────────────────────────┐  │   ▲
                └──▶│  Physics DSP (dynamic)    │──┘   │
                    └──────────────────────────┘    BLEND knob
```

---

## 2. Why blend *different* amps (not the same one)

A crossfade only earns its place when the two ends are **genuinely different and
complementary**. Blending an amp with itself moves only along a subtle
"static vs. responsive" axis and produces no new tone in the middle — not worth a
knob. Blending two *different, complementary* voices is the classic dual-amp rig
(Fender + Marshall, Vox + Mesa): you do it precisely because each covers what the
other lacks, and the middle is a voice neither amp makes alone.

Plausibly complementary pair for this plugin:

| Source | Character |
|--------|-----------|
| **Orange Micro Dark** (neural capture) | Dark, gainy, MOSFET/Class-D tightness, firm low end |
| **EL84 push-pull** (the DSP) | Chimey, mid-forward, compresses and blooms |

Tight modern lows from one, singing mids and touch-compression from the other.
The "muddy average" failure mode only happens when the two sources are *redundant*
(overlapping character); for a complementary pair the blend fills gaps instead.

---

## 3. What makes this more than a normal dual-amp blend

In a standard dual-amp rig **both** sides are static — you blend two fixed tones.
Here, **one side is a frozen neural snapshot and the other is a live, knob-reactive
physics model.** Consequences:

- The **capture** holds the real amp's exact fingerprint, fixed.
- The **DSP** *moves* as you play — gain, `Shape`, `Volume`, sag, PSU droop all
  respond to input level and knob positions (see the signal chain in `CLAUDE.md`).

So even at a fixed BLEND setting, **the effective balance shifts with playing
dynamics**: soft playing sits nearer the clean static capture; digging in pushes
the DSP half into breathing/bloom, so the blend leans toward the EL84 side on
transients. The knob sets a *center of gravity*; the amp's own dynamics move you
around within it. That interaction — static reference + dynamic model on one
control — is the novel part, and the defensible pitch:

> **"Authenticity you can make responsive."** Zero = the exact captured amp.
> Full = a physics amp that sags and blooms. Everywhere between = a voice that's
> part real-Orange, part EL84, and fully alive.

---

## 4. Signal-chain placement — the one decision that drives everything

The capture point of the real amp determines the wiring. **Decide this first.**

### Option A — Capture is full rig (amp + mic'd cab / includes an IR)
- The capture already contains a cabinet.
- The DSP path also ends in a cab/IR.
- **Blending full outputs double-cabs** unless the DSP cab is bypassed when BLEND
  favors the capture — messy, and the two cab colorations fight in the middle.

### Option B — Capture is power-amp out via load-box (no cab)  ✅ preferred
- Capture the real amp *before* its speaker → no cab baked in.
- Blend the two **amp** signals *before* the DSP's cab stage.
- Run **one shared cab/IR** on the blended result.
- Result: the slider morphs only the **amp character**; the cabinet is constant, so
  the middle positions stay coherent. This is the clean design.

```
Option B wiring (preferred):

  DI ─┬─▶ neural capture (amp only) ──┐
      │                          equal-power blend ─▶ shared cab/IR ─▶ output stage
      └─▶ DSP amp (pre-cab tap)  ─────┘
```

The DSP already has a natural pre-cab tap: the chain reaches
`OT saturation → OT LP 7kHz` *before* the cab filters / IR convolution
(`CLAUDE.md` signal chain). That's the blend point for Option B.

---

## 5. Engineering requirements (the boring-but-critical bits)

A blend only *adds tone* if the two paths are aligned; otherwise it *comb-filters*.

1. **Latency alignment.** A neural net (WaveNet/LSTM/GRU) has its own processing
   delay; the DSP has oversampling latency. Delay-compensate the shorter path to
   sample accuracy, or transients smear/hollow out.
2. **Level matching.** RMS-match both paths *before* the crossfade or BLEND
   degenerates into a volume knob. Hook: the existing `outputGainCompensation`.
3. **Equal-power crossfade**, not linear — two decorrelated nonlinear signals dip
   ~3 dB at the 50/50 point with a linear fade. Use `sin/cos` (or √) law.
4. **CPU / endpoint bypass.** Running the full oversampled DSP *and* a neural net
   simultaneously is heavy. Bypass the idle path at BLEND = 0 and BLEND = 1;
   consider a threshold near each end below which the far path is muted.
5. **Anti-alias the capture path too.** Per `handoff-IR-limiter-aliasing.md`, a
   capture taken *through the IR path* would bake limiter aliasing into the target
   — capture cleanly (Option B avoids the IR path entirely on capture).
6. **It's a voicing control, not physics.** 50/50 is not "half a real amp" in any
   physical sense — and that's fine. It's a design morph knob; tune the endpoints
   and the law so the middle sounds *intentional*, not averaged.

---

## 6. Runtime / model format (open, to research later)

- **Inference engine:** RTNeural is the common lightweight choice for real-time
  neural amp inference in C++ (header-only, no heavy deps) — candidate, not a
  decision.
- **Capture pipeline:** NAM-style training against the real amp. Note that
  training against *real hardware* has a **fixed target from day one**, so the
  "retrain after every DSP tweak" churn that parks the other neural item
  (`CLAUDE.md:76`) **does not apply to this feature** — the capture and the DSP
  only meet at the mixer, never in training. This feature is therefore *less*
  gated on freezing the DSP than the distillation model is.
- **Model storage:** embed the stock Micro Dark capture (like stock IRs), and
  allow user-loaded captures later (mirror the IR loader's user-slot pattern).

---

## 7. How this fits the roadmap

- **Not the same as the parked "neural model."** `ROADMAP.md:187` and
  `COMPETITIVE_ROADMAP.md:170` park a net that *imitates the DSP* (efficiency,
  blocked on freezing the tone). This hybrid-blend feature *adds a real-amp voice
  alongside the DSP* (tone/feel). Different purpose, different training target.
- **Still a post-1.0 growth bet, not a launch gate** — consistent with the
  competitive roadmap's positioning of neural work as a *v1.x differentiator*.
- **Soft dependency on tone sign-off:** the DSP endpoint should be voiced/stable
  enough that the blend's DSP side isn't a moving target for the *player*
  (presets, expectations), even though it isn't a training target.

---

## 8. Open questions

1. **Capture point** — full rig (Option A) or power-amp/load-box (Option B)?
   Drives the entire wiring. *Recommendation: B.*
2. **UI** — reuse `NotchKnob`, or a dedicated horizontal slider/labeled fader
   ("CAPTURE ⟷ MODEL")? A labeled slider reads the intent more clearly than a
   0–10 knob.
3. **One capture or a small library?** Ship one stock Micro Dark capture, or a
   few captures the blend can target (adds "which amp am I blending toward").
4. **Per-preset blend?** Should BLEND be a saved parameter per preset (likely yes,
   via APVTS like every other param).
5. **Does the DSP need to voice the Micro Dark at all?** Not required for the
   "two complementary flavors" version. Only relevant if a future "same amp,
   static ↔ dynamic" mode is ever wanted (explicitly *not* the pitch here).

---

*Concept doc — Monolit Beatz / Silent Riot. Sketch only; supersede with a proper
spec section before any implementation.*
