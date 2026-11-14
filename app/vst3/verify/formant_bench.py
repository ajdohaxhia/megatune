#!/usr/bin/env python3
"""Does the shifter move the formants along with the pitch?

The bench vowel is a sum of harmonics whose amplitudes follow a known envelope E(f).
After shifting, the output harmonics sit at new frequencies f_k. If formants are
preserved their amplitudes still follow E(f_k); if the spectrum was simply stretched
they follow E(f_k / ratio). We fit a_k ~ g * E(f_k / s) for the stretch s:
s = 1 means preserved, s = ratio means not preserved.
usage: formant_bench.py /path/to/MegaTuneBench [workdir]"""
import subprocess, sys, os, numpy as np
BENCH = sys.argv[1]; WORK = sys.argv[2] if len(sys.argv) > 2 else "."
SR = 48000
def E(f):
    return 1 / (1 + ((f - 700) / 130) ** 2) + 0.7 / (1 + ((f - 1200) / 180) ** 2) + 0.25 / (1 + ((f - 2600) / 250) ** 2) + 0.02
def render(name, **kw):
    out = os.path.join(WORK, name + ".f32")
    msg = subprocess.run([BENCH, out] + [f"{k}={v}" for k, v in kw.items()], capture_output=True, text=True, check=True).stdout
    y = np.fromfile(out, dtype=np.float32).reshape(-1, 2).astype(np.float64)[:, 0]
    return y, int(msg.split()[1])
def harmonics(y, f0guess, fmax=4000):
    n = len(y); w = np.hanning(n); F = np.abs(np.fft.rfft(y * w)); f = np.fft.rfftfreq(n, 1 / SR)
    lo, hi = np.searchsorted(f, f0guess * 0.94), np.searchsorted(f, f0guess * 1.06)
    f0 = f[lo + np.argmax(F[lo:hi])]
    fk, ak = [], []
    for k in range(1, int(fmax / f0) + 1):
        a, b = np.searchsorted(f, k * f0 * (1 - 0.03 / k) - 3), np.searchsorted(f, k * f0 * (1 + 0.03 / k) + 3)
        fk.append(k * f0); ak.append(np.sqrt(np.sum(F[a:b] ** 2)))       # energy of the whole peak
    return f0, np.array(fk), np.array(ak)
def stretch(fk, ak):
    best = (1e9, 1.0)
    for s in np.arange(0.45, 2.3, 0.005):
        model = np.log(E(fk / s)); d = np.log(ak + 1e-12) - model; err = np.mean((d - d.mean()) ** 2)
        if err < best[0]: best = (err, s)
    return best[1], np.sqrt(best[0])
chrom = {f"note{i}": 1 for i in range(12)}
print("vowel 110 Hz, no vibrato; stretch 1.00 = formants preserved")
print(f"{'engine':12s} {'shift':>6s} {'f0 out':>8s} {'stretch':>8s} {'if not preserved':>17s} {'fit rms (log)':>14s}")
for engine, extra in (("Quality", {}), ("Low Latency", {"lowlatency": 1}), ("Signalsmith", {"smith": 1})):
    for shift in (0, -12, -6, -3, 3, 6, 12):
        y, lat = render(f"formant_{engine.replace(' ', '')}_{shift}", signal="vowel", hz=110, vibrato=0, seconds=5, speed=0, shift=shift, **extra, **chrom)
        seg = y[2 * SR: 4 * SR + SR // 2]
        ratio = 2 ** (shift / 12)
        f0, fk, ak = harmonics(seg, 110 * ratio)
        s, rms = stretch(fk, ak)
        print(f"{engine:12s} {shift:+6d} {f0:8.2f} {s:8.2f} {ratio:17.2f} {rms:14.2f}")
