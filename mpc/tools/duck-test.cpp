// Filterbank test: flat reconstruction and ducking depth, without latency.
#include "dsp/FilterDucker.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace qdssd;

int main()
{
    const float sr = 48000.0f;
    const int n = 20000;
    std::vector<float> in ((size_t) n), out ((size_t) n);
    for (int i = 0; i < n; ++i)
        in[(size_t) i] = 0.4f * std::sin (2.0f * 3.14159265f * 1000.0f * (float) i / sr);

    int failures = 0;
    auto check = [&] (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
        if (! ok) ++failures;
    };

    FilterDucker d;
    d.prepare (sr);
    d.setParams (0.0f, 5.0f, 50.0f, 1.0f, false);
    float w[FilterDucker::Bands] = {};
    d.setBandWeights (w);
    d.process (in.data(), in.data(), out.data(), out.data(), n);

    double err = 0.0, peak = 0.0;
    for (int i = 2000; i < n; ++i)
    {
        err = std::max (err, (double) std::fabs (out[(size_t) i] - in[(size_t) i]));
        peak = std::max (peak, (double) std::fabs (in[(size_t) i]));
    }
    check (err < 1e-5, "flat reconstruction with gains 1");

    for (int b = 0; b < FilterDucker::Bands; ++b)
        w[b] = 1.0f;
    d.reset();
    d.setParams (1.0f, 5.0f, 50.0f, 1.0f, false);
    d.setBandWeights (w);
    d.process (in.data(), in.data(), out.data(), out.data(), n);

    double rmsIn = 0.0, rmsOut = 0.0;
    for (int i = n / 2; i < n; ++i)
    {
        rmsIn += (double) in[(size_t) i] * in[(size_t) i];
        rmsOut += (double) out[(size_t) i] * out[(size_t) i];
    }
    const double ratioDb = 10.0 * std::log10 ((rmsOut + 1e-12) / (rmsIn + 1e-12));
    std::printf ("full duck: %.1f dB\n", ratioDb);
    check (ratioDb < -40.0, "all bands ducked to silence");

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "duck-test: PASS" : "duck-test: FAIL", failures);
    return failures == 0 ? 0 : 1;
}
