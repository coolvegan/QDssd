// Vocal key analyser used by QDSSD SC Input.
//
// One STFT frame produces chroma[12], note probabilities[128] and a 128-band
// log spectrum. Nothing here touches the audio path; the plugin passes the
// vocal through unchanged.
#pragma once

#include "Bus.hpp"
#include "Fft.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace qdssd
{

class NoteAnalyzer
{
public:
    static constexpr int N = 1024;
    static constexpr int Hop = N / 4;
    static constexpr int Bins = N / 2 + 1;
    static constexpr int Bands = 128;

    void prepare (double sampleRate)
    {
        sr = (float) std::max (8000.0, sampleRate);
        fft.prepare (N);
        for (int n = 0; n < N; ++n)
            window[(size_t) n] = (float) std::sqrt (0.5 * (1.0 - std::cos (2.0 * 3.14159265358979323846 * n / N)));
        for (int b = 0; b < Bands; ++b)
        {
            const float f0 = 20.0f * std::pow (1000.0f, (float) b / (float) Bands);
            const float f1 = 20.0f * std::pow (1000.0f, (float) (b + 1) / (float) Bands);
            bandLo[(size_t) b] = std::max (1, (int) (f0 * N / sr));
            bandHi[(size_t) b] = std::max (bandLo[(size_t) b], (int) (f1 * N / sr));
        }
        reset();
    }

    void reset()
    {
        ring.fill (0.0f);
        smoothNotes.fill (0.0f);
        smoothChroma.fill (0.0f);
        writePos = 0;
        hopCounter = 0;
    }

    void setGateDb (float db) { gate = std::pow (10.0f, db / 20.0f); }


    // Returns true when a new frame was written into `frame`.
    bool process (const float* inL, const float* inR, int n, BusFrame& frame)
    {
        bool ready = false;
        for (int i = 0; i < n; ++i)
        {
            const float l = inL ? inL[i] : 0.0f;
            const float r = inR ? inR[i] : l;
            ring[(size_t) writePos] = 0.5f * (l + r);
            if (++writePos >= N) writePos = 0;
            if (++hopCounter >= Hop)
            {
                hopCounter = 0;
                analyse (frame);
                ready = true;
            }
        }
        return ready;
    }

private:
    void analyse (BusFrame& frame)
    {
        for (int n = 0; n < N; ++n)
        {
            const int idx = (writePos + n) & (N - 1);
            re[(size_t) n] = ring[(size_t) idx] * window[(size_t) n];
            im[(size_t) n] = 0.0f;
        }
        fft.forward (re.data(), im.data());

        float mag[Bins];
        double energy = 0.0;
        for (int k = 0; k < Bins; ++k)
        {
            mag[k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
            energy += (double) mag[k] * mag[k];
        }
        frame.level = (float) (2.0 * std::sqrt (energy) / (double) N);   // RMS
        frame.timeNs = monotonicNs();
        frame.flags = 1;

        float chroma[12] = {};
        float notes[128] = {};
        const int loBin = std::max (1, (int) (55.0f * N / sr));
        const int hiBin = std::min (Bins - 1, (int) (2000.0f * N / sr));

        for (int k = loBin; k <= hiBin; ++k)
        {
            const float f = (float) k * sr / (float) N;
            const float midiF = 69.0f + 12.0f * std::log2 (f / 440.0f);
            const int midi = (int) std::lround (midiF);
            if (midi < 0 || midi > 127)
                continue;
            notes[midi] += mag[k];
            chroma[midi % 12] += mag[k];
        }

        float maxNote = 0.0f;
        for (float v : notes)
            maxNote = std::max (maxNote, v);

        if (frame.level < gate || maxNote <= 0.0f)
        {
            frame.flags = 3;   // valid but silent
            std::memset (frame.chroma, 0, sizeof frame.chroma);
            std::memset (frame.notes, 0, sizeof frame.notes);
            std::memset (frame.spectrum, 0, sizeof frame.spectrum);
            for (float& v : smoothNotes) v *= 0.5f;
            for (float& v : smoothChroma) v *= 0.5f;
            return;
        }

        // Temporal smoothing with a floor: a note fades in over a few frames and
        // drops out below 40/255, so the mask cannot flicker frame by frame.
        for (int i = 0; i < 128; ++i)
        {
            const float v = 255.0f * (notes[i] / maxNote);   // linear; the main plugin shapes it
            smoothNotes[(size_t) i] += 0.45f * (v - smoothNotes[(size_t) i]);
            if (smoothNotes[(size_t) i] < 40.0f) smoothNotes[(size_t) i] = 0.0f;
            frame.notes[i] = (uint8_t) std::min (255.0f, smoothNotes[(size_t) i]);
        }
        for (int i = 0; i < 12; ++i)
        {
            const float v = 255.0f * chroma[i] / maxNote;
            smoothChroma[(size_t) i] += 0.45f * (v - smoothChroma[(size_t) i]);
            frame.chroma[i] = (uint8_t) std::min (255.0f, smoothChroma[(size_t) i]);
        }

        float peaks[Bands];
        float maxPeak = 0.0f;
        for (int b = 0; b < Bands; ++b)
        {
            float peak = 0.0f;
            for (int k = bandLo[(size_t) b]; k <= bandHi[(size_t) b] && k < Bins; ++k)
                peak = std::max (peak, mag[k]);
            peaks[b] = peak;
            maxPeak = std::max (maxPeak, peak);
        }
        // Store dB relative to the loudest band: absolute dB would make every
        // window sidelobe look like signal and duck the whole spectrum.
        for (int b = 0; b < Bands; ++b)
        {
            const float db = 20.0f * std::log10 ((peaks[b] + 1.0e-9f) / (maxPeak + 1.0e-9f));
            frame.spectrum[b] = (int8_t) std::max (-120.0f, std::min (0.0f, db));
        }
    }

    Fft fft;
    std::array<float, N> window {};
    std::array<float, N> ring {};
    std::array<float, N> re {}, im {};
    std::array<int, Bands> bandLo {}, bandHi {};
    std::array<float, 128> smoothNotes {};
    std::array<float, 12> smoothChroma {};
    float sr = 44100.0f;
    float gate = 0.003f;
    int writePos = 0;
    int hopCounter = 0;
};

} // namespace qdssd
