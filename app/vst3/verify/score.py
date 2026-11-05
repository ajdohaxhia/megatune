import sys, numpy as np
sys.path.insert(0,'/Users/ade/Documents/Craft Anything/metatune/app/vst3/verify')
import formant_harness as H, measure as M
OUT="/Users/ade/Documents/Craft Anything/metatune/app/vst3/verify/out"
def Hm(f,form,sr=48000):
    out=np.ones_like(f,dtype=float)
    for fc,bw in form:
        r=np.exp(-np.pi*bw/sr); th=2*np.pi*fc/sr; z=np.exp(-2j*np.pi*f/sr)
        out=out/np.abs(1+(-2*r*np.cos(th))*z+(r*r)*z*z)
    return out
def fit_engine(y,f0,f1s,f2s,bws,fmax=5000):
    n=np.arange(len(y)); ks=np.arange(1,int(fmax/f0)+1); fr=ks*f0
    X=np.sum(y*np.exp(-2j*np.pi*np.outer(ks,n)*f0/len(y)/1.0),axis=1) if False else None
    amp=np.abs(np.exp(-2j*np.pi*np.outer(ks,n)*f0/48000.0)@y/len(y))
    yv=np.log(amp+1e-15)
    LH=np.log(Hm(fr,f1s[:,None,None],bws) if False else np.ones(1))
    def grid(a,b):
        z=np.exp(-2j*np.pi*fr/48000)
        def mag(fc,bw):
            r=np.exp(-np.pi*bw/48000); th=2*np.pi*fc/48000
            return 1.0/np.abs(1+(-2*r*np.cos(th))*z+(r*r)*z*z)
        return mag(a[:,None,None],bws[0])*mag(b[None,:,None],bws[1])*mag(2600,bws[2])
    LH=np.log(grid(f1s,f2s)+1e-15); r=yv[None,None,:]-LH; r=r-r.mean(axis=2,keepdims=True)
    L=np.sum(r*r,axis=2); i=np.unravel_index(np.argmin(L),L.shape)
    e,f1c,f2c=float(L[i]),float(f1s[i[0]]),float(f2s[i[1]])
    f1f=np.arange(max(200,f1c-15),f1c+15.1,1.0); f2f=np.arange(max(300,f2c-30),f2c+30.1,2.0)
    LH2=np.log(grid(f1f,f2f)+1e-15); r2=yv[None,None,:]-LH2; r2=r2-r2.mean(axis=2,keepdims=True)
    L2=np.sum(r2*r2,axis=2); j=np.unravel_index(np.argmin(L2),L2.shape)
    return float(L2[j]),float(f1f[j[0]]),float(f2f[j[1]])
def f0of(y,sr=48000):
    W=int(0.5*sr); out=[]
    for s in range(0,len(y)-W,W):
        f=y[s:s+W]; f=f-f.mean(); ac=np.correlate(f,f,'full')[W-1:]
        lo,hi=int(sr/900),int(sr/90); out.append(sr/(lo+int(np.argmax(ac[lo:hi]))))
    return float(np.median(out))
f1s=np.arange(300,1001,10.0); f2s=np.arange(800,2401,20.0); bws=(110,150,250)
# base reference from synthetic 220 vowel
src=np.zeros(64*218); src[::218]=1.0; v=H.vowel(src,[(700,110),(1200,150),(2600,250)])[:64*218]
_,bf1,bf2=fit_engine(v,48000/218,f1s,f2s,bws)
print(f"reference fit=({bf1:.0f},{bf2:.0f})")
for eng in ("baseline","psola","smith_off","smith_on"):
    for st in (3,-3,6,-6):
        p=f"{OUT}/cmp_{eng}_{st:+d}.wav"
        try: sr,y=M.read(p)
        except FileNotFoundError: print(eng,st,"missing"); continue
        f0=f0of(y,sr); L,a,b=fit_engine(y,f0,f1s,f2s,bws)
        d1=100*(a-bf1)/bf1; d2=100*(b-bf2)/bf2
        exp=220*2**(st/12)
        print(f"{eng:10s} {st:+d}st F0={f0:6.1f}(exp {exp:6.1f}) drift F1={d1:+6.1f}% F2={d2:+6.1f}% loss={L:.2f}")
