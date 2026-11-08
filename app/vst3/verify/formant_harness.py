"""Formant harness: true 2-pole resonator cascade + pole-based LPC estimator.

Reference 1 (design): the generator's resonator frequencies (F1/F2/F3 given).
Reference 2 (measured filter response): peaks of the cascade impulse response.
The estimator is scored against Reference 2, not against its own filter design.
"""
import numpy as np

SR = 48000

def resonator(x, f, bw, sr=SR):
    r = np.exp(-np.pi * bw / sr); th = 2 * np.pi * f / sr
    a1, a2 = -2 * r * np.cos(th), r * r
    y = np.zeros_like(x); y1 = y2 = 0.0
    for i, v in enumerate(x):
        o = v - a1 * y1 - a2 * y2
        y[i] = o; y2 = y1; y1 = o
    return y

def vowel(source, formants, sr=SR):
    y = source.copy()
    for f, bw in formants:
        y = resonator(y, f, bw, sr)
    return (0.1 * y / (np.max(np.abs(y)) + 1e-12)).astype(np.float32)

def response_peaks(formants, sr=SR, n=8192):
    ir = np.zeros(n); ir[0] = 1.0
    for f, bw in formants: ir = resonator(ir, f, bw, sr)
    mag = np.abs(np.fft.rfft(ir)); fr = np.fft.rfftfreq(n, 1/sr)
    pk = [fr[i] for i in range(1, len(mag)-1)
          if mag[i] > mag[i-1] and mag[i] > mag[i+1] and 200 < fr[i] < 4000]
    return [round(p) for p in pk[:3]]

def _levinson(r, order, ridge=1e-9):
    r = np.array(r, float); r[0] = r[0]*(1.0+ridge) + 1e-15
    a = np.zeros(order+1); a[0] = 1.0; e = r[0]
    for i in range(1, order+1):
        acc = r[i] + (np.dot(a[1:i], r[i-1:0:-1]) if i > 1 else 0.0)
        if e <= 1e-20 or not np.isfinite(e): return None
        k = -acc/e
        if not np.isfinite(k) or abs(k) >= 0.9999: return None
        if i > 1: a[1:i] = a[1:i] + k*a[i-1:0:-1]
        a[i] = k; e *= (1-k*k)
    return a if np.all(np.isfinite(a)) else None

def lpc_poles(x, sr=SR, order=12):
    x = np.asarray(x, float); x = x - x.mean()
    x = x * np.hanning(len(x))
    if np.sqrt(np.mean(x**2)) < 1e-7: return []
    r = np.correlate(x, x, "full")[len(x)-1:len(x)+order]
    a = _levinson(r, order)
    if a is None: return []
    out = []
    for z in np.roots(a):
        if np.imag(z) <= 0: continue
        f = np.angle(z)*sr/(2*np.pi); bw = -np.log(abs(z))*sr/np.pi
        if 150 < f < 4000 and bw < 700: out.append(f)
    return sorted(out)

def formants_of(x, sr=SR, order=12):
    W = 2048; res = []
    for s in range(0, len(x) - W, W // 2):
        p = lpc_poles(x[s:s+W], sr, order)
        if len(p) >= 2: res.append(p[:2])
    return np.median(np.array(res), axis=0) if res else np.array([np.nan, np.nan])

def run():
    rng = np.random.default_rng(3)
    V1 = [(700,110),(1200,150),(2600,250)]
    V2 = [(400,80),(2200,200),(3000,300)]
    print("=== control A: noise excitation, zero drift ===")
    for name, V in (("V1", V1), ("V2", V2)):
        ref = response_peaks(V)
        x = vowel(0.3*rng.standard_normal(SR), V)
        est = formants_of(x)
        err = [round(est[i]-ref[i],0) for i in range(2)]
        print(f"{name} response={ref} est={np.round(est[:2],0)} abs_err={err}")
    print("=== control B: periodic excitation, zero drift ===")
    for f0 in (155.6,185.0,220.0,261.6,311.1):
        src = np.zeros(SR); src[::int(round(SR/f0))] = 1.0
        est = formants_of(vowel(src, V1)); ref = response_peaks(V1)
        print(f"F0={f0:5.1f} est={np.round(est[:2],0)} ref={ref[:2]} abs_err={[round(est[i]-ref[i],0) for i in range(2)]}")
    print("=== control C: known nonzero drift ===")
    ref0 = response_peaks(V1)
    for pct in (-10,-5,5,10):
        V = [(f*(1+pct/100), bw) for f,bw in V1]
        src = np.zeros(SR); src[::int(round(SR/220.0))] = 1.0
        est = formants_of(vowel(src, V)); ref = response_peaks(V)
        true_d = [100*(ref[i]-ref0[i])/ref0[i] for i in range(2)]
        meas_d = [100*(est[i]-ref0[i])/ref0[i] for i in range(2)]
        print(f"shift{pct:+3d}% true_drift={np.round(true_d,1)} meas_drift={np.round(meas_d,1)} "
              f"drift_err_pp={np.round(np.array(meas_d)-np.array(true_d),1)}")

if __name__ == "__main__":
    run()

# ---- harmonic-envelope fit (synthetic fixtures) ----
def harm_amps(x, f0, sr=SR, fmax=5000):
    W = len(x); w = np.hanning(W)
    X = np.abs(np.fft.rfft(x * w)); fr = np.fft.rfftfreq(W, 1/sr)
    fk, ak = [], []
    k = 1
    while k * f0 < fmax:
        idx = int(np.argmin(np.abs(fr - k * f0)))
        lo, hi = max(0, idx - 2), min(len(X), idx + 3)
        j = lo + int(np.argmax(X[lo:hi]))
        fk.append(fr[j]); ak.append(X[j]); k += 1
    return np.array(fk), np.array(ak)

def cascade_H(f, formants, sr=SR):
    resp = np.ones_like(f, dtype=float)
    for fc, bw in formants:
        r = np.exp(-np.pi * bw / sr); th = 2 * np.pi * fc / sr
        a1 = -2 * r * np.cos(th); a2 = r * r
        z = np.exp(-2j * np.pi * f / sr)
        resp = resp / np.abs(1 + a1 * z + a2 * z * z)
    return resp

def fit_harmonics(fk, ak, f3=2600.0, bw3=250.0):
    lf = np.log(fk); y = np.log(ak + 1e-12)
    A = np.vstack([np.ones_like(lf), lf]).T
    best = None
    for f1 in np.arange(250, 1100, 10.0):
        for f2 in np.arange(800, 3000, 20.0):
            h = np.log(cascade_H(fk, [(f1, 110), (f2, 150), (f3, bw3)]) + 1e-12)
            coef, *_ = np.linalg.lstsq(A, y - h, rcond=None)
            err = float(np.sum((y - h - A @ coef) ** 2))
            if best is None or err < best[0]: best = (err, f1, f2)
    return best

def measure_fit(x, f0):
    fk, ak = harm_amps(x, f0)
    err, f1, f2 = fit_harmonics(fk, ak)
    return f1, f2, err, len(fk)

def run_harmonic():
    V1 = [(700,110),(1200,150),(2600,250)]
    print("=== zero drift, identical formants, harmonic fit ===")
    vals = {}
    for f0 in (155.6,185.0,220.0,261.6,311.1):
        src = np.zeros(SR); src[::int(round(SR/f0))] = 1.0
        x = vowel(src, V1); f1, f2, err, k = measure_fit(x, f0)
        vals[f0] = (f1, f2); print(f"F0={f0:5.1f} fit F1={f1:6.0f} F2={f2:6.0f} harm={k} resid={err:.3f}")
    b = vals[220.0]
    for f0 in vals:
        print(f"  drift vs 220: F0={f0:5.1f} F1={100*(vals[f0][0]-b[0])/b[0]:+5.1f}% F2={100*(vals[f0][1]-b[1])/b[1]:+5.1f}%")
    print("=== known drift at paired output F0 ===")
    pairs = [(3,261.6),(-3,185.0),(6,311.1),(-6,155.6)]
    for st, f0 in pairs:
        pct = 100*(2**(st/12)-1)
        V = [(f*(1+pct/100), bw) for f, bw in V1]
        src = np.zeros(SR); src[::int(round(SR/f0))] = 1.0
        x = vowel(src, V); f1, f2, err, k = measure_fit(x, f0)
        m1 = 100*(f1-b[0])/b[0]; m2 = 100*(f2-b[1])/b[1]
        print(f"shift{st:+d} st (F0={f0:5.1f}) true={pct:+5.1f}% measured F1={m1:+5.1f}% (err {m1-pct:+4.1f}pp) F2={m2:+5.1f}% (err {m2-pct:+4.1f}pp)")

# ---- projection-based fitter (matches the verified forward-model test) ----
def _proj(sig, ks, f0, sr=SR):
    n = np.arange(len(sig))
    return np.exp(-2j*np.pi*np.outer(ks, n)*f0/sr) @ sig / len(sig)

def _Hmag(f, form, sr=SR):
    out = np.ones_like(f, dtype=float)
    for fc, bw in form:
        r = np.exp(-np.pi*bw/sr); th = 2*np.pi*fc/sr
        a1 = -2*r*np.cos(th); a2 = r*r; z = np.exp(-2j*np.pi*f/sr)
        out = out/np.abs(1 + a1*z + a2*z*z)
    return out

def fit_proj(xexc, yout, period, f1s, f2s, bws, f3=2600.0, fmax=5000.0):
    N = period*64; f0 = SR/period
    ks = np.arange(1, int(fmax/f0)+1)
    X = _proj(xexc[:N], ks, f0); Y = _proj(yout[:N], ks, f0)
    y = np.log(np.abs(Y/X)+1e-15); f = ks*f0
    best = None
    for f1 in f1s:
        for f2 in f2s:
            h = np.log(_Hmag(f, [(f1,bws[0]),(f2,bws[1]),(f3,bws[2])])+1e-15)
            r = y - h; r = r - r.mean()
            e = float(np.sum(r*r))
            if best is None or e < best[0]: best = (e, f1, f2)
    return best[0], best[1], best[2]

def run_proj_controls():
    import numpy as np
    V1 = [(700,110),(1200,150),(2600,250)]; V2 = [(400,80),(2200,200),(3000,300)]
    f1s = np.arange(250,1101,10.0); f2s = np.arange(800,3001,20.0)
    def fixture(form, f0):
        P = int(round(SR/f0)); N = P*64
        src = np.zeros(N); src[::P] = 1.0
        return src, vowel(src, form), P
    print("=== regression: P=308 fixture must recover 700/1200 ===")
    src, out, P = fixture(V1, 155.6)
    e, a, b = fit_proj(src, out, P, f1s, f2s, (110,150,250))
    old = np.log(np.abs(_proj(out, np.arange(1,30), SR/P)/_proj(src, np.arange(1,30), SR/P))+1e-15)
    def loss(f1,f2):
        f = np.arange(1,30)*SR/P
        r = old - np.log(_Hmag(f, [(f1,110),(f2,150),(2600,250)])+1e-15); r -= r.mean(); return float(np.sum(r*r))
    print(f"fit=({a:.0f},{b:.0f}) loss={e:.5f} | old(1090,1520) loss={loss(1090,1520):.5f} | ratio {loss(1090,1520)/max(e,1e-12):.1f}x")
    print("=== zero drift across F0 (identical formants) ===")
    base = {}
    for f0 in (155.6,185.0,220.0,261.6,311.1):
        src, out, P = fixture(V1, f0); e,a,b = fit_proj(src,out,P,f1s,f2s,(110,150,250))
        base[f0] = (a,b); print(f"F0={f0:5.1f} fit=({a:.0f},{b:.0f}) loss={e:.4f}")
    r0 = base[220.0]
    for f0 in base:
        print(f"  drift: F0={f0:5.1f} F1={100*(base[f0][0]-r0[0])/r0[0]:+5.1f}% F2={100*(base[f0][1]-r0[1])/r0[1]:+5.1f}%")
