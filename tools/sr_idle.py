"""Idle self-noise / low-frequency instability diagnostic.

With a SILENT input the amp must go quiet and stay quiet. Anything left over is
self-generated: a feedback loop oscillating, or a slow drift that never settles.

Separates the two cases, which need different fixes:
  * settling transient -- decays away; only audible as a thump when the plugin loads
  * sustained          -- still there after several seconds = self-oscillation

Reports the steady-state level and where the energy sits, so low-frequency
rumble is distinguishable from broadband hiss.

    python tools/sr_idle.py <sr_render> <out-dir> [gain ...]
"""
import json, subprocess, sys, pathlib
import numpy as np

RENDER = sys.argv[1]
OUTDIR = pathlib.Path(sys.argv[2]); OUTDIR.mkdir(parents=True, exist_ok=True)
GAINS  = [float(g) for g in (sys.argv[3:] or ["0.5", "0.7", "0.8", "0.9", "1.0"])]
SECS   = 8.0

def render(gain):
    tag = OUTDIR / f"idle_{int(gain*100)}"
    subprocess.run([RENDER, "--out", str(tag), "--signal", f"silence:{SECS}",
                    "--gain", f"{gain}", "--volume", "0.7", "--shape", "0.5", "--cab", "1"],
                   capture_output=True, check=True)
    sr = json.loads(tag.with_suffix(".json").read_text())["sampleRate"]
    x = np.fromfile(str(tag) + ".f32", dtype=np.float32)
    if x.size % 2 == 0:
        x = x.reshape(-1, 2)[:, 0]
    return x, float(sr)

def db(v):
    return 20 * np.log10(max(float(v), 1e-30))

print(f"SILENT input, {SECS:.0f} s, volume 0.7, cab on")
print("'settled' is the last 3 s. Sustained energy there = self-oscillation.\n")
print(f"{'gain':>6}  {'first 0.5s':>11}  {'settled rms':>12}  {'settled pk':>11}  "
      f"{'dominant':>10}  {'<120Hz share':>13}   verdict")
print("-" * 92)

for g in GAINS:
    x, sr = render(g)
    head = x[: int(sr * 0.5)]
    tail = x[int(sr * (SECS - 3.0)) :]

    rms = db(np.sqrt((tail.astype(np.float64) ** 2).mean()))
    pk  = db(np.abs(tail).max())

    w = np.hanning(len(tail))
    X = np.abs(np.fft.rfft(tail * w)) / (np.sum(w) / 2)
    f = np.fft.rfftfreq(len(tail), 1 / sr)
    sel = f > 0.5
    dom = f[sel][np.argmax(X[sel])] if sel.any() else 0.0
    lo_share = 100.0 * (X[(f > 0.5) & (f < 120)] ** 2).sum() / max((X[sel] ** 2).sum(), 1e-30)

    verdict = ("SELF-OSCILLATING" if rms > -60 else
               "audible residue" if rms > -80 else "quiet")
    print(f"{g*10:>6.1f}  {db(np.abs(head).max()):>10.1f}  {rms:>11.1f}  {pk:>10.1f}  "
          f"{dom:>9.1f}Hz  {lo_share:>12.1f}%   {verdict}")
