# Real-Time Safety Fixes Plan

> Steps use checkbox (`- [ ]`) syntax for tracking. Nothing is done until
> `bash scripts/verify.sh` passes (see **Verification** for what "passes" means while the
> Linux tone-lock references are unusable). No task here is allowed to change the sound.

**Goal:** Close the six findings of the real-time stress review
([`../records/reviews/review-rt-stress-2026-10-29.md`](../records/reviews/review-rt-stress-2026-10-29.md)),
so the plugin meets the three contracts the harness measures — no allocation, no blocking
lock, no missed deadline on the audio thread — and survives a host or a neighbour plugin
behaving badly.

**Baseline:** `main` at `e56ab9f` (tone frozen at `tone-v1`), plus the harness from PR #57.
Every number below was measured by `SilentRiotRTStress`, ThreadSanitizer or AddressSanitizer
on that baseline, not estimated.

**Findings → tasks:**

| # | Finding | Severity | Task |
|---|---|---|---|
| RT-1 | One non-finite input sample silences the amp for the session; no in-session recovery | CRITICAL | 1 (+ optional 6) |
| RT-5 | A host block larger than `prepareToPlay`'s maximum is a heap write past the end | MEDIUM | 2 |
| RT-2 | `setLatencySamples()` → a synchronous host callback, on the audio thread | HIGH | 3 |
| RT-3 | `triggerAsyncUpdate()` on the audio thread on `IR_INDEX` change: mutex + `write()` syscall | HIGH | 3 |
| RT-4 | Data race on the tuner ring buffer | LOW | 4 |
| RT-6 | 8× does not fit in one core above 48 kHz | INFO | 5 (docs only) |

**Status:** the three owner decisions below are settled (2026-10-30), so Task 1 can start.

**Order:** 1 first — it is the only finding a player can hit by accident, and it is the
smallest change. Then 2 (a crash, independent of everything). Then 3, the only task that
moves code between threads. Then 4 and 5, both trivial. **One task per commit**, each with
its own bit-identical check. Tasks 1, 2, 4 and 5 are independent of each other; 3 depends on
nothing but is the one worth reviewing closely.

## Owner decisions (decided 2026-10-30)

All three went with the recommendation. The rejected options are kept with their reasoning,
so a later reader knows they were weighed rather than missed.

- **D1 — how the latency report leaves the audio thread (Task 3): defer it**, publishing from
  a message-thread timer. The audio signal stays bit-identical, so the tone lock cannot move;
  the host learns about a latency change up to one timer period (50 ms) after it happens,
  during which its delay compensation is 2 samples out. Nothing is audible in that window —
  the oversample switch is already crossfading through silence.
  *Rejected for now:* making the reported latency **constant across oversample factors**
  (always 12 samples, padding 2× by 2), so `setLatencySamples()` is only ever called from
  `prepareToPlay` and the host never renegotiates at all. It is the better architecture and it
  matches the existing decision that latency is constant across cab modes — but it shifts the
  2× output by 2 samples, which the tone lock's `oversample-2x` case may well see. That makes
  it a tone change, and the tone is frozen. **Revisit at `tone-v2`**, alongside E3.

- **D2 — `IR_INDEX` automation (Task 3): keep it automatable.** The review offered "make
  `IR_INDEX` non-automatable" as one way out of RT-3, but the timer in Task 3 removes the
  audio-thread work *and* keeps the capability, so the trade buys nothing.
  *Rejected:* `withAutomatable(false)`. Only worth revisiting if there is a separate reason to
  hide cab switching from host automation.

- **D3 — how far to go on RT-1: Task 1 only.** Scrubbing the input removes every path by which
  a non-finite sample can get *in*, which is the only route ever observed.
  *Deferred, not rejected:* Task 6, re-seeding the amp if the output goes non-finite anyway.
  It would additionally cover a NaN generated *inside* the engine — which has never happened
  in 102 runs, 138 s of audio, every hostile input the harness has, and a clean
  AddressSanitizer/UndefinedBehaviorSanitizer pass — and it costs a real risk: a false trigger
  would re-seed the amp mid-performance. Build it only if a user reports silence that Task 1
  cannot explain.

## Global constraints

- **The sound does not change.** Every task must be bit-identical on the normal path. Prove
  it per task with the `sr_render` diff under **Verification**, not by eye and not by the
  tone lock alone (its Linux references are currently unusable — see Next 7 in
  `.agents/STATE.md`).
- No allocation, no lock and no syscall in `processBlock`. Each task must leave
  `SilentRiotRTStress` reporting fewer violations than before and none that are new.
- Anything that must reach the message thread gets there by the message thread *polling*, not
  by the audio thread *posting* — posting is RT-3.
- New parameters, if any, are **appended** to `createParameterLayout()`. (No task here adds one.)
- Keep the B1 rule: never zero the amp's filters to "fix" a state problem. `resetDSPState()`
  seeds the idle operating point instead, which is why Task 6 is even possible.

---

## Task 1 — Scrub non-finite input (RT-1)

**Why.** `processBlock` scrubs its *output* in three places, so the host never receives a bad
sample — and that is exactly what hides the bug. Nothing scrubs the *input*, and everything
stateful between the two latches a NaN forever: the `FilterBank` biquads, both Miller
low-passes, the cached triode plate voltages, the LTP cathode node, the grid charge, the PSU
and the sag envelope, and the delay and reverb lines. Measured: one NaN or one `+Inf` sample
takes the output from rms 0.293 to **0.000000000**, and `reset()`, a PWR off/on cycle and
every knob on the panel leave it there. Only `prepareToPlay()` recovers it, which in most
hosts means reloading the plugin.

**Steps**

- [x] At the very top of `processBlock`, before the tuner capture and the bypass-delay write,
      replace every non-finite input sample with 0, across all channels:
      ```cpp
      for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
      {
          auto* s = buffer.getWritePointer(ch);
          for (int i = 0; i < buffer.getNumSamples(); ++i)
              if (! std::isfinite(s[i])) s[i] = 0.0f;
      }
      ```
      Before the tuner capture on purpose: the ring and the pitch detector should not see
      NaN either, and `bypassDelay` must not be primed with one.
- [x] Do the same in `processBlockBypassed`. It currently hands the host whatever it was
      given, delayed — so a bypassed plugin passes NaN straight through, and poisons the
      tuner ring and the bypass line on the way.
- [x] Note in `CLAUDE.md` (Important Pitfalls) that the input is a system boundary like the
      parameters are (review S1), and why the output scrub is not enough on its own.

**Cost.** One `isfinite` and a predictable branch per input sample, at **base** rate — against
an amp that costs ~2 µs per *oversampled* sample. Unmeasurable: re-run `--only=overhead` and
expect the 2.60 µs/block figure to be unchanged within noise.

**Tone.** Neutral by construction: a finite sample is not touched. Bit-identical on the
normal path.

**Test** (new `tests/processor_robustness_tests.cpp`, in `SilentRiotProcessorTests`)

- [x] One NaN sample in an otherwise normal block: the amp still plays 2 s later, at the same
      rms as before the event (within a few per cent).
- [x] Same for `+Inf`, `-Inf`, and a whole block of NaN.
- [x] `processBlockBypassed` with NaN input: output is finite.
- [x] The DSP readout (`getDSPReadout()`) stays finite throughout — it is what the ADVANCED
      panel shows, and today it goes NaN with the engine.
- [x] A normal signal is **bit-identical** with and without the scrub — pin it by rendering
      the same input through the harness and comparing samples exactly.
      *Done as:* one binary cannot hold both versions, so the test pins the two halves —
      a NaN input sample renders bit-identically to a 0 sample, and host bypass (a pure
      delay behind the same scrub) passes finite edge values (−0, denormals, ±`FLT_MAX`)
      bit-exact. The with/without comparison is the `sr_render` diff: four renders
      (cab on, cab off, IR, 2× crank) `cmp`-identical to the pre-scrub build.

**Verification**

- [x] `SilentRiotRTStress --only=recover` reports no findings (it currently reports four).
- [x] `SilentRiotRTStress --only=hostile` loses its "state poisoned" finding.

---

## Task 2 — Survive a block larger than the host promised (RT-5)

**Why.** The oversamplers, the convolution, the delay and the reverb all size their buffers
from `prepareToPlay`'s `samplesPerBlock`. A bigger block walks off the end of them.
AddressSanitizer pins it exactly: a 4-byte **write** 0 bytes past a 2112-byte heap block, in
`Oversampling2TimesPolyphaseIIR::processSamplesUp`, called from `PluginProcessor.cpp:767`. A
write past the end, so it can corrupt whatever the allocator put next — in a host, that is
another plugin's state.

The host is at fault: VST3 and AU both say the block may not exceed the declared maximum. But
hosts do break it, most often on an offline bounce or a freeze, and the failure mode is the
whole host going down.

**Steps**

- [ ] Store the prepared maximum in `prepareToPlay` (`preparedBlockSize`), rather than
      trusting `getBlockSize()`, which a host can change with `setPlayConfigDetails` without
      preparing again.
- [ ] At the top of `processBlock`, if `buffer.getNumSamples() > preparedBlockSize`, split the
      buffer into views of at most that many samples and run the existing body on each:
      ```cpp
      juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers(),
                                    buffer.getNumChannels(), offset, n);
      ```
      A view, so there is no copy and no allocation.
- [ ] Same guard in `processBlockBypassed`. (`bypassDelay` is a ring sized by its maximum
      delay, not by the block, so it is already safe — but keep the two paths symmetrical.)
- [ ] `jassert` on the over-sized case so a debug build still points at the misbehaving host.

**Tone.** No effect at or below the prepared size: the loop body does not run, so the path is
bit-identical. Above it, the only behavioural differences are that per-block work (metering,
the cab-mode commit, the oversample fade) happens per sub-block — finer, not different.

**Test**

- [ ] Prepared at 128, fed 2048: output finite, bounded, and equal to the same input rendered
      in 128-sample blocks (the split must be seamless, not just safe).
- [ ] A block of exactly `preparedBlockSize` still takes the single-pass path.

**Verification**

- [ ] `SilentRiotRTStress --only=overblock --overblock` completes instead of crashing.
- [ ] The same under AddressSanitizer: 0 errors.

---

## Task 3 — Nothing on the audio thread talks to the host or the message queue (RT-2, RT-3)

**Why.** Two separate contract breaks, one shared cause: the audio thread reaching out.

*RT-2.* The oversample commit calls `recalcRateDependentCoeffs()`, which ends in
`setLatencySamples()`. JUCE turns that into `updateHostDisplay()`, which takes `listenerLock`
and calls every listener's `audioProcessorChanged` **synchronously, on the audio thread**.
Hosts react to a latency change by recompiling their graph and redoing delay compensation;
several take their own locks for it and some stop and restart the plugin. Measured: 250 locks
over 1500 blocks of oversample switching — and zero when no listener is attached, which is
why it had never shown up. Every host attaches one.

*RT-3.* `processBlock` calls `triggerAsyncUpdate()` when `IR_INDEX` changes. The work is
correctly deferred to the message thread; the *deferral* is not safe. On Linux it takes a
`CriticalSection`, appends to a `ReferenceCountedArray` (which allocates when it grows) and
does a `write()` syscall on a socketpair. Measured: ~3000 locks over 1500 blocks of
`IR_INDEX` automation, and in the concurrency run **2 of 4136 were contended** — the audio
thread actually blocked, waiting on the message thread draining the same queue.

**Design.** One small `juce::Timer` on the processor, at 20 Hz, doing both jobs from the
message thread. The processor already requires a MessageManager (APVTS owns a timer of its
own), so this adds no new dependency, and the audio thread stops reaching out entirely.

**Steps**

- [ ] `SilentRiotProcessor` also inherits `private juce::Timer`. `startTimerHz(20)` in the
      constructor, `stopTimer()` first thing in the destructor.
- [ ] Give `recalcRateDependentCoeffs()` a `LatencyPublish` argument: `Now` (from
      `prepareToPlay`, on the message thread) calls `setLatencySamples()` as today; `Deferred`
      (from the oversample commit, on the audio thread) stores the value in
      `std::atomic<int> pendingLatency` and calls nothing.
- [ ] Keep the audio thread's own copy of the latency it computed, and make
      `syncBypassDelay()` use **that** rather than `getLatencySamples()` — otherwise the
      bypass line lags the real latency by up to one timer period.
- [ ] In `timerCallback()`: publish `pendingLatency` when it differs from
      `getLatencySamples()`; then compare `IR_INDEX` with `cachedIRIndex` and, when they
      differ, call the existing `applyIRIndexParam(IRLoad::Async)`.
- [ ] Delete the IR branch from `processBlock` — the `triggerAsyncUpdate()` call, the
      `pendingIRIndex` handshake and the ordering comment that goes with it. `cachedIRIndex`
      becomes message-thread-only state, which is simpler than what it replaces. Keep
      `handleAsyncUpdate()` only if something else still needs it; otherwise drop the
      `AsyncUpdater` base too (and its `cancelPendingUpdate()` in the destructor).
- [ ] Re-check the two GUI paths still behave: `selectUserIR()` sets `cachedIRIndex` before
      the parameter, `selectBuiltInCab()` just after. With the timer, the one-block window in
      `selectBuiltInCab()` becomes a one-tick window that resolves to the same index — but
      tighten it anyway by setting the cache first, like `selectUserIR()` does.

**Latency of the new path.** An automated `IR_INDEX` change is picked up within 50 ms instead
of one block. Nothing hears it: `CabModeSwitcher` already refuses to enter IR mode until
`cabSim.hasRealIR()`, and JUCE's convolution takes longer than that to build an engine.

**Tone.** Bit-identical. No DSP changes; only where two calls are made from.

**Test**

- [ ] With a running message loop, automating `IR_INDEX` still swaps the cab (and an index
      past the list still disables the convolution, review S2). The existing processor tests
      drive IR selection through `loadUserIR`/`selectUserIR` directly, so this path needs a
      test that actually pumps the loop — `tests/rt_stress.cpp` shows the pattern.
- [ ] An oversample switch still reports the right latency to the host, within one tick.
- [ ] Host bypass is still exactly `getLatencySamples()` behind, across an oversample switch.

**Verification**

- [ ] `SilentRiotRTStress --only=modes,ir,threads,mono` reports **no** lock findings.
- [ ] ThreadSanitizer over `threads,ir,modes` stays at its one known race (RT-4) until Task 4
      removes it.

---

## Task 4 — Make the tuner ring a race-free ring (RT-4)

**Why.** `tunerRing` is a plain `std::array<float, 8192>` written by the audio thread and read
by the editor thread. The write *position* is a correctly ordered `std::atomic<uint64_t>`; the
samples are not, so overlapping access is a data race and formally undefined behaviour. It is
the only race ThreadSanitizer finds in the plugin. The design knowingly tolerates a mixed
window ("the next 30 Hz frame recovers a clean one", which is true) — the risk is the
compiler, not the CPU.

**Steps**

- [ ] `std::array<std::atomic<float>, kTunerRingSize> tunerRing {}`.
- [ ] `std::memory_order_relaxed` on both the store in `pushTunerSamples` and the load in
      `readLatestTunerSamples`. Leave the release/acquire pair on `tunerWritePos` exactly as
      it is — that is what orders the samples against the count.
- [ ] Keep the comment about tolerating a stale window; it is still the design, it is just no
      longer undefined.

**Cost.** None on x86-64 or AArch64: a relaxed atomic load/store of an aligned 32-bit value
compiles to the same plain instruction.

**Test**

- [ ] The existing tuner tests still pass unchanged.

**Verification**

- [ ] ThreadSanitizer over `threads,ir,modes,params,recover` reports **0 races**.

---

## Task 5 — Say what oversampling costs (RT-6)

**Why.** Not a defect — a sizing fact. Cost tracks the internal rate (base × factor) almost
exactly, at roughly 2 µs per oversampled sample on a 2.1 GHz core. On that machine 8× cannot
make its deadline above 48 kHz, and 4× cannot above 96 kHz. A player who turns oversampling up
at 96 kHz and hears dropouts has no way to know why.

**Steps**

- [ ] Add a short paragraph to [`../manual/controls.md`](../manual/controls.md) under
      oversampling: cost is proportional to sample rate × factor, 8× costs about four times
      what 2× does, and at high session rates the top settings may not keep up on a modest
      machine. Give the measured table as a guide and name the machine.
- [ ] No UI change. Greying out settings by a guessed CPU budget would be wrong on hardware
      we have not measured.

**Verification**

- [ ] `bash scripts/verify.sh --quick` (the link and text checks cover the new doc text).

---

## Task 6 — Optional: recover from a non-finite state the input scrub cannot explain

**Deferred by D3.** Build it only if a user reports the amp going silent after Task 1 is in.
Written up now so the reasoning is not lost.

**Idea.** `processBlock` already scans its output for non-finite samples. Count the blocks in
which it finds any; after a few consecutive ones, call `resetDSPState()` on the audio thread.
That is allocation-free (bisection plus bounded iteration, see `dsp/IdleOperatingPoint.h`) and
— crucially — it seeds the **idle operating point** rather than zeroing, which is exactly the
B1 fix, so it does not replay the startup burst.

**Why it is not Task 1.** It costs a real risk for a hypothetical gain: a false trigger would
reseed the amp mid-performance, and the reseed is a far more expensive block than any other.
Nothing in 102 runs, 138 s of audio, every hostile input the harness has, and a clean
AddressSanitizer/UndefinedBehaviorSanitizer pass has ever produced a NaN from inside the
engine. Task 1 removes the only route that has.

---

## Verification

Per task, in this order:

1. **Bit-identical check.** Build `sr_render` (`-DSR_BUILD_TOOLS=ON`) before and after the
   task and diff the renders byte-for-byte:
   ```bash
   sr_render --out /tmp/before --signal sine:220:0.18:1.0 --gain 0.5 --volume 0.5 --shape 0.5 --cab 1
   # apply the task, rebuild
   sr_render --out /tmp/after  --signal sine:220:0.18:1.0 --gain 0.5 --volume 0.5 --shape 0.5 --cab 1
   cmp /tmp/before.f32 /tmp/after.f32
   ```
   Repeat with `--cab 0` and with an IR. Any difference is a bug in the task — every task here
   claims bit-identity on the normal path. **This is the gate, not the tone lock:** the
   Linux/GCC tone-lock references cannot currently pass (see Next 7 in `.agents/STATE.md`),
   so the lock can confirm "no new failures" but cannot confirm "no change".
2. **`bash scripts/verify.sh`.** Expect 440/445 with *exactly* the five known tone-lock
   failures and the same worst-band deltas as the baseline. A sixth failure, or a moved delta,
   means the task changed the sound.
3. **The harness.** `SilentRiotRTStress` with the scenarios named under the task, plus a full
   run before the last task is merged.
4. **Sanitizers**, for Tasks 2, 3 and 4: the AddressSanitizer and ThreadSanitizer builds in
   [`../dev/testing.md`](../dev/testing.md).

## Not in this plan

- **The tone lock on Linux/GCC** (`.agents/STATE.md` Next 7). It blocks a clean `verify.sh`
  on this platform and so it shapes how these tasks are verified, but it is not an RT finding
  and its fix is an owner call about tolerances or per-toolchain references.
- **Making latency constant across oversample factors.** The better architecture, but it moves
  the 2× output by 2 samples — a `tone-v2` conversation, not a safety fix. Weighed and deferred
  at D1; revisit alongside E3.
- **The CPU cost itself.** Making the amp cheaper (a lighter solver, a cheaper Koren
  evaluation) is a tone-changing optimisation and belongs with the re-calibration that is on
  hold, not here.
