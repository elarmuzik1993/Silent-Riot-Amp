#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Boris Miscenco
"""
sr_analyze.py — Silent Riot tone-sign-off analyzer.

Reads a raw float32 render produced by `sr_render` (an interleaved `.f32` file
plus a `.json` sidecar describing sample rate / channels / frames) and computes
the objective metrics defined in docs/Tone_SignOff_Protocol.md §4:

    level    : peak dBFS, RMS dBFS, integrated loudness (LUFS, ITU-R BS.1770)
    thd      : THD %, per-harmonic dBc table, H2/H3 balance      (needs --f0)
    spectrum : Welch PSD, 1/12-octave smoothed                   (sweep / noise)
    sag      : attack-peak vs sustain depth (dB), approx recovery (transient DI)
    noise    : noise floor dBFS over a (silent) region
    spurs    : non-harmonic / aliasing energy in dBc             (needs --f0)

No JUCE dependency — pure numpy/scipy, so it runs anywhere and is unit-testable
on synthetic signals (see `--selftest`). The render side is the only part that
needs the plugin built.

Usage:
    sr_analyze.py level    render.f32
    sr_analyze.py thd      render.f32 --f0 220
    sr_analyze.py spurs    render.f32 --f0 11000
    sr_analyze.py sag      render.f32
    sr_analyze.py spectrum render.f32 --json
    sr_analyze.py --selftest

The `.json` sidecar is found next to the `.f32` (same stem). A raw mono file with
no sidecar may be analyzed with explicit --sr / --channels.
"""
import argparse
import json
import os
import sys

import numpy as np

try:
    from scipy import signal as sp_signal
except Exception:  # pragma: no cover - scipy is required for spectrum/LUFS
    sp_signal = None

EPS = 1e-20


# ───────────────────────── I/O ─────────────────────────

def load_render(path, sr=None, channels=None):
    """Load an interleaved float32 render. Prefers a `<stem>.json` sidecar."""
    data = np.fromfile(path, dtype=np.float32)
    sidecar = os.path.splitext(path)[0] + ".json"
    if os.path.exists(sidecar):
        with open(sidecar) as f:
            meta = json.load(f)
        sr = int(meta["sampleRate"])
        channels = int(meta.get("numChannels", 1))
    if sr is None:
        raise SystemExit(f"no sidecar {sidecar} and no --sr given")
    channels = channels or 1
    if channels > 1:
        n = (len(data) // channels) * channels
        data = data[:n].reshape(-1, channels)
    else:
        data = data.reshape(-1, 1)
    return data.astype(np.float64), sr


def mono(data):
    """Down-mix to mono for single-channel metrics (channels are identical here)."""
    return data[:, 0] if data.ndim == 2 else data


def db(x):
    return 20.0 * np.log10(np.maximum(np.abs(x), EPS))


def steady(x, skip_frac=0.2):
    """Drop the leading transient / smoother ramp before steady-state metrics."""
    i = int(len(x) * skip_frac)
    return x[i:]


# ───────────────────────── level / loudness ─────────────────────────

def measure_level(data, sr):
    x = mono(data)
    xs = steady(x)
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    rms = float(np.sqrt(np.mean(xs * xs))) if len(xs) else 0.0
    out = {
        "peak_dbfs": round(db(peak), 3),
        "rms_dbfs": round(db(rms), 3),
        "channel_match_db": round(channel_match_db(data), 6),
    }
    if sp_signal is not None:
        out["lufs_integrated"] = round(lufs_integrated(data, sr), 3)
    return out


def channel_match_db(data):
    """L vs R level delta in dB (gate A4 — mono->stereo copy must be identical)."""
    if data.ndim != 2 or data.shape[1] < 2:
        return 0.0
    l = np.sqrt(np.mean(data[:, 0] ** 2)) + EPS
    r = np.sqrt(np.mean(data[:, 1] ** 2)) + EPS
    return float(abs(20.0 * np.log10(l / r)))


# K-weighting biquads, ITU-R BS.1770-4, defined at 48 kHz. We resample the input
# to 48 kHz for the loudness measurement so the standard coefficients apply
# regardless of the render rate.
_K_STAGE1_B = [1.53512485958697, -2.69169618940638, 1.19839281085285]
_K_STAGE1_A = [1.0, -1.69065929318241, 0.73248077421585]
_K_STAGE2_B = [1.0, -2.0, 1.0]
_K_STAGE2_A = [1.0, -1.99004745483398, 0.99007225036621]


def lufs_integrated(data, sr):
    """Integrated loudness (LUFS), ITU-R BS.1770-4 with -70/-10 gating."""
    target_sr = 48000
    if sr != target_sr:
        chans = []
        for c in range(data.shape[1]):
            chans.append(sp_signal.resample_poly(data[:, c], target_sr, sr))
        x = np.stack(chans, axis=1)
    else:
        x = data
    # K-weight each channel
    y = np.empty_like(x)
    for c in range(x.shape[1]):
        s1 = sp_signal.lfilter(_K_STAGE1_B, _K_STAGE1_A, x[:, c])
        y[:, c] = sp_signal.lfilter(_K_STAGE2_B, _K_STAGE2_A, s1)

    block = int(0.400 * target_sr)
    hop = int(0.100 * target_sr)
    if len(y) < block:
        return -120.0
    # Per-block mean-square summed over channels (stereo weights = 1.0)
    starts = range(0, len(y) - block + 1, hop)
    z = np.array([np.sum(np.mean(y[s:s + block] ** 2, axis=0)) for s in starts])
    loud = -0.691 + 10.0 * np.log10(np.maximum(z, EPS))

    abs_keep = loud > -70.0
    if not np.any(abs_keep):
        return -120.0
    rel_gate = -0.691 + 10.0 * np.log10(np.mean(z[abs_keep])) - 10.0
    keep = abs_keep & (loud > rel_gate)
    if not np.any(keep):
        return -120.0
    return float(-0.691 + 10.0 * np.log10(np.mean(z[keep])))


# ───────────────────────── harmonics / THD ─────────────────────────

def _spectrum_mag(x, sr):
    xs = steady(x)
    w = np.hanning(len(xs))
    X = np.fft.rfft(xs * w)
    freqs = np.fft.rfftfreq(len(xs), 1.0 / sr)
    return freqs, np.abs(X)


def _peak_near(freqs, mag, f, halfwidth_hz):
    lo = np.searchsorted(freqs, f - halfwidth_hz)
    hi = np.searchsorted(freqs, f + halfwidth_hz)
    if hi <= lo:
        return 0.0
    return float(np.max(mag[lo:hi]))


def measure_thd(data, sr, f0, max_harmonic=10):
    x = mono(data)
    freqs, mag = _spectrum_mag(x, sr)
    hw = max(f0 * 0.05, freqs[1] * 2)  # bin neighborhood for picking peaks
    fund = _peak_near(freqs, mag, f0, hw)
    if fund <= EPS:
        return {"error": "fundamental not found", "f0": f0}
    harmonics = {}
    sq = 0.0
    for k in range(2, max_harmonic + 1):
        fk = k * f0
        if fk >= sr / 2:
            break
        h = _peak_near(freqs, mag, fk, hw)
        harmonics[f"H{k}"] = round(db(h / fund), 2)  # dBc
        sq += h * h
    thd = float(np.sqrt(sq) / fund)
    h2 = harmonics.get("H2", -999.0)
    h3 = harmonics.get("H3", -999.0)
    return {
        "f0": f0,
        "thd_pct": round(thd * 100.0, 3),
        "harmonics_dbc": harmonics,
        "h2_minus_h3_db": round(h2 - h3, 2),     # >0 => even-dominant (warm)
        "even_dominant": bool(h2 > h3),
    }


def measure_spurs(data, sr, f0, band=(20.0, None)):
    """Non-harmonic energy in dBc relative to the fundamental — aliasing gate E3.

    Everything that is NOT within a narrow window of k*f0 is treated as a spur.
    """
    x = mono(data)
    freqs, mag = _spectrum_mag(x, sr)
    nyq = sr / 2.0
    hi = band[1] if band[1] else nyq
    in_band = (freqs >= band[0]) & (freqs <= hi)
    power = mag ** 2
    # Mask out harmonic windows (incl. fundamental)
    harm_mask = np.zeros_like(freqs, dtype=bool)
    hw = max(f0 * 0.03, freqs[1] * 2)
    k = 1
    while k * f0 < nyq:
        harm_mask |= (np.abs(freqs - k * f0) <= hw)
        k += 1
    fund = _peak_near(freqs, mag, f0, hw)
    if fund <= EPS:
        return {"error": "fundamental not found", "f0": f0}
    spur_mask = in_band & ~harm_mask
    spur_energy = float(np.sum(power[spur_mask]))
    max_spur = float(np.sqrt(np.max(power[spur_mask]))) if np.any(spur_mask) else 0.0
    return {
        "f0": f0,
        "total_spur_dbc": round(10.0 * np.log10(max(spur_energy, EPS) / (fund * fund)), 2),
        "max_spur_dbc": round(db(max_spur / fund), 2),
    }


# ───────────────────────── spectrum ─────────────────────────

def measure_spectrum(data, sr, frac_oct=12):
    if sp_signal is None:
        raise SystemExit("scipy required for spectrum")
    x = mono(data)
    f, pxx = sp_signal.welch(x, sr, nperseg=min(8192, len(x)))
    mag = 10.0 * np.log10(np.maximum(pxx, EPS))
    fs, ms = _frac_octave_smooth(f, mag, frac_oct)
    return {"freq_hz": [round(v, 1) for v in fs],
            "mag_db": [round(v, 2) for v in ms]}


def _frac_octave_smooth(f, mag_db, frac):
    lo = max(f[1], 20.0)
    hi = f[-1]
    n = int(np.log2(hi / lo) * frac)
    centers = lo * 2.0 ** (np.arange(n) / frac)
    out = []
    lin = 10.0 ** (mag_db / 10.0)
    for fc in centers:
        f1, f2 = fc / 2 ** (0.5 / frac), fc * 2 ** (0.5 / frac)
        m = (f >= f1) & (f < f2)
        out.append(10.0 * np.log10(np.mean(lin[m])) if np.any(m) else np.nan)
    centers = centers[~np.isnan(out)]
    out = np.array(out)[~np.isnan(out)]
    return centers, out


# ───────────────────────── envelope / sag ─────────────────────────

def measure_sag(data, sr):
    """Attack-peak vs sustain depth and an approximate recovery time.

    Approximate by design — labeled as such in the protocol. Sustain is sampled
    at 300 ms; recovery is the time for the smoothed envelope to come within 10%
    of the sustain level after the attack peak.
    """
    x = np.abs(mono(data))
    if sp_signal is not None:
        b, a = sp_signal.butter(2, 20.0 / (sr / 2.0))
        env = sp_signal.lfilter(b, a, x)
    else:
        env = _onepole_env(x, sr, 20.0)
    attack_win = int(0.05 * sr)
    if len(env) < int(0.35 * sr):
        return {"error": "render too short for sag (need >350ms)"}
    peak = float(np.max(env[:attack_win]))
    peak_idx = int(np.argmax(env[:attack_win]))
    sustain = float(env[int(0.30 * sr)])
    sag_db = round(db(peak) - db(sustain), 2)  # >0 => sustain dipped below attack
    # recovery proxy
    rec = None
    target = sustain * 1.10
    for i in range(peak_idx, int(0.30 * sr)):
        if env[i] <= target:
            rec = round((i - peak_idx) / sr * 1000.0, 1)
            break
    return {"attack_peak_dbfs": round(db(peak), 2),
            "sustain_300ms_dbfs": round(db(sustain), 2),
            "sag_depth_db": sag_db,
            "recovery_ms_approx": rec}


def _onepole_env(x, sr, fc):
    a = np.exp(-2.0 * np.pi * fc / sr)
    y = np.empty_like(x)
    prev = 0.0
    for i, v in enumerate(x):
        prev = (1 - a) * v + a * prev
        y[i] = prev
    return y


def measure_noise(data, sr):
    x = mono(data)
    return {"noise_floor_dbfs": round(db(np.sqrt(np.mean(x * x) + EPS)), 2),
            "dc_offset_dbfs": round(db(abs(np.mean(x)) + EPS), 2)}


# ───────────────────────── self-test ─────────────────────────

def selftest():
    """Validate the analyzer on synthetic signals with known answers."""
    sr = 48000
    t = np.arange(sr) / sr
    ok = True

    # 1) pure -6 dBFS 220 Hz sine: peak ~ -6, THD ~ 0
    amp = 10 ** (-6 / 20)
    x = (amp * np.sin(2 * np.pi * 220 * t)).astype(np.float32).reshape(-1, 1)
    lvl = measure_level(x.astype(np.float64), sr)
    thd = measure_thd(x.astype(np.float64), sr, 220)
    ok &= abs(lvl["peak_dbfs"] + 6.0) < 0.2
    ok &= thd["thd_pct"] < 0.5
    print(f"[1] pure sine: peak={lvl['peak_dbfs']} dBFS (want -6), "
          f"THD={thd['thd_pct']}% (want ~0)  -> {'ok' if abs(lvl['peak_dbfs']+6)<0.2 else 'FAIL'}")

    # 2) sine + 10% H2: THD ~ 10%, H2 dominant
    y = np.sin(2 * np.pi * 220 * t) + 0.10 * np.sin(2 * np.pi * 440 * t)
    y = (0.5 * y).astype(np.float64).reshape(-1, 1)
    thd2 = measure_thd(y, sr, 220)
    ok &= abs(thd2["thd_pct"] - 10.0) < 1.0
    ok &= thd2["even_dominant"]
    print(f"[2] +10% H2: THD={thd2['thd_pct']}% (want ~10), "
          f"H2/H3 even_dominant={thd2['even_dominant']} (want True)")

    # 3) loudness sanity: -23 LUFS pink-ish (just check it returns a finite value)
    if sp_signal is not None:
        lu = lufs_integrated(y, sr)
        ok &= np.isfinite(lu)
        print(f"[3] LUFS finite={np.isfinite(lu)} ({round(lu,1)} LUFS)")

    # 4) spurs: clean tone has very low non-harmonic energy
    sp = measure_spurs(x.astype(np.float64), sr, 220)
    ok &= sp["total_spur_dbc"] < -60
    print(f"[4] clean-tone spurs={sp['total_spur_dbc']} dBc (want < -60)")

    print("SELFTEST:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


# ───────────────────────── CLI ─────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Silent Riot tone-sign-off analyzer")
    ap.add_argument("metric", nargs="?",
                    choices=["level", "thd", "spurs", "spectrum", "sag", "noise", "all"])
    ap.add_argument("render", nargs="?", help="path to .f32 render")
    ap.add_argument("--f0", type=float, help="fundamental Hz (thd/spurs)")
    ap.add_argument("--sr", type=int, help="sample rate if no sidecar")
    ap.add_argument("--channels", type=int)
    ap.add_argument("--json", action="store_true", help="emit JSON")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        sys.exit(selftest())
    if not args.metric or not args.render:
        ap.error("metric and render path required (or use --selftest)")

    data, sr = load_render(args.render, args.sr, args.channels)

    def need_f0():
        if args.f0 is None:
            ap.error(f"--f0 required for '{args.metric}'")
        return args.f0

    if args.metric == "level":
        res = measure_level(data, sr)
    elif args.metric == "thd":
        res = measure_thd(data, sr, need_f0())
    elif args.metric == "spurs":
        res = measure_spurs(data, sr, need_f0())
    elif args.metric == "spectrum":
        res = measure_spectrum(data, sr)
    elif args.metric == "sag":
        res = measure_sag(data, sr)
    elif args.metric == "noise":
        res = measure_noise(data, sr)
    elif args.metric == "all":
        res = {"level": measure_level(data, sr),
               "sag": measure_sag(data, sr),
               "noise": measure_noise(data, sr)}
        if args.f0:
            res["thd"] = measure_thd(data, sr, args.f0)
            res["spurs"] = measure_spurs(data, sr, args.f0)

    print(json.dumps(res, indent=2))


if __name__ == "__main__":
    main()
