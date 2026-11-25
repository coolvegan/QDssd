// Key mask helpers shared by QDSSD and the end-to-end test.
//
// The bus frame carries note probabilities and a 128-band spectrum. The
// ducker works on 16 log bands (60 Hz .. 16 kHz). The intensity profile
// decides how aggressively the key is turned into band weights; Depth stays
// the continuous control on top of it.
#pragma once

#include "Bus.hpp"
#include "FilterDucker.hpp"
#include "NoteAnalyzer.hpp"

#include <algorithm>
#include <cmath>

namespace qdssd
{

struct IntensityProfile
{
    const char* name;
    float noteExp;    // how much weak notes count
    float smear;      // neighbour-band coupling
    float broadband;  // floor applied above the sub band
    float curve;      // exponent applied to the final weights
    float depthScale; // scales the Depth control
};

inline IntensityProfile intensityProfile (int index)
{
    switch (index)
    {
        case 0: return { "Gentle",  0.65f, 0.40f, 0.15f, 1.00f, 0.50f };
        case 2: return { "Extreme", 0.40f, 1.00f, 0.70f, 0.65f, 1.00f };
        default: return { "Strong", 0.50f, 0.80f, 0.40f, 0.80f, 0.80f };
    }
}

struct KeyMask
{
    static float bandEdge (int i)
    {
        return 60.0f * std::pow (16000.0f / 60.0f, (float) i / (float) FilterDucker::Bands);
    }

    static int bandFor (float freq)
    {
        if (freq <= 60.0f) return 0;
        const float pos = std::log (freq / 60.0f) / std::log (16000.0f / 60.0f) * (float) FilterDucker::Bands;
        return std::max (0, std::min (FilterDucker::Bands - 1, (int) pos));
    }

    static void bandWeights (const BusFrame& f, float* w, float sr, const IntensityProfile& p)
    {
        for (int b = 0; b < FilterDucker::Bands; ++b)
            w[b] = 0.0f;

        const float nyquist = 0.5f * sr;

        for (int note = 0; note < 128; ++note)
        {
            const float raw = (float) f.notes[note] / 255.0f;
            if (raw <= 0.0f)
                continue;
            const float prob = std::pow (raw, p.noteExp);
            if (prob < 0.12f)
                continue;
            const float f0 = 440.0f * std::pow (2.0f, ((float) note - 69.0f) / 12.0f);
            for (int h = 1; h <= 8; ++h)
            {
                const float freq = f0 * (float) h;
                if (freq >= nyquist)
                    break;
                const float v = prob * std::pow (1.0f / (float) h, 0.8f);
                const int b = bandFor (freq);
                w[b] = std::max (w[b], v);
            }
        }

        for (int s = 0; s < NoteAnalyzer::Bands; ++s)
        {
            const float freq = 20.0f * std::pow (1000.0f, ((float) s + 0.5f) / (float) NoteAnalyzer::Bands);
            const float db = (float) f.spectrum[s];
            const float v = std::max (0.0f, std::min (1.0f, (db + 45.0f) / 45.0f));
            const int b = bandFor (freq);
            w[b] = std::max (w[b], v);
        }

        // A ducked band also pulls its neighbours down: 12 dB/oct splits are
        // broad, so a tone always straddles two bands.
        {
            float sm[FilterDucker::Bands];
            for (int b = 0; b < FilterDucker::Bands; ++b)
                sm[b] = w[b];
            for (int b = 0; b < FilterDucker::Bands; ++b)
            {
                const float l = sm[std::max (0, b - 1)];
                const float r = sm[std::min (FilterDucker::Bands - 1, b + 1)];
                const float l2 = sm[std::max (0, b - 2)];
                const float r2 = sm[std::min (FilterDucker::Bands - 1, b + 2)];
                w[b] = std::max (w[b], p.smear * std::max (l, r));
                w[b] = std::max (w[b], 0.6f * p.smear * std::max (l2, r2));
            }
        }

        // Broadband floor: duck everywhere above the sub, not only on notes.
        // Level-independent floor: a quiet vocal still ducks the beat. Only a
        // silent frame (flag bit 1) disables it. Scaling by level made the
        // effect vanish on vocals that sit low in the mix.
        const float lvl = (f.flags & 2u) ? 0.0f : 1.0f;
        const float floorValue = p.broadband * lvl;
        for (int b = 2; b < FilterDucker::Bands; ++b)
            w[b] = std::max (w[b], floorValue);

        for (int b = 0; b < FilterDucker::Bands; ++b)
            w[b] = std::pow (w[b], p.curve);

        // Keep the sub intact: a vocal duck should not move the kick/808.
        w[0] *= 0.30f;
        w[1] *= 0.30f;
    }
};

} // namespace qdssd
