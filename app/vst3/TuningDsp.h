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

// Streaming monophonic TD-PSOLA.
// Output marks live on the output timeline and advance by sourcePeriod/ratio
// (fractional, kept in a double). Each grain uses the nearest source pitch mark
// to (outputMark - latency), so source marks repeat or skip as the ratio
// requires, instead of the read head being clamped. Overlap-add is normalised
// by the accumulated window weight.
class Psola
{
public:
    void prepare (int latencySamples)
    {
        ring.assign ((size_t) N, 0.0f);
        acc.assign ((size_t) ACC, 0.0f);
        wsum.assign ((size_t) ACC, 0.0f);
        latency = latencySamples;
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        std::fill (acc.begin(), acc.end(), 0.0f);
        std::fill (wsum.begin(), wsum.end(), 0.0f);
        w = 0; written = 0; outIdx = 0; nextCenter = 0.0;
        period = 200.0; ratio = 1.0; lastMark = 0; haveMark = false;
        started = false; voiced = false;
    }

    void setTarget (float sourcePeriod, float r, bool isVoiced)
    {
        if (sourcePeriod > 24.0f && sourcePeriod < 1500.0f) period = sourcePeriod;
        ratio = std::max (0.25f, std::min (4.0f, r));
        voiced = isVoiced;
    }

    float process (float in)
    {
        ring[(size_t) w] = in;
        if (++w == N) w = 0;
        ++written;

        const bool identity = std::abs (ratio - 1.0f) < 1.0e-4f;
        if (identity != wasIdentity) { started = false; haveMark = false; wasIdentity = identity; }
        if (identity)
        {
            ++outIdx;                              // keep the output timeline moving
            if (written <= latency) return 0.0f;
            const int idx = ((w - latency) % N + N) % N;
            return ring[(size_t) idx];            // exact delayed input at ratio 1
        }

        if (! started)
        {
            if (written < latency + (int) period + 8) { ++outIdx; return 0.0f; }
            started = true;
            nextCenter = (double) std::max ((long long) latency + (long long) period, outIdx + (long long) period);
            haveMark = false;
        }

        int guard = 0;
        while (nextCenter - period <= (double) outIdx && guard++ < 8)
        {
            addGrain (nextCenter);   // grains always scheduled; voicing only drives the mix
            nextCenter += period / ratio;
        }

        const int pos = (int) (outIdx % ACC);
        const float v = wsum[(size_t) pos] > 1.0e-6f ? acc[(size_t) pos] / wsum[(size_t) pos] : 0.0f;
        acc[(size_t) pos] = 0.0f; wsum[(size_t) pos] = 0.0f;
        ++outIdx;
        return v;
    }

private:
    void addGrain (double center)
    {
        const int p = (int) std::max (24.0, std::min (1500.0, period));
        const double target = center - (double) latency;

        double mark;
        if (! haveMark) { mark = target; haveMark = true; }
        else
        {
            const double steps = std::round ((target - lastMark) / period);
            mark = lastMark + steps * period;          // fractional: no per-grain drift
        }
        lastMark = mark;
        const long long markI = (long long) std::llround (mark);

        const long long oldest = written - (N - 2);
        for (int k = 0; k < 2 * p; ++k)
        {
            const long long a = markI - p + k;
            if (a < 0 || a >= written || a < oldest) continue;
            const long long outPos = (long long) std::llround (center) - p + k;
            if (outPos < outIdx) continue;
            const float win = 0.5f - 0.5f * std::cos (6.2831853f * (k + 0.5f) / (float) (2 * p));
            const int pos = (int) (((outPos % ACC) + ACC) % ACC);
            acc[(size_t) pos] += ring[(size_t) (a % N)] * win;
            wsum[(size_t) pos] += win;
        }
    }

    static const int N = 16384;
    static const int ACC = 4096;
    std::vector<float> ring, acc, wsum;
    int w = 0, latency = 2048;
    long long written = 0, outIdx = 0;
    double lastMark = 0.0;
    bool wasIdentity = false;
    double nextCenter = 0.0, period = 200.0, ratio = 1.0;
    bool started = false, haveMark = false, voiced = false;
};

} // namespace megatune
