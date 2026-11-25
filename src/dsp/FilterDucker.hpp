// Zero-latency spectral ducker: 16 log bands built from complementary
// Butterworth splits. Each split is a 2nd-order lowpass with HP = x - LP, so
// the tree reconstructs the input exactly when all band gains are 1 and the
// whole path has no latency at all.
#pragma once

#include "Bus.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace qdssd
{

class FilterDucker
{
public:
    static constexpr int Bands = 16;
    static constexpr int Levels = 4;   // 1 -> 2 -> 4 -> 8 -> 16

    void prepare (double sampleRate)
    {
        sr = (float) std::max (8000.0, sampleRate);

        const float fLo = 60.0f;
        const float fHi = 16000.0f;
        for (int i = 0; i <= Bands; ++i)
            edges[(size_t) i] = fLo * std::pow (fHi / fLo, (float) i / (float) Bands);

        int nodes = 1;
        for (int level = 0; level < Levels; ++level)
        {
            const int mid = 1 << (Levels - 1 - level);
            for (int j = 0; j < nodes; ++j)
            {
                const int lo = j * (Bands / nodes);
                const int hi = (j + 1) * (Bands / nodes);
                (void) hi;
                setLowpass (filters[level][j], edges[(size_t) (lo + mid)]);
            }
            nodes <<= 1;
        }
        reset();
    }

    void reset()
    {
        for (auto& level : filters)
            for (auto& f : level)
                f.reset();
        gains.fill (1.0f);
        mixSm = mix;
        deltaSm = delta ? 1.0f : 0.0f;
    }

    // weights: 0..1 per band, 1 = full duck.
    void setBandWeights (const float* w)
    {
        for (int b = 0; b < Bands; ++b)
            target[(size_t) b] = 1.0f - depth * (w ? w[b] : 0.0f);
    }

    void setParams (float depthLin, float attackMs, float releaseMs,
                    float mixValue, bool deltaOn)
    {
        depth = std::max (0.0f, std::min (1.0f, depthLin));
        mix = std::max (0.0f, std::min (1.0f, mixValue));
        delta = deltaOn;
        attack = 1.0f - std::exp (-1.0f / std::max (1.0f, 0.001f * attackMs * sr));
        release = 1.0f - std::exp (-1.0f / std::max (1.0f, 0.001f * releaseMs * sr));
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = inL ? inL[i] : 0.0f;
            const float r = inR ? inR[i] : l;

            mixSm += 0.002f * (mix - mixSm);
            deltaSm += 0.002f * ((delta ? 1.0f : 0.0f) - deltaSm);

            float yl = 0.0f;
            float yr = 0.0f;
            splitAndSum (l, r, yl, yr);

            const float outl = (l + mixSm * (yl - l)) * (1.0f - deltaSm) + (l - yl) * deltaSm;
            const float outr = (r + mixSm * (yr - r)) * (1.0f - deltaSm) + (r - yr) * deltaSm;

            outL[i] = outl;
            if (outR)
                outR[i] = outr;
        }
    }

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;

        void reset() { z1 = z2 = 0.0f; }

        inline float lowpass (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void setLowpass (Biquad& f, float freq)
    {
        const float w0 = 2.0f * 3.14159265358979323846f * std::min (freq, 0.45f * sr) / sr;
        const float c = std::cos (w0);
        const float s = std::sin (w0);
        const float alpha = s / (2.0f * 0.70710678f);
        const float a0 = 1.0f + alpha;
        f.b0 = (1.0f - c) * 0.5f / a0;
        f.b1 = (1.0f - c) / a0;
        f.b2 = f.b0;
        f.a1 = -2.0f * c / a0;
        f.a2 = (1.0f - alpha) / a0;
        f.reset();
    }

    inline void splitAndSum (float l, float r, float& yl, float& yr)
    {
        float curL[Bands];
        float curR[Bands];
        float nextL[Bands];
        float nextR[Bands];
        curL[0] = l;
        curR[0] = r;

        int count = 1;
        for (int level = 0; level < Levels; ++level)
        {
            for (int j = 0; j < count; ++j)
            {
                const float lpL = filters[level][j].lowpass (curL[j]);
                const float lpR = filters[level][j].lowpass (curR[j]);
                nextL[2 * j] = lpL;
                nextL[2 * j + 1] = curL[j] - lpL;
                nextR[2 * j] = lpR;
                nextR[2 * j + 1] = curR[j] - lpR;
            }
            for (int j = 0; j < count * 2; ++j)
            {
                curL[j] = nextL[j];
                curR[j] = nextR[j];
            }
            count <<= 1;
        }

        yl = 0.0f;
        yr = 0.0f;
        for (int b = 0; b < Bands; ++b)
        {
            const float targetGain = target[(size_t) b];
            const float a = targetGain < gains[(size_t) b] ? attack : release;
            gains[(size_t) b] += a * (targetGain - gains[(size_t) b]);
            yl += curL[b] * gains[(size_t) b];
            yr += curR[b] * gains[(size_t) b];
        }
    }

    std::array<std::array<Biquad, 8>, Levels> filters {};
    std::array<float, Bands + 1> edges {};
    std::array<float, Bands> target { [] { std::array<float, Bands> a {}; a.fill (1.0f); return a; }() };
    std::array<float, Bands> gains { [] { std::array<float, Bands> a {}; a.fill (1.0f); return a; }() };
    float sr = 44100.0f;
    float depth = 1.0f;
    float mix = 1.0f;
    bool delta = false;
    float attack = 0.01f, release = 0.001f;
    float mixSm = 1.0f, deltaSm = 0.0f;
};

} // namespace qdssd
