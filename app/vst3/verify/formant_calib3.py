import numpy as np
from formant_harness import vowel, response_peaks, SR
from formant_calib import pulse_train, V1, V2, _levinson

def lpc_a(x, sr=SR, order=12, W=2048):
    out=[]
    for s in range(0,len(x)-W,W//2):
        fr=x[s:s+W]
        if np.sqrt(np.mean(fr**2))<1e-6: continue
        fr=(fr-fr.mean())*np.hanning(W)
        r=np.correlate(fr,fr,"full")[W-1:W+order]
        a=_levinson(r,order)
        if a is not None: out.append(a)
    return out

def peaks_of_a(a, sr=SR, order_bins=4096):
    f=np.fft.rfftfreq(order_bins,1/sr)
    z=np.exp(-2j*np.pi*f/sr)
    H=1.0/np.abs(np.polyval(a[::-1],z))
    band=(f>200)&(f<4000); e=np.where(band,H,-1)
    pk=[i for i in range(1,len(e)-1) if e[i]>e[i-1] and e[i]>e[i+1] and band[i]]
    merged=[]
    for i in pk:
        if merged and f[i]-f[merged[-1]]<120:
            if e[i]>e[merged[-1]]: merged[-1]=i
        else: merged.append(i)
    return [f[i] for i in merged[:2]]

def est_spectrum(x, order=12, W=2048):
    A=lpc_a(x,order,W); P=[peaks_of_a(a) for a in A if len(peaks_of_a(a))>=2]
    return np.median(np.array([p[:2] for p in P]),axis=0) if P else np.array([np.nan,np.nan])

def score(label, fn):
    errs=[]
    for name,V in (("V1",V1),("V2",V2)):
        ref=np.array(response_peaks(V)[:2]); rng=np.random.default_rng(3)
        for exc,src in (("noise",0.3*rng.standard_normal(SR)),)+tuple((f"pulse{f0:.0f}",pulse_train(f0)) for f0 in (155.6,220.0,311.1)):
            errs.append(100*(fn(vowel(src,V))-ref)/ref)
    errs=np.array(errs)
    print(f"{label:24s} max|err|={np.nanmax(np.abs(errs)):6.2f}% mean|err|={np.nanmean(np.abs(errs)):5.2f}% nan={int(np.isnan(errs).any(axis=1).sum())}")

print("=== zero-drift accuracy ===")
for o in (12,16,20): score(f"lpc spectrum order{o}", lambda x,o=o: est_spectrum(x,o))
for o in (12,16,20): score(f"lpc spectrum W4096 o{o}", lambda x,o=o: est_spectrum(x,o,4096))

print("\n=== drift, pulse F0=220, spectrum order16 W4096 ===")
ref0=np.array(response_peaks(V1)[:2])
for pct in (-10,-5,5,10):
    V=[(f*(1+pct/100),bw) for f,bw in V1]; ref=np.array(response_peaks(V)[:2])
    est=est_spectrum(vowel(pulse_train(220.0),V),16,4096)
    td=100*(ref-ref0)/ref0; md=100*(est-ref0)/ref0
    print(f"{pct:+3d}%: true={np.round(td,1)} meas={np.round(md,1)} err_pp={np.round(md-td,1)}")
print("\n=== drift, noise, spectrum order16 W4096 ===")
rng=np.random.default_rng(7); noise=0.3*rng.standard_normal(SR)
for pct in (-10,-5,5,10):
    V=[(f*(1+pct/100),bw) for f,bw in V1]; ref=np.array(response_peaks(V)[:2])
    est=est_spectrum(vowel(noise,V),16,4096)
    td=100*(ref-ref0)/ref0; md=100*(est-ref0)/ref0
    print(f"{pct:+3d}%: true={np.round(td,1)} meas={np.round(md,1)} err_pp={np.round(md-td,1)}")
