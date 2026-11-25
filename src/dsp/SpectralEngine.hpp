// QDSSD spectral engine: STFT resonance suppressor.
//
// Analysis: sqrt-Hann window, 4x overlap (hop = N/4).
// Effective analysis*synthesis window is Hann, whose COLA sum at hop N/4 is
// 2.0, so the overlap-add normalisation is 0.5 (the Gemini drafts used 1/1.5,
// which is only correct for Hann*Hann).
//
// Latency is exactly N samples with this ring-buffer scheme.
#pragma once

#include "Fft.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace qdssd
{

struct SpectralParams
{
    float depthDb = 8.0f;        // 0..24
    float selectivityDb = 3.0f; // 0..12
    float sharpness = 0.5f;     // 0..1
    float ratio = 4.0f;         // 1..10
    float focus = 0.0f;         // -1 low .. +1 high
    float attackMs = 10.0f;
    float releaseMs = 200.0f;
    float stereoLink = 0.5f;    // 0..1
    float mix = 1.0f;           // 0..1
    bool delta = false;
    int mode = 0;               // 0 resonance, 1 vocal, 2 both
};

template <int N>
class SpectralEngine
{
public:
    static constexpr int Hop = N / 4;
    static constexpr int Bins = N / 2 + 1;
    static constexpr int Latency = N;
    static constexpr int MaxRadius = 32;

    void prepare (double sampleRate)
    {
        sr = (float) std::max (8000.0, sampleRate);
        fft.prepare (N);

        // sqrt-Hann, periodic
        for (int n = 0; n < N; ++n)
            window[(size_t) n] = (float) std::sqrt (0.5 * (1.0 - std::cos (2.0 * 3.14159265358979323846 * n / N)));

        // ERB-ish radius per bin and a log-frequency tilt curve.
        for (int k = 0; k < Bins; ++k)
        {
            const float freq = (float) k * sr / (float) N;
            const float erbHz = 21.4f * std::log10 (1.0f + 0.00437f * std::max (freq, 1.0f));
            const float binsPerErb = erbHz * (float) N / sr;
            radiusBase[(size_t) k] = std::max (1.0f, binsPerErb);

            const float logPos = std::log2 (std::max (freq, 20.0f) / 1000.0f) / std::log2 (20000.0f / 1000.0f);
            tilt[(size_t) k] = std::max (-1.0f, std::min (1.0f, logPos));
        }

        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            in[ch].fill (0.0f);
            out[ch].fill (0.0f);
            dry[ch].fill (0.0f);
            gain[ch].fill (1.0f);
        }
        writePos = 0;
        readPos = 0;
        hopCounter = 0;
        mask.fill (0.0f);
        mixSm = params.mix;
        deltaSm = params.delta ? 1.0f : 0.0f;
    }

    void setMask (const float* m)
    {
        for (int k = 0; k < Bins; ++k)
            mask[(size_t) k] = m ? m[k] : 0.0f;
    }

    void setParams (const SpectralParams& p)
    {
        params = p;
        alphaAttack = std::exp (-(float) Hop / std::max (1.0f, 0.001f * params.attackMs * sr));
        alphaRelease = std::exp (-(float) Hop / std::max (1.0f, 0.001f * params.releaseMs * sr));
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = inL ? inL[i] : 0.0f;
            const float r = inR ? inR[i] : l;

            float dl = dry[0][(size_t) readPos];
            float dr = dry[1][(size_t) readPos];
            float wl = out[0][(size_t) readPos];
            float wr = out[1][(size_t) readPos];
            out[0][(size_t) readPos] = 0.0f;
            out[1][(size_t) readPos] = 0.0f;

            dry[0][(size_t) writePos] = l;
            dry[1][(size_t) writePos] = r;
            in[0][(size_t) writePos] = l;
            in[1][(size_t) writePos] = r;

            // Smooth mix and delta per sample so switching cannot click.
            mixSm += 0.002f * (params.mix - mixSm);
            deltaSm += 0.002f * ((params.delta ? 1.0f : 0.0f) - deltaSm);

            const float dlMix = dl + mixSm * (wl - dl);
            const float drMix = dr + mixSm * (wr - dr);
            const float dlDelta = dl - wl;
            const float drDelta = dr - wr;

            outL[i] = dlMix * (1.0f - deltaSm) + dlDelta * deltaSm;
            if (outR)
                outR[i] = drMix * (1.0f - deltaSm) + drDelta * deltaSm;

            ++writePos; if (writePos >= N) writePos = 0;
            ++readPos;  if (readPos >= N)  readPos = 0;

            if (++hopCounter >= Hop)
            {
                hopCounter = 0;
                frame();
            }
        }
    }

private:
    static float dbToLin (float db) { return std::pow (10.0f, -db / 20.0f); }

    float medianIn (const float* src, int lo, int hi)
    {
        const int len = hi - lo + 1;
        for (int i = 0; i < len; ++i)
            scratch[(size_t) i] = src[lo + i];
        const int mid = len / 2;
        std::nth_element (scratch.begin(), scratch.begin() + mid, scratch.begin() + len);
        return scratch[(size_t) mid];
    }

    void frame()
    {
        float attDb[2][Bins];

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int n = 0; n < N; ++n)
            {
                const int idx = (writePos + n) & (N - 1);
                re[ch][(size_t) n] = in[ch][(size_t) idx] * window[(size_t) n];
                im[ch][(size_t) n] = 0.0f;
            }

            fft.forward (re[ch].data(), im[ch].data());

            for (int k = 0; k < Bins; ++k)
            {
                const float mag = std::sqrt (re[ch][(size_t) k] * re[ch][(size_t) k]
                                            + im[ch][(size_t) k] * im[ch][(size_t) k]);
                logMag[ch][(size_t) k] = 20.0f * std::log10 (mag + 1.0e-9f);
            }

            for (int k = 0; k < Bins; ++k)
            {
                const int rad = std::max (2, std::min (MaxRadius,
                                  (int) (radiusBase[(size_t) k] * (1.5f - params.sharpness))));
                const int lo = std::max (0, k - rad);
                const int hi = std::min (Bins - 1, k + rad);
                const float baseline = medianIn (logMag[ch].data(), lo, hi);

                const float weight = std::max (0.1f, std::min (3.0f, 1.0f + params.focus * tilt[(size_t) k]));
                const float excess = (logMag[ch][(size_t) k] - baseline) * weight;
                const float over = excess - params.selectivityDb;

                attDb[ch][k] = over > 0.0f
                    ? std::min (params.depthDb, over * (1.0f - 1.0f / std::max (1.0f, params.ratio)))
                    : 0.0f;
            }
        }

        // Stereo linking: blend towards the larger attenuation of both sides.
        for (int k = 0; k < Bins; ++k)
        {
            const float maxAtt = std::max (attDb[0][k], attDb[1][k]);
            attDb[0][k] += (maxAtt - attDb[0][k]) * params.stereoLink;
            attDb[1][k] += (maxAtt - attDb[1][k]) * params.stereoLink;
        }

        // External vocal mask (0..1 per bin) scales the depth. Vocal replaces
        // the resonance attenuation, Both takes the larger of the two.
        if (params.mode != 0)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < Bins; ++k)
                {
                    const float vocal = mask[(size_t) k] * params.depthDb;
                    attDb[ch][k] = params.mode == 1 ? vocal : std::max (attDb[ch][k], vocal);
                }
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            // Frequency smoothing of the target gain (3 taps), then per-bin time ballistics.
            float target[Bins];
            for (int k = 0; k < Bins; ++k)
                target[k] = dbToLin (attDb[ch][k]);

            for (int k = 0; k < Bins; ++k)
            {
                const int k0 = std::max (0, k - 1);
                const int k1 = std::min (Bins - 1, k + 1);
                const float smooth = 0.25f * target[k0] + 0.5f * target[k] + 0.25f * target[k1];

                const float alpha = smooth < gain[ch][(size_t) k] ? alphaAttack : alphaRelease;
                gain[ch][(size_t) k] = alpha * gain[ch][(size_t) k] + (1.0f - alpha) * smooth;

                re[ch][(size_t) k] *= gain[ch][(size_t) k];
                im[ch][(size_t) k] *= gain[ch][(size_t) k];
            }

            // Mirror the real spectrum onto the negative frequencies before the inverse.
            for (int k = 1; k < N / 2; ++k)
            {
                re[ch][(size_t) (N - k)] = re[ch][(size_t) k];
                im[ch][(size_t) (N - k)] = -im[ch][(size_t) k];
            }
            im[ch][0] = 0.0f;
            if (N / 2 < N) im[ch][(size_t) (N / 2)] = 0.0f;

            fft.inverse (re[ch].data(), im[ch].data());

            for (int n = 0; n < N; ++n)
            {
                const int idx = (readPos + n) & (N - 1);
                out[ch][(size_t) idx] += re[ch][(size_t) n] * window[(size_t) n] * 0.5f;
            }
        }
    }

    Fft fft;
    std::array<float, N> window {};
    std::array<float, Bins> radiusBase {};
    std::array<float, Bins> tilt {};
    std::array<float, Bins> logMag[2];
    std::array<float, Bins> gain[2];
    std::array<float, Bins> mask {};

    std::array<float, N> in[2], out[2], dry[2];
    std::array<float, N> re[2], im[2];
    std::array<float, 2 * MaxRadius + 1> scratch {};

    SpectralParams params;
    float sr = 44100.0f;
    float alphaAttack = 0.5f, alphaRelease = 0.5f;
    float mixSm = 1.0f, deltaSm = 0.0f;
    int writePos = 0, readPos = 0, hopCounter = 0;
};

} // namespace qdssd
