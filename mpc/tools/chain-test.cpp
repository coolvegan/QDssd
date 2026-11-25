// Mask + ducker test: analyser output -> band weights -> filterbank.
// The shared-memory bus itself is covered by bus-test.cpp.
#include "dsp/FilterDucker.hpp"
#include "dsp/KeyMask.hpp"
#include "dsp/NoteAnalyzer.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace qdssd;

static double amp (const std::vector<float>& x, int start, int n, double f, double sr)
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

int main()
{
    const double sr = 48000.0;
    const int total = (int) (sr * 3.0);
    std::vector<float> vocal ((size_t) total), beat ((size_t) total), out ((size_t) total);

    for (int i = 0; i < total; ++i)
    {
        const double t = (double) i / sr;
        const double v = 0.30 * std::sin (2.0 * 3.14159265358979323846 * 220.0 * t)
                       + 0.20 * std::sin (2.0 * 3.14159265358979323846 * 330.0 * t)
                       + 0.15 * std::sin (2.0 * 3.14159265358979323846 * 440.0 * t);
        vocal[(size_t) i] = (float) v;
        beat[(size_t) i] = (float) (0.4 * std::sin (2.0 * 3.14159265358979323846 * 60.0 * t) + v);
    }

    NoteAnalyzer analyzer;
    analyzer.prepare (sr);
    analyzer.setGateDb (-55.0f);

    BusFrame frame;
    int pos = 0;
    while (pos < total)
    {
        const int n = std::min (256, total - pos);
        analyzer.process (vocal.data() + pos, vocal.data() + pos, n, frame);
        pos += n;
    }

    double prevDb = 1.0;
    bool ok = true;
    for (int p = 0; p < 3; ++p)
    {
        const IntensityProfile profile = intensityProfile (p);
        float w[FilterDucker::Bands] = {};
        KeyMask::bandWeights (frame, w, (float) sr, profile);

        float sum = 0.0f;
        for (float x : w) sum += x;

        FilterDucker ducker;
        ducker.prepare ((float) sr);
        ducker.setParams (1.0f - std::pow (10.0f, -(24.0f * profile.depthScale) / 20.0f), 10.0f, 150.0f, 1.0f, false);
        ducker.setBandWeights (w);
        std::fill (out.begin(), out.end(), 0.0f);
        ducker.process (beat.data(), beat.data(), out.data(), out.data(), total);

        const int start = (int) (sr * 1.5);
        const int len = (int) (sr * 0.5);
        const double db220 = 20.0 * std::log10 (amp (out, start, len, 220.0, sr) / amp (beat, start, len, 220.0, sr));
        const double db60 = 20.0 * std::log10 (amp (out, start, len, 60.0, sr) / amp (beat, start, len, 60.0, sr));
        std::printf ("%-7s weights %.1f   220 Hz %6.2f dB   60 Hz %6.2f dB\n", profile.name, sum, db220, db60);

        if (db60 < -1.5 || db60 > 1.5) ok = false;
        if (p > 0 && db220 >= prevDb) ok = false;   // Gentle < Strong < Extreme
        if (p == 2 && db220 > -5.0) ok = false;      // Extreme must be clearly audible
        prevDb = db220;
    }

    std::printf ("%s\n", ok ? "chain-test: PASS" : "chain-test: FAIL");
    return ok ? 0 : 1;
}
