import numpy as np
from formant_harness import vowel, response_peaks, SR
from formant_calib import pulse_train, V1, V2

def cepstral_formants(x, sr=SR, W=4096, lifter_ms=2.0):
    res = []
    for s in range(0, len(x)-W, W//2):
        fr = x[s:s+W]
        if np.sqrt(np.mean(fr**2)) < 1e-6: continue
        fr = (fr-fr.mean())*np.hanning(W)
        logmag = np.log(np.abs(np.fft.rfft(fr)) + 1e-10)
        cep = np.fft.irfft(logmag, n=W)
        q = int(sr*lifter_ms/1000)
        cep[q:-q] = 0.0
        env = np.fft.rfft(cep, n=W).real
        fr_hz = np.fft.rfftfreq(W, 1/sr)
        band = (fr_hz > 200) & (fr_hz < 4000)
        e = env.copy(); e[~band] = -1e9
        # peak pick with prominence
        pk = [i for i in range(1, len(e)-1) if e[i] > e[i-1] and e[i] > e[i+1] and band[i]]
        if len(pk) < 2: continue
        # merge peaks closer than 150 Hz, keep strongest
        merged = []
        for i in pk:
            if merged and fr_hz[i]-fr_hz[merged[-1]] < 150:
                if e[i] > e[merged[-1]]: merged[-1] = i
            else: merged.append(i)
        if len(merged) >= 2: res.append([fr_hz[merged[0]], fr_hz[merged[1]]])
    return np.median(np.array(res), axis=0) if res else np.array([np.nan, np.nan])

def score(label, fn):
    errs = []
    for name, V in (("V1",V1),("V2",V2)):
        ref = np.array(response_peaks(V)[:2])
        rng = np.random.default_rng(3)
        for exc, src in (("noise", 0.3*rng.standard_normal(SR)),) + tuple(
                (f"pulse{f0:.0f}", pulse_train(f0)) for f0 in (155.6,220.0,311.1)):
            est = fn(vowel(src, V))
            errs.append(100*(est-ref)/ref)
    errs = np.array(errs)
    print(f"{label:26s} max|err|={np.nanmax(np.abs(errs)):6.2f}% mean|err|={np.nanmean(np.abs(errs)):5.2f}% nan={int(np.isnan(errs).any(axis=1).sum())}")
    return errs

print("=== zero-drift accuracy ===")
score("cepstral 4096 2ms", lambda x: cepstral_formants(x))
score("cepstral 4096 1.5ms", lambda x: cepstral_formants(x, lifter_ms=1.5))
score("cepstral 2048 2ms", lambda x: cepstral_formants(x, W=2048))

print("\n=== drift detection, pulse F0=220, cepstral ===")
ref0 = np.array(response_peaks(V1)[:2])
for pct in (-10,-5,5,10):
    V = [(f*(1+pct/100), bw) for f,bw in V1]
    ref = np.array(response_peaks(V)[:2])
    est = cepstral_formants(vowel(pulse_train(220.0), V))
    td = 100*(ref-ref0)/ref0; md = 100*(est-ref0)/ref0
    print(f"{pct:+3d}%: true={np.round(td,1)} meas={np.round(md,1)} err_pp={np.round(md-td,1)}")

print("\n=== drift detection, noise excitation, cepstral ===")
rng = np.random.default_rng(7); noise = 0.3*rng.standard_normal(SR)
for pct in (-10,-5,5,10):
    V = [(f*(1+pct/100), bw) for f,bw in V1]
    ref = np.array(response_peaks(V)[:2])
    est = cepstral_formants(vowel(noise, V))
    td = 100*(ref-ref0)/ref0; md = 100*(est-ref0)/ref0
    print(f"{pct:+3d}%: true={np.round(td,1)} meas={np.round(md,1)} err_pp={np.round(md-td,1)}")
