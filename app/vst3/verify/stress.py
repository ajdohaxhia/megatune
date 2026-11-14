import subprocess, sys, os, numpy as np
BENCH, WORK = sys.argv[1], sys.argv[2]
def render(name, **kw):
    out = os.path.join(WORK, "s_" + name.replace(" ", "_").replace("/", "_") + ".f32")
    r = subprocess.run([BENCH, out] + [f"{k}={v}" for k, v in kw.items()], capture_output=True, text=True)
    if r.returncode != 0: return None, None, -1
    y = np.fromfile(out, dtype=np.float32).reshape(-1, 2).astype(np.float64)
    x = np.fromfile(out + ".in", dtype=np.float32).astype(np.float64)
    return x, y, int(r.stdout.split()[1])
def f0_track(x, sr, lo, hi):
    win = int(sr * 0.043); hop = int(sr * 0.0107); out = []
    lmin, lmax = int(sr / hi), int(sr / lo)
    step = max(1, int(sr // 48000))
    for s in range(0, len(x) - win - lmax - 2, hop):
        a = x[s:s + win:step]
        ea = np.sum(a * a)
        if ea < 1e-4 / step: out.append((0.0, 0.0)); continue
        lags = np.arange(lmin, lmax + 1)
        c = np.array([np.sum(a * x[s + l:s + l + win:step]) / np.sqrt(ea * np.sum(x[s + l:s + l + win:step] ** 2) + 1e-12) for l in lags])
        k = int(np.argmax(c))
        for j in range(1, len(c) - 1):
            if c[j] > 0.97 * c[k] and c[j] >= c[j - 1] and c[j] >= c[j + 1]: k = j; break
        d = 0.0
        if 0 < k < len(c) - 1:
            den = c[k - 1] - 2 * c[k] + c[k + 1]
            d = 0.5 * (c[k - 1] - c[k + 1]) / den if abs(den) > 1e-12 else 0.0
        out.append((sr / (lmin + k + d), c[k]))
    return np.array(out)
def cents(hz): m = 69 + 12 * np.log2(hz / 440.0); return 100 * (m - np.round(m))
def tuning(name, **kw):
    sr = kw.get("rate", 48000); hz = kw.get("hz", 210)
    outhz = kw.pop("outhz", hz)
    x, y, lat = render(name, **kw)
    if x is None: print(f"{name:40s} BENCH FAILED"); return
    bad = (~np.isfinite(y)).sum(); peak = np.max(np.abs(y)) if len(y) else 0
    tin, tout = f0_track(x, sr, hz * 0.6, hz * 1.9), f0_track(y[:, 0], sr, outhz * 0.6, outhz * 1.9)
    n = min(len(tin), len(tout)); skip = 40
    v = (tout[skip:n, 1] > 0.8) & (tout[skip:n, 0] > 0)
    vi = tin[skip:n, 0] > 0
    ci = cents(tin[skip:n, 0][vi]); co = cents(tout[skip:n, 0][v]) if v.any() else np.array([999.0])
    # clicks: largest sample-to-sample jump relative to the input's largest jump
    dj = np.max(np.abs(np.diff(y[sr // 2:, 0]))) / (np.max(np.abs(np.diff(x))) + 1e-12)
    print(f"{name:40s} lat {lat:5d} in {np.mean(np.abs(ci)):5.1f}c out {np.mean(np.abs(co)):5.1f}c p95 {np.percentile(np.abs(co),95):5.1f}c "
          f"per {np.mean(tout[skip:n,1][v]) if v.any() else 0:.3f} voiced {v.mean()*100:3.0f}% (in {np.mean(tin[skip:n,1]>0.8)*100:3.0f}%) peak {peak:.2f} jump x{dj:.2f} nonfinite {bad}")
ch = {f"note{i}": 1 for i in range(12)}
which = sys.argv[3] if len(sys.argv) > 3 else "all"
if which in ("all", "rates"):
    for sr in (44100, 88200, 96000, 192000):
        tuning(f"rate {sr}", rate=sr, speed=0, seconds=3, **ch)
        tuning(f"rate {sr} low latency", rate=sr, speed=0, seconds=3, lowlatency=1, **ch)
        tuning(f"rate {sr} 100 Hz", rate=sr, hz=98, speed=0, seconds=3, **ch)
        tuning(f"rate {sr} 100 Hz low latency", rate=sr, hz=98, speed=0, seconds=3, lowlatency=1, **ch)
if which in ("all", "blocks"):
    for b in (16, 37, 64, 1024, 4096):
        tuning(f"block {b}", block=b, speed=0, **ch)
    tuning("mono bus", mono=1, speed=0, **ch)
    tuning("mono bus doubler", mono=1, speed=0, doubler=60, width=80, **ch)
if which in ("all", "pitch"):
    for hz in (72, 82, 98, 130, 330, 520, 700, 900, 1100):
        tuning(f"vowel {hz} Hz", hz=hz, speed=0, **ch)
        tuning(f"vowel {hz} Hz low latency", hz=hz, speed=0, lowlatency=1, **ch)
    tuning("sine 210", signal="sine", hz=213, speed=0, **ch)
    tuning("signalsmith", speed=0, smith=1, **ch)
    tuning("legacy engine", speed=0, engine=0, **ch)
    tuning("shift +12", speed=0, shift=12, **ch)
    tuning("shift -12", speed=0, shift=-12, **ch)
