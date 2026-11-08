"""LPC-spectrum envelope formant estimator (robust to wide/merged poles)."""
import numpy as np
from formant_harness import _levinson, SR, vowel, response_peaks
from formant_calib import pulse_train, V1, V2

def frame_formants(x, sr=SR, order=14, W=2048, fmin=200, fmax=4000):
    fr = x[:W].astype(float); fr = (fr-fr.mean())*np.hanning(len(fr))
    if np.sqrt(np.mean(fr**2)) < 1e-7: return None
    r = np.correlate(fr, fr, "full")[len(fr)-1:len(fr)+order]
    a = _levinson(r, order)
    if a is None: return None
    f = np.arange(fmin, fmax, 1.0)
    z = np.exp(-2j*np.pi*f/sr)
    H = 1.0/np.abs(np.polyval(a[::-1], z))
    logH = np.log(H+1e-12)
    pk = [i for i in range(1, len(logH)-1) if logH[i] > logH[i-1] and logH[i] > logH[i+1]]
    merged = []
    for i in pk:
        if merged and f[i]-f[merged[-1]] < 150:
            if logH[i] > logH[merged[-1]]: merged[-1] = i
        else: merged.append(i)
    return [f[i] for i in merged[:2]]

def formants(x, sr=SR, order=14, W=2048):
    res = []
    for s in range(0, len(x)-W, W//2):
        p = frame_formants(x[s:s+W], sr, order, W)
        if p and len(p) >= 2: res.append(p[:2])
    return np.median(np.array(res), axis=0) if res else np.array([np.nan, np.nan])
