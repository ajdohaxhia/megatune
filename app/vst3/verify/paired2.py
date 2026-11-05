import sys, numpy as np
sys.path.insert(0,'/Users/ade/Documents/Craft Anything/metatune/app/vst3/verify')
import formant_harness as H
SR=H.SR
def proj(sig,ks,f0):
    n=np.arange(len(sig)); return np.exp(-2j*np.pi*np.outer(ks,n)*f0/SR)@sig/len(sig)
def Hgrid(f,f1s,f2s,bws,f3=2600.0):
    z=np.exp(-2j*np.pi*f/SR)
    def mag(fc,bw):
        r=np.exp(-np.pi*bw/SR); th=2*np.pi*fc/SR
        return 1.0/np.abs(1+(-2*r*np.cos(th))*z+(r*r)*z*z)
    return mag(f1s[:,None,None],bws[0])*mag(f2s[None,:,None],bws[1])*mag(f3,bws[2])
def fit(xexc,yout,period,f1s,f2s,bws):
    N=period*64; f0=SR/period; ks=np.arange(1,int(5000/f0)+1)
    y=np.log(np.abs(proj(yout[:N],ks,f0)/proj(xexc[:N],ks,f0))+1e-15); f=ks*f0
    LH=np.log(Hgrid(f,f1s,f2s,bws)+1e-15)
    r=y[None,None,:]-LH; r=r-r.mean(axis=2,keepdims=True)
    L=np.sum(r*r,axis=2); i=np.unravel_index(np.argmin(L),L.shape)
    e0,f1c,f2c=float(L[i]),float(f1s[i[0]]),float(f2s[i[1]])
    f1f=np.arange(max(200,f1c-15),f1c+15.1,1.0); f2f=np.arange(max(300,f2c-30),f2c+30.1,2.0)
    LH2=np.log(Hgrid(f,f1f,f2f,bws)+1e-15)
    r2=y[None,None,:]-LH2; r2=r2-r2.mean(axis=2,keepdims=True)
    L2=np.sum(r2*r2,axis=2); j=np.unravel_index(np.argmin(L2),L2.shape)
    return float(L2[j]),float(f1f[j[0]]),float(f2f[j[1]])
def fixture(form,f0):
    P=int(round(SR/f0)); N=P*64; src=np.zeros(N); src[::P]=1.0
    return src,H.vowel(src,form),P
V=[(700,110),(1200,150),(2600,250)]; bws=(110,150,250)
f1s=np.arange(300,1001,10.0); f2s=np.arange(800,2401,20.0)
src,out,P=fixture(V,220.0); base=fit(src,out,P,f1s,f2s,bws)
print(f"base220 fit=({base[1]:.1f},{base[2]:.1f}) loss={base[0]:.4f}")
for st,f0,mags in ((3,261.6,(4.5,5.5)),(6,311.1,(9.5,10.5))):
    for pct in [m*s for s in (1,-1) for m in mags]:
        Vd=[(f*(1+pct/100),bw) for f,bw in V]
        src,out,P=fixture(Vd,f0); L,a,b=fit(src,out,P,f1s,f2s,bws)
        m1=100*(a-base[1])/base[1]; m2=100*(b-base[2])/base[2]
        print(f"{st:+d}st true={pct:+5.1f}% F1={m1:+6.2f}% (err {m1-pct:+5.2f}pp) F2={m2:+6.2f}% (err {m2-pct:+5.2f}pp) fit=({a:.0f},{b:.0f}) loss={L:.3f}")
