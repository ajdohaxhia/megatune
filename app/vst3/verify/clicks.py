#!/usr/bin/env python3
"""Click meter. A click is a burst of high-frequency energy in the output that the input
does not have at that moment. Output and (latency-aligned) input are compared in 1 ms
frames above 9 kHz; a frame counts when the output is more than 15 dB above the loudest
input frame within +-4 ms and is not just noise-floor level.
usage: clicks.py /path/to/MegaTuneBench [workdir] [recording.f32 rate]"""
import subprocess, sys, os, numpy as np
BENCH = sys.argv[1]; WORK = sys.argv[2] if len(sys.argv) > 2 else "."
def render(name, **kw):
    out = os.path.join(WORK, "c_" + name.replace(" ", "_").replace(",", "") + ".f32")
    msg = subprocess.run([BENCH, out] + [f"{k}={v}" for k, v in kw.items()], capture_output=True, text=True, check=True).stdout
    y = np.fromfile(out, dtype=np.float32).reshape(-1, 2).astype(np.float64); x = np.fromfile(out + ".in", dtype=np.float32).astype(np.float64)
    return x, y, int(msg.split()[1])
def band(sig, sr):
    F = np.fft.rfft(sig); f = np.fft.rfftfreq(len(sig), 1 / sr); F[f < 9000] = 0
    return np.fft.irfft(F, len(sig))
def frames(sig, n):
    m = len(sig) // n
    return np.sqrt(np.mean(sig[:m * n].reshape(m, n) ** 2, axis=1))
def find(x, y, lat, sr):
    n = int(sr // 1000); count = len(x) - lat
    ein = frames(band(x[:count], sr), n)
    local = np.array([ein[max(0, i - 4):i + 5].max() for i in range(len(ein))])
    loud = np.sqrt(np.mean(x ** 2)) * 10 ** (-50 / 20)                 # ignore anything 50 dB under the programme
    events = []; worst = -99.0
    for c in range(2):
        eo = frames(band(y[lat:lat + count, c], sr), n); m = min(len(eo), len(local))
        over = 20 * np.log10((eo[:m] + 1e-12) / (local[:m] + loud))
        over[:100] = -99; worst = max(worst, over.max())
        hits = np.where(over > 15)[0]
        for h in hits:
            if not events or all(abs(h - e) > 20 for e in events): events.append(int(h))
    return sorted(events), worst
def clicks(name, **kw):
    sr = kw.get("rate", 48000)
    x, y, lat = render(name, **kw)
    events, worst = find(x, y, lat, sr)
    times = " ".join(f"{e / 1000:.2f}" for e in events[:8]) + (" ..." if len(events) > 8 else "")
    print(f"{name:40s} clicks {len(events):4d}   loudest burst {worst:+6.1f} dB over the input   {('at ' + times + ' s') if events else ''}")
    return len(events), worst
chrom = {f"note{i}": 1 for i in range(12)}
if __name__ == "__main__":
    if len(sys.argv) > 4:
        rec = {"file": sys.argv[3], "rate": int(sys.argv[4])}
        clicks("recording, default", **rec)
        clicks("recording, hard tune chromatic", speed=-3, **rec, **chrom)
        clicks("recording, natural (speed 90)", speed=90, **rec)
        clicks("recording, low latency", lowlatency=1, **rec)
        clicks("recording, doubler 50 width 60", doubler=50, width=60, **rec)
        clicks("recording, tuning off (amount 0)", amount=0, **rec)
    else:
        for ll in (0, 1):
            tag = " low latency" if ll else ""
            clicks("sung notes with gaps" + tag, signal="notes", hz=220, vibrato=15, seconds=6, lowlatency=ll)
            clicks("sung notes hard tune" + tag, signal="notes", hz=220, vibrato=15, seconds=6, speed=-3, lowlatency=ll, **chrom)
            clicks("low notes with gaps" + tag, signal="notes", hz=110, vibrato=15, seconds=6, lowlatency=ll)
            clicks("held vowel" + tag, signal="vowel", seconds=4, lowlatency=ll)
            clicks("glide" + tag, signal="glide", hz=200, vibrato=0, seconds=4, lowlatency=ll)
        clicks("notes doubler and width", signal="notes", hz=220, vibrato=15, seconds=6, doubler=60, width=80)
        clicks("notes shift -5", signal="notes", hz=220, vibrato=15, seconds=6, shift=-5)
        clicks("notes shift +4", signal="notes", hz=220, vibrato=15, seconds=6, shift=4)
