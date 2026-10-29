# Testing

## `verify.sh`: the definition of done

```bash
bash scripts/verify.sh           # static checks + Release build + every test
bash scripts/verify.sh --quick   # static checks only: no build, no network, silent when clean
```

Nothing is done until the full run passes. It mirrors the CI `test` job
(`.github/workflows/build.yml`), which runs `--quick` and then the build and tests. It
builds into `build-verify/`, so it never touches the
`build/` tree that `scripts/build.sh` uses. Logs go to `build-verify/verify-*.log`.

The static checks, run in both modes:

| # | Check |
|---|---|
| 1 | Every `.cpp` under `src/` is listed in the root `CMakeLists.txt` |
| 2 | `sr_render`'s and `sr_editor_snapshot`'s source lists in `tools/CMakeLists.txt` contain every plugin `.cpp` |
| 3 | Every shell script in `scripts/` and `.agents/` parses |
| 4 | `tools/signoff_gates.json` is valid JSON |
| 5 | Every hand-written source file has an `SPDX-License-Identifier` in its first 3 lines |
| 6 | Every repo path named in the docs and in code comments exists (`scripts/check_links.py`; deliberate exceptions in `scripts/linkcheck-allow.txt`) |
| 7 | Every non-ASCII character in a `src/` string literal can render (`scripts/check_text.py`): spelled as `\x` escapes inside `juce::CharPointer_UTF8` (a raw literal turns to mojibake) and present in every embedded font in `resources/fonts/` (a missing glyph draws as an empty box) |

The full run then configures Release with `SR_BUILD_TESTS=ON`, builds, and runs `ctest`.

## The test targets

Tests use Catch2 v3, one test file per DSP module, all under `tests/`.

| Target | What goes in it | Links |
|---|---|---|
| `SilentRiotTests` | Pure C++ DSP: tube models, filters, PSU and sag, LTP, effects, pitch detector, parameter smoothing | No JUCE |
| `SilentRiotJuceTests` | Code that needs JUCE: cabinet IRs, convolution, IR level match and loading, the oversample switcher and latency, delay and reverb | JUCE modules |
| `SilentRiotProcessorTests` | Anything in `processBlock`'s wiring, on the real `SilentRiotProcessor` | The plugin's own SharedCode target |

A new test file is added to its target's list in `tests/CMakeLists.txt`. A fourth
executable, `SilentRiotRTStress`, is built from the same directory but is not a ctest —
see [Real-time stress](#real-time-stress-silentriotrtstress) below.

Run one target, or one test case by name:

```bash
ctest --test-dir build --output-on-failure -R "LTP"
./build/tests/SilentRiotTests "LTP: reset restores idle cathode + clears plate cache"
```

## Testing the processor: `ProcessorHarness`

A unit test of a DSP class can't see how `processBlock` wires the classes together: edge
detection, flags, the order of stages. For those, `tests/processor_harness.h` builds and
prepares the real processor and drives it the way a host does:

```cpp
#include "processor_harness.h"
using namespace sr::test;

TEST_CASE("Processor: power off silences the output", "[processor]")
{
    ProcessorHarness h;                                   // 44.1 kHz, 512-sample blocks, stereo
    h.set(sr::Params::IDs::POWER, 0.0f);                  // parameters in natural units
    REQUIRE(h.run(0.5, sine(220.0f, 0.25f)) < dB(-300.0)); // run() returns the output peak
}
```

`tests/processor_effects_tests.cpp` has fuller examples.

It also offers `Mode::Bypassed` (runs `processBlockBypassed`), `loadIR()` (loads a user IR
and waits for the convolution to pick it up) and `writeWav()` for IR fixtures. Each case
runs the 4× oversampled amp, so it takes seconds: keep the rendered durations short.

## Real-time stress: `SilentRiotRTStress`

Unit tests prove the DSP is right; they do not prove the plugin is a good real-time
citizen. `tests/rt_stress.cpp` drives the real processor from a dedicated audio thread,
with a live JUCE message thread beside it, and holds it to three contracts measured
rather than argued about (`tests/rt_probe.h`):

- **no allocation on the audio thread** — global `operator new`/`delete` are replaced, and
  every allocation made inside `processBlock` is counted with a backtrace;
- **no blocking lock** — `pthread_mutex_lock` is interposed, and locks taken inside
  `processBlock` are counted (contended ones separately);
- **no missed deadline** — every block is timed against `numSamples / sampleRate`.

Every scenario also checks what the host receives: finite samples inside ±1.

It is built by `verify.sh` but is **not** a ctest — it runs for minutes and is meant to be
run on demand, and before a release:

```bash
cmake -B build-verify -G Ninja -DCMAKE_BUILD_TYPE=Release -DSR_BUILD_TESTS=ON
cmake --build build-verify --target SilentRiotRTStress
./build-verify/tests/SilentRiotRTStress --help
./build-verify/tests/SilentRiotRTStress --quick                 # ~1 minute
./build-verify/tests/SilentRiotRTStress --md=rt-report.md       # full run, Markdown report
./build-verify/tests/SilentRiotRTStress --only=modes,ir         # one or more scenarios
```

| Scenario | What it stresses |
|---|---|
| `load` | Every sample rate × oversample factor × block size × effects on/off: can it hold its deadline at all |
| `blocks` | Host block size changing on every call, down to 1 sample |
| `params` | Every parameter re-randomised each block, with NaN/Inf injected past the parameter objects |
| `hostile` | NaN, Inf, 1e30, denormals, full-scale square and Nyquist input, then whether the amp recovers |
| `modes` | Power, tuner mute, CAB, 7W, oversample, host bypass and `reset()` churn |
| `ir` | IR loading and IR_INDEX automation, including indices past the list |
| `threads` | State save/restore, presets and IR loads on the message thread during playback |
| `recover` | Whether one non-finite input sample permanently silences the amp |
| `overhead` | Fixed cost per block before any DSP runs |
| `denormals` | Block time while a tail decays into denormal range |
| `stability` | Every knob maxed, long run, then silence |

Exit code is 1 when a CRITICAL finding is recorded, so it can gate a release. The findings
of the first run are in
[`../records/reviews/review-rt-stress-2026-10-29.md`](../records/reviews/review-rt-stress-2026-10-29.md).

Under a sanitizer the interposers compile themselves out, so the same binary can be built
with ThreadSanitizer and the sanitizer stays the instrument:

```bash
cmake -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSR_BUILD_TESTS=ON \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -O1 -g -DNDEBUG" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build-tsan --target SilentRiotRTStress
./build-tsan/tests/SilentRiotRTStress --only=threads,ir --quick
```

## Tone sign-off

Unit tests pin behaviour; they don't decide whether the amp *sounds* right. That is the
job of the tone sign-off: a protocol ([`tone-signoff.md`](tone-signoff.md)) and a render
and analysis harness ([`../../tools/README.md`](../../tools/README.md)) that runs the real
`processBlock` offline and gates level, THD, sag, noise and aliasing. Any change that
alters the sound needs a sign-off run as well as `verify.sh`. The render tool is not built
by CI or by `verify.sh`; build it with `-DSR_BUILD_TOOLS=ON`.

## Tone lock

The sound is frozen at `tone-v1`. `tests/tone_lock_tests.cpp` (in `SilentRiotProcessorTests`)
renders 24 settings through the real `processBlock` and compares each with a reference
fingerprint in `tests/tone/golden/`; `tests/tone_lock_core_tests.cpp` tests the fingerprint
itself. A failure there means your change moves the sound. See [`tone-lock.md`](tone-lock.md)
for what is locked and the only way to change it.
