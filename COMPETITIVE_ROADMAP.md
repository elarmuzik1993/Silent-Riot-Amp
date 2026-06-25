# Silent Riot — Competitive Roadmap (to a paid 1.0)

_Created: 2026-06-24 · Version at planning: v0.3.4 · Branch: `claude/plugin-audit-rating-ngbq53`_

This roadmap takes the project from "excellent engine, unfinished product" (audit
rating **7.5/10**) into **competitive, paid territory**. It is scoped to three
deliberate decisions:

- **Business model:** Commercial / paid.
- **Differentiation bet:** _Perfect one amp + real cabs_ — depth over breadth, the
  boutique route. The product is the **feel and tone of a single EL84 voicing**, not a
  feature checklist.
- **Platform priority:** Polish **Windows + Linux** first. macOS / AU / AAX explicitly
  deferred.

> Companion to `ROADMAP.md` (the engineering-to-1.0 audit). That doc closed the
> correctness/CI/release-eng gaps (P0–P4, all merged). This doc is about turning the
> finished engine into a **product people pay for**.

---

## Positioning — what "competitive" means here

The boutique paid niche (think STL, Mercuriall, ML Sound Lab single amps; the tier
_below_ Neural DSP's price) is won on three things, in order:

1. **Feel under the fingers** — dynamics, sag, pick response. This engine already models
   the physics (PSU droop, cathode sag, grid conduction, LTP). The job is to _finalize
   and prove_ it, not build it.
2. **Cabs that sound like real speakers** — currently the single biggest gap (all stock
   IRs are synthetic placeholders).
3. **Trust to charge money** — signed installers, a license gate, a trial, presets that
   sell the sound in 10 seconds.

Everything below serves those three. Anything that doesn't is deferred.

---

## Phase A — Freeze the tone _(this IS the product)_ — **do first**

The differentiation bet is "perfect one amp." Until the tone is frozen, nothing
downstream (cabs, presets, marketing demos, the parked neural model) can be finalized —
they all train/tune against a moving target.

- [ ] **[H] Define a tone sign-off protocol.** Pick reference material (DI clips at
  clean / edge-of-breakup / crank; a real AC30/Orange reference or target artist clips).
  Standardize gain-staging and A/B method. Without a written target, "ear-tuning" never
  terminates.
- [ ] **[H] Finalize the parked tuning constants** against that protocol and freeze them:
  `outputGainCompensation` (1.0), `V1B_RSOURCE_MAX_REAL` (150KΩ), `XCUR_SENSITIVITY`
  (2.0), the Shape EQ gain ranges, and the dynamics feel (sag clamp, grid charge).
  _(CLAUDE.md "Next" + ROADMAP Parked.)_
- [ ] **[H] Fix the two known DSP defects that affect the sound** (currently sitting in
  open PRs):
  - **IR-path limiter aliasing** (PR #7/#9): the output `tanh` runs at base SR in the IR
    convolution path (`PluginProcessor.cpp:398-414`) but oversampled in the IIR path —
    folds harmonics back to **-28 dBc** under drive through a bright IR. Fix: surgically
    oversample just the IR-path limiter. **This directly degrades the cab tone**, so it
    must land before cabs are finalized.
  - **LTP `reset()` stale `vCathode`** (PR #8): a sample-rate change mid-playback leaves
    the phase-inverter solver half-reset. Real-time correctness bug; merge the fix.
- [ ] **[H] Lock the tone with a golden/null regression** — the one open item from
  ROADMAP P4. Build a true render test against the real `processBlock` (needs a small
  plugin-host test target) so that **once frozen, the tone cannot silently drift** with
  later refactors. This is the safety net that makes "frozen" mean something.
- [ ] **[M] Document the frozen voicing** in the spec (final constants + the reference
  clips that signed it off) so it's reproducible.

**Exit criteria:** tone constants frozen and documented; both DSP defects fixed; a golden
test guards the rendered output; you can A/B against your reference and sign off.

---

## Phase B — Real cabinets _(the other half of the tone)_

All 8 stock IRs are synthetic placeholders (`IRLibrary.h::generateSyntheticIR`). For a
boutique amp sim the cab is half the sound — this is the highest-impact _product_ gap.

- [ ] **[H] Source real cabinet IRs.** Two paths — pick one:
  - **License** a commercial IR pack with redistribution rights (fastest; verify the
    license explicitly permits bundling inside a paid plugin).
  - **Capture your own** (real cab + mics) — slower, but becomes a genuine
    differentiator and avoids licensing ambiguity. On-brand for the boutique bet.
  - Curate **quality over quantity** — ~6–10 great cab/mic/position combos beats 100
    mediocre ones, and matches the depth-over-breadth strategy.
- [ ] **[H] Wire the stock-IR browser** (ROADMAP P2, deferred). Today the loader is
  user-IR-only; the stock table isn't reachable from `IR_INDEX` and the category tabs
  (ALL/STOCK/USER, UI spec §5.7) aren't built. Make the bundled cabs browsable and set a
  real cab as the factory default.
- [ ] **[M] IR UX polish** — level-match IRs so switching doesn't jump in loudness; show
  mic/position labels; keep the waveform display.
- [ ] **[M] Re-validate the IR path** under the Phase-A aliasing fix with the real IRs
  (bright real cabs are exactly what exposed the aliasing).

**Exit criteria:** N real, licensed/owned cabs ship and are browsable; factory default
uses one; no level jumps; IR path is clean under drive.

---

## Phase C — Commercial readiness _(you're charging money)_

None of this exists yet, and all of it is required to sell. Keep it lean — indie-grade,
not enterprise.

- [ ] **[H] Code signing.** Windows **Authenticode** cert (acquire from a CA;
  placeholder slots already exist in `release.yml`). Unsigned plugins trip SmartScreen
  and many hosts' security prompts — a conversion killer. Linux: ship SHA256 sums (done)
  + clear install instructions.
- [ ] **[H] Installers.** Windows: NSIS or Inno Setup placing the VST3 in the standard
  dir + Standalone. Linux: `.deb` + the existing tarball. Currently the release pipeline
  ships raw archives only — fine for devs, not for paying customers.
- [ ] **[H] Licensing / activation.** Decide the model (offline license file/serial is
  the indie-friendly default; avoid heavy DRM that hurts UX). Implement a gate +
  **time-limited or noise-burst trial** — non-negotiable for a paid plugin; most sales
  come after a trial.
- [ ] **[M] Legal/store basics.** EULA, version/changelog discipline, an update channel,
  a refund policy. Pick a storefront with VAT handling (Gumroad/Paddle) and/or a
  distributor (Plugin Boutique).

**Exit criteria:** signed installers for Win/Linux; a working trial→license flow; a place
to buy it.

---

## Phase D — Product polish & feel

The finishing layer that separates "works" from "feels premium."

- [ ] **[H] Curated factory presets** built on the _frozen_ tone + real cabs. Today's 5
  presets are "starter values" (ROADMAP P2). For a boutique amp, presets are a primary
  sales tool — they demo the sound instantly. Name them by use-case/feel.
- [ ] **[M] Front-of-amp essentials** (kept minimal, per depth-over-breadth):
  - **Noise gate** — near-essential for high-gain guitar; its absence is conspicuous.
  - **Boost / tight EQ** in front — very on-brand for an EL84 amp (everyone runs a
    Tubescreamer into a Vox/Orange). High value, low scope. _Optional but recommended._
- [ ] **[M] GUI finish.** Fine-tune the light theme (functional but not polished),
  complete the deferred keyboard/accessibility coverage (IRLoaderPanel list/drop-zone,
  Settings close-X), and clean up the remaining layout magic-number ladders
  (`PluginEditor.cpp` paint/resized).
- [ ] **[M] Global output trim + clear metering** so users can level-match the plugin in
  a mix.

**Exit criteria:** presets that sell the sound; a gate (and ideally a boost); a GUI with
no rough edges; level-matchable output.

---

## Phase E — Launch & growth

- [ ] **[H] Demo assets.** Audio demos (raw DI + processed, multiple genres), a short
  video walkthrough, clean screenshots, a product page. For an amp sim, _the audio demo
  is the sales pitch_.
- [ ] **[M] Closed beta.** Get it into the hands of a handful of guitarists for real
  tone/feel feedback before charging — the cheapest insurance against shipping a voicing
  only you like.
- [ ] **[M] Manual + support.** A short user manual, a changelog, and an update
  mechanism. Boutique buyers expect responsiveness.
- [ ] **[L] Post-launch backlog** (only after 1.0 sells): revisit the parked **neural
  capture** (now trainable against the frozen DSP), then **AU/macOS** if Mac demand
  shows up. These are growth bets, not launch blockers.

---

## Explicitly out of scope for paid 1.0

Deferred by the chosen strategy — tracked so they don't creep into the launch:

- **macOS / AU / AAX** — biggest reach-per-effort _later_, but needs a macOS CI runner +
  Apple notarization (and an Avid agreement for AAX). Revisit post-launch on demand.
- **Neural / NAM-style capture** — parked until the DSP target is frozen (Phase A does
  that). A strong _v1.x_ differentiator, not a 1.0 gate.
- **Multi-amp / FX suite** — directly contradicts the depth-over-breadth bet. Not now.

---

## Suggested sequencing

```
A (freeze tone + fix DSP defects + golden test)   ← unblocks everything
        │
        ▼
B (real cabs + browser)        C (signing/installer/license/trial)   ← can run in parallel
        │                                   │
        └───────────────┬───────────────────┘
                        ▼
              D (presets, gate, GUI finish)
                        │
                        ▼
              E (demos, beta, launch)
```

**Critical path is A → B → D → E.** Phase C (commercial plumbing) has no dependency on
the audio work and should run in parallel to avoid being a last-minute scramble.

## Why this gets to competitive territory

The engine already out-classes most indie amp sims on DSP and engineering hygiene (the
audit's strong scores). The 7.5 ceiling came entirely from _product_ gaps: synthetic
cabs, an unfrozen tone, two known defects, and zero commercial plumbing. This roadmap
closes exactly those, in dependency order, without diluting the boutique bet — turning a
strong engine into a paid product that can stand next to the established single-amp
boutique releases on Windows and Linux.
