# Tone Sign-Off Harness (`tools/`)

The render + analyze + gate pipeline that implements
[`docs/dev/tone-signoff.md`](../docs/dev/tone-signoff.md). Same harness
serves Phase A's two roadmap items: **the tone sign-off** and **the golden-render
drift lock**.

```
DI / test signal ──► sr_render ──► <id>.f32 + .json + .wav ──► sr_analyze ──► gates ──► PASS/FAIL
                     (real processBlock)                       (numpy/scipy)   (signoff.py)
```

## Pieces

| File | What it is | Needs JUCE? | Verified here |
|---|---|---|---|
| `sr_render.cpp` | Headless CLI that runs the **real** `SilentRiotProcessor::processBlock` over a DI/test signal and writes a render | **yes** (build with `-DSR_BUILD_TOOLS=ON`) | ✅ builds and runs (MSVC/Ninja, Windows) |
| `sr_analyze.py` | Pure numpy/scipy analyzer: level/LUFS, THD+harmonics, spectrum, sag, noise, aliasing spurs | no | ✅ `--selftest` passes |
| `sr_thump.py` | Low-frequency punch: compression slope per band vs input level | no | ✅ found the OT saturation imbalance |
| `sr_irpath.py` | Cab-IR path: IR vs built-in level per point and rate, IR start-up, render repeatability, built-in-cab bit-identity baseline (`--save-baseline` / `--diff-baseline`) | no | ✅ `docs/records/handoffs/handoff-ir-path-sound.md` |
| `make_ir_fixtures.py` | Writes the synthetic test IRs in `tests/fixtures/ir/` (neutral / dark / bright) | no | ✅ deterministic |
| `sr_idle.py` | Idle self-noise: separates settling transient from self-oscillation | no | ✅ found the 17.7 Hz motorboating |
| `sr_wobble.py` | Envelope modulation on a constant tone (free-running loops) | no | ✅ |
| `sr_bloom.py` | Transient recovery — rebound after the decay ("delayed pluck") | no | ✅ |
| `signoff.py` | Orchestrates renders → analysis → §4 gate table + exit code | no | ✅ end-to-end against a stub render |
| `sr_editor_snapshot.cpp` | Paints the real editor offscreen to a PNG (the manual's screenshots in `resources/images/`). `sr_editor_snapshot out.png [fx] [ir] [adv] [off] [scale=N] [theme=N] [set=<paramID>:<0..1>]...`, run under `xvfb-run` (`set=` sets any parameter, e.g. `set=boostOn:1`; `effects-panel.png` is the `fx set=boostOn:1 set=reverbOn:1` render cropped to 0,270 700×300); links the full plugin source list (checked by `verify.sh`) | **yes** (`-DSR_BUILD_TOOLS=ON`) | ✅ Linux/Xvfb |
| `signoff_gates.json` | The §4 gates + test matrix as data (targets `TBM` until the Calibration Pass) | no | ✅ parses |

> **Build note.** `sr_render` was unbuilt for a long time and had accumulated two
> pieces of rot, both now fixed: it needs `JucePlugin_Name` /
> `JucePlugin_VersionString` defined explicitly (`juce_add_console_app` does not
> synthesise them the way `juce_add_plugin` does), and its source list had drifted
> from the plugin's — it was missing the whole effects module, the tuner and
> `UserIRState`. **The source list is still duplicated by hand**: if you add a
> plugin `.cpp` to the root `CMakeLists.txt`, add it here too or the link fails.

## `sr_thump.py` — low-end punch

Thump is low-frequency transient punch. The OT saturation model integrates before
it saturates (`flux = ∫V dt`), so `flux ∝ 1/f` and **bass saturates first by
design**. Drive that stage too hard and the low band is compressed away while the
mids still come through — audibly "underpowered, missing the thump".

This drives the real `processBlock` with sines across the band at a range of input
levels and reports the compression slope (dB out per dB in) per frequency.
`1.00` = linear; near `0.00` = that band no longer responds to how hard you play.

```bash
python tools/sr_thump.py <path-to-sr_render> <out-dir> [gain 0..1]
```

It caught `OT_SAT_DRIVE` being tuned for the pre-recalibration current scale:
the low band read a slope of **−0.01** against the mids' **0.17**, i.e. the low
end had *zero* dynamic response. Retuning `OT_SAT_DRIVE` 400 → 190 closed the
low-vs-mid deficit from **+0.18 to +0.04**.

## `sr_idle.py` — is it oscillating, or just settling?

With a **silent** input the amp must go quiet and stay quiet. Anything left over
is self-generated. This separates the two cases, which need different fixes:

- **settling transient** — decays away; only audible as a thump on plugin load
- **sustained** — still there after several seconds = a feedback loop oscillating

Reports the settled level, the dominant frequency and the share of energy below
120 Hz, so low-frequency rumble is distinguishable from broadband hiss.

```bash
python tools/sr_idle.py <path-to-sr_render> <out-dir> [gain ...]
```

It found a **17.7 Hz motorboating limit cycle** at gain 10 with no input
(−20.3 dBFS settled, 94.9% of energy below 120 Hz) caused by
`GRID_CONDUCT_THRESH_EL84_N` being 0 V on a tube biased at −9.6 V. After the fix
the same measurement reads **−279.8 dBFS**.

Note this is the case a signal-present test *cannot* see — a tone masks the
oscillation. If a user reports noise "even when not playing", start here.

## `sr_wobble.py` / `sr_bloom.py` — is a loop free-running?

`sr_wobble.py` drives a constant tone and looks at the *envelope's* own spectrum
between 0.3 and 25 Hz: a steady input must give a steady envelope, so a discrete
peak means a loop is oscillating. `sr_bloom.py` plucks a note and flags any
rebound after the decay — what a "delayed pluck" artifact looks like in the DSP.

Both document which loops exist (sag, PSU, speaker coupling, grid conduction) and
their time constants, which is what makes a positive result actionable.

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
Or a real DI via `--in file.wav`, rendered at the file's own sample rate (or
`--in di.raw --in-raw --in-sr 48000` for the repo's `tests/di_sample_82hz.raw` —
a raw file has no header, so its rate is required, from `--in-sr` or `--sr`;
without either sr_render exits with an error rather than guess 44100 and play
it back transposed). `--in-sr` only applies with `--in-raw`. The input is never
resampled: an explicit `--sr` that disagrees with the input's rate is an error.

Every option takes a value except the `--in-raw` flag. An unknown (e.g.
misspelled) option, a missing value, a stray argument, `--in` together with
`--signal`, a sample rate outside 8000..768000 Hz (from an argument or the input
file's header), a `--block` outside 1..65536 or an empty input is an error,
never a quiet render.

Knobs `--gain/--shape/--volume` are internal `0..1`. Toggles `--7w/--cab/--power`
are `0|1`. `--os 0|1|2` = 2×/4×/8×. `--ir file.wav` enables the convolution path.

## How this becomes the golden lock

After the Calibration Pass (§5) ratifies the gates and the tone is frozen, commit
the `.f32` renders of the canonical matrix under `tests/tone/golden/`. A CI job
re-renders and byte/tolerance-compares against them — any later code change that
moves the rendered output fails. One harness, two jobs: `[tone-signoff]` (gates)
and `[tone-golden]` (drift).
