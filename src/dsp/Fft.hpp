// Minimal in-place radix-2 complex FFT for the QDSSD spectral engine.
// Tables are built in prepare(); the transform itself allocates nothing.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace qdssd
{

class Fft
{
public:
    void prepare (int size)
    {
        n = size < 8 ? 8 : size;
        twRe.resize ((size_t) n / 2);
        twIm.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            const double a = -2.0 * 3.14159265358979323846 * (double) i / (double) n;
            twRe[(size_t) i] = (float) std::cos (a);
            twIm[(size_t) i] = (float) std::sin (a);
        }

        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        rev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    int size() const { return n; }

    void forward (float* re, float* im) { transform (re, im, false); }

    void inverse (float* re, float* im)
    {
        transform (re, im, true);
        const float s = 1.0f / (float) n;
        for (int i = 0; i < n; ++i)
        {
            re[i] *= s;
            im[i] *= s;
        }
    }

private:
    void transform (float* re, float* im, bool inv)
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n / len;
            for (int base = 0; base < n; base += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    const int k = j * step;
                    const float wr = twRe[(size_t) k];
                    const float wi = inv ? -twIm[(size_t) k] : twIm[(size_t) k];
                    const int a = base + j;
                    const int b = a + half;
                    const float xr = re[b] * wr - im[b] * wi;
                    const float xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr;
                    im[b] = im[a] - xi;
                    re[a] += xr;
                    im[a] += xi;
                }
            }
        }
    }

    int n = 0;
    std::vector<float> twRe, twIm;
    std::vector<int> rev;
};

} // namespace qdssd
