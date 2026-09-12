# INPUT / OUTPUT Level Trims Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two tone-neutral gain-staging trims — INPUT (±12 dB, normalises pickup/interface level) and OUTPUT (silence…+12 dB post-limiter master) — without changing the amp's sound at their default centre positions.

**Architecture:** Two new APVTS float parameters, both defaulting to 0.5 (unity). Pure-function tapers live beside the existing `gainDriveTaper`/`volumeDriveTaper` in `TriodeWaveshaper.h`. INPUT is applied at base SR on channel 0 after the power-off early return and before the input meter; OUTPUT is applied on the full stereo bus after the widener and before the safety clip and output meter. Both multiplies are skipped entirely while the trim sits at unity, so the dry path stays bit-identical.

**Tech Stack:** C++17, JUCE 7, CMake + Ninja, Catch2 v3.

## Global Constraints

- All DSP constants live in `Constants.h`. Suffix `_N` = normalized, `_REAL` = real units, no suffix = dimensionless.
- `namespace sr` for all DSP code.
- **No dynamic allocation in `processBlock`.**
- New APVTS parameters are **append-only** at the end of `createParameterLayout()` — hosts index automation positionally.
- Every raw host float read in `processBlock` must be sanitized (see Task 2).
- The dry amp path must remain **bit-identical** at default parameter values.
- Windows build (toolchain is off PATH):
  - vcvars: `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat`
  - cmake: `...\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
  - Build tree is already configured at `build-win/` with `SR_BUILD_TESTS=ON` and `SR_BUILD_TOOLS=ON`.
- Commit messages include `Refs USE-59`. They carry **no AI attribution** — no
  `Co-Authored-By:` trailer, no session link, no generated-with footer. See
  **Commit & PR conventions** in `AGENTS.md`, which governs this.

**Build helper** — create `build.bat` once (outside the repo, e.g. in your temp dir) and reuse it for every task:

```bat
@echo off
set VS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
set CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
cd /d C:\Users\boris\Silent-Riot-Amp
"%CMAKE%" --build build-win --target %*
```

A `vswhere.exe is not recognized` line on the way through is benign.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `src/utils/Constants.h` | three range constants | 1 |
| `src/dsp/tubes/TriodeWaveshaper.h` | `inputTrimGain` / `outputTrimGain` pure tapers | 1 |
| `tests/signal_chain_tests.cpp` | taper regression tests | 1 |
| `src/utils/ParamSanitize.h` | `sanitizeUnitParamOr` fallback overload | 2 |
| `tests/param_sanitize_tests.cpp` | overload coverage | 2 |
| `src/utils/Parameters.h` | two parameter IDs | 3 |
| `src/PluginProcessor.h` | two `SmoothedValue` members | 3 |
| `src/PluginProcessor.cpp` | param layout, sanitized reads, smoother init, both trim applications, meter taps, softClip condition | 3 |
| `src/gui/EffectsPanel.h/.cpp` | two knobs in the UTILITY card | 4 |
| `src/PluginEditor.h/.cpp` | two slider attachments | 4 |
| `docs/SilentRiot_UI_Spec.md`, `CLAUDE.md` | doc updates | 5 |

---

### Task 1: Trim taper functions

**Files:**
- Modify: `src/utils/Constants.h` (after `OUTPUT_MAKEUP_GAIN`)
- Modify: `src/dsp/tubes/TriodeWaveshaper.h` (after `volumeDriveTaper`)
- Test: `tests/signal_chain_tests.cpp` (append after the volume taper tests)

**Interfaces:**
- Consumes: nothing.
- Produces: `float sr::inputTrimGain(float knob)` and `float sr::outputTrimGain(float knob)`, both taking a 0–1 knob value and returning a linear gain multiplier. Both return exactly `1.0f` at `knob == 0.5f`. Task 3 calls both.

- [ ] **Step 1: Write the failing tests**

Append to `tests/signal_chain_tests.cpp`. This file already has `Approx` in scope (used by the existing taper tests) — match that style, do not qualify it as `Catch::Approx`.

```cpp
// ── Input / output level trims (regression) ───────────────────
//
// Gain-staging utilities, NOT tone controls. Both are unity at knob centre and
// the processor skips the multiply entirely while they sit there, so the dry
// path stays bit-identical at defaults.
//
// INPUT is linear in dB across the whole sweep. OUTPUT is deliberately steeper
// below the detent than above, so the bottom half works as a fader while the
// top half stays fine enough for mix trimming.

TEST_CASE("Input trim: unity at centre", "[chain][trim]") {
    REQUIRE(sr::inputTrimGain(0.5f) == Approx(1.0f).margin(1e-7f));
}

TEST_CASE("Input trim: endpoints are -12 and +12 dB", "[chain][trim]") {
    REQUIRE(sr::inputTrimGain(0.0f) == Approx(0.2511886f).margin(1e-6f));  // -12 dB
    REQUIRE(sr::inputTrimGain(1.0f) == Approx(3.9810717f).margin(1e-5f));  // +12 dB
}

TEST_CASE("Input trim: monotonically increasing", "[chain][trim]") {
    float prev = -1.0f;
    for (float x = 0.0f; x <= 1.0f; x += 0.01f) {
        float y = sr::inputTrimGain(x);
        REQUIRE(y >= prev);
        prev = y;
    }
}

TEST_CASE("Input trim: clamps out-of-range knob values", "[chain][trim]") {
    REQUIRE(sr::inputTrimGain(-0.5f) == Approx(sr::inputTrimGain(0.0f)).margin(1e-7f));
    REQUIRE(sr::inputTrimGain(1.5f)  == Approx(sr::inputTrimGain(1.0f)).margin(1e-7f));
}

TEST_CASE("Output trim: unity at centre", "[chain][trim]") {
    REQUIRE(sr::outputTrimGain(0.5f) == Approx(1.0f).margin(1e-7f));
}

TEST_CASE("Output trim: knob 0 is exact silence", "[chain][trim]") {
    // Must be exactly zero, not merely small — this is the master fade's floor.
    REQUIRE(sr::outputTrimGain(0.0f) == 0.0f);
}

TEST_CASE("Output trim: boosts to +12 dB at full knob", "[chain][trim]") {
    REQUIRE(sr::outputTrimGain(1.0f) == Approx(3.9810717f).margin(1e-5f));
}

TEST_CASE("Output trim: fader law below the detent", "[chain][trim]") {
    REQUIRE(sr::outputTrimGain(0.40f) == Approx(0.2511886f).margin(1e-6f));  // -12 dB
    REQUIRE(sr::outputTrimGain(0.25f) == Approx(0.0316228f).margin(1e-7f));  // -30 dB
}

TEST_CASE("Output trim: fade below centre is steeper than boost above", "[chain][trim]") {
    // Same knob distance either side of the detent, much bigger cut than boost.
    const float cut   = sr::outputTrimGain(0.4f);   // -12 dB
    const float boost = sr::outputTrimGain(0.6f);   // +2.4 dB
    REQUIRE((1.0f / cut) > boost);
}

TEST_CASE("Output trim: monotonically increasing", "[chain][trim]") {
    float prev = -1.0f;
    for (float x = 0.0f; x <= 1.0f; x += 0.01f) {
        float y = sr::outputTrimGain(x);
        REQUIRE(y >= prev);
        prev = y;
    }
}

TEST_CASE("Output trim: clamps out-of-range knob values", "[chain][trim]") {
    REQUIRE(sr::outputTrimGain(-0.5f) == 0.0f);
    REQUIRE(sr::outputTrimGain(1.5f)  == Approx(sr::outputTrimGain(1.0f)).margin(1e-7f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```
build.bat SilentRiotTests
```

Expected: compile error — `'inputTrimGain': is not a member of 'sr'`. That is the correct failure; the functions do not exist yet.

- [ ] **Step 3: Add the range constants**

In `src/utils/Constants.h`, immediately after the `OUTPUT_MAKEUP_GAIN` declaration:

```cpp
// ── Input / output level trims (gain staging) ───────────────
// Tone-neutral utility trims, unity at knob centre — NOT part of the amp's
// voice. INPUT normalises pickup and interface level so the GAIN knob means
// roughly the same thing across rigs, which matters now that the amp actually
// responds to input level (36 dB in -> 10.8 dB out). OUTPUT is a post-limiter
// master, so boosting it cannot add amp compression.
constexpr float INPUT_TRIM_RANGE_DB  = 12.0f;  // +/- about unity
constexpr float OUTPUT_TRIM_BOOST_DB = 12.0f;  // above unity
constexpr float OUTPUT_TRIM_FADE_DB  = 60.0f;  // below unity, then true silence
```

- [ ] **Step 4: Add the taper functions**

In `src/dsp/tubes/TriodeWaveshaper.h`, immediately after `volumeDriveTaper` and before the closing `} // namespace sr`. `<cmath>` and `<algorithm>` are already included at the top of this file.

```cpp
// ── Gain-staging trims (see Constants.h) ────────────────────
// Both are unity at knob centre (0.5). The processor skips the multiply
// entirely while a trim sits at unity, so the dry path stays bit-identical at
// defaults — do not "simplify" these to always-on gain stages.

// INPUT: -12 .. +12 dB, linear in dB across the whole sweep.
inline float inputTrimGain(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return std::pow(10.0f, ((x - 0.5f) * 2.0f * INPUT_TRIM_RANGE_DB) / 20.0f);
}

// OUTPUT: silence .. +12 dB. Steeper below the detent than above, so the bottom
// half works as a master fade (knob 4 -> -12 dB, 2 -> -36 dB) while the top half
// stays fine enough for mix trimming. Knob 0 is exact silence. The slope
// discontinuity at the detent is intentional: this is a control curve, not a
// waveshaper, so it has no audio consequence.
inline float outputTrimGain(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    if (x <= 0.0f) return 0.0f;                 // exact silence, not just small
    const float dB = (x >= 0.5f) ? (x - 0.5f) * 2.0f * OUTPUT_TRIM_BOOST_DB
                                 : (x - 0.5f) * 2.0f * OUTPUT_TRIM_FADE_DB;
    return std::pow(10.0f, dB / 20.0f);
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```
build.bat SilentRiotTests
build-win\tests\SilentRiotTests.exe "[trim]"
```

Expected: `All tests passed` — 11 test cases.

- [ ] **Step 6: Run the full suite for regressions**

```
build-win\tests\SilentRiotTests.exe
```

Expected: `All tests passed`, with the case count risen by 11 from the 255 that pass before this task. **No failures.** (Don't match the assertion total exactly — it moves with every added test.)

- [ ] **Step 7: Commit**

```bash
git add src/utils/Constants.h src/dsp/tubes/TriodeWaveshaper.h tests/signal_chain_tests.cpp
git commit -m "Add INPUT/OUTPUT trim taper functions

Pure tapers for the two gain-staging trims, beside the existing gain and
volume tapers. Both unity at knob centre. OUTPUT is deliberately steeper
below the detent than above so the bottom half works as a master fade,
and returns exact zero at knob 0.

Refs USE-59"
```

---

### Task 2: Sanitize fallback overload

**Why this exists:** `sanitizeUnitParam()` falls back to `0.0f` on NaN/inf, and its comment claims that is neutral "for every one of these params". That is true for the existing effects params (0 = bypassed) but **false** for a centre-detented trim: `0.0f` is −12 dB on INPUT and exact silence on OUTPUT. Without this task, one NaN from a misbehaving host mutes the plugin.

**Files:**
- Modify: `src/utils/ParamSanitize.h`
- Test: `tests/param_sanitize_tests.cpp` (append)

**Interfaces:**
- Consumes: nothing.
- Produces: `float sr::sanitizeUnitParamOr(float v, float fallback)` — clamps finite values to [0,1], returns `fallback` for NaN/±inf. `sanitizeUnitParam(v)` is preserved exactly, now delegating with fallback `0.0f`. Task 3 calls the overload with `0.5f`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/param_sanitize_tests.cpp`. This file qualifies as `Catch::Approx` — match that.

```cpp
TEST_CASE("sanitizeUnitParamOr: in-range values pass through unchanged", "[paramSanitize]")
{
    REQUIRE(sanitizeUnitParamOr(0.8f, 0.5f) == Catch::Approx(0.8f));
    REQUIRE(sanitizeUnitParamOr(0.0f, 0.5f) == Catch::Approx(0.0f));
    REQUIRE(sanitizeUnitParamOr(1.0f, 0.5f) == Catch::Approx(1.0f));
}

TEST_CASE("sanitizeUnitParamOr: out-of-range values clamp, not fall back", "[paramSanitize]")
{
    REQUIRE(sanitizeUnitParamOr(-3.0f, 0.5f) == Catch::Approx(0.0f));
    REQUIRE(sanitizeUnitParamOr(3.0f, 0.5f)  == Catch::Approx(1.0f));
}

TEST_CASE("sanitizeUnitParamOr: NaN and infinities use the caller's fallback", "[paramSanitize]")
{
    // The centre-detented trims pass 0.5 here. Falling back to 0.0 would mean
    // -12 dB on the input trim and total silence on the output trim.
    REQUIRE(sanitizeUnitParamOr(std::nan(""), 0.5f) == Catch::Approx(0.5f));
    REQUIRE(sanitizeUnitParamOr(std::numeric_limits<float>::infinity(), 0.5f)
            == Catch::Approx(0.5f));
    REQUIRE(sanitizeUnitParamOr(-std::numeric_limits<float>::infinity(), 0.5f)
            == Catch::Approx(0.5f));
}

TEST_CASE("sanitizeUnitParam still falls back to 0.0 for bypass-at-zero params", "[paramSanitize]")
{
    REQUIRE(sanitizeUnitParam(std::nan("")) == Catch::Approx(0.0f));
    REQUIRE(sanitizeUnitParam(0.7f) == Catch::Approx(0.7f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```
build.bat SilentRiotTests
```

Expected: compile error — `'sanitizeUnitParamOr': identifier not found`.

- [ ] **Step 3: Add the overload and correct the stale comment**

Replace the existing comment + function in `src/utils/ParamSanitize.h` with:

```cpp
// Most params in this project are "0 = neutral/bypassed" (gate threshold 0 =
// always-open, boost/delay/reverb knobs at 0 = no effect), so 0.0f is the right
// fallback for them. Centre-detented trims are NOT: for those, 0.0f means full
// cut — and on the output trim, silence — so a single NaN from a host would mute
// the plugin. Those callers pass their own neutral value via the overload.
inline float sanitizeUnitParamOr(float v, float fallback)
{
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : fallback;
}

inline float sanitizeUnitParam(float v)
{
    return sanitizeUnitParamOr(v, 0.0f);
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```
build.bat SilentRiotTests
build-win\tests\SilentRiotTests.exe "[paramSanitize]"
```

Expected: `All tests passed`.

- [ ] **Step 5: Commit**

```bash
git add src/utils/ParamSanitize.h tests/param_sanitize_tests.cpp
git commit -m "Add fallback-taking sanitize overload for centre-detented params

sanitizeUnitParam falls back to 0.0 on NaN/inf, which its comment called
neutral for every param. That holds for the existing bypass-at-zero effects
params but not for a centre-detented trim, where 0.0 is full cut and, on an
output trim, silence -- one NaN from a host would mute the plugin.

sanitizeUnitParam is unchanged in behaviour, now delegating with 0.0.

Refs USE-59"
```

---

### Task 3: Parameters and processor wiring

**Files:**
- Modify: `src/utils/Parameters.h` (after `WIDTH` in `namespace IDs`)
- Modify: `src/PluginProcessor.h` (smoother members, next to `boostLevelSm`)
- Modify: `src/PluginProcessor.cpp` (param layout, `prepareToPlay`, `resetDSPState`, `processBlock`)

**Interfaces:**
- Consumes: `sr::inputTrimGain` / `sr::outputTrimGain` (Task 1); `sr::sanitizeUnitParamOr` (Task 2).
- Produces: APVTS parameters `"inputLevel"` and `"outputLevel"`, both `AudioParameterFloat` over 0–1 with default `0.5f`, exposed as `sr::Params::IDs::INPUT_LEVEL` and `sr::Params::IDs::OUTPUT_LEVEL`. Task 4 attaches sliders to these IDs.

- [ ] **Step 1: Add the parameter IDs**

In `src/utils/Parameters.h`, after the `WIDTH` line and before the closing `}` of `namespace IDs`:

```cpp
    // ── Gain staging (utility trims, unity at knob centre 0.5) ──
    static const juce::String INPUT_LEVEL  = "inputLevel";   // float 0–1: ±12 dB
    static const juce::String OUTPUT_LEVEL = "outputLevel";  // float 0–1: silence..+12 dB
```

- [ ] **Step 2: Add the smoother members**

In `src/PluginProcessor.h`, replace the existing `boostLevelSm` declaration line with:

```cpp
    juce::SmoothedValue<float> boostLevelSm, boostTightSm, boostBrightSm, gateThreshSm;
    // Gain-staging trims, smoothed at base SR. processBlock skips the multiply
    // entirely while a trim sits at unity and is not ramping, which is what keeps
    // the dry path bit-identical at defaults.
    juce::SmoothedValue<float> inputTrimSm, outputTrimSm;
```

- [ ] **Step 3: Append the parameters to the layout**

In `src/PluginProcessor.cpp`, in `createParameterLayout()`, immediately after the `WIDTH` `params.push_back(...)` block and before `return { params.begin(), params.end() };`:

```cpp
    // ── Gain staging (utility trims) ──
    // APPEND-ONLY BELOW THIS POINT. Hosts that index automation positionally
    // bind lanes by order, so inserting a parameter above this line shifts every
    // later lane and breaks automation in sessions saved with an earlier build.
    // Add new parameters at the end, never in the middle.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::INPUT_LEVEL, 1 }, "Input Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ Params::IDs::OUTPUT_LEVEL, 1 }, "Output Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
```

- [ ] **Step 4: Seed the smoothers in prepareToPlay**

In `src/PluginProcessor.cpp`, in `prepareToPlay`, immediately after the `boostBrightSm.setCurrentAndTargetValue(...)` block:

```cpp
    inputTrimSm.reset(sampleRate, fxRampSec);
    outputTrimSm.reset(sampleRate, fxRampSec);
    // 0.5 (unity), not 0.0 — see ParamSanitize.h. A 0.0 fallback here would
    // mean -12 dB in and silence out on the first block after a NaN.
    inputTrimSm.setCurrentAndTargetValue(sanitizeUnitParamOr(
        apvts.getRawParameterValue(Params::IDs::INPUT_LEVEL)->load(), 0.5f));
    outputTrimSm.setCurrentAndTargetValue(sanitizeUnitParamOr(
        apvts.getRawParameterValue(Params::IDs::OUTPUT_LEVEL)->load(), 0.5f));
```

- [ ] **Step 5: Snap the smoothers in resetDSPState**

In `src/PluginProcessor.cpp`, at the end of `resetDSPState()`, before its closing brace:

```cpp
    // Snap the gain-staging trims to their current targets so a re-prepare or
    // sample-rate change doesn't audibly ramp from a stale value.
    inputTrimSm.setCurrentAndTargetValue(inputTrimSm.getTargetValue());
    outputTrimSm.setCurrentAndTargetValue(outputTrimSm.getTargetValue());
```

- [ ] **Step 6: Add the sanitized parameter reads**

In `processBlock`, alongside the other sanitized float reads (immediately after the `widthParam` line):

```cpp
    float inTrimParam  = sanitizeUnitParamOr(
        apvts.getRawParameterValue(Params::IDs::INPUT_LEVEL)->load(), 0.5f);
    float outTrimParam = sanitizeUnitParamOr(
        apvts.getRawParameterValue(Params::IDs::OUTPUT_LEVEL)->load(), 0.5f);
```

- [ ] **Step 7: Apply the INPUT trim and move the input meter**

In `processBlock`, replace the whole `// ── Input level metering (RMS before processing) ──` block with:

```cpp
    // ── INPUT trim (gain staging) ──
    // Tone-neutral ±12 dB trim so the GAIN knob means roughly the same thing
    // across pickups and interfaces. Sits AFTER the power/tuner-mute return
    // (before it would be wasted work on a cleared buffer) and BEFORE the input
    // meter, so the meter shows what the amp is actually being fed. The amp
    // reads channel 0 only, so only channel 0 is trimmed — same convention as
    // the gate/boost block below. Skipped entirely at unity to keep the dry
    // path bit-identical.
    inputTrimSm.setTargetValue(inTrimParam);
    if (inputTrimSm.isSmoothing() || inputTrimGain(inputTrimSm.getCurrentValue()) != 1.0f)
    {
        const int n = buffer.getNumSamples();
        float* ch0 = buffer.getWritePointer(0);
        for (int i = 0; i < n; ++i)
            ch0[i] *= inputTrimGain(inputTrimSm.getNextValue());
    }

    // ── Input level metering (RMS, post-trim) ──
    // Post-trim on purpose: the meter has to show what the amp sees, or the
    // INPUT knob can't be set by eye.
    {
        const int n = buffer.getNumSamples();
        if (n > 0)
        {
            const float* ch0 = buffer.getReadPointer(0);
            float sumSq = 0.0f;
            for (int i = 0; i < n; ++i)
                sumSq += ch0[i] * ch0[i];
            inputLevelForGUI.store(std::sqrt(sumSq / static_cast<float>(n)), std::memory_order_relaxed);
        }
    }
```

- [ ] **Step 8: Apply the OUTPUT trim and extend the safety-clip condition**

In `processBlock`, immediately **before** the `// ── Post-effects safety limiter ──` block, insert:

```cpp
    // ── OUTPUT trim (gain staging) ──
    // Post-limiter master: it rides on top of OUTPUT_MAKEUP_GAIN and cannot add
    // amp compression, because the amp's own tanh already ran. Runs on the full
    // stereo bus (delay/reverb/widener are stereo), advancing the smoother once
    // per SAMPLE rather than once per channel — per-channel advance would
    // desynchronise L and R during a ramp. Skipped entirely at unity.
    outputTrimSm.setTargetValue(outTrimParam);
    const bool outputTrimActive =
        outputTrimSm.isSmoothing() || outputTrimGain(outputTrimSm.getCurrentValue()) != 1.0f;
    if (outputTrimActive)
    {
        const int n     = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            const float g = outputTrimGain(outputTrimSm.getNextValue());
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer(ch)[i] *= g;
        }
    }
    // Only boosting can push past full scale; at or below unity the amp's own
    // tanh already guarantees ±1, so the dry path stays untouched.
    const bool outputBoosting =
        outputTrimActive && outputTrimGain(outputTrimSm.getCurrentValue()) > 1.0f;
```

Then change the safety-limiter condition from:

```cpp
    if (delayOn || reverbOn || widenerActive)
```

to:

```cpp
    if (delayOn || reverbOn || widenerActive || outputBoosting)
```

The output metering block that follows already reads the post-trim, post-clip buffer — leave it as is, but update its comment to `// ── GUI level metering (RMS of channel 0, post-trim) ──`.

- [ ] **Step 9: Build and run the full suite**

```
build.bat SilentRiotTests SilentRiotJuceTests sr_render
build-win\tests\SilentRiotTests.exe
build-win\tests\SilentRiotJuceTests.exe
```

Expected: both report `All tests passed`, no failures.

- [ ] **Step 10: Verify the dry path is bit-identical at defaults**

This is the guarantee the whole design rests on. Render on the previous commit and on this one, then byte-compare.

```bash
# stash this task's work, build a reference render
git stash push -m io-trim-wip
```
```
build.bat sr_render
```
```bash
"build-win/tools/sr_render_artefacts/Release/sr_render.exe" --out /tmp/before \
  --signal note:110:0.5:2.0 --gain 1.00 --volume 0.70 --shape 0.50 --7w 0 --cab 1
"build-win/tools/sr_render_artefacts/Release/sr_render.exe" --out /tmp/before_idle \
  --signal silence:5.0 --gain 1.00 --volume 0.70 --shape 0.50 --7w 0 --cab 1
git stash pop
```
```
build.bat sr_render
```
```bash
"build-win/tools/sr_render_artefacts/Release/sr_render.exe" --out /tmp/after \
  --signal note:110:0.5:2.0 --gain 1.00 --volume 0.70 --shape 0.50 --7w 0 --cab 1
"build-win/tools/sr_render_artefacts/Release/sr_render.exe" --out /tmp/after_idle \
  --signal silence:5.0 --gain 1.00 --volume 0.70 --shape 0.50 --7w 0 --cab 1
cmp /tmp/before.f32 /tmp/after.f32
cmp /tmp/before_idle.f32 /tmp/after_idle.f32
```

Expected: both `cmp` calls produce **no output** (byte-identical). If either reports a difference, the trim is being applied at unity — check the `isSmoothing() || gain != 1.0f` guards before continuing.

- [ ] **Step 11: Commit**

```bash
git add src/utils/Parameters.h src/PluginProcessor.h src/PluginProcessor.cpp
git commit -m "Wire INPUT/OUTPUT level trims into the signal chain

INPUT trims channel 0 after the power/tuner-mute return and before the input
meter, so the meter shows what the amp is fed. OUTPUT trims the full stereo
bus after the widener, advancing its smoother once per sample so L and R stay
in step during a ramp, and only arms the safety clip when actually boosting.

Both multiplies are skipped while the trim sits at unity and is not ramping.
Verified bit-identical against the parent commit for both a note and silence
render.

Parameters are appended at the end of the layout so positional host
automation indexing is preserved.

Refs USE-59"
```

---

### Task 4: UTILITY card knobs

**Files:**
- Modify: `src/gui/EffectsPanel.h` (members + accessors)
- Modify: `src/gui/EffectsPanel.cpp` (`resized()` UTILITY block)
- Modify: `src/PluginEditor.h` (attachment members)
- Modify: `src/PluginEditor.cpp` (attachment construction)

**Interfaces:**
- Consumes: APVTS IDs `Params::IDs::INPUT_LEVEL` / `OUTPUT_LEVEL` (Task 3).
- Produces: `EffectsPanel::inputLevelKnob()` and `EffectsPanel::outputLevelKnob()`, both returning `NotchKnob&`.

- [ ] **Step 1: Add the knob members**

In `src/gui/EffectsPanel.h`, after the `NotchKnob width { "WIDTH", 0.0f };` line:

```cpp
    NotchKnob inputLevel { "INPUT",  0.5f };   // centre = unity, ±12 dB
    NotchKnob outputLevel{ "OUTPUT", 0.5f };   // centre = unity, silence..+12 dB
```

- [ ] **Step 2: Add the accessors**

In `src/gui/EffectsPanel.h`, in the public accessor block after `NotchKnob& widthKnob() { return width; }`:

```cpp
    NotchKnob& inputLevelKnob()  { return inputLevel; }
    NotchKnob& outputLevelKnob() { return outputLevel; }
```

- [ ] **Step 3: Add them to the UTILITY layout**

In `src/gui/EffectsPanel.cpp`, in `resized()`, change the UTILITY block's comment and knob list:

```cpp
    // UTILITY card: no toggle — every knob self-gates or is unity-at-centre
    // (THRESH 0 = gate off, WIDTH 0 = dry, INPUT/OUTPUT centre = unity).
    // Four knobs take layoutKnobRow from 80 px to ~76 px each; that fits, but
    // check the labels render if you add a fifth.
    {
        auto inner = cardBounds(area, 0, 0).reduced(10, 6);
        inner.removeFromTop(16);   // title strip ("UTILITY")
        inner.removeFromTop(4);
        layoutKnobRow(inner, { &gateThresh, &width, &inputLevel, &outputLevel });
    }
```

- [ ] **Step 4: Add the attachment members**

In `src/PluginEditor.h`, extend the existing effects attachment declaration list to include two more:

```cpp
    std::unique_ptr<SliderA> inputLevelA, outputLevelA;
```

- [ ] **Step 5: Construct the attachments**

In `src/PluginEditor.cpp`, immediately after the `widthA` line (currently line 198, the last effects attachment):

```cpp
    inputLevelA  = std::make_unique<SliderA>(proc.apvts, IDs::INPUT_LEVEL,
                                             effectsPanel.inputLevelKnob().getSlider());
    outputLevelA = std::make_unique<SliderA>(proc.apvts, IDs::OUTPUT_LEVEL,
                                             effectsPanel.outputLevelKnob().getSlider());
```

- [ ] **Step 6: Build the standalone and confirm the labels render**

```
build.bat SilentRiot_Standalone
"build-win\SilentRiot_artefacts\Release\Standalone\Silent Riot.exe"
```

Open the EFFECTS panel and look at the UTILITY card. Confirm:
- four knobs are visible and evenly spaced,
- the labels `THRESH WIDTH INPUT OUTPUT` are fully readable and **not clipped** at ~76 px,
- INPUT and OUTPUT both sit at centre by default,
- turning OUTPUT fully down silences the plugin, and turning it up gets louder.

**If the labels clip**, shorten to `"IN"` / `"OUT"` in the Step 1 declarations rather than reflowing the card. Re-run this step.

- [ ] **Step 7: Commit**

```bash
git add src/gui/EffectsPanel.h src/gui/EffectsPanel.cpp src/PluginEditor.h src/PluginEditor.cpp
git commit -m "Add INPUT/OUTPUT trim knobs to the UTILITY card

Third and fourth knobs in the card that already hosts the non-voice utility
controls. No ON toggle, consistent with the card's convention that centre or
zero is a no-op.

Refs USE-59"
```

---

### Task 5: Documentation and sign-off regression

**Files:**
- Modify: `docs/SilentRiot_UI_Spec.md` (UTILITY described as two-knob near lines 590, 600, 649)
- Modify: `CLAUDE.md` (UTILITY card description; meters are now post-trim)

**Interfaces:**
- Consumes: everything from Tasks 1–4.
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Update the UI spec**

In `docs/SilentRiot_UI_Spec.md`, three passages describe UTILITY as a two-knob card.

Card diagram (~line 589) — change this line:

```
│  THRESH   WIDTH   │ │ LEVEL TIGHT BRIGHT│
```

to:

```
│THRESH WIDTH IN OUT│ │ LEVEL TIGHT BRIGHT│
```

Prose (~line 600) — replace:

```markdown
- **UTILITY** has **no ON/OFF toggle** — both of its knobs are autonomous and
  self-gate at 0 (THRESH 0 = gate bypassed, WIDTH 0 = widener skipped). This is
  why the card is named UTILITY rather than NOISE GATE.
```

with:

```markdown
- **UTILITY** has **no ON/OFF toggle** — all of its knobs are autonomous.
  THRESH and WIDTH self-gate at 0 (gate bypassed, widener skipped); INPUT and
  OUTPUT are unity at centre. This is why the card is named UTILITY rather than
  NOISE GATE — none of it is part of the amp's voice.
```

Parameter table (~line 649) — replace the header and add two rows:

```
── Effects: UTILITY (no ON toggle — knobs self-gate at 0 or unity at centre) ─
gateThreshold   float   0–1          0.0       Gate Threshold (0 = off)
width           float   0–1          0.0       Width (0 = off)
inputLevel      float   0–1          0.5       Input Level  (centre = unity, ±12 dB)
outputLevel     float   0–1          0.5       Output Level (centre = unity, silence..+12 dB)
```

- [ ] **Step 2: Update CLAUDE.md**

In the **GUI Architecture** section, extend the UTILITY sentence in the stereo-widener bullet to mention the two trims, and add to the **Editor timer** paragraph that the level meters now read post-trim. Add to **Important Pitfalls**:

```markdown
- INPUT/OUTPUT level trims are gain staging, NOT tone controls: unity at knob centre, and `processBlock` skips the multiply entirely while a trim sits at unity so the dry path stays bit-identical at defaults. Do not "simplify" them into always-on gain stages. They read through `sanitizeUnitParamOr(v, 0.5f)`, not `sanitizeUnitParam` — a 0.0 fallback would mean −12 dB in and silence out. New APVTS parameters must be **appended** to `createParameterLayout()`; inserting mid-list shifts positional host automation lanes
```

- [ ] **Step 3: Run the full sign-off and confirm no tone regression**

```
build.bat sr_render
python tools\signoff.py --render-bin "build-win\tools\sr_render_artefacts\Release\sr_render.exe" --out-dir C:\Users\boris\AppData\Local\Temp\so_trims
```

Expected — these four must be **unchanged** from before this work:

| gate | expected |
|---|---|
| D1 sag depth | 2.77 |
| B2 edge THD | 7.86 |
| B3 crank THD | 20.5 |
| A2 peak | −6.00 |

`signoff.py` exits non-zero while any gate fails. **A1, B1, E1 and E3 fail for pre-existing reasons** (documented in `docs/handoff-power-section-calibration.md`) — read the table, do not trust the exit code. If any of the four gates above moved, the trims are not unity-transparent; go back to Task 3 Step 10.

- [ ] **Step 4: Run both test suites one final time**

```
build-win\tests\SilentRiotTests.exe
build-win\tests\SilentRiotJuceTests.exe
```

Expected: both `All tests passed`.

- [ ] **Step 5: Commit**

```bash
git add docs/SilentRiot_UI_Spec.md CLAUDE.md
git commit -m "Document the INPUT/OUTPUT level trims

UI spec described UTILITY as a two-knob card. Also records that the level
meters now read post-trim, and the two pitfalls worth not rediscovering:
the trims must stay unity-transparent, and new APVTS params are append-only.

Verified D1 2.77 / B2 7.86 / B3 20.5 / A2 -6.00 unchanged.

Refs USE-59"
```

---

## Notes for the implementer

**Two deliberate deviations from the spec**, both found while writing this plan:

1. The spec said `sanitizeUnitParam()`. It cannot be used — it falls back to `0.0f`, which for these centre-detented trims means −12 dB and silence. Task 2 adds the overload. The spec has been amended.
2. The spec said a 30 ms smoothing ramp. This plan reuses the existing `fxRampSec = 0.05` (50 ms) that every other effects smoother uses, rather than introducing a second constant. The spec has been amended.

**The one thing that must not regress:** the dry path is bit-identical at default parameter values. Task 3 Step 10 is the check, and it is not optional — the entire "tone-neutral utility" premise fails without it.
