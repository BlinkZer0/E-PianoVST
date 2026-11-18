// Engine tools for the console test app:
//   --parity <scenarios.json>   render each scenario, print stats as JSON (compared with the JS engine)
//   --loudness                  loudness of every factory preset (used to level-match presets)
#include <juce_core/juce_core.h>
#include "EngineParamsFromValues.h"
#include "PresetManager.h"

using namespace epiano;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

std::array<float, kNumParams> defaultValues()
{
    std::array<float, kNumParams> v {};
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = paramDefs()[(size_t) i].def;
    return v;
}

void applyPresetData (std::array<float, kNumParams>& v, const PresetData& p)
{
    for (int i = 0; i < kNumParams; ++i)
        if (i != master && ! std::isnan (p.values[(size_t) i])) v[(size_t) i] = p.values[(size_t) i];
}

const PresetData* findFactory (const PresetManager& pm, const juce::String& catSlashName)
{
    for (const auto& p : pm.factory())
        if (p.category + "/" + p.name == catSlashName) return &p;
    return nullptr;
}

int paramIndex (const juce::String& id)
{
    for (int i = 0; i < kNumParams; ++i)
        if (paramDefs()[(size_t) i].id == id) return i;
    return -1;
}
} // namespace

int runParity (const juce::File& scenarioFile)
{
    const auto json = juce::JSON::parse (scenarioFile);
    if (! json.isArray()) { std::printf ("cannot read %s\n", scenarioFile.getFullPathName().toRawUTF8()); return 2; }

    PresetManager pm;
    juce::Array<juce::var> results;

    for (const auto& sc : *json.getArray())
    {
        auto values = defaultValues();
        if (sc.hasProperty ("preset"))
        {
            const auto* p = findFactory (pm, sc["preset"].toString());
            if (p == nullptr) { std::printf ("unknown preset %s\n", sc["preset"].toString().toRawUTF8()); return 2; }
            applyPresetData (values, *p);
        }
        if (auto* obj = sc["params"].getDynamicObject())
            for (const auto& prop : obj->getProperties())
            {
                const int i = paramIndex (prop.name.toString());
                if (i < 0) { std::printf ("unknown param %s\n", prop.name.toString().toRawUTF8()); return 2; }
                values[(size_t) i] = (float) prop.value;
            }

        const int blocks = (int) sc["blocks"];
        const double bpm = sc.hasProperty ("bpm") ? (double) sc["bpm"] : 120.0;
        constexpr int kWindows = 8;
        const int perWindow = blocks * kBlock / kWindows;
        std::array<double, kWindows> sumL {}, sumR {};
        double peak = 0;

        SynthEngine engine;
        engine.prepare (kSr);
        std::array<float, kBlock> L {}, R {};

        for (int b = 0; b < blocks; ++b)
        {
            engine.setParams (makeEngineParams ([&values] (int i) { return values[(size_t) i]; }));
            if (auto* evs = sc["events"].getArray())
                for (const auto& ev : *evs)
                {
                    if ((int) ev[0] != b) continue;
                    const auto type = ev[1].toString();
                    if (type == "on")           engine.noteOn ((int) ev[2], (int) ev[3]);
                    else if (type == "off")     engine.noteOff ((int) ev[2]);
                    else if (type == "wheel")   engine.setModWheel ((double) ev[2]);
                    else if (type == "sustain") engine.setSustain ((int) ev[2] != 0);
                    else if (type == "bend")    engine.setPitchBend ((double) ev[2]);
                }
            engine.setTransport (bpm, 0.0, false);
            engine.render (L.data(), R.data(), kBlock);
            for (int i = 0; i < kBlock; ++i)
            {
                const int w = std::min (kWindows - 1, (b * kBlock + i) / perWindow);
                sumL[(size_t) w] += (double) L[(size_t) i] * L[(size_t) i];
                sumR[(size_t) w] += (double) R[(size_t) i] * R[(size_t) i];
                peak = std::max (peak, (double) std::max (std::abs (L[(size_t) i]), std::abs (R[(size_t) i])));
            }
        }

        juce::Array<juce::var> rl, rr;
        for (int w = 0; w < kWindows; ++w)
        {
            rl.add (std::sqrt (sumL[(size_t) w] / perWindow));
            rr.add (std::sqrt (sumR[(size_t) w] / perWindow));
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", sc["name"]);
        o->setProperty ("rmsL", rl);
        o->setProperty ("rmsR", rr);
        o->setProperty ("peak", peak);
        results.add (juce::var (o));
    }
    std::printf ("%s\n", juce::JSON::toString (juce::var (results), true).toRawUTF8());
    return 0;
}

// Loudest 100 ms window (RMS) while a chord (or one note for bass / mono presets)
// is held for 3 s at velocity 100, with master low enough that the soft clipper
// stays linear. Reported with and without the preset's own Amp level.
int runLoudness()
{
    PresetManager pm;
    std::printf ("[\n");
    const auto& f = pm.factory();
    for (size_t pi = 0; pi < f.size(); ++pi)
    {
        auto values = defaultValues();
        applyPresetData (values, f[pi]);
        values[master] = 0.05f;
        const bool single = f[pi].category == "Bass" || values[voiceMode] >= 0.5f
                            || f[pi].name.containsIgnoreCase ("Bass");

        SynthEngine engine;
        engine.prepare (kSr);
        std::array<float, kBlock> L {}, R {};
        double best = 0, acc = 0;
        int cnt = 0;
        const int blocks = (int) (kSr * 3 / kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            engine.setParams (makeEngineParams ([&values] (int i) { return values[(size_t) i]; }));
            if (b == 1)
            {
                if (single) engine.noteOn (36, 100);
                else { engine.noteOn (60, 100); engine.noteOn (64, 100); engine.noteOn (67, 100); }
            }
            engine.setTransport (120.0, 0.0, false);
            engine.render (L.data(), R.data(), kBlock);
            for (int i = 0; i < kBlock; ++i) acc += 0.5 * ((double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]);
            if (++cnt == 37) { best = std::max (best, std::sqrt (acc / (cnt * kBlock))); acc = 0; cnt = 0; }
        }
        const double loud = best / 0.05;
        const double withoutAmp = loud / std::pow (10.0, values[ampLevel] / 20.0);
        std::printf ("  { \"index\": %d, \"name\": \"%s\", \"category\": \"%s\", \"loudness\": %.6f, \"loudnessNoAmp\": %.6f, \"ampLevel\": %.2f }%s\n",
                     (int) pi, f[pi].name.toRawUTF8(), f[pi].category.toRawUTF8(), loud, withoutAmp, values[ampLevel],
                     pi + 1 < f.size() ? "," : "");
    }
    std::printf ("]\n");
    return 0;
}
