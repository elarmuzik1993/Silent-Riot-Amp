#!/usr/bin/env python3
"""
signoff.py — orchestrate the tone sign-off (docs/Tone_SignOff_Protocol.md).

For each render in tools/signoff_gates.json:
  1. run `sr_render` to produce a <id>.f32 / .json / .wav at the point's params,
  2. analyze it with sr_analyze,
  3. evaluate the gates that reference it,
and print a pass/fail table. `null`/unsatisfiable targets are reported as
CALIBRATE (the Calibration Pass §5 ratifies them before they gate).

This is the runnable glue once `sr_render` is built (-DSR_BUILD_TOOLS=ON). With
--dry-run it prints the exact render commands without executing — useful in this
environment, where JUCE can't be fetched to build the binary.

Usage:
  signoff.py --render-bin build/sr_render --out-dir /tmp/signoff
  signoff.py --dry-run
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import sr_analyze as A  # noqa: E402


def render_cmd(bin_path, r, point, out_path):
    cmd = [bin_path, "--out", out_path]
    if "signal" in r:
        cmd += ["--signal", r["signal"]]
    if "in" in r:
        cmd += ["--in", r["in"]]
    if r.get("ir") and not r["ir"].startswith("OPTIONAL"):
        # Relative IR paths (the tests/fixtures/ir/ WAVs) are relative to the
        # repo root, so the table works from any working directory.
        cmd += ["--ir", os.path.join(ROOT, r["ir"])]
    for k, pk in [("gain", "gain"), ("shape", "shape"), ("volume", "volume"),
                  ("7w", "7w"), ("cab", "cab")]:
        if pk in point:
            cmd += [f"--{k}", str(point[pk])]
    return cmd


def analyze(metric, f32, f0=None):
    data, sr = A.load_render(f32)
    if metric == "level":
        return A.measure_level(data, sr)
    if metric == "thd":
        return A.measure_thd(data, sr, f0)
    if metric == "spurs":
        return A.measure_spurs(data, sr, f0)
    if metric == "sag":
        return A.measure_sag(data, sr)
    if metric == "noise":
        return A.measure_noise(data, sr)
    raise ValueError(metric)


def check(op, target, value):
    if value is None:
        return "CALIBRATE"
    try:
        if op == "<=":
            return "PASS" if value <= target else "FAIL"
        if op == ">=":
            return "PASS" if value >= target else "FAIL"
        if op == "==":
            return "PASS" if value == target else "FAIL"
        if op == "band":
            return "PASS" if target[0] <= value <= target[1] else "FAIL"
    except TypeError:
        return "CALIBRATE"
    return "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=os.path.join(HERE, "signoff_gates.json"))
    ap.add_argument("--render-bin", help="path to built sr_render")
    ap.add_argument("--out-dir", default="/tmp/sr_signoff")
    ap.add_argument("--dry-run", action="store_true",
                    help="print render commands only; do not execute or analyze")
    args = ap.parse_args()

    cfg = json.load(open(args.config))
    points = cfg["points"]
    renders = {r["id"]: r for r in cfg["renders"]}
    os.makedirs(args.out_dir, exist_ok=True)

    # 1. render
    f32_for = {}
    for rid, r in renders.items():
        point = points[r["point"]]
        out = os.path.join(args.out_dir, rid)
        cmd = render_cmd(args.render_bin or "sr_render", r, point, out)
        if args.dry_run:
            print("  " + " ".join(cmd))
            continue
        if not args.render_bin:
            ap.error("--render-bin required unless --dry-run")
        subprocess.run(cmd, check=True)
        f32_for[rid] = out + ".f32"

    if args.dry_run:
        print("\n(dry run — build sr_render with -DSR_BUILD_TOOLS=ON, then re-run "
              "with --render-bin to execute and gate)")
        return

    # 2. + 3. analyze + gate
    cache = {}
    rows, fails, must_fail = [], 0, 0
    for g in cfg["gates"]:
        r = renders[g["render"]]
        key = (g["render"], g["metric"])
        if key not in cache:
            cache[key] = analyze(g["metric"], f32_for[g["render"]], r.get("probe_f0"))
        res = cache[key]
        value = res.get(g["field"])
        status = check(g["op"], g["target"], value)
        if status == "FAIL":
            fails += 1
            if g.get("must_pass"):
                must_fail += 1
        tgt = g["target"]
        rows.append((g["id"], "M" if g.get("must_pass") else " ", g["render"],
                     g["field"], str(value), f"{g['op']} {tgt}", status, g["desc"]))

    w = [max(len(str(r[i])) for r in rows) for i in range(8)]
    hdr = ("ID", "!", "RENDER", "FIELD", "VALUE", "TARGET", "STATUS", "DESC")
    w = [max(w[i], len(hdr[i])) for i in range(8)]
    line = "  ".join(h.ljust(w[i]) for i, h in enumerate(hdr))
    print(line)
    print("-" * len(line))
    for r in rows:
        print("  ".join(str(r[i]).ljust(w[i]) for i in range(8)))

    print(f"\n{len(rows)} gates · {fails} fail · {must_fail} MUST-PASS fail")
    sys.exit(1 if must_fail else 0)


if __name__ == "__main__":
    main()
