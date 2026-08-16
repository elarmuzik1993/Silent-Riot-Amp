"""Envelope-modulation ("wobble" / motorboating) detector.

A steady input must produce a steady output envelope. If it doesn't, some
feedback loop in the amp is self-oscillating at a sub-audio rate. Audibly that
is wobbling, pulsing, or "delayed plucking".

The amp has several such loops:
  * sag       -- current -> cathode voltage -> bias -> current   (10/100 ms)
  * PSU       -- current -> supply droop -> gain -> current      (10-80 ms)
  * spk coupling -- envelope -> impedance -> gain -> envelope    (15-20 ms)
  * grid conduction -- coupling-cap charge -> bias -> conduction (22-100 ms)

Each has a loop gain that scales with plate current, so a tube recalibration can
turn a previously stable loop into an oscillating one.

Method: render a CONSTANT sine, take the amplitude envelope, remove its mean,
and look at the envelope's own spectrum between 0.3 and 25 Hz. A clean amp shows
only the settling transient; a wobbling one shows a discrete peak.

    python tools/sr_wobble.py <sr_render> <out-dir> [gain 0..1]
"""
import json, subprocess, sys, pathlib
import numpy as np

RENDER = sys.argv[1]
OUTDIR = pathlib.Path(sys.argv[2]); OUTDIR.mkdir(parents=True, exist_ok=True)
GAINS  = [float(g) for g in (sys.argv[3:] or ["0.3", "0.6", "0.8", "1.0"])]

SECS = 4.0
F0   = 110.0
AMP  = 0.25

def render(gain):
    tag = OUTDIR / f"w_{int(gain*100)}"
    subprocess.run([RENDER, "--out", str(tag), "--signal", f"sine:{F0}:{AMP}:{SECS}",
                    "--gain", f"{gain}", "--volume", "0.7", "--shape", "0.5", "--cab", "1"],
                   capture_output=True, check=True)
    sr = json.loads(tag.with_suffix(".json").read_text()).get("sample_rate", 44100)
    x = np.fromfile(str(tag) + ".f32", dtype=np.float32)
    if x.size % 2 == 0:
        x = x.reshape(-1, 2)[:, 0]
    return x, float(sr)

def envelope_spectrum(x, sr):
    # amplitude envelope via rectify + decimate to ~1 kHz
    env = np.abs(x)
    dec = max(1, int(sr // 1000))
    n   = (len(env) // dec) * dec
    env = env[:n].reshape(-1, dec).mean(axis=1)
    esr = sr / dec
    env = env[int(esr * 1.0):]            # drop the first second (settling)
    if len(env) < 64:
        return None, None, None
    env = env - env.mean()
    w = np.hanning(len(env))
    E = np.abs(np.fft.rfft(env * w)) / (np.sum(w) / 2)
    f = np.fft.rfftfreq(len(env), 1 / esr)
    return f, E, env

print(f"Envelope modulation on a CONSTANT {F0:.0f} Hz sine, amp {AMP}, volume 0.7")
print("A steady input must give a steady envelope. A discrete peak = self-oscillation.\n")
print(f"{'gain':>6}  {'wobble freq':>12}  {'depth %':>9}  {'mean env':>9}   verdict")
print("-" * 64)

for g in GAINS:
    x, sr = render(g)
    f, E, env = envelope_spectrum(x, sr)
    if f is None:
        print(f"{g*10:>6.1f}  (render too short)")
        continue
    band = (f >= 0.3) & (f <= 25.0)
    k = np.argmax(E[band])
    peak_f = f[band][k]
    peak_a = E[band][k]
    mean_env = np.abs(x[len(x)//2:]).mean()
    depth = 100.0 * peak_a / max(mean_env, 1e-9)
    verdict = "WOBBLING" if depth > 2.0 else ("slight" if depth > 0.7 else "steady")
    print(f"{g*10:>6.1f}  {peak_f:>10.2f} Hz  {depth:>8.2f}%  {mean_env:>9.4f}   {verdict}")
