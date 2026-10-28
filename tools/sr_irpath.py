#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Boris Miscenco
"""IR-path diagnostic: level, startup and repeatability of the cab convolution.

Measures the findings in docs/superpowers/plans/2026-10-26-ir-path-sound-fixes.md
against the real processBlock (sr_render), with the synthetic IRs from
tools/make_ir_fixtures.py (tests/fixtures/ir/).

Modes (pick one; the default is the level table):

  (default)            note:220:0.1:2.0 with the built-in cab and with each
                       fixture, at P-CLEAN / P-EDGE / P-CRANK and 44.1/48/96 kHz.
                       Prints ch0 RMS from 0.5 s on, and IR minus built-in (dB).
  --startup            IR mode with --warmup-ms 0, >7 kHz energy per 10 ms window
                       next to a CAB-off and a built-in-cab render (also warmup 0).
                       A burst at CAB-off level = the convolution is passing the
                       amp through un-cabbed.
  --repeat N           One unloaded IR-mode render (warmup 0) as the reference,
                       then N renders with every core busy. Reports the first
                       sample (ms) where each differs. Per --block size given.
  --save-baseline DIR  Render every signoff_gates.json render (as signoff.py does
                       it, at 44.1 kHz) plus the built-in-cab note at the three
                       points and three rates, and save the .f32s in DIR.
  --diff-baseline DIR  Re-render the same set and compare bit for bit with DIR.
                       Exit status 1 on any difference.

Usage:
  python3 tools/sr_irpath.py --render-bin build/tools/sr_render_artefacts/Release/sr_render
  python3 tools/sr_irpath.py --render-bin ... --startup [--fixture neutral] [--sr 44100]
  python3 tools/sr_irpath.py --render-bin ... --repeat 6 --block 64 512 2048
  python3 tools/sr_irpath.py --render-bin ... --save-baseline DIR
  python3 tools/sr_irpath.py --render-bin ... --diff-baseline DIR
"""
import argparse
import concurrent.futures as cf
import json
import os
import pathlib
import subprocess
import sys
import tempfile

import numpy as np
from scipy import signal

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
FIXTURES = ROOT / "tests" / "fixtures" / "ir"
GATES = HERE / "signoff_gates.json"
sys.path.insert(0, str(HERE))
from signoff import render_cmd  # noqa: E402  (same command line signoff.py uses)

FIXTURE_NAMES = ["neutral", "dark", "bright"]
POINT_NAMES = ["P-CLEAN", "P-EDGE", "P-CRANK"]
RATES = [44100, 48000, 96000]
NOTE = "note:220:0.1:2.0"
RMS_FROM_S = 0.5
HF_HZ = 7000.0
WIN_MS = 10.0


def points():
    return json.loads(GATES.read_text())["points"]


def point_args(p):
    return [a for k in ("gain", "shape", "volume", "7w", "cab") if k in p
            for a in (f"--{k}", str(p[k]))]


def render(bin_path, out_base, args):
    """Run sr_render; return (ch0, sr) from the interleaved stereo .f32."""
    subprocess.run([bin_path, "--out", str(out_base)] + args,
                   capture_output=True, check=True)
    meta = json.loads(pathlib.Path(str(out_base) + ".json").read_text())
    x = np.fromfile(str(out_base) + ".f32", dtype=np.float32)
    x = x.reshape(-1, meta["numChannels"])
    return x, float(meta["sampleRate"])


def rms_db(x):
    return 10 * np.log10(max(np.mean(np.square(x.astype(np.float64))), 1e-30))


def highpassed(x, sr):
    """Content above 7 kHz (8th-order Butterworth high-pass, causal).

    Filtering the whole render first, rather than taking an FFT per window, keeps
    the 220 Hz fundamental's leakage out of a 10 ms window's high bins."""
    sos = signal.butter(8, HF_HZ, btype="highpass", fs=sr, output="sos")
    return signal.sosfilt(sos, x.astype(np.float64))


def energy_db(x):
    return 10 * np.log10(max(np.mean(np.square(x)), 1e-30))


def pool_map(fn, items, jobs):
    with cf.ThreadPoolExecutor(max_workers=jobs) as ex:
        return list(ex.map(fn, items))


# ── default: level table ────────────────────────────────────────────────────
def mode_levels(a, outdir):
    pts = points()
    jobs = []
    for sr in RATES:
        for pn in POINT_NAMES:
            for cab in ["builtin"] + FIXTURE_NAMES:
                jobs.append((sr, pn, cab))

    def run(job):
        sr, pn, cab = job
        args = ["--signal", NOTE, "--sr", str(sr), "--block", str(a.block[0])] + point_args(pts[pn])
        if cab != "builtin":
            args += ["--ir", str(FIXTURES / f"{cab}.wav")]
        x, fs = render(a.render_bin, outdir / f"lvl_{sr}_{pn}_{cab}", args)
        return rms_db(x[int(RMS_FROM_S * fs):, 0])

    res = dict(zip(jobs, pool_map(run, jobs, a.jobs)))

    print(f"ch0 RMS from {RMS_FROM_S} s on, {NOTE}, dBFS  (IR - built-in in brackets)\n")
    hdr = f"{'point':<8} {'rate':>6} {'built-in':>9}" + "".join(f" {n:>17}" for n in FIXTURE_NAMES)
    print(hdr)
    print("-" * len(hdr))
    for pn in POINT_NAMES:
        for sr in RATES:
            b = res[(sr, pn, "builtin")]
            cells = "".join(f" {res[(sr, pn, n)]:>8.2f} ({res[(sr, pn, n)] - b:+6.2f})"
                            for n in FIXTURE_NAMES)
            print(f"{pn:<8} {sr:>6} {b:>9.2f}{cells}")
    print("\nRate dependence (level at rate minus level at 48 kHz, dB):\n")
    print(f"{'point':<8} {'cab':<9} {'44.1k':>7} {'96k':>7}")
    for pn in POINT_NAMES:
        for cab in ["builtin"] + FIXTURE_NAMES:
            r48 = res[(48000, pn, cab)]
            print(f"{pn:<8} {cab:<9} {res[(44100, pn, cab)] - r48:>+7.2f} "
                  f"{res[(96000, pn, cab)] - r48:>+7.2f}")


# ── --startup ──────────────────────────────────────────────────────────────
def mode_startup(a, outdir):
    p = points()[a.point]
    base = ["--signal", NOTE, "--sr", str(a.sr), "--block", str(a.block[0]),
            "--warmup-ms", "0"] + point_args(p)
    ir, sr = render(a.render_bin, outdir / "st_ir", base + ["--ir", str(FIXTURES / f"{a.fixture}.wav")])
    off, _ = render(a.render_bin, outdir / "st_caboff", base + ["--cab", "0"])
    bi, _ = render(a.render_bin, outdir / "st_builtin", base)
    w = int(round(WIN_MS / 1000 * sr))
    hp = {k: highpassed(y[:, 0], sr) for k, y in (("ir", ir), ("off", off), ("bi", bi))}
    print(f">{HF_HZ / 1000:.0f} kHz energy (mean square) per {WIN_MS:.0f} ms window, dB  "
          f"({a.point}, {NOTE}, {int(sr)} Hz, block {a.block[0]}, warmup 0, IR {a.fixture})\n")
    print(f"{'t ms':>6} {'IR':>8} {'CAB off':>8} {'built-in':>9} {'IR-off':>8}")
    for k in range(int(a.startup_ms / WIN_MS)):
        s = slice(k * w, (k + 1) * w)
        e_ir, e_off, e_bi = (energy_db(hp[n][s]) for n in ("ir", "off", "bi"))
        print(f"{k * WIN_MS:>6.0f} {e_ir:>8.1f} {e_off:>8.1f} {e_bi:>9.1f} {e_ir - e_off:>+8.1f}")
    s = slice(int(0.5 * sr), int(1.5 * sr))
    print(f"\nsettled (0.5-1.5 s): IR {energy_db(hp['ir'][s]):.1f} dB, "
          f"CAB off {energy_db(hp['off'][s]):.1f} dB, built-in {energy_db(hp['bi'][s]):.1f} dB")


# ── --repeat ───────────────────────────────────────────────────────────────
def first_diff(x, y):
    d = np.nonzero(x != y)[0]
    return (int(d[0]), float(np.max(np.abs(x.astype(np.float64) - y)))) if d.size else (None, 0.0)


def mode_repeat(a, outdir):
    p = points()[a.point]
    ncpu = os.cpu_count() or 2
    print(f"IR-mode repeatability: {a.point}, {NOTE}, {a.sr} Hz, warmup 0, IR {a.fixture}; "
          f"1 unloaded reference, then {a.repeat} renders with {ncpu} busy-loop processes\n")
    for block in a.block:
        args = ["--signal", NOTE, "--sr", str(a.sr), "--block", str(block),
                "--warmup-ms", "0", "--ir", str(FIXTURES / f"{a.fixture}.wav")] + point_args(p)
        ref, sr = render(a.render_bin, outdir / f"rep_{block}_ref", args)
        busy = [subprocess.Popen([sys.executable, "-c", "while True: pass"]) for _ in range(ncpu)]
        try:
            runs = [render(a.render_bin, outdir / f"rep_{block}_{i}", args)[0]
                    for i in range(a.repeat)]
        finally:
            for b in busy:
                b.kill()
            for b in busy:
                b.wait()
        diffs = []
        for i, x in enumerate(runs):
            idx, mx = first_diff(ref.reshape(-1), x.reshape(-1))
            frame = None if idx is None else idx // ref.shape[1]
            diffs.append(frame)
            where = "identical" if frame is None else \
                f"differs from {frame / sr * 1000:7.2f} ms (frame {frame}), max |diff| {mx:.3g}"
            print(f"  block {block:>5}  run {i + 1}: {where}")
        same = sum(d is None for d in diffs)
        print(f"  block {block:>5}: {same}/{len(diffs)} bit-identical to the reference\n")


# ── --save-baseline / --diff-baseline ─────────────────────────────────────
def baseline_jobs():
    """(name, args) for every built-in-cab render later tasks must keep bit-identical."""
    cfg = json.loads(GATES.read_text())
    pts = cfg["points"]
    jobs = []
    for r in cfg["renders"]:
        if r.get("ir") and not r["ir"].startswith("OPTIONAL"):
            continue  # an IR render, not a built-in-cab one
        cmd = render_cmd("x", r, pts[r["point"]], "x")[3:]  # drop bin, --out, path
        jobs.append((f"signoff_{r['id']}", cmd))
    for sr in RATES:
        for pn in POINT_NAMES:
            jobs.append((f"note220_{pn}_{sr}",
                         ["--signal", NOTE, "--sr", str(sr)] + point_args(pts[pn])))
    return jobs


def render_baseline(a, outdir):
    def run(job):
        name, args = job
        subprocess.run([a.render_bin, "--out", str(outdir / name)] + args,
                       capture_output=True, check=True)
        return name
    return pool_map(run, baseline_jobs(), a.jobs)


def mode_save_baseline(a):
    d = pathlib.Path(a.save_baseline)
    d.mkdir(parents=True, exist_ok=True)
    names = render_baseline(a, d)
    for n in names:
        (d / f"{n}.wav").unlink(missing_ok=True)  # the .f32 is the exact copy
    print(f"saved {len(names)} built-in-cab renders (.f32 + .json) in {d}")


def mode_diff_baseline(a, outdir):
    d = pathlib.Path(a.diff_baseline)
    names = render_baseline(a, outdir)
    bad = 0
    for n in names:
        ref = np.fromfile(d / f"{n}.f32", dtype=np.float32)
        new = np.fromfile(outdir / f"{n}.f32", dtype=np.float32)
        if ref.shape == new.shape and ref.tobytes() == new.tobytes():
            print(f"  {n:<28} bit-identical")
            continue
        bad += 1
        if ref.shape != new.shape:
            print(f"  {n:<28} DIFFERS: length {ref.size} vs {new.size}")
        else:
            idx, mx = first_diff(ref, new)
            meta = json.loads((d / f"{n}.json").read_text())
            frame = idx // meta["numChannels"]
            print(f"  {n:<28} DIFFERS from {frame / meta['sampleRate'] * 1000:.2f} ms, "
                  f"max |diff| {mx:.3g}")
    print(f"\n{len(names) - bad}/{len(names)} built-in-cab renders bit-identical to {d}")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--render-bin", required=True, help="path to built sr_render")
    ap.add_argument("--out-dir", default=os.path.join(tempfile.gettempdir(), "sr_irpath"))
    ap.add_argument("--block", type=int, nargs="+", default=[512],
                    help="host block size(s); --repeat runs each, other modes use the first")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1,
                    help="parallel renders for the level table and baseline modes")
    m = ap.add_mutually_exclusive_group()
    m.add_argument("--startup", action="store_true")
    m.add_argument("--repeat", type=int, metavar="N")
    m.add_argument("--save-baseline", metavar="DIR")
    m.add_argument("--diff-baseline", metavar="DIR")
    ap.add_argument("--fixture", default="neutral", choices=FIXTURE_NAMES,
                    help="IR for --startup / --repeat")
    ap.add_argument("--point", default="P-EDGE", help="sign-off point for --startup / --repeat")
    ap.add_argument("--sr", type=int, default=44100, help="rate for --startup / --repeat")
    ap.add_argument("--startup-ms", type=float, default=150.0, help="windows shown by --startup")
    a = ap.parse_args()

    outdir = pathlib.Path(a.out_dir)
    outdir.mkdir(parents=True, exist_ok=True)
    if a.startup:
        mode_startup(a, outdir)
    elif a.repeat:
        mode_repeat(a, outdir)
    elif a.save_baseline:
        mode_save_baseline(a)
    elif a.diff_baseline:
        sys.exit(mode_diff_baseline(a, outdir))
    else:
        mode_levels(a, outdir)


if __name__ == "__main__":
    main()
