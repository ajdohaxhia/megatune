"""Formant estimator calibration on known-resonance fixtures.

Ground truth = peaks of the resonator cascade's impulse response (not the design
parameters), so the estimator is scored against the actual filter.
"""
import numpy as np
from formant_harness import resonator, vowel, response_peaks, SR

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

def poles(x, sr=SR, order=14, pre=0.97):
    x = np.asarray(x, float); x = x - x.mean()
    if pre: x = np.append(x[0], x[1:] - pre*x[:-1])
    x = x * np.hanning(len(x))
    if np.sqrt(np.mean(x**2)) < 1e-7: return []
    r = np.correlate(x, x, "full")[len(x)-1:len(x)+order]
    a = _levinson(r, order)
    if a is None: return []
    out = []
    for z in np.roots(a):
        if np.imag(z) <= 0: continue
        f = np.angle(z)*sr/(2*np.pi); bw = -np.log(abs(z))*sr/np.pi
        if 150 < f < 4000 and bw < 700: out.append((f, bw))
    return sorted(out, key=lambda t: t[0])

def estimate(x, sr=SR, order=14, pre=0.97, W=2048):
    res = []
    for s in range(0, len(x)-W, W//2):
        p = poles(x[s:s+W], sr, order, pre)
        if len(p) >= 2: res.append([p[0][0], p[1][0]])
    return np.median(np.array(res), axis=0) if res else np.array([np.nan, np.nan])

def pulse_train(f0, n=SR):
    src = np.zeros(n); src[::int(round(SR/f0))] = 1.0; return src

V1 = [(700,110),(1200,150),(2600,250)]
V2 = [(400,80),(2200,200),(3000,300)]

def score(order, pre, label):
    rows = []
    for name, V in (("V1",V1),("V2",V2)):
        ref = response_peaks(V); rng = np.random.default_rng(3)
        for exc, src in (("noise", 0.3*rng.standard_normal(SR)),) + tuple(
                (f"pulse{f0:.0f}", pulse_train(f0)) for f0 in (155.6,220.0,311.1)):
            est = estimate(vowel(src, V), order=order, pre=pre)
            err = 100*(est - np.array(ref[:2]))/np.array(ref[:2])
            rows.append((f"{name}/{exc}", err))
    errs = np.array([r[1] for r in rows])
    print(f"{label:28s} max|err|={np.nanmax(np.abs(errs)):6.2f}%  mean|err|={np.nanmean(np.abs(errs)):5.2f}%  nan={int(np.isnan(errs).any(axis=1).sum())}")
    return rows

print("=== zero-drift accuracy (percent error vs response peaks) ===")
for order, pre, lab in ((12,0.0,"current order12 pre0"),(14,0.0,"order14 pre0"),
                        (14,0.97,"order14 pre.97"),(16,0.97,"order16 pre.97")):
    score(order, pre, lab)

print("\n=== drift detection, pulse F0=220 ===")
ref0 = response_peaks(V1)
for order, pre, lab in ((12,0.0,"current"),(14,0.97,"pre.97 order14")):
    line = []
    for pct in (-10,-5,5,10):
        V = [(f*(1+pct/100), bw) for f,bw in V1]
        ref = response_peaks(V)
        est = estimate(vowel(pulse_train(220.0), V), order=order, pre=pre)
        true_d = 100*(np.array(ref[:2])-np.array(ref0[:2]))/np.array(ref0[:2])
        meas_d = 100*(est - np.array(ref0[:2]))/np.array(ref0[:2])
        line.append(f"{pct:+d}%:true={np.round(true_d,1)} meas={np.round(meas_d,1)} err_pp={np.round(meas_d-true_d,1)}")
    print(f"{lab}: " + " | ".join(line))
