// QDSSD SC Input — key sender for QDSSD on the vocal track.
//
// v1.0.0 — one control: which bus to publish to. Analysis is always Hybrid
// (note probabilities + 128-band spectrum). The audio passes through
// unchanged.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mpc_plugin.h"
#include "dsp/Bus.hpp"
#include "dsp/NoteAnalyzer.hpp"

#include <algorithm>

using mpc::intParam;

class QDssdScInput : public mpc::Plugin
{
public:
    enum P
    {
        pBus,
        kNumParams
    };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "QDSSD SC Input";
        i.vendor = "r3d0xStyle";
        i.uniqueId = MPC_FOURCC ('q', 'd', 's', 'i');
        i.version = 1103;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        i.wantsMidi = false;
        return i;
    }

    QDssdScInput() : mpc::Plugin (makeInfo())
    {
        addParam (intParam ("bus", "To Bus", 1, 8, 1));
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        (void) maxBlockSize;
        analyzer.prepare (sampleRate);
        bus.open (true);
        reset();
    }

    void reset() override
    {
        analyzer.reset();
        analyzer.setGateDb (-55.0f);
        frame = qdssd::BusFrame {};
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

        for (int i = 0; i < n; ++i)
        {
            const float l = inL ? inL[i] : 0.0f;
            const float r = inR ? inR[i] : l;
            outL[i] = l;
            if (outR)
                outR[i] = r;
        }

        if (bus.data() != nullptr && analyzer.process (inL, inR, n, frame))
        {
            const int index = std::max (0, std::min (7, getInt (pBus) - 1));
            qdssd::writeFrame (bus.data(), index, frame);
        }
    }

private:
    qdssd::NoteAnalyzer analyzer;
    qdssd::BusMapping bus;
    qdssd::BusFrame frame;
};

MPC_PLUGIN_ENTRY (QDssdScInput)
