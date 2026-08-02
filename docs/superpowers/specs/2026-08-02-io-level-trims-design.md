# Design — INPUT / OUTPUT level trims

Date: 2026-08-02
Status: approved, pending implementation plan
Context: USE-59 follow-up

## Why

The `VOLUME_KNOB_HEADROOM` change that restored the amp's give cost ~20 dB of
level, of which `OUTPUT_MAKEUP_GAIN = 3.5` recovers ~11 dB. The remaining gap is
crest factor and cannot be reclaimed internally without re-compressing — see
defect 8 in `handoff-power-section-calibration.md`. That leaves two real needs
the amp has no control for:

- **Output**: the player wants to set the plugin's level for their mix without
  touching a calibration constant, and without spending the give.
- **Input**: pickups and interfaces vary by well over 20 dB. Now that the amp
  responds to input level (36 dB in → 10.8 dB out, where it used to be 2.4 dB),
  where a player sits on the give curve depends on their rig. The GAIN knob
  should mean roughly the same thing for everyone.

These are **gain-staging utilities, not tone controls**. Both are unity by
default and the amp is bit-identical at defaults.

## Scope

In scope: two new parameters, their tapers, chain placement, UTILITY card UI,
meter re-tapping, tests.

Out of scope:

- `BOOST` is unchanged. It overlaps INPUT slightly (0..+18 dB pre-amp) but
  serves a different job: BOOST is a voiced effect (level + tight + bright) you
  switch in; INPUT is an always-active, tone-neutral trim that also cuts.
- `OUTPUT_MAKEUP_GAIN = 3.5` stays as the internal calibration that makes unity
  sound right. The user trim rides on top of it.
- A1 (clean loudness) and B1 (clean THD) re-ratification remains a separate open
  item for a Calibration Pass.

## Chain placement

Anchor to the section comments, not line numbers — these drift.

```
buffer in
  ├─ "Live tuner capture"            UNCHANGED — raw tap, tunes at any trim
  ├─ if (!powerOn || tunerMute) return;        <-- NOTE: early return sits here
  ├─ INPUT trim (smoothed)                     NEW — AFTER the early return
  ├─ "Input level metering (RMS)"    moves post-trim
  ├─ "Pre-amp effects (gate → boost)"
  ├─ oversample → AMP → cab → tanh → downsample
  ├─ delay → reverb → widener
  ├─ OUTPUT trim (smoothed)                    NEW
  ├─ "Post-effects safety limiter"   condition extended
  └─ "GUI level metering (RMS)"      moves post-trim
```

The INPUT trim goes **after** the `!powerOn || tunerMute` early return, not
immediately after the tuner tap. Putting it before the return would be wasted
work on a cleared buffer and would not change behaviour.

Both trims are linear gains at base sample rate, outside the oversampled amp
loop — there is no nonlinearity to alias.

The tuner tap stays **before** the INPUT trim, so cutting input never costs
tuning. Both meters move **after** their trim, so the trims are settable by eye:
the input meter shows what the amp is being fed, the output meter shows what the
DAW receives.

## Parameters

Appended to `createParameterLayout()` **after `WIDTH`, which is currently the
last parameter added**. Appending rather than inserting preserves positional
automation indexing for sessions saved with v0.3.4. (APVTS state save/restore is
keyed by parameter ID in the ValueTree, so state reload is safe either way; this
protects host automation lanes specifically.) A comment marks the append point so
a future edit does not insert mid-list.

```cpp
// Parameters.h
static const juce::String INPUT_LEVEL  = "inputLevel";   // float 0–1, 0.5 = unity
static const juce::String OUTPUT_LEVEL = "outputLevel";  // float 0–1, 0.5 = unity
```

Both default `0.5f`.

### Host-value sanitization (mandatory)

Every float parameter read in `processBlock` is wrapped in `sanitizeUnitParam()`
(see `ParamSanitize.h`), and `prepareToPlay` seeds its smoothers from sanitized
values for the same reason. A misbehaving host can push NaN/inf into a raw
parameter and poison filter state permanently — this was fixed deliberately in
commit `34a1758` and is guarded by `param_sanitize_tests.cpp`.

**`sanitizeUnitParam()` cannot be used as-is.** It falls back to `0.0f`, and its
comment states that is neutral "for every one of these params" — true for the
existing effects params (0 = bypassed), but false for a centre-detented trim:
`0.0f` is −12 dB on INPUT and **exact silence** on OUTPUT. A single NaN from a
host would mute the plugin.

Add a fallback-taking overload and route the trims through it:

```cpp
inline float sanitizeUnitParamOr(float v, float fallback)
{
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : fallback;
}
inline float sanitizeUnitParam(float v) { return sanitizeUnitParamOr(v, 0.0f); }
```

```cpp
float inTrim  = sanitizeUnitParamOr(apvts.getRawParameterValue(Params::IDs::INPUT_LEVEL )->load(), 0.5f);
float outTrim = sanitizeUnitParamOr(apvts.getRawParameterValue(Params::IDs::OUTPUT_LEVEL)->load(), 0.5f);
```

Correct the now-false comment in `ParamSanitize.h`, and add coverage for the
overload to `param_sanitize_tests.cpp`.

### Channel handling

The amp reads **channel 0 only** and copies to stereo on output — the pre-amp
gate/boost block follows that same convention. So:

- **INPUT** trims channel 0 only, advancing the smoother once per sample.
- **OUTPUT** runs after delay/reverb/widener on a genuine stereo bus, so it must
  trim every channel with the smoother advanced once per **sample**, not once per
  channel (sample-outer, channel-inner), or the two channels desynchronise during
  a ramp.

## Tapers

Live alongside `gainDriveTaper` / `volumeDriveTaper` in `TriodeWaveshaper.h`;
ranges as constants in `Constants.h`.

```cpp
constexpr float INPUT_TRIM_RANGE_DB  = 12.0f;  // ±
constexpr float OUTPUT_TRIM_BOOST_DB = 12.0f;  // above centre
constexpr float OUTPUT_TRIM_FADE_DB  = 60.0f;  // below centre, then true silence

inline float inputTrimGain(float x) {                    // -12 .. +12 dB
    x = std::clamp(x, 0.0f, 1.0f);
    return std::pow(10.0f, ((x - 0.5f) * 2.0f * INPUT_TRIM_RANGE_DB) / 20.0f);
}

inline float outputTrimGain(float x) {                   // silence .. +12 dB
    x = std::clamp(x, 0.0f, 1.0f);
    if (x <= 0.0f) return 0.0f;                          // exact silence
    const float dB = (x >= 0.5f) ? (x - 0.5f) * 2.0f * OUTPUT_TRIM_BOOST_DB
                                 : (x - 0.5f) * 2.0f * OUTPUT_TRIM_FADE_DB;
    return std::pow(10.0f, dB / 20.0f);
}
```

Both return exactly `1.0f` at `x = 0.5f`.

OUTPUT's fader law below centre is deliberately steeper than above: knob 4 →
−12 dB, 3 → −24, 2 → −36, 1 → −48, 0 → silent. The slope discontinuity at the
detent is intended — fine resolution above unity for mix trimming, quick fade
below. This is a control curve, not a waveshaper, so the kink has no audio
consequence.

Both values are smoothed with `juce::SmoothedValue<float>` at base SR, reusing
the existing `fxRampSec = 0.05` (50 ms) that every other effects smoother
already uses, rather than introducing a second ramp constant. The
skip-when-unity check tests the *smoothed* value via `isSmoothing()`, so a trim
returning to centre resumes the bit-exact path once its ramp completes.

## Tone-neutrality guarantee

`PluginProcessor.cpp:692` documents that the dry amp path is **bit-identical**
when the effects module is off. Always-on gain stages would silently break that,
so:

- Skip the INPUT multiply entirely when its smoothed gain is exactly 1.0 and the
  smoother has settled.
- Same for OUTPUT.
- Extend the existing softClip condition to
  `delayOn || reverbOn || widenerActive || outputBoosting`, where
  `outputBoosting` means the smoothed OUTPUT gain exceeds 1.0. At or below unity
  the amp's own tanh already guarantees ±1, so no clip is needed and the dry path
  stays untouched.

At defaults the plugin is sample-for-sample what it is today.

## UI

Two additional `NotchKnob`s in the UTILITY card, which already hosts the
self-gating utility knobs:

```
+---------------------------+
| UTILITY                   |
|  (o)   (o)   (o)   (o)    |
| THRESH WIDTH INPUT OUTPUT |
+---------------------------+
```

`layoutKnobRow(inner, { &gateThresh, &width, &inputLevel, &outputLevel })` —
the helper already takes a list and clamps to the available height, and the
BOOST/DELAY/REVERB cards already run three knobs each. No ON toggle, consistent
with the card's convention that centre/zero is a no-op. APVTS `SliderAttachment`
as with every other knob.

## Files to touch

| file | change |
|---|---|
| `src/utils/Parameters.h` | two new IDs |
| `src/utils/Constants.h` | three range constants |
| `src/dsp/tubes/TriodeWaveshaper.h` | `inputTrimGain` / `outputTrimGain` |
| `src/PluginProcessor.h` | two `juce::SmoothedValue<float>` members |
| `src/PluginProcessor.cpp` | params appended after `WIDTH`; sanitized reads; smoother init in `prepareToPlay`; snap in `resetDSPState()`; two trim applications; softClip condition; both meter taps move |
| `src/gui/EffectsPanel.h` | two `NotchKnob` members + two accessors |
| `src/gui/EffectsPanel.cpp` | add to the UTILITY `layoutKnobRow` list; labels |
| `src/PluginEditor.cpp` | two `SliderAttachment`s |
| `tests/signal_chain_tests.cpp` | taper tests |
| `tests/param_sanitize_tests.cpp` | sanitize coverage for both |
| `docs/SilentRiot_UI_Spec.md` | UTILITY card is described as two-knob at lines ~590/600/649 |
| `CLAUDE.md` | UTILITY card description; note that meters are now post-trim |

`prepareToPlay` must seed both smoothers from **sanitized** parameter values
(matching the existing `boostLevelSm` block) so the first block does not ramp
from a stale or poisoned value. `resetDSPState()` should snap both smoothers to
their current target rather than to unity, so a re-prepare does not audibly jump.

## Testing

Unit (`signal_chain_tests.cpp`, mirroring the gain/volume taper tests):

- `inputTrimGain(0.5) == 1.0` and `outputTrimGain(0.5) == 1.0` exactly.
- Endpoints: input ±12 dB at 0 and 1; output +12 dB at 1; `outputTrimGain(0.0)`
  returns exactly `0.0f`.
- Monotonic across [0,1] for both.
- Out-of-range knob values clamp.

Bit-exactness (concrete method — there is no processBlock-level harness in the
JUCE-free suite, so use the render tool):

```bash
# on the parent commit
sr_render --out /tmp/before --signal note:110:0.5:2.0 \
          --gain 1.00 --volume 0.70 --shape 0.50 --7w 0 --cab 1
# after the change, defaults unchanged
sr_render --out /tmp/after  --signal note:110:0.5:2.0 \
          --gain 1.00 --volume 0.70 --shape 0.50 --7w 0 --cab 1
cmp /tmp/before.f32 /tmp/after.f32     # must be byte-identical
```

Repeat with `--signal silence:5.0` to cover the idle path. The skip-when-unity
check is what makes this pass; if `cmp` reports a difference, the trim is being
applied at unity and the guarantee at the "Post-effects safety limiter" comment
is broken.

Express "settled" with `juce::SmoothedValue::isSmoothing()` — the skip applies
only when the smoother is not ramping *and* its current value is exactly 1.0f.

Regression (sign-off harness, at defaults — must be unchanged):

| gate | expected |
|---|---|
| D1 sag depth | 2.77 |
| B2 edge THD | 7.86 |
| B3 crank THD | 20.5 |
| A2 peak | −6.00 |

## Risks

- **Knob width.** `layoutKnobRow` sets `knobW = jmin(80, rowWidth / n)`. The
  UTILITY card's inner row is ~305 px, so a fourth knob takes each from 80 px to
  ~76 px. It fits arithmetically, but `NotchKnob` draws knob + value + label, and
  "THRESH"/"OUTPUT" at 76 px needs **visual confirmation in the built UI** — do
  not assume it. If labels clip, shorten to "IN"/"OUT" rather than reflowing the
  card.

- Adding parameters shifts nothing for state restore, but any host that stores
  automation by index would see two new lanes at the end. Mitigated by appending.
- Moving the meter taps changes what the VU meters read at non-unity trims. This
  is the intended behaviour, but it means the meters are no longer a raw view of
  the interface — noted in `CLAUDE.md` under GUI Architecture.
