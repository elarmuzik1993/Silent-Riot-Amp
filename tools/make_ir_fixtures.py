#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Boris Miscenco
"""Write the synthetic cabinet-IR fixtures used to measure the IR path.

Three deterministic 48 kHz, 8192-sample, mono, 32-bit float WAVs, each the
impulse response of an analog-style filter, peak-normalised to 1.0:

  neutral.wav  4th-order Butterworth band-pass, 80 Hz - 5 kHz
  dark.wav     4th-order Butterworth band-pass, 80 Hz - 3 kHz
  bright.wav   4th-order Butterworth band-pass, 80 Hz - 8 kHz, plus a +4 dB
               peak (RBJ peaking EQ, Q = 1.0) at 3 kHz

"4th-order" is scipy's N = 4 for `butter(..., btype="bandpass")`. Every file
passes isValidUserIRFormat() (1 channel, 8192 = MAX_USER_IR_SAMPLES samples,
48 kHz) and isLoadableIR() (finite, peak 1.0 >= the -80 dBFS trim threshold).
They stand in for real cab IRs in docs/records/handoffs/handoff-ir-path-sound.md and in the
IR-path plan (docs/plans/2026-10-26-ir-path-sound-fixes.md).

Usage:
  python3 tools/make_ir_fixtures.py [out-dir]   # default tests/fixtures/ir
"""
import pathlib
import sys

import numpy as np
from scipy import signal
from scipy.io import wavfile

SR = 48000
N = 8192
ORDER = 4

HERE = pathlib.Path(__file__).resolve().parent
DEFAULT_OUT = HERE.parent / "tests" / "fixtures" / "ir"


def bandpass_ir(lo_hz, hi_hz):
    sos = signal.butter(ORDER, [lo_hz, hi_hz], btype="bandpass", fs=SR, output="sos")
    x = np.zeros(N)
    x[0] = 1.0
    return signal.sosfilt(sos, x)


def peaking(x, f0, gain_db, q):
    """RBJ cookbook peaking EQ."""
    a = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * f0 / SR
    alpha = np.sin(w0) / (2.0 * q)
    b = [1 + alpha * a, -2 * np.cos(w0), 1 - alpha * a]
    den = [1 + alpha / a, -2 * np.cos(w0), 1 - alpha / a]
    return signal.lfilter(b, den, x)


def peak_normalise(x):
    return (x / np.max(np.abs(x))).astype(np.float32)


def fixtures():
    return {
        "neutral.wav": peak_normalise(bandpass_ir(80.0, 5000.0)),
        "dark.wav": peak_normalise(bandpass_ir(80.0, 3000.0)),
        "bright.wav": peak_normalise(peaking(bandpass_ir(80.0, 8000.0), 3000.0, 4.0, 1.0)),
    }


def main():
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUT
    out.mkdir(parents=True, exist_ok=True)
    for name, ir in fixtures().items():
        assert ir.shape == (N,) and np.all(np.isfinite(ir))
        wavfile.write(str(out / name), SR, ir)
        tail_db = 20 * np.log10(max(np.max(np.abs(ir[-256:])), 1e-30))
        print(f"{out / name}: {N} samples @ {SR} Hz, peak 1.0, last-256 peak {tail_db:.1f} dBFS")


if __name__ == "__main__":
    main()
