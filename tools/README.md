# Tone Sign-Off Harness (`tools/`)

The render + analyze + gate pipeline that implements
[`docs/Tone_SignOff_Protocol.md`](../docs/Tone_SignOff_Protocol.md). Same harness
serves Phase A's two roadmap items: **the tone sign-off** and **the golden-render
drift lock**.

```
DI / test signal ──► sr_render ──► <id>.f32 + .json + .wav ──► sr_analyze ──► gates ──► PASS/FAIL
                     (real processBlock)                       (numpy/scipy)   (signoff.py)
```

## Pieces

| File | What it is | Needs JUCE? | Verified here |
|---|---|---|---|
| `sr_render.cpp` | Headless CLI that runs the **real** `SilentRiotProcessor::processBlock` over a DI/test signal and writes a render | **yes** (build with `-DSR_BUILD_TOOLS=ON`) | ❌ not compiled — JUCE fetch is blocked in the dev sandbox; build in CI / locally |
| `sr_analyze.py` | Pure numpy/scipy analyzer: level/LUFS, THD+harmonics, spectrum, sag, noise, aliasing spurs | no | ✅ `--selftest` passes |
| `signoff.py` | Orchestrates renders → analysis → §4 gate table + exit code | no | ✅ end-to-end against a stub render |
| `signoff_gates.json` | The §4 gates + test matrix as data (targets `TBM` until the Calibration Pass) | no | ✅ parses |

> **Honesty note.** Everything Python is self-tested and runs today. `sr_render.cpp`
> is written to the JUCE 7 API used elsewhere in the repo but **was not compiled**
> here (the sandbox proxy blocks the JUCE clone). First build it in a JUCE-enabled
> environment and fix any compile nits before relying on it. It is OFF by default,
> so it cannot affect the plugin build or CI until someone opts in.

## Build the render tool

```bash
cmake -B build -G Ninja -DSR_BUILD_TOOLS=ON
cmake --build build --target sr_render
# binary: build/sr_render_artefacts/.../sr_render  (path varies by generator)
```

## Run

```bash
# install analyzer deps once
pip install numpy scipy

# 1. preview the render matrix without building anything
python3 tools/signoff.py --dry-run

# 2. one-off render + analyze
build/.../sr_render --out /tmp/edge --signal sine:220:0.18:1.0 \
    --gain 0.5 --volume 0.5 --shape 0.5 --cab 1
python3 tools/sr_analyze.py thd /tmp/edge.f32 --f0 220

# 3. full sign-off (renders the matrix, applies the gates, exits non-zero on a
#    MUST-PASS failure)
python3 tools/signoff.py --render-bin build/.../sr_render --out-dir /tmp/signoff
```

## `sr_render` signals & params

`--signal` one of: `sine:F:AMP:SECS`, `note:F:AMP:SECS` (5 ms attack, for sag),
`sweep:F0:F1:SECS` (log chirp), `pink:AMP:SECS`, `silence:SECS`, `step:AMP:SECS`.
Or a real DI via `--in file.wav` (or `--in di.raw --in-raw` for the repo's
`tests/di_sample_82hz.raw`).

Knobs `--gain/--shape/--volume` are internal `0..1`. Toggles `--7w/--cab/--power`
are `0|1`. `--os 0|1|2` = 2×/4×/8×. `--ir file.wav` enables the convolution path.

## How this becomes the golden lock

After the Calibration Pass (§5) ratifies the gates and the tone is frozen, commit
the `.f32` renders of the canonical matrix under `tests/tone/golden/`. A CI job
re-renders and byte/tolerance-compares against them — any later code change that
moves the rendered output fails. One harness, two jobs: `[tone-signoff]` (gates)
and `[tone-golden]` (drift).
