#!/usr/bin/env python3
"""Objective checks for the tuner, doubler and widener, using MegaTuneBench.
usage: bench.py /path/to/MegaTuneBench [workdir]"""
import subprocess, sys, os, numpy as np
BENCH = sys.argv[1]; WORK = sys.argv[2] if len(sys.argv) > 2 else "."
SR = 48000

def render(name, **kw):
    out = os.path.join(WORK, name + ".f32")
    msg = subprocess.run([BENCH, out] + [f"{k}={v}" for k, v in kw.items()], capture_output=True, text=True, check=True).stdout
    y = np.fromfile(out, dtype=np.float32).reshape(-1, 2).astype(np.float64)
    x = np.fromfile(out + ".in", dtype=np.float32).astype(np.float64)
    return x, y, int(msg.split()[1])

def f0_track(x, lo=80.0, hi=600.0, win=2048, hop=512):
    """Normalised autocorrelation pitch track; returns (hz, periodicity) per frame."""
    out = []
    lmin, lmax = int(SR / hi), int(SR / lo)
    for s in range(0, len(x) - win - lmax - 2, hop):
        a = x[s:s + win]
        if np.sum(a * a) < 1e-4: out.append((0.0, 0.0)); continue
        c = np.array([np.sum(a * x[s + l:s + l + win]) / np.sqrt(np.sum(a * a) * np.sum(x[s + l:s + l + win] ** 2) + 1e-12)
                      for l in range(lmin, lmax + 1)])
        k = int(np.argmax(c)); 
        # prefer the shortest lag within 3% of the best peak (avoid sub-octaves)
        for j in range(1, len(c) - 1):
            if c[j] > 0.97 * c[k] and c[j] >= c[j - 1] and c[j] >= c[j + 1]: k = j; break
        d = 0.0
        if 0 < k < len(c) - 1:
            den = c[k - 1] - 2 * c[k] + c[k + 1]
            d = 0.5 * (c[k - 1] - c[k + 1]) / den if abs(den) > 1e-12 else 0.0
        out.append((SR / (lmin + k + d), c[k]))
    return np.array(out)

def cents_to_scale(hz, ref=440.0):
    midi = 69 + 12 * np.log2(hz / ref)
    return 100 * (midi - np.round(midi))

def tuning(name, **kw):
    x, y, lat = render(name, **kw)
    tin, tout = f0_track(x), f0_track(y[:, 0])
    n = min(len(tin), len(tout)); skip = 40
    v = (tout[skip:n, 1] > 0.8) & (tout[skip:n, 0] > 0)
    ci = cents_to_scale(tin[skip:n, 0][tin[skip:n, 0] > 0]); co = cents_to_scale(tout[skip:n, 0][v])
    print(f"{name:34s} in |err| {np.mean(np.abs(ci)):5.1f}c  out |err| {np.mean(np.abs(co)):5.1f}c  out p95 {np.percentile(np.abs(co),95):5.1f}c  "
          f"periodicity in {np.mean(tin[skip:n,1]):.3f} out {np.mean(tout[skip:n,1][v]):.3f}  voiced frames {v.mean()*100:3.0f}%")

def stereo(name, **kw):
    x, y, lat = render(name, amount=0, **kw)
    l, r = y[SR:, 0], y[SR:, 1]; d = x[SR - lat:len(x) - lat][:len(l)]
    mid, side = 0.5 * (l + r), 0.5 * (l - r)
    corr = np.sum(l * r) / np.sqrt(np.sum(l * l) * np.sum(r * r) + 1e-20)
    db = lambda a, b: 10 * np.log10((np.sum(a * a) + 1e-20) / (np.sum(b * b) + 1e-20))
    print(f"{name:34s} L/R corr {corr:+.3f}  side/mid {db(side, mid):6.1f} dB  out/dry {db(np.r_[l, r], np.r_[d, d]):+5.2f} dB  "
          f"mono-sum residual vs dry {db(mid - d, d):6.1f} dB")

ONLY_STEREO = len(sys.argv) > 3
print("== Tuning (vowel 210 Hz, +-35c vibrato + drift; chromatic scale; target = nearest semitone) ==")
chrom = {f"note{i}": 1 for i in range(12)}
for sp in (-3, 0, 25, 100):
    tuning(f"vowel speed={sp}", signal="vowel", speed=sp, **chrom)
tuning("vowel low latency speed=0", signal="vowel", speed=0, lowlatency=1, **chrom)
tuning("vowel 110 Hz speed=0", signal="vowel", hz=108, speed=0, **chrom)
tuning("vowel 110 Hz low latency", signal="vowel", hz=108, speed=0, lowlatency=1, **chrom)
tuning("vowel 420 Hz speed=0", signal="vowel", hz=420, speed=0, **chrom)
tuning("off-pitch notes speed=0", signal="notes", hz=220, speed=0, vibrato=15, **chrom)
tuning("off-pitch notes speed=25", signal="notes", hz=220, speed=25, vibrato=15, **chrom)
tuning("glide speed=0", signal="glide", hz=200, speed=0, vibrato=0, **chrom)
tuning("vowel +3 st shift", signal="vowel", speed=0, shift=3, **chrom)
tuning("vowel -12 st shift", signal="vowel", speed=0, shift=-12, **chrom)
tuning("steady sine 213 Hz speed=0", signal="sine", hz=213, speed=0, **chrom)
tuning("vowel amount=0 (dry reference)", signal="vowel", amount=0, **chrom)
print("== Doubler / Width (tuning off, mono source on a stereo bus) ==")
for d, w in ((0, 0), (25, 0), (50, 0), (75, 0), (100, 0), (0, 50), (0, 100), (65, 100), (100, 100)):
    stereo(f"doubler={d} width={w}", signal="vowel", doubler=d, width=w)
