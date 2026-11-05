import json, os, subprocess, sys
import numpy as np
import wave

ROOT = "/Users/ade/Documents/Craft Anything/metatune/app/vst3"
PLUGIN = "/Users/ade/Documents/Craft Anything/metatune/app/MegaTune.vst3"
RENDER = f"{ROOT}/host/build/MegaTuneRender_artefacts/Release/MegaTuneRender"
OUT = "/tmp/mt-verify"
os.makedirs(OUT, exist_ok=True)
SR = 48000

def synth_vowel(f0, formants, dur=2.0, sr=SR, vib=0.0):
    n = int(dur * sr)
    t = np.arange(n) / sr
    f = f0 * 2 ** ((vib * np.sin(2 * np.pi * 5.5 * t)) / 1200.0)
    phase = np.cumsum(2 * np.pi * f / sr)
    x = np.zeros(n)
    for k in range(1, 40):
        fk = f0 * k
        if fk > 8000: break
        a = sum(g / (1 + ((fk - fc) / bw) ** 2) for fc, bw, g in formants)
        x += a * np.sin(phase * k)
    return (0.12 * x / (np.max(np.abs(x)) + 1e-9)).astype(np.float32)

def write(path, x, sr=SR):
    y = np.clip(x, -1, 1)
    v = (y * 32767).astype("<i2")
    stereo = np.stack([v, v], axis=1).reshape(-1)
    w = wave.open(path, "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(stereo.tobytes()); w.close()

def read(path):
    w = wave.open(path, "rb")
    n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    data = w.readframes(n); w.close()
    if sw == 2:
        x = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    elif sw == 3:
        b = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v >= (1 << 23), v - (1 << 24), v)
        x = v.astype(np.float64) / float(1 << 23)
    elif sw == 4:
        x = np.frombuffer(data, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise RuntimeError("unsupported sample width %d" % sw)
    if ch > 1: x = x.reshape(-1, ch)[:, 0]
    return sr, x

def lpc(x, order):
    x = x - np.mean(x)
    r = np.correlate(x, x, "full")[len(x) - 1: len(x) + order]
    a = np.zeros(order + 1); a[0] = 1.0; e = r[0] + 1e-12
    for i in range(1, order + 1):
        acc = r[i]
        for j in range(1, i):
            acc += a[j] * r[i - j]
        k = -acc / e
        a_new = a.copy()
        for j in range(1, i):
            a_new[j] = a[j] + k * a[i - j]
        a_new[i] = k
        a = a_new
        e *= (1 - k * k)
    return a

def formants(x, sr, order=12, fmax=4000):
    x = np.append(x[0], x[1:] - 0.97 * x[:-1])
    win = int(0.030 * sr)
    hop = int(0.010 * sr)
    out = []
    freqs = np.fft.rfftfreq(1024, 1 / sr)
    for start in range(0, len(x) - win, hop):
        frame = x[start:start + win] * np.hanning(win)
        if np.sqrt(np.mean(frame ** 2)) < 1e-4: continue
        a = lpc(frame, order)
        w, h = np.fft.rfft(a, 1024), None
        mag = 1.0 / (np.abs(w) + 1e-9)
        db = 20 * np.log10(mag + 1e-12)
        cand = []
        for i in range(1, len(db) - 1):
            if db[i] > db[i - 1] and db[i] > db[i + 1] and 200 < freqs[i] < fmax:
                cand.append((db[i], freqs[i]))
        cand.sort(reverse=True)
        peaks = []
        for _, fr in cand:
            if all(abs(fr - q) > 150 for q in peaks):
                peaks.append(fr)
            if len(peaks) == 3: break
        peaks.sort()
        if len(peaks) >= 2: out.append(peaks + [np.nan] * (3 - len(peaks)))
    return np.median(np.array(out), axis=0) if out else np.array([np.nan] * 3)

def f0_track(x, sr):
    win = int(0.040 * sr)
    vals = []
    for start in range(0, len(x) - win, win // 2):
        frame = x[start:start + win]
        if np.sqrt(np.mean(frame ** 2)) < 1e-4: continue
        frame = frame - frame.mean()
        ac = np.correlate(frame, frame, "full")[win - 1:]
        lo, hi = int(sr / 1000), int(sr / 60)
        seg = ac[lo:hi]
        if len(seg) < 2: continue
        lag = lo + int(np.argmax(seg))
        vals.append(sr / lag)
    return np.median(vals) if vals else 0.0

def render(src, dst, shift, low=0):
    r = subprocess.run([RENDER, PLUGIN, src, dst, str(shift), str(low)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print("render failed:", r.stdout, r.stderr); sys.exit(1)
    return r.stdout.strip()

def main():
    results = {}
    # ---- formant preservation on a known synthetic vowel (F0 = A3 220) ----
    vowel = synth_vowel(220.0, [(700, 110, 1.0), (1200, 150, 0.7), (2600, 250, 0.3)])
    src = f"{OUT}/vowel_in.wav"; write(src, vowel)
    base = formants(vowel, SR)
    print("DEBUG base formants", base, file=sys.stderr)
    formant_rows = []
    for shift in (0, 3, -3, 6, -6):
        dst = f"{OUT}/vowel_{shift:+d}.wav"
        render(src, dst, shift, 0)
        _, y = read(dst)
        f = formants(y, SR)
        drift = [100.0 * (f[i] - base[i]) / base[i] for i in range(3)]
        formant_rows.append({"shift": shift, "f1": f[0], "f2": f[1], "f3": f[2],
                             "drift_pct": [round(d, 1) for d in drift]})
    results["formants"] = formant_rows

    # ---- detector behaviour: low/high notes, vibrato, breath, unvoiced ----
    cases = {
        "low_G2_98Hz": synth_vowel(95.0, [(700, 110, 1.0), (1200, 150, 0.7)]),
        "high_E5_660Hz": synth_vowel(640.0, [(700, 110, 1.0), (1200, 150, 0.7)]),
        "vibrato_A3": synth_vowel(214.0, [(700, 110, 1.0), (1200, 150, 0.7)], vib=50.0),
        "breath_noise": (0.05 * np.random.randn(SR * 2)).astype(np.float32),
    }
    det = {}
    for name, x in cases.items():
        p = f"{OUT}/{name}.wav"; write(p, x)
        o = f"{OUT}/{name}_out.wav"
        render(p, o, 0, 0)
        _, y = read(o)
        det[name] = {"in_f0": round(f0_track(x, SR), 1), "out_f0": round(f0_track(y, SR), 1)}
    results["detector"] = det

    # ---- real vocal: render +3 / -3 and measure F0 + formants ----
    dry = "/Users/ade/Documents/Craft Anything/metatune/test-audio/ade-dry.wav"
    vocal = {}
    if os.path.exists(dry):
        sr2, x = read(dry)
        for shift in (3, -3):
            dst = f"{OUT}/vocal_{shift:+d}.wav"
            render(dry, dst, shift, 0)
            _, y = read(dst)
            vocal[f"shift_{shift:+d}"] = {"path": dst,
                "f0_in": round(f0_track(x, sr2), 1), "f0_out": round(f0_track(y, sr2), 1)}
        vocal["input_path"] = dry
    results["real_vocal"] = vocal

    # ---- low-note shifting per mode at both rates ----
    low = {}
    for sr3 in (44100, 48000):
        v = synth_vowel(200.0, [(700, 110, 1.0), (1200, 150, 0.7)], sr=sr3)
        p = f"{OUT}/low200_{sr3}.wav"; write(p, v, sr3)
        for mode in (0, 1):
            o = f"{OUT}/low200_{sr3}_{mode}.wav"
            render(p, o, 0, mode)
            sr4, y = read(o)
            low[f"{sr3}_{'low' if mode else 'quality'}"] = round(f0_track(y, sr4), 1)
    results["low_note_200Hz_target_220"] = low

    # ---- latency at 44.1 and 48 kHz ----
    lat = {}
    for sr in (44100, 48000):
        for low in (0, 1):
            r = subprocess.run([RENDER, "--latency", PLUGIN, str(sr)], capture_output=True, text=True)
            lat[f"{sr}_{'low' if low else 'quality'}"] = r.stdout.strip()
    results["latency"] = lat

    print(json.dumps(results, indent=1))

if __name__ == "__main__":
    main()
