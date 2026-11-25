// QDSSD — Dynamic Spectral Sidechain Ducking (MPC standalone VST2)
//
// v1.0.0 — one page, five controls, zero latency.
//
// The ducker is a 16-band filterbank fed by the key from QDSSD SC Input over a
// shared-memory bus. There is no STFT in the audio path, so no PDC is needed
// and the beat stays in time.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mpc_plugin.h"
#include "dsp/Bus.hpp"
#include "dsp/FilterDucker.hpp"
#include "dsp/KeyMask.hpp"

#include <algorithm>
#include <cmath>

using mpc::boolParam;
using mpc::choiceParam;
using mpc::floatParam;
using mpc::intParam;

class QDssdMpc : public mpc::Plugin
{
public:
    enum P
    {
        pBus, pDepth, pSpeed, pMix, pDelta, pIntensity,
        kNumParams
    };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "QDSSD";
        i.vendor = "r3d0xStyle";
        i.uniqueId = MPC_FOURCC ('q', 'd', 's', 'd');
        i.version = 1100;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        i.wantsMidi = false;
        i.latencySamples = 0;
        return i;
    }

    QDssdMpc() : mpc::Plugin (makeInfo())
    {
        addParam (intParam ("bus", "Bus", 1, 8, 1));
        addParam (floatParam ("depth", "Depth", 0.0f, 24.0f, 24.0f, "dB", 1));
        addParam (floatParam ("speed", "Speed", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam (floatParam ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam (boolParam ("delta", "Delta", false));
        addParam (choiceParam ("intensity", "Intensity", { "Gentle", "Strong", "Extreme" }, 1));
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        (void) maxBlockSize;
        sr = (float) sampleRate;
        ducker.prepare (sampleRate);
        bus.open (false);
        reset();
    }

    void reset() override
    {
        ducker.reset();
        keyFrame = qdssd::BusFrame {};
        setLatency (0);
    }

    void process (mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        if (n <= 0)
            return;

        const float* inL = ctx.numInputs > 0 ? ctx.inputs[0] : nullptr;
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : inL;
        float* outL = ctx.numOutputs > 0 ? ctx.outputs[0] : nullptr;
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : outL;
        if (! outL)
            return;

        // The MPC may create the processor before the sender. Retry until the
        // shared-memory pool exists.
        if (bus.data() == nullptr)
            bus.open (false);

        bool haveKey = false;
        if (bus.data() != nullptr)
        {
            const int index = std::max (0, std::min (7, getInt (pBus) - 1));
            haveKey = qdssd::readFrame (bus.data(), index, qdssd::monotonicNs(), keyFrame);
        }

        const qdssd::IntensityProfile profile = qdssd::intensityProfile (getInt (pIntensity));

        float weights[qdssd::FilterDucker::Bands] = {};
        if (haveKey)
            qdssd::KeyMask::bandWeights (keyFrame, weights, sr, profile);

        const float speed = std::max (0.0f, std::min (100.0f, get (pSpeed))) * 0.01f;
        const float attackMs = 1.0f + 79.0f * speed;
        const float releaseMs = 60.0f + 740.0f * speed;
        const float depthDb = std::max (0.0f, get (pDepth)) * profile.depthScale;
        const float depthLin = 1.0f - std::pow (10.0f, -depthDb / 20.0f);

        ducker.setParams (depthLin, attackMs, releaseMs, get (pMix) * 0.01f, getBool (pDelta));
        ducker.setBandWeights (weights);
        ducker.process (inL, inR, outL, outR, n);
    }

private:
    qdssd::FilterDucker ducker;
    qdssd::BusMapping bus;
    qdssd::BusFrame keyFrame;
    float sr = 44100.0f;
};

MPC_PLUGIN_ENTRY (QDssdMpc)
