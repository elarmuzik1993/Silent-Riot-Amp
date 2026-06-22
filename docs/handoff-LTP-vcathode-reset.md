# Handoff — LTP `reset()` does not restore `vCathode` (audit #5)

**Component:** `LTPModel::reset()` (`src/dsp/power/LTPSolver.h:18`)
**Status:** confirmed, measured. Not yet fixed.
**Severity:** **Cosmetic / correctness only — NOT an audible issue** (measured).
**Audience:** DSP maintainer on the "DSP audit / sound polish" branch.

---

## 1. Summary

`resetDSPState()` exists to put the engine into "a defined idle state" on every
`prepareToPlay` (its own comment, `PluginProcessor.cpp:107`). Every stateful
block restores to its idle operating point on `reset()` **except the LTP phase
inverter**, whose `reset()` clears only the cached plate voltages and leaves
`vCathode` carrying whatever value the previous playback session ended on:

```cpp
// src/dsp/power/LTPSolver.h
void init()  { … vCathode = PI_CATHODE_INIT_N; vPlateA = vPlateB = -1.0f; }
void reset() { vPlateA = vPlateB = -1.0f; }   // ← vCathode untouched
```

So after a sample-rate / buffer-size change (or any host re-`prepare`), the
solver starts from a stale tail-cathode voltage instead of the idle point that
`init()` establishes. This contradicts the documented intent of
`resetDSPState()` and is inconsistent with `psu.reset()`, `sag.reset()`,
`speakerCoupling.reset()`, etc., which all restore idle.

**Bottom line up front:** I measured it. It is **sonically negligible** — the
re-prepare transient is dominated by the *intentional* plate-voltage re-seed,
the `vCathode` part is mostly common-mode (rejected by the downstream
push-pull), and everything settles in **~0.04 ms during silence**. This is a
code-hygiene fix, not a tone fix.

---

## 2. Lifecycle / why it's reachable

- Constructor → `ltpModel.init()` (`PluginProcessor.cpp:23`) sets
  `vCathode = PI_CATHODE_INIT_N` (1.5 V). First run is fine.
- `prepareToPlay` → `resetDSPState()` → `ltpModel.reset()`
  (`PluginProcessor.cpp:117`) on **every subsequent** prepare. `reset()` does
  **not** touch `vCathode`, so it keeps its last live value across SR/buffer
  changes.
- `reset()` is called from nowhere else (grep-confirmed), so this is strictly a
  re-prepare-time concern — which in practice means *during silence*.

---

## 3. Measurement (the interesting part)

Harness drives the **real** `solveLTP`, then emulates a re-prepare with zero
input (the realistic case — SR changes happen with transport stopped) starting
from several `vCathode` values. `vSupply = V_PI_NOMINAL_N` (280 V), idle
`vCathode` ≈ 0.75 V, `PI_CATHODE_INIT_N` = 1.5 V.

| Start `vCathode` | peak \|diff\| | peak \|sumDev\| | settle |
|------------------|--------------|-----------------|--------|
| 23.5 V (loud-note peak, realistic worst) | 0.0118 | 0.188 | 7 samp (0.036 ms) |
| 50 V (non-physical extreme) | 0.0115 | 0.188 | 7 samp (0.036 ms) |
| 1.5 V (`PI_CATHODE_INIT_N` — the fix) | 0.0125 | 0.037 | 5 samp (0.026 ms) |
| 0.75 V (exact idle — ideal ref) | 0.0203 | 0.103 | 7 samp (0.036 ms) |

`diff` = `piA−piB` (the differential signal the chain actually uses);
`sumDev` = deviation of `piA+piB` (common mode) from idle.

What the data says:

1. **The differential transient is tiny and barely depends on `vCathode`.**
   peak \|diff\| is ~0.012 whether `vCathode` starts at 1.5 V, 23.5 V, or 50 V —
   it's actually *smaller* than the "ideal" idle reset (0.020). The differential
   transient is driven by the **plate re-seed** (`vPlateA/B = -1` → first guess
   `0.6·vSupply` ≈ 0.56 vs. true idle ≈ 0.83), which is intentional and present
   in all cases.

2. **`vCathode` staleness only moves the common mode** (`sumDev`: 0.19 stale vs
   0.10 idle). But common mode is **rejected by the push-pull** downstream —
   `powerOut = iA·condA − iB·condB`, so a bump that lifts both grids together
   largely cancels in the EL84 difference. It never reaches the speaker as a
   differential signal.

3. **Settling is ~7 samples ≈ 0.036 ms at 192 kHz**, independent of the starting
   value, and it happens **while the host is silent** (re-prepare). Even a
   non-physical 50 V start settles in the same 7 samples.

4. **The "fix" doesn't even reliably reduce the transient** — restoring to
   `PI_CATHODE_INIT_N` happens to give the smallest common-mode bump here, but
   the plate re-seed dominates either way.

This mirrors audit findings #1 and #2: a real-on-paper state bug whose audible
consequence, once measured, is essentially nil.

---

## 4. Recommendation

**Fix it, but as a one-line correctness/consistency cleanup — not as a sound
fix, and not as a blocker for the neural capture.**

Rationale to fix at all:
- `resetDSPState()` promises "a defined idle state"; `vCathode` is the only
  state escaping that promise. Leaving it is a latent inconsistency that could
  bite a future change (e.g. if `vCathode` is ever given a real RC time
  constant, or if `reset()` starts being called mid-stream).
- It makes `LTPModel` match every sibling `reset()` in the engine.
- Zero tone risk: the differential output is unaffected (§3).

### Proposed change
`src/dsp/power/LTPSolver.h`:
```cpp
// Clear cached plate voltages AND restore the tail to its idle operating
// point, so a re-prepare starts from a defined idle state (matches init()).
void reset() { vPlateA = vPlateB = -1.0f; vCathode = PI_CATHODE_INIT_N; }
```
(One line. `PI_CATHODE_INIT_N` is already in scope via `Constants.h`.)

### Test to add
`tests/ltp_tests.cpp` has `init`-time coverage (`:196`) but none for `reset()`.
Add:
```cpp
TEST_CASE("LTP: reset restores idle cathode + clears plate cache", "[ltp]") {
    auto ltp = freshLTP();
    for (int i = 0; i < 200; ++i) solveLTP(0.1f, V_PI_NOMINAL_N, ltp); // perturb
    ltp.reset();
    REQUIRE(ltp.vCathode == PI_CATHODE_INIT_N);
    REQUIRE(ltp.vPlateA  == -1.0f);
    REQUIRE(ltp.vPlateB  == -1.0f);
}
```
No existing test asserts the current (stale) behavior, so this is safe to add.

---

## 5. Verification plan

1. Build with `-DSR_BUILD_TESTS=ON`; run `SilentRiotTests` — all existing LTP
   tests must still pass (the change only touches the re-prepare seed).
2. Add the test in §4; confirm it passes after the change and fails before it.
3. Optional A/B: render a fixed DI through the plugin, force a sample-rate change
   mid-session, and confirm no audible click at the transition (expected: none,
   before or after — the differential transient is ~−34 dBFS-equivalent for
   ~0.04 ms during silence).

---

## 6. One-line takeaway

`LTPModel::reset()` leaves `vCathode` stale, breaking the "defined idle state"
contract — but measurement shows the resulting transient is ~0.04 ms, mostly
common-mode (push-pull-rejected), and during silence. Fix is a **one-line**
correctness/consistency cleanup with no tone impact; **low priority**, and it
does **not** block the neural capture.
