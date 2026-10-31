# Real-time stress review — 2026-10-29

A measurement of how the shipped `SilentRiotProcessor` behaves as a **real-time
citizen**, as opposed to whether its DSP is right. The harness
(`tests/rt_stress.cpp`, see [`../../dev/testing.md`](../../dev/testing.md)) drives
the real processor from a dedicated audio thread with a live JUCE message thread
beside it, and holds it to three contracts it is never otherwise checked against:

1. **no allocation on the audio thread** — global `operator new`/`delete` replaced,
   every allocation inside `processBlock` counted with a backtrace;
2. **no blocking lock** — `pthread_mutex_lock` interposed, locks taken inside
   `processBlock` counted (contended ones separately);
3. **no missed deadline** — every block timed against `numSamples / sampleRate`.

Plus, on every run: the host must only ever receive finite samples inside ±1.

Frozen record. Re-run it with the commands at the end.

**Fixed in PR #57:** RT-1…RT-5, and RT-6 is documented in the manual — see
[`../../plans/2026-10-30-rt-safety-fixes.md`](../../plans/2026-10-30-rt-safety-fixes.md).

| | |
|---|---|
| Base commit | `e56ab9f` (tone frozen at `tone-v1`) |
| Machine | Intel Xeon @ 2.10 GHz, 4 vCPU, Linux 6.18 — a **cloud vCPU**, roughly 2–3× slower per core than a current desktop |
| Build | GCC 13, Release (`-O3`, LTO), JUCE 7.0.12 |
| Coverage | 12 scenarios, 102 runs, 138 s of rendered audio; plus a ThreadSanitizer run and an AddressSanitizer + UndefinedBehaviorSanitizer run |

## Findings

| # | Severity | What |
|---|---|---|
| [RT-1](#rt-1) | **CRITICAL** | One non-finite input sample silences the amp for the rest of the session; no in-session recovery |
| [RT-2](#rt-2) | **HIGH** | `setLatencySamples()` — and so a synchronous call into the host — runs on the audio thread on every oversample switch |
| [RT-3](#rt-3) | **HIGH** | `triggerAsyncUpdate()` on the audio thread when `IR_INDEX` changes: mutex, `write()` syscall, possible heap growth; observed contended |
| [RT-4](#rt-4) | LOW | Data race on the tuner ring buffer (formally UB, benign on x86/ARM) |
| [RT-5](#rt-5) | MEDIUM | A host block larger than `prepareToPlay`'s maximum segfaults |
| [RT-6](#rt-6) | INFO | CPU headroom: 8× above 96 kHz does not fit in one core of this machine |

Severities are engineering judgement. The harness itself only reports *contract
violated / not violated*, and it exits non-zero on any of them.

---

<a name="rt-1"></a>
## RT-1 — CRITICAL: one non-finite input sample silences the amp permanently

`processBlock` scrubs its **output** — `if (! std::isfinite(finalOut)) finalOut = 0.0f;`
in the amp loop, the same per-sample sweep on the IR path, and the `softClip`
sweep after the effects. Nothing scrubs its **input**. Between the two sits
every stateful object in the amp, and each of them latches a NaN forever:
the `FilterBank` biquads, the Miller low-passes, the cached triode plate
voltages, the LTP cathode node, the grid charge, the PSU and the sag envelope.
The host never sees a non-finite sample — it just stops hearing the amp.

```
[recover] does the amp survive a non-finite input sample?
  1 NaN sample     rms before 0.29261 -> after 0.002637473 -> reset() 0.000000000
                   -> PWR off/on 0.000000000 -> prepareToPlay() 0.29289   [DSP readout went non-finite]
  64 NaN samples   (identical)
  1 +Inf sample    (identical)
  1 sample of 1e30 rms before 0.29261 -> after 0.165091046 -> reset() 0.293210034  (recovers)
```

- **One** sample is enough. 64 are no worse: the state is already poisoned.
- `+Inf` behaves exactly like NaN. A large finite value (`1e30`) is survived.
- There is **no in-session recovery**. `reset()` deliberately leaves the amp on
  its operating point (review B1 — zeroing it there replays the startup burst),
  and a PWR off/on cycle only sets `tailsStale`, which clears the *effect* tails.
  Only a fresh `prepareToPlay()` — a host sample-rate or buffer-size change, or
  reloading the plugin — brings it back.
- The ADVANCED panel's PSU rails read non-finite too, so the readout shows the
  damage even though the meters show silence.

**How it happens in the field:** an upstream plugin emitting NaN after its own
denormal or overflow bug (common enough that most amp sims guard against it), a
failing interface, a corrupt file, or a host handing over an uninitialised first
block.

**Suggested fix.** Scrub the input at the very top of `processBlock`, before the
tuner capture and the bypass-delay write, so those are protected too:

```cpp
for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
{
    auto* s = buffer.getWritePointer(ch);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (! std::isfinite(s[i])) s[i] = 0.0f;
}
```

One compare and branch per input sample **at base rate** — against an amp that
costs ~2 µs per oversampled sample, this is noise. It is tone-neutral by
construction: no finite sample is touched, so the tone lock must still pass
(verify, don't assume). It belongs with a processor test that feeds a single
NaN and requires the amp to still play afterwards.

A second, larger option — detecting a poisoned state and re-seeding the amp
through `resetDSPState()` — is not recommended: it re-opens the B1 startup-burst
problem for a case the guard above prevents outright.

---

<a name="rt-2"></a>
## RT-2 — HIGH: the host is called from the audio thread on an oversample switch

`processBlock` commits a pending oversample change and calls
`recalcRateDependentCoeffs()`, which ends in `setLatencySamples()`. JUCE turns
that into `AudioProcessor::updateHostDisplay()`, which takes `listenerLock` and
calls every listener's `audioProcessorChanged` **synchronously, on the audio
thread**.

```
modes [oversample switch every block]: 250 mutex locks on the audio thread
  [0] pthread_mutex_lock
        SilentRiotProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)
```

This was invisible until the harness attached a listener, because with an empty
listener list `updateHostDisplay()` iterates nothing and takes no lock. **Every
host attaches one**, so the harness now does too (`HostListener` in
`tests/rt_stress.cpp`).

**Why it matters.** A latency change makes a host recompile its graph and redo
plugin delay compensation; several hosts take their own graph lock for it, and
some stop and restart the plugin. Doing that from inside the audio callback is a
stall at best and a deadlock at worst. It is reachable from the plugin's own UI:
switching 2× ↔ 4×/8× moves the reported latency from 10 to 12 samples. (4× ↔ 8×
does not change it, so no call is made.)

**Suggested fix.** Keep the computation where it is, move the *report* off the
audio thread: store the new latency in an atomic and hand it to the message
thread (the existing `AsyncUpdater`, or a dedicated one) which calls
`setLatencySamples()`. The value stays exact, and the switch already crossfades,
so a one-message-loop delay in the report is inaudible. Note that this cannot be
done with `triggerAsyncUpdate()` from the audio thread — that is RT-3.

---

<a name="rt-3"></a>
## RT-3 — HIGH: `triggerAsyncUpdate()` on the audio thread when `IR_INDEX` changes

`PluginProcessor.cpp:637`, in the IR-change branch of `processBlock`. The call is
documented as deferring the work to the message thread, and it does — but the
*deferral itself* is not real-time safe. On Linux,
`AsyncUpdater::triggerAsyncUpdate()` → `MessageManager::MessageBase::post()` →
`InternalMessageQueue::postMessage()` takes a `CriticalSection`, appends to a
`ReferenceCountedArray` (which allocates when it grows) and performs a `write()`
syscall on a socketpair. macOS and Windows have equivalents.

```
ir [IR_INDEX automated every block]: 2970 mutex locks on the audio thread (1 contended)
  [0] pthread_mutex_lock
        juce::AsyncUpdater::triggerAsyncUpdate()
        SilentRiotProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)

threads [msg thread + automation thread]: 4212 mutex locks on the audio thread (2 contended)
```

The two **contended** locks in the concurrency run are the point: the audio
thread actually blocked, waiting on the message thread draining the same queue.
That is the priority inversion this contract exists to prevent. No allocation
was observed — the queue array had already grown and JUCE reuses one message
object — but the lock and the syscall happen every time.

**Reachability.** The GUI paths are already careful: both `selectUserIR()` and
`selectBuiltInCab()` set `cachedIRIndex` themselves, so `processBlock` sees an
index it already knows and does not post. (`selectUserIR()` sets the cache
*before* the parameter and so never posts; `selectBuiltInCab()` sets it just
after, leaving a one-block window in which a post can still happen.) What is
left is **host automation of the "IR Selection" parameter** — it is an
automatable `AudioParameterInt` — and hosts that restore a session by writing
parameter values directly.

**Options.**

1. Mark `IR_INDEX` non-automatable (`AudioParameterIntAttributes().withAutomatable(false)`).
   It is a device selector, not a musical control, and this removes the only
   remaining trigger. The parameter stays in the layout, so saved sessions and
   the positional ordering are unaffected — but it is a visible change to the
   plugin's public interface and wants the owner's sign-off.
2. Keep it automatable and let the message thread *poll* `pendingIRIndex` from a
   timer instead of being poked from the audio thread.
3. Accept and document it: one lock per IR change, bounded by how fast
   automation can move.

---

<a name="rt-4"></a>
## RT-4 — LOW: data race on the tuner ring buffer

The only race ThreadSanitizer reports across the `threads`, `ir`, `modes`,
`params` and `recover` scenarios:

```
SUMMARY: ThreadSanitizer: data race src/PluginProcessor.cpp:1142
         in SilentRiotProcessor::pushTunerSamples(float const*, int)
  Write by the audio thread   pushTunerSamples  (PluginProcessor.cpp:1142)
  Read  by the editor thread  readLatestTunerSamples (PluginProcessor.cpp:1160)
```

The write *position* is a correctly ordered `std::atomic<uint64_t>`
(release/acquire). The samples are a plain `std::array<float, 8192>`, so
overlapping access is formally a data race — undefined behaviour — even though
the design knowingly tolerates a stale or mixed window ("torn reads are
harmless — the next 30 Hz frame recovers a clean window", and that is true).

On x86-64 and AArch64 an aligned 32-bit load or store does not tear, so the real
risk is the compiler, not the CPU.

**Suggested fix.** Make the ring `std::array<std::atomic<float>, kTunerRingSize>`
and use `std::memory_order_relaxed` on both sides. On x86 and ARM that compiles
to the same plain loads and stores — no audio-thread cost — and it removes both
the UB and the TSan report. The alternative is a TSan suppression with a comment
pointing here.

---

<a name="rt-5"></a>
## RT-5 — MEDIUM: an over-sized host block is a segfault

```
$ SilentRiotRTStress --only=overblock --overblock     # prepared 128, fed 2048
Segmentation fault

#0 juce::dsp::Oversampling2TimesPolyphaseIIR<float>::processSamplesUp(...)
#1 juce::dsp::Oversampling<float>::processSamplesUp(...)
#2 SilentRiotProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)
```

Under AddressSanitizer, with the exact fault:

```
ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size 4 at 0x51d000010cc0 thread T2
    #0 juce::dsp::Oversampling2TimesPolyphaseIIR<float>::processSamplesUp(...)
         juce_dsp/processors/juce_Oversampling.cpp:346
    #1 juce::dsp::Oversampling<float>::processSamplesUp(...)
         juce_dsp/processors/juce_Oversampling.cpp:707
    #2 SilentRiotProcessor::processBlock(...)   src/PluginProcessor.cpp:767

0x51d000010cc0 is located 0 bytes after 2112-byte region
  allocated by juce::HeapBlock<char, true>::mallocWrapper
```

It writes straight off the end of the buffer the oversampler sized in
`prepareToPlay` — a write past the heap block, not a read, so it can corrupt
whatever the allocator put next.

The oversamplers size their buffers from `prepareToPlay`'s `samplesPerBlock`, and
a larger block walks off the end of them. The host is at fault — the VST3 and AU
contracts both say the block may not exceed the declared maximum — but the
failure mode is a crash of the whole host, and hosts do break this, most often on
an offline bounce or a freeze.

This scenario is **opt-in** (`--overblock`) precisely because it ends the run.

**Suggested fix.** Process in sub-blocks of at most the prepared size at the top
of `processBlock`: a short loop over `juce::AudioBuffer` views, no state change,
no tone change. It also removes the matching hazard in `CabinetSim` and the
limiter oversampler.

---

<a name="rt-6"></a>
## RT-6 — INFO: CPU headroom

Mean per-block cost as a percentage of that block's own deadline, 512-sample
blocks, cranked settings. **On this machine** — one 2.1 GHz cloud vCPU, roughly
2–3× slower per core than a current desktop — so read the ratios, not the
absolutes.

| Base rate | 2× | 4× | 8× |
|---|---:|---:|---:|
| 44.1 kHz | 21.3% | 43.7% | 70.1% |
| 48 kHz | 23.1% | 39.5% | 74.0% |
| 88.2 kHz | 41.1% | 70.2% | **123.0%** |
| 96 kHz | 45.7% | 83.7% | **139.9%** |
| 176.4 kHz | 80.4% | **140.0%** | **242.4%** |
| 192 kHz | 96.8% | **157.2%** | **283.7%** |

Three full runs agreed to roughly ±10% per cell — this is a shared vCPU, so
treat the ratios between cells as the result and the absolutes as an order of
magnitude.

Cost tracks the internal rate (base × factor) almost exactly: the amp costs
roughly **2 µs per oversampled sample** on this core, which is what a
Koren-model preamp, a root-finding phase inverter and two pentodes come to.
Enabling every effect adds 3–10%.

Nothing here is a defect — the defaults (44.1/48 kHz, 4×) sit at a third of one
core on slow hardware. It is a sizing fact worth having: **8× is not usable
above 48 kHz on a machine like this**, and the combinations in bold cannot make
their deadline at all. If 8× at high rates is meant to be offered, the UI should
say what it costs.

Two related measurements:

- **Fixed cost per block**, with POWER off so the block returns straight after
  reading its parameters: **2.60 µs** at a 1-sample block, 2.95 µs at 512. That
  is ~28 `getRawParameterValue()` calls, each a lookup in APVTS's
  `std::map<StringRef, …>` — a tree walk with string compares, paid once per
  block whatever the block size. At a 64-sample buffer and 48 kHz it is 0.2% of
  the deadline, so **caching the `std::atomic<float>*` pointers is not worth
  doing today** — but it is worth knowing before many more parameters are added.
- **Small buffers**: a 1-sample block costs 10.05 µs, 48% of its 20.8 µs
  deadline, of which 2.6 µs is the fixed cost above. Nothing breaks, it is just
  where the per-block overhead starts to matter.

---

## What passed

Worth recording, because these are the things that usually fail.

- **Zero allocations on the audio thread**, in every scenario: block-size churn
  down to 1 sample, a full parameter storm, mode churn, IR automation, and the
  concurrency run. The project's "no dynamic allocation in `processBlock`" rule
  holds in the shipped code, and is now measured rather than asserted.
- **The host never received a bad sample.** Across 102 runs and 138 s of audio:
  0 non-finite samples, 0 samples past ±1. With the effects on the peak is
  exactly 1.000 — the post-effects soft clip holds the bus.
- **Hostile input is contained** at the output: full-scale noise, a 1.6 kHz
  square, +20 dBFS sine, denormals, 1e30, NaN, ±Inf, alternating ±1, impulse
  trains. All finite, all in range. (What they do to the *state* is RT-1.)
- **Host NaN on the parameters is survived.** Injected past the parameter
  objects straight into the raw atomics — GAIN, VOLUME, SHAPE, feedback, size,
  both trims, width, gate — the plugin keeps playing and recovers when sane
  values return. Review S1's boundary sanitisation does its job; it is the audio
  input that has no equivalent.
- **No denormal stall.** A tail decaying into denormal range costs **0.68×** a
  loud block, not 10×: `ScopedNoDenormals` and `ANTI_DENORMAL` work.
- **No runaway.** Every knob maxed — gain, volume, boost, delay feedback 1.0 at
  20 ms, reverb size 1.0, width 1.0, both trims maxed — for 20 s of noise then
  20 s of silence: peak 1.000, nothing non-finite, no growth. The delay's
  `loopNorm` (review B8) holds.
- **Block size is not assumed.** 1-sample blocks, random 1..64, random 1..2048:
  correct throughout.
- **Mode churn is clean.** Power, tuner mute, CAB, 7W, oversample, host bypass
  and `reset()` driven as fast as the block rate allows — no allocation, no
  non-finite sample, no overshoot.
- **AddressSanitizer and UndefinedBehaviorSanitizer are clean.** A second build
  (`-fsanitize=address,undefined`) ran every scenario — the load matrix, block
  churn to 1 sample, the parameter storm with injected NaN, hostile input, mode
  churn, IR automation, the concurrency run, the mono bus, denormals and the
  stability soak: **0 AddressSanitizer errors and 0 UndefinedBehaviorSanitizer
  runtime errors**, over 45 s of audio. No out-of-bounds access, no
  use-after-free, no signed overflow, no bad shift, no invalid cast anywhere in
  normal operation. (The one overflow this project does have needs the host to
  break its contract — RT-5.)
- **A mono bus is as healthy as a stereo one.** 1 in / 1 out, with effects, with
  an IR and under `IR_INDEX` churn, and under full-scale Nyquist input: output
  bounded, no allocation, recovery clean. Nothing in the suite had covered the
  single-channel paths before.
- **ThreadSanitizer found one race** (RT-4) and nothing else — not in the IR
  hand-off, the cab-mode switcher, state save/restore during playback, the
  meters or the readout snapshots. The `threads` scenario ran ~1200
  message-thread operations (state save, state restore, preset recall, IR load,
  IR select) against a flat-out audio thread and an automation thread.

## Re-running

```bash
cmake -B build-verify -G Ninja -DCMAKE_BUILD_TYPE=Release -DSR_BUILD_TESTS=ON
cmake --build build-verify --target SilentRiotRTStress

./build-verify/tests/SilentRiotRTStress                      # everything, ~2 min
./build-verify/tests/SilentRiotRTStress --quick              # small matrix, ~1 min
./build-verify/tests/SilentRiotRTStress --only=recover       # RT-1
./build-verify/tests/SilentRiotRTStress --only=modes         # RT-2
./build-verify/tests/SilentRiotRTStress --only=ir,threads    # RT-3
./build-verify/tests/SilentRiotRTStress --only=overblock --overblock   # RT-5 (crashes)
./build-verify/tests/SilentRiotRTStress --md=rt-report.md    # machine-readable table
```

AddressSanitizer + UndefinedBehaviorSanitizer:

```bash
cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSR_BUILD_TESTS=ON \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -DNDEBUG" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan --target SilentRiotRTStress
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
  ./build-asan/tests/SilentRiotRTStress --quick
```

A sanitizer build is a CMake `Debug` config, so JUCE's assertions are on
(`juce_add_plugin` defines `DEBUG=1` there, which beats `-DNDEBUG`). The
`params` scenario therefore prints a run of
`JUCE Assertion failure in juce_NormalisableRange.h:253` — that is
`clampTo0To1` seeing the NaN the harness injected straight into the raw
parameter atomic, read back by APVTS's own value-tree flush. It is expected,
it is what a host-supplied NaN would do too, and it is compiled out of
Release; the audio path's own sanitisation (review S1) is what keeps it
harmless.

ThreadSanitizer (RT-4) — the harness compiles its own interposers out under a
sanitizer, so the sanitizer stays the instrument:

```bash
cmake -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSR_BUILD_TESTS=ON \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -O1 -g -DNDEBUG" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build-tsan --target SilentRiotRTStress
./build-tsan/tests/SilentRiotRTStress --only=threads,ir,modes,params,recover --quick
```

Exit code is 1 when a contract was violated, so the harness can gate a release.
