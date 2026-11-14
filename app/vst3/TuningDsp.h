#pragma once
// Pitch detection and DSP helpers for the MegaTune reconstruction.
#include <algorithm>
#include <cmath>
#include <vector>
#include <atomic>

namespace megatune {

// read-only, test/diagnostic trace of the most recent decision target pitch class
// (-1 = no target). Written on the audio thread, read by tests; never drives DSP.
extern std::atomic<int> lastTargetPc;
extern std::atomic<unsigned> debugMask;
extern std::atomic<int> debugNearest, debugMidi, debugKey;

struct YinPitch {
    float hz = 0.0f;          // 0 when unvoiced
    float aperiodicity = 1.0f; // 0 = perfectly periodic, 1 = aperiodic
    bool voiced = false;
};

// Reusable detector scratch. Prepare it off the audio thread so a steady-state
// analysis window does not allocate.
struct YinWorkspace {
    std::vector<float> difference, normalised;
    void ensure (int size)
    {
        if ((int) difference.size() < size)
        {
            difference.resize ((size_t) size);
            normalised.resize ((size_t) size);
        }
    }
};

// YIN (de Cheveigne & Kawahara) with cumulative-mean-normalised difference,
// absolute threshold and parabolic lag interpolation. x[0] is the newest sample.
inline YinPitch detectYin (const float* x, int n, float sampleRate,
                           float minHz, float maxHz, float threshold,
                           YinWorkspace& workspace)
{
    YinPitch out;
    if (n <= 0 || sampleRate <= 0.0f || minHz <= 0.0f || maxHz <= minHz) return out;

    const int minTau = std::max (2, (int) std::floor (sampleRate / maxHz));
    const int maxTau = std::min (n / 2, (int) std::ceil (sampleRate / minHz));
    if (maxTau <= minTau + 2) return out;

    const int w = n - maxTau;
    if (w < 64) return out;

    workspace.ensure (maxTau + 1);
    auto& d = workspace.difference;
    auto& dn = workspace.normalised;
    for (int tau = 1; tau <= maxTau; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < w; ++j)
        {
            const float diff = x[j] - x[j + tau];
            sum += diff * diff;
        }
        d[(size_t) tau] = sum;
    }

    float running = 0.0f;
    for (int tau = 1; tau <= maxTau; ++tau)
    {
        running += d[(size_t) tau];
        dn[(size_t) tau] = running > 1.0e-12f ? d[(size_t) tau] * (float) tau / running : 1.0f;
    }

    int best = -1;
    for (int tau = minTau; tau <= maxTau; ++tau)
    {
        if (dn[(size_t) tau] < threshold)
        {
            while (tau + 1 <= maxTau && dn[(size_t) tau + 1] < dn[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        float m = 1.0e9f;
        for (int tau = minTau; tau <= maxTau; ++tau)
            if (dn[(size_t) tau] < m) { m = dn[(size_t) tau]; best = tau; }
        if (best < 0 || m > 0.30f) return out;
    }

    const float centre = dn[(size_t) best];
    const float left   = best > minTau ? dn[(size_t) best - 1] : centre;
    const float right  = best < maxTau ? dn[(size_t) best + 1] : centre;
    const float denom  = left - 2.0f * centre + right;
    float offset = std::abs (denom) > 1.0e-9f ? 0.5f * (left - right) / denom : 0.0f;
    offset = std::min (0.5f, std::max (-0.5f, offset));

    const float tau = (float) best + offset;
    if (tau <= 1.0f) return out;

    out.hz = sampleRate / tau;
    out.aperiodicity = std::min (1.0f, std::max (0.0f, centre));
    out.voiced = true;
    return out;
}

// 3-point median, used to stop single-frame octave slips reaching the target.
inline float median3 (float a, float b, float c)
{
    return a + b + c - std::min (a, std::min (b, c)) - std::max (a, std::max (b, c));
}

} // namespace megatune

namespace megatune {

inline float hermite4 (float y0, float y1, float y2, float y3, float t)
{
    return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3
                                           + t * (3.0f * (y1 - y2) + y3 - y0)));
}

inline int nextPow2 (int v) { int n = 1; while (n < v) n <<= 1; return n; }

// Refine a coarse period estimate on the full-rate signal. x[0] is the newest
// sample and at least n + coarse + radius + 2 samples must be readable.
// Returns the period (in samples) minimising the normalised squared difference,
// with parabolic interpolation, or the coarse value when the fit is degenerate.
inline float refinePeriod (const float* x, int n, float coarse, int radius)
{
    const int centre = (int) std::lround (coarse);
    const int lo = std::max (2, centre - radius), hi = centre + radius;
    const int count = hi - lo + 1;
    if (count < 3 || count > 64) return coarse;
    float d[64];
    int best = 0;
    for (int q = 0; q < count; ++q)
    {
        const int tau = lo + q;
        float diff = 0.0f, energy = 1.0e-12f;
        for (int j = 0; j < n; ++j)
        {
            const float a = x[j], b = x[j + tau], e = a - b;
            diff += e * e; energy += a * a + b * b;
        }
        d[q] = diff / energy;
        if (d[q] < d[best]) best = q;
    }
    if (best == 0 || best == count - 1) return coarse;
    const float denom = d[best - 1] - 2.0f * d[best] + d[best + 1];
    float offset = std::abs (denom) > 1.0e-12f ? 0.5f * (d[best - 1] - d[best + 1]) / denom : 0.0f;
    offset = std::min (0.5f, std::max (-0.5f, offset));
    return (float) (lo + best) + offset;
}

// Streaming stereo TD-PSOLA.
//
// Output grains live on the output timeline and advance by period/ratio. Each
// grain is a two-period Hann window cut from the source around a pitch mark.
// Marks advance by whole periods from the previous mark and are then aligned to
// it by normalised cross-correlation on the mono sum (sub-sample, parabolic),
// so consecutive grains stay phase coherent even when the detected period is
// slightly off or drifting. Both channels share the same marks, which keeps the
// stereo image intact. Overlap-add is normalised by the summed window weight.
//
// The output is cross-faded against the latency-aligned dry signal: unvoiced
// material and ratio == 1 pass through bit-exactly, and there are no gaps when
// the shifter engages.
class StereoPsola
{
public:
    void prepare (int latencySamples, double sampleRate)
    {
        latency = capacity = std::max (1, latencySamples);
        maxPeriod = std::max (64.0, sampleRate / 55.0);
        minPeriod = std::max (8.0, sampleRate / 1400.0);
        accSize = nextPow2 ((int) (4.0 * maxPeriod) + 64);
        ringSize = nextPow2 (latency + (int) (8.0 * maxPeriod) + 64);
        for (int c = 0; c < 2; ++c)
        {
            ring[c].assign ((size_t) ringSize, 0.0f);
            acc[c].assign ((size_t) accSize, 0.0f);
        }
        wsum.assign ((size_t) accSize, 0.0f);
        fadeIn = (float) (1.0 / (0.004 * sampleRate));
        fadeOut = (float) (1.0 / (0.012 * sampleRate));
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (ring[c].begin(), ring[c].end(), 0.0f);
            std::fill (acc[c].begin(), acc[c].end(), 0.0f);
        }
        std::fill (wsum.begin(), wsum.end(), 0.0f);
        written = 0; nextCenter = 0.0; lastMark = 0.0;
        period = std::min (maxPeriod, std::max (minPeriod, 200.0));
        ratio = 1.0; mix = 0.0f; active = false; haveMark = false; filled = false;
    }

    // Change the dry/wet delay without reallocating (never above the prepared one).
    void setLatency (int latencySamples)
    {
        const int l = std::max (1, std::min (capacity, latencySamples));
        if (l == latency) return;
        latency = l; active = false; haveMark = false; mix = 0.0f;
        for (int c = 0; c < 2; ++c) std::fill (acc[c].begin(), acc[c].end(), 0.0f);
        std::fill (wsum.begin(), wsum.end(), 0.0f);
    }

    void setPeriod (float sourcePeriod)
    {
        if (sourcePeriod >= minPeriod && sourcePeriod <= maxPeriod) period = sourcePeriod;
    }
    void setRatio (float r) { ratio = std::max (0.25f, std::min (4.0f, r)); }

    // in/out: two samples (L, R). `voiced` selects the shifted path.
    void process (const float* in, float* out, bool voiced)
    {
        const long long t = written;                       // index of this sample
        const int rm = ringSize - 1, am = accSize - 1;
        ring[0][(size_t) (t & rm)] = in[0];
        ring[1][(size_t) (t & rm)] = in[1];
        ++written;

        float dry[2] = { 0.0f, 0.0f };
        if (t >= latency)
        {
            dry[0] = ring[0][(size_t) ((t - latency) & rm)];
            dry[1] = ring[1][(size_t) ((t - latency) & rm)];
        }

        const bool want = voiced && std::abs (ratio - 1.0) >= 1.0e-4;
        if (want && ! active)
        {
            active = true; haveMark = false; filled = false;
            nextCenter = (double) t + period;
        }
        else if (! want && active && mix <= 0.0f)
        {
            active = false;
            for (int c = 0; c < 2; ++c) std::fill (acc[c].begin(), acc[c].end(), 0.0f);
            std::fill (wsum.begin(), wsum.end(), 0.0f);
        }

        float wet[2] = { dry[0], dry[1] };
        if (active)
        {
            int guard = 0;
            while (nextCenter - period <= (double) (t + 1) && guard++ < 16)
            {
                addGrain (nextCenter, t);
                nextCenter += period / ratio;
            }
            const size_t pos = (size_t) (t & am);
            const float w = wsum[pos];
            for (int c = 0; c < 2; ++c)
            {
                const float a = acc[c][pos];
                // Where grains overlap (upward shifts) the sum is divided by the window sum.
                // Where they do not reach each other (downward shifts) it is left alone:
                // dividing there re-inflates the neighbouring periods the window removed and
                // rebuilds the original pitch. Until the first grains arrive, the remainder of
                // the window is filled with dry signal so the shifter engages without a gap.
                wet[c] = w >= 1.0f ? a / w : (filled ? a : a + (1.0f - w) * dry[c]);
                acc[c][pos] = 0.0f;
            }
            if (w >= 0.5f) filled = true;
            wsum[pos] = 0.0f;
        }

        mix = want ? std::min (1.0f, mix + fadeIn) : std::max (0.0f, mix - fadeOut);
        out[0] = dry[0] + mix * (wet[0] - dry[0]);
        out[1] = dry[1] + mix * (wet[1] - dry[1]);
    }

private:
    float mono (long long a) const
    {
        const size_t i = (size_t) (a & (ringSize - 1));
        return ring[0][i] + ring[1][i];
    }

    // Align a candidate mark to the previous one by normalised cross-correlation.
    double refine (double candidate, double reference, int p, long long newest) const
    {
        const long long cand = (long long) std::llround (candidate);
        const long long ref = (long long) std::llround (reference);
        const long long oldest = written - ringSize + 2;
        int radius = std::max (2, std::min (p / 4, 240));
        radius = (int) std::min<long long> (radius, newest - cand);
        if (radius < 1 || ref - p < oldest || cand - radius - p < oldest) return candidate;
        const int step = p > 256 ? 2 : 1;
        // The predicted position gets a mild preference when choosing the whole-sample lag, so
        // noise cannot make the marks wander. The sub-sample part is then fitted on the raw
        // correlation: fitting it on the biased scores pulls every mark towards the prediction
        // and detunes the output whenever the source pitch is moving.
        float raw[2 * 240 + 1];
        float bestScore = 0.0f; int bestDelta = 0; bool found = false;
        for (int delta = -radius; delta <= radius; ++delta)
        {
            float cross = 0.0f, energy = 1.0e-9f;
            // Tapered window: with a rectangular one, whatever sits at the two edges moves
            // the peak, which jitters the marks and biases a pure tone.
            const float slope = 1.0f / (float) p;
            for (int j = -p; j < p; j += step)
            {
                const float w = 1.0f - slope * (float) std::abs (j);
                const float a = mono (cand + delta + j);
                cross += w * a * mono (ref + j);
                energy += w * a * a;
            }
            const float value = cross / std::sqrt (energy);
            raw[delta + radius] = value;
            const float score = value * (1.0f - 0.08f * (float) std::abs (delta) / (float) radius);
            if (score > bestScore) { bestScore = score; bestDelta = delta; found = true; }
        }
        if (! found) return candidate;
        // The preference can pick the lag next to the true peak; step onto the raw peak so the
        // sub-sample fit below is not clamped half a sample short.
        if (bestDelta < radius && raw[bestDelta + radius + 1] > raw[bestDelta + radius]) ++bestDelta;
        else if (bestDelta > -radius && raw[bestDelta + radius - 1] > raw[bestDelta + radius]) --bestDelta;
        float frac = 0.0f;
        if (bestDelta > -radius && bestDelta < radius)
        {
            const float y0 = raw[bestDelta + radius - 1], y1 = raw[bestDelta + radius], y2 = raw[bestDelta + radius + 1];
            const float denom = y0 - 2.0f * y1 + y2;
            if (denom < -1.0e-12f) frac = std::min (0.5f, std::max (-0.5f, 0.5f * (y0 - y2) / denom));
        }
        // The lag is measured between windows at whole-sample positions, so it has to be
        // added to the reference's true (fractional) position. Returning cand + delta alone
        // drops the reference's fraction on every grain and detunes the output by up to
        // half a sample per period.
        return reference + (double) (cand - ref + bestDelta) + (double) frac;
    }

    void addGrain (double center, long long t)
    {
        const double pd = std::max (minPeriod, std::min (maxPeriod, period));
        const int p = (int) std::ceil (pd);
        const long long newest = written - 3 - p;         // mark + p + 2 must be written
        const double target = center - (double) latency;

        double mark;
        if (! haveMark)
        {
            mark = std::min (target, (double) newest);
            haveMark = true;
        }
        else
        {
            const double steps = std::round ((target - lastMark) / pd);
            mark = lastMark + steps * pd;
            while (mark > (double) newest) mark -= pd;
            if (std::abs (mark - lastMark) > 0.5 && std::abs (mark - lastMark) < 8.0 * pd)
                mark = std::min ((double) newest, refine (mark, lastMark, p, newest));
        }
        lastMark = mark;

        const long long k0 = std::max ((long long) std::ceil (center - pd), t);
        const long long k1 = (long long) std::floor (center + pd);
        const double s0 = mark - center + (double) k0;
        const long long a0 = (long long) std::floor (s0);
        const float frac = (float) (s0 - (double) a0);
        const long long oldest = written - ringSize + 2;
        const int rm = ringSize - 1, am = accSize - 1;
        const double winScale = 3.14159265358979323846 / pd;
        for (long long k = k0; k <= k1; ++k)
        {
            const long long a = a0 + (k - k0);
            if (a - 1 < 0 || a - 1 < oldest || a + 2 >= written) continue;
            const float win = 0.5f + 0.5f * (float) std::cos (winScale * ((double) k - center));
            const size_t pos = (size_t) (k & am);
            for (int c = 0; c < 2; ++c)
            {
                const auto& r = ring[c];
                acc[c][pos] += win * hermite4 (r[(size_t) ((a - 1) & rm)], r[(size_t) (a & rm)],
                                               r[(size_t) ((a + 1) & rm)], r[(size_t) ((a + 2) & rm)], frac);
            }
            wsum[pos] += win;
        }
    }

    std::vector<float> ring[2], acc[2], wsum;
    int ringSize = 16384, accSize = 4096, latency = 2048, capacity = 2048;
    long long written = 0;
    double nextCenter = 0.0, period = 200.0, ratio = 1.0, lastMark = 0.0;
    double maxPeriod = 872.0, minPeriod = 34.0;
    float mix = 0.0f, fadeIn = 0.005f, fadeOut = 0.002f;
    bool active = false, haveMark = false, filled = false;
};

// Vocal doubler and stereo widener.
//
// Two extra voices are read from a short delay line fed with the (tuned) mono
// sum. Each voice has its own base delay and a slow two-sine delay modulation,
// which gives it an independent, drifting detune of a few cents and a timing
// offset of 12-30 ms, the two cues of a real double take.
//
//   Doubler  blends the two voices in (dry/wet, up to an equal-power double).
//   Width    pans the doubled voices apart (centre -> hard L/R) and, on its own,
//            adds the voice difference as a pure side signal. That widens a
//            mono vocal and cancels completely when the output is summed to
//            mono. Any stereo content already present is widened as well.
class StereoDoubler
{
public:
    void prepare (double sampleRate)
    {
        rate = sampleRate;
        size = nextPow2 ((int) (0.060 * sampleRate) + 16);
        line.assign ((size_t) size, 0.0f);
        smooth = (float) (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));
        // one-pole high-pass (~120 Hz) on the voices keeps low-end mono and tight
        hpCoef = (float) std::exp (-2.0 * 3.14159265358979323846 * 120.0 / sampleRate);
        reset();
    }

    void reset()
    {
        std::fill (line.begin(), line.end(), 0.0f);
        write = 0; amount = 0.0f; width = 0.0f;
        for (auto& v : voices) { v.phaseA = v.startA; v.phaseB = v.startB; v.lp = 0.0f; }
    }

    // l/r in-out. amount and width are 0..1. With stereo == false only l is used.
    void process (float& l, float& r, float amountTarget, float widthTarget, bool stereo)
    {
        const int mask = size - 1;
        const float mid = stereo ? 0.5f * (l + r) : l;
        line[(size_t) write] = mid;

        amount += (amountTarget - amount) * smooth;
        width += (widthTarget - width) * smooth;

        float v[2];
        for (int i = 0; i < 2; ++i)
        {
            auto& vc = voices[i];
            const double ms = vc.baseMs + vc.depthA * std::sin (vc.phaseA) + vc.depthB * std::sin (vc.phaseB);
            vc.phaseA += twoPi * vc.rateA / rate; if (vc.phaseA > twoPi) vc.phaseA -= twoPi;
            vc.phaseB += twoPi * vc.rateB / rate; if (vc.phaseB > twoPi) vc.phaseB -= twoPi;
            const double pos = (double) write - ms * 0.001 * rate;
            const double fl = std::floor (pos);
            const int a = (int) fl; const float t = (float) (pos - fl);
            const float y = hermite4 (line[(size_t) ((a - 1) & mask)], line[(size_t) (a & mask)],
                                      line[(size_t) ((a + 1) & mask)], line[(size_t) ((a + 2) & mask)], t);
            vc.lp = y + hpCoef * (vc.lp - y);              // low-pass state
            v[i] = y - vc.lp;                              // high-passed voice
        }
        write = (write + 1) & mask;

        if (amount < 1.0e-5f && width < 1.0e-5f) return;   // exact pass-through when off

        // Equal-power blend: the dry voice falls to 0.70 while the two doubles rise, so the
        // level stays put across the whole control instead of dipping in the middle.
        const float blend = 0.795f * amount;
        const float dryGain = std::cos (blend);
        const float wet = 1.04f * std::sin (blend);
        if (! stereo)
        {
            l = dryGain * l + wet * 0.7071f * (v[0] + v[1]);
            return;
        }
        const float angle = 0.78539816f * width;            // 0 = centre, pi/4 = hard pan
        const float near = std::cos (0.78539816f - angle), far = std::sin (0.78539816f - angle);
        const float side = 0.42f * width * (1.0f - 0.8f * amount) * (v[0] - v[1]);
        const float s = 0.5f * (l - r) * (1.0f + 0.6f * width);
        const float wl = dryGain * (mid + s) + wet * (near * v[0] + far * v[1]) + side;
        const float wr = dryGain * (mid - s) + wet * (far * v[0] + near * v[1]) - side;
        // Widening adds side energy, which used to make the signal up to 1.3 dB louder. The
        // gain below undoes exactly the energy these terms add for a source whose doubled
        // voices are as loud as it is and uncorrelated with it (true of a voice; a pure tone
        // can differ). It depends only on the two controls, so nothing rides the signal and
        // the mono sum stays the dry signal times one number.
        const float k = 0.42f * width * (1.0f - 0.8f * amount);
        const float base = dryGain * dryGain + voicePower * wet * wet;
        const float added = voicePower * (2.0f * wet * k * (near - far) + 2.0f * k * k);
        const float gain = std::sqrt (base / (base + added));
        l = wl * gain; r = wr * gain;
    }

private:
    struct Voice
    {
        double baseMs, depthA, rateA, depthB, rateB, startA, startB;
        double phaseA = 0.0, phaseB = 0.0; float lp = 0.0f;
    };
    static constexpr double twoPi = 6.283185307179586;
    // depth (ms) x rate (Hz): peak detune = 1731 * 2*pi * rate * depth/1000 cents,
    // i.e. roughly +-7 and +-5 cents per component, never in step between voices.
    Voice voices[2] = { { 15.0, 2.1, 0.31, 0.55, 0.83, 0.0, 1.3 },
                        { 24.0, 2.6, 0.23, 0.65, 0.67, 2.1, 4.0 } };
    std::vector<float> line;
    double rate = 48000.0;
    int size = 4096, write = 0;
    float amount = 0.0f, width = 0.0f, smooth = 0.001f, hpCoef = 0.98f;
    static constexpr float voicePower = 0.96f;   // doubled voice vs source, after its 120 Hz high-pass
};

} // namespace megatune
