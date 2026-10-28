# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Boris Miscenco
"""Transient recovery ("bloom" / delayed-pluck) diagnostic.

After a note attack the amp's envelope should decay monotonically. If it dips and
then RISES again, something recovered on its own time constant and pushed the
note back up — audibly a delayed second pluck, or a wobble riding on the decay.

Candidate mechanisms, all ~20-100 ms:
  * grid-conduction coupling-cap charge (GRID_TAU_PREAMP/PI/EL84_S = 22/47/100 ms)
  * sag envelope release (SAG_RELEASE_MS = 100 ms)
  * PSU node recovery (TAU_POWER/SCREEN/PI/PREAMP_MS = 10/60/35/80 ms)

Method: render a plucked note, take the envelope, and look for any rise after the
initial decay. Reports the largest rebound and when it happens.

    python tools/sr_bloom.py <sr_render> <out-dir> [gain ...]
"""
import json, subprocess, sys, pathlib
import numpy as np

RENDER = sys.argv[1]
OUTDIR = pathlib.Path(sys.argv[2]); OUTDIR.mkdir(parents=True, exist_ok=True)
GAINS  = [float(g) for g in (sys.argv[3:] or ["0.3", "0.6", "0.9", "1.0"])]

def render(gain):
    tag = OUTDIR / f"b_{int(gain*100)}"
    subprocess.run([RENDER, "--out", str(tag), "--signal", "note:110:0.6:2.5",
                    "--gain", f"{gain}", "--volume", "0.7", "--shape", "0.5", "--cab", "1"],
                   capture_output=True, check=True)
    sr = json.loads(tag.with_suffix(".json").read_text())["sampleRate"]
    x = np.fromfile(str(tag) + ".f32", dtype=np.float32)
    if x.size % 2 == 0:
        x = x.reshape(-1, 2)[:, 0]
    return x, float(sr)

def env_db(x, sr, hop_ms=5.0):
    hop = max(1, int(sr * hop_ms / 1000.0))
    n = (len(x) // hop) * hop
    e = np.abs(x[:n]).reshape(-1, hop).max(axis=1)
    return 20 * np.log10(np.maximum(e, 1e-9)), hop / sr

print("Transient recovery after a plucked note (110 Hz, volume 0.7)")
print("Envelope should decay monotonically. A rebound = something blooming back.\n")
print(f"{'gain':>6}  {'peak dBFS':>10}  {'max rebound':>12}  {'at':>8}   verdict")
print("-" * 62)

for g in GAINS:
    x, sr = render(g)
    e, dt = env_db(x, sr)
    pk = np.argmax(e)
    tail = e[pk:]
    # running minimum after the peak; any excess above it is a rebound
    runmin = np.minimum.accumulate(tail)
    rebound = tail - runmin
    # ignore the first 15 ms (attack ringing) and the noise floor
    start = int(0.015 / dt)
    valid = np.arange(len(rebound)) >= start
    valid &= tail > (e[pk] - 40.0)
    if not valid.any():
        print(f"{g*10:>6.1f}  {e[pk]:>9.1f}   (no valid tail)")
        continue
    k = int(np.argmax(np.where(valid, rebound, -np.inf)))
    amt = rebound[k]
    verdict = "BLOOMS BACK" if amt > 1.5 else ("slight" if amt > 0.5 else "clean decay")
    print(f"{g*10:>6.1f}  {e[pk]:>9.1f}  {amt:>10.2f} dB  {k*dt*1000:>6.0f} ms   {verdict}")
