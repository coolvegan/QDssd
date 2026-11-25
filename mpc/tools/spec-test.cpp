#include "dsp/SpectralEngine.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace qdssd;

static double sineAmp (const std::vector<float>& x, int start, int n, double f, double sr)
{
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i)
    {
        const double p = 2.0 * 3.14159265358979323846 * f * (double) (start + i) / sr;
        re += x[(size_t) (start + i)] * std::cos (p);
        im += x[(size_t) (start + i)] * std::sin (p);
    }
    return 2.0 * std::sqrt (re * re + im * im) / (double) n;
}

static double rms (const std::vector<float>& x, int start, int n)
{
    double s = 0;
    for (int i = 0; i < n; ++i)
        s += (double) x[(size_t) (start + i)] * x[(size_t) (start + i)];
    return std::sqrt (s / (double) n);
}

int main()
{
    const double sr = 48000.0;
    const int N = 1024;
    const int frames = (int) (sr * 3.0);

    std::vector<float> in ((size_t) frames), out ((size_t) frames);
    unsigned seed = 12345;
    for (int i = 0; i < frames; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const double noise = ((double) (seed >> 8) / 16777216.0 - 0.5) * 0.05;
        const double tone = 0.5 * std::sin (2.0 * 3.14159265358979323846 * 1000.0 * i / sr);
        in[(size_t) i] = (float) (tone + noise);
    }

    SpectralParams p;
    p.depthDb = 0.0f;
    p.mix = 1.0f;
    p.attackMs = 5.0f;
    p.releaseMs = 50.0f;
    p.selectivityDb = 3.0f;

    SpectralEngine<N> e;
    e.prepare (sr);
    e.setParams (p);

    int i = 0;
    while (i < frames)
    {
        const int n = std::min (256, frames - i);
        e.process (in.data() + i, in.data() + i, out.data() + i, out.data() + i, n);
        i += n;
    }

    const int start = N + (int) (sr * 1.0);
    const int len = (int) (sr * 1.0);
    const double ampIn = sineAmp (in, start, len, 1000.0, sr);
    const double ampOut0 = sineAmp (out, start, len, 1000.0, sr);
    std::printf ("depth 0 dB : in %.4f out %.4f (%.2f dB)\n", ampIn, ampOut0, 20 * std::log10 (ampOut0 / ampIn));
    std::printf ("rms in %.4f out %.4f\n", rms (in, start, len), rms (out, start, len));

    // Now with suppression.
    SpectralEngine<N> e2;
    e2.prepare (sr);
    p.depthDb = 12.0f;
    p.ratio = 6.0f;
    e2.setParams (p);

    std::vector<float> out2 ((size_t) frames);
    i = 0;
    while (i < frames)
    {
        const int n = std::min (256, frames - i);
        e2.process (in.data() + i, in.data() + i, out2.data() + i, out2.data() + i, n);
        i += n;
    }

    const double ampOut = sineAmp (out2, start, len, 1000.0, sr);
    std::printf ("depth 12 dB: in %.4f out %.4f (%.2f dB)\n", ampIn, ampOut, 20 * std::log10 (ampOut / ampIn));

    const bool unity = std::fabs (20 * std::log10 (ampOut0 / ampIn)) < 1.0;
    const bool suppressed = 20 * std::log10 (ampOut / ampIn) < -6.0;
    std::printf ("%s\n", (unity && suppressed) ? "spec-test: PASS" : "spec-test: FAIL");
    return (unity && suppressed) ? 0 : 1;
}
