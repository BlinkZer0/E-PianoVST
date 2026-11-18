#pragma once
// Maps raw parameter values (indexed by ParamIndex) to the engine's snapshot.
// Shared by the plugin (atomic APVTS values) and the console tests.
#include "Params.h"
#include "SynthEngine.h"

namespace epiano
{
template <typename GetValue> // float (int paramIndex)
EngineParams makeEngineParams (GetValue&& get)
{
    auto choice = [&get] (int index, int maxIndex) { return juce::jlimit (0, maxIndex, (int) std::lround (get (index))); };
    auto on = [&get] (int index) { return get (index) >= 0.5f; };

    EngineParams p;
    for (int k = 0; k < kNumOsc; ++k)
    {
        const int b = osc1On + k * 5;
        auto& o = p.osc[(size_t) k];
        o.on    = on (b);
        o.wave  = choice (b + 1, 3);
        o.semis = get (b + 2) + get (b + 3) / 100.0;
        o.level = get (b + 4);
        o.sampleMode = choice (osc1Smp + k, 3);
    }
    p.attack   = get (ampAttack);
    p.decay    = get (ampDecay);
    p.sustain  = get (ampSustain);
    p.release  = get (ampRelease);
    p.cutoff   = get (fltCutoff);
    p.reso     = get (fltReso);
    p.envAmt   = get (fltEnvAmt);
    p.fltDecay = get (fltDecay);
    p.ampGain  = std::pow (10.0, get (ampLevel) / 20.0);
    p.master   = get (master);

    for (int i = 0; i < kNumLfo; ++i)
    {
        auto& l = p.lfo[(size_t) i];
        l.on       = on (lfoParam (i, 0));
        l.wave     = choice (lfoParam (i, 1), 5);
        l.rate     = get (lfoParam (i, 2));
        l.sync     = on (lfoParam (i, 3));
        l.div      = choice (lfoParam (i, 4), kNumLfoDivs - 1);
        l.perVoice = choice (lfoParam (i, 5), 1) == 1;
        l.retrig   = choice (lfoParam (i, 6), 1) == 1;
        l.phase    = get (lfoParam (i, 7)) / 360.0;
        l.amount   = get (lfoParam (i, 8));
    }
    for (int s = 0; s < kNumModSlots; ++s)
    {
        auto& m = p.mod[(size_t) s];
        m.on     = on (modParam (s, 0));
        m.src    = choice (modParam (s, 1), kNumModSources - 1);
        m.via    = choice (modParam (s, 2), kNumModSources - 1);
        m.dst    = choice (modParam (s, 3), kNumModDests - 1);
        m.amount = get (modParam (s, 4));
    }
    p.drive.on      = on (drvOn);
    p.drive.type    = choice (drvType, 3);
    p.drive.drive   = get (drvDrive);
    p.drive.tone    = get (drvTone);
    p.drive.mix     = get (drvMix);
    p.drive.outGain = std::pow (10.0, get (drvOut) / 20.0);
    p.voiceMode       = choice (voiceMode, 2);
    p.glideOn         = on (glideOn);
    p.glideTime       = get (glideTime);
    p.glideLegatoOnly = choice (glideMode, 1) == 1;
    return p;
}
} // namespace epiano
