# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Boris Miscenco
"""Palm-mute / "thump" diagnostic.

Thump is low-frequency transient punch. The OT saturation model integrates
before it saturates (flux = int V dt), so flux ~ 1/f and BASS saturates first by
design. If that stage is driven too hard, the low band gets compressed away
while the mids come through -- which is exactly "underpowered, missing thump".

Measure: drive the real processBlock with sines across the band at a range of
input levels, and compute the compression slope (dB out per dB in) per frequency.
1.0 = linear. Much less than 1.0 = that band is being squashed.
"""
import json, subprocess, sys, pathlib
import numpy as np

RENDER = sys.argv[1]
OUTDIR = pathlib.Path(sys.argv[2]); OUTDIR.mkdir(parents=True, exist_ok=True)
GAIN   = sys.argv[3] if len(sys.argv) > 3 else "0.9"

FREQS = [82.41, 110.0, 164.81, 220.0, 440.0, 880.0]
AMPS  = [0.05, 0.10, 0.20, 0.40]          # ~18 dB span

def render(f, amp):
    tag = OUTDIR / f"t_{int(f*100)}_{int(amp*1000)}"
    cmd = [RENDER, "--out", str(tag), "--signal", f"sine:{f}:{amp}:0.7",
           "--gain", GAIN, "--volume", "0.7", "--shape", "0.5", "--cab", "1"]
    subprocess.run(cmd, capture_output=True, check=True)
    sr = json.loads((tag.with_suffix(".json")).read_text())["sampleRate"]
    x  = np.fromfile(str(tag) + ".f32", dtype=np.float32)
    if x.size % 2 == 0:
        x = x.reshape(-1, 2)[:, 0]         # interleaved stereo -> ch0
    return x, float(sr)

def fundamental_db(x, sr, f0):
    x = x[len(x)//3:]                       # skip settling
    w = np.hanning(len(x))
    X = np.abs(np.fft.rfft(x * w)) / (np.sum(w) / 2)
    fr = np.fft.rfftfreq(len(x), 1/sr)
    k  = np.argmin(np.abs(fr - f0))
    band = X[max(0, k-2):k+3]
    return 20*np.log10(max(band.max(), 1e-12))

print(f"Compression slope per band  (gain={GAIN}, volume=0.7, cab on)")
print(f"1.00 = linear/uncompressed.  Lower = that band is being squashed.\n")
print(f"{'freq':>8}  " + "  ".join(f"{a:>7.2f}" for a in AMPS) + "   slope")
print("-" * 66)

slopes = {}
for f in FREQS:
    outs = []
    for a in AMPS:
        x, sr = render(f, a)
        outs.append(fundamental_db(x, sr, f))
    ins = 20*np.log10(np.array(AMPS))
    slope = np.polyfit(ins, np.array(outs), 1)[0]
    slopes[f] = slope
    print(f"{f:>8.1f}  " + "  ".join(f"{o:>7.1f}" for o in outs) + f"   {slope:>5.2f}")

lo = np.mean([slopes[f] for f in FREQS if f <= 164.81])
hi = np.mean([slopes[f] for f in FREQS if f >= 440.0])
print(f"\nlow band (82-165 Hz) mean slope : {lo:.2f}")
print(f"mid band (440-880 Hz) mean slope: {hi:.2f}")
print(f"low-vs-mid deficit              : {hi-lo:+.2f}")
if hi - lo > 0.15:
    print("\n=> LOW END IS BEING COMPRESSED HARDER THAN THE MIDS.")
    print("   That is the OT flux saturation signature (flux ~ 1/f), i.e. lost thump.")
else:
    print("\n=> low and mid compress comparably; thump loss is NOT from OT saturation.")
