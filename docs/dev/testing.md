# Testing

## `verify.sh`: the definition of done

```bash
bash scripts/verify.sh           # static checks + Release build + every test
bash scripts/verify.sh --quick   # static checks only: no build, no network, silent when clean
```

Nothing is done until the full run passes. It mirrors the CI `test` job
(`.github/workflows/build.yml`) and builds into `build-verify/`, so it never touches the
`build/` tree that `scripts/build.sh` uses. Logs go to `build-verify/verify-*.log`.

The static checks, run in both modes:

| # | Check |
|---|---|
| 1 | Every `.cpp` under `src/` is listed in the root `CMakeLists.txt` |
| 2 | `sr_render`'s source list in `tools/CMakeLists.txt` contains every plugin `.cpp` |
| 3 | Every shell script in `scripts/` and `.agents/` parses |
| 4 | `tools/signoff_gates.json` is valid JSON |
| 5 | Every hand-written source file has an `SPDX-License-Identifier` in its first 3 lines |
| 6 | Every repo path named in the docs and in code comments exists (`scripts/check_links.py`; deliberate exceptions in `scripts/linkcheck-allow.txt`) |

The full run then configures Release with `SR_BUILD_TESTS=ON`, builds, and runs `ctest`.

## The three test targets

Tests use Catch2 v3, one test file per DSP module, all under `tests/`.

| Target | What goes in it | Links |
|---|---|---|
| `SilentRiotTests` | Pure C++ DSP: tube models, filters, PSU and sag, LTP, effects, pitch detector, parameter smoothing | No JUCE |
| `SilentRiotJuceTests` | Code that needs JUCE: cabinet IRs, convolution, IR level match and loading, the oversample switcher and latency, delay and reverb | JUCE modules |
| `SilentRiotProcessorTests` | Anything in `processBlock`'s wiring, on the real `SilentRiotProcessor` | The plugin's own SharedCode target |

A new test file is added to its target's list in `tests/CMakeLists.txt`.

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

## Tone sign-off

Unit tests pin behaviour; they don't decide whether the amp *sounds* right. That is the
job of the tone sign-off: a protocol ([`tone-signoff.md`](tone-signoff.md)) and a render
and analysis harness ([`../../tools/README.md`](../../tools/README.md)) that runs the real
`processBlock` offline and gates level, THD, sag, noise and aliasing. Any change that
alters the sound needs a sign-off run as well as `verify.sh`. The render tool is not built
by CI or by `verify.sh`; build it with `-DSR_BUILD_TOOLS=ON`.
