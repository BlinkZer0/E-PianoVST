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

        // generated test samples: decaying sine, identical in the JS harness
        std::vector<std::vector<float>> sampleStore;
        if (auto* smps = sc["samples"].getArray())
            for (const auto& s : *smps)
            {
                const int len = (int) s["length"];
                const double rate = (double) s["rate"], freq = (double) s["freq"];
                std::vector<float> d ((size_t) len);
                for (int i = 0; i < len; ++i)
                    d[(size_t) i] = (float) (std::sin (2 * kPi * freq * i / rate) * std::exp (-3.0 * i / len));
                sampleStore.push_back (std::move (d));
                const auto& data = sampleStore.back();
                engine.setSample ((int) s["osc"], SampleBuffer::make (data.data(), len, rate, (int) s["root"]));
            }

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

// Worst-case CPU: 88 voices, 3 saws, both LFOs per voice, 4 routes, drive on.
int runBench()
{
    auto values = defaultValues();
    auto set = [&values] (const char* id, float v) { values[(size_t) paramIndex (id)] = v; };
    set ("osc1Wave", 1); set ("osc2On", 1); set ("osc2Wave", 1); set ("osc3On", 1); set ("osc3Wave", 2);
    set ("ampSustain", 1); set ("ampRelease", 5);
    set ("lfo1On", 1); set ("lfo1Mode", 1); set ("lfo2On", 1); set ("lfo2Mode", 1); set ("lfo2Wave", 5);
    set ("mod1Src", 1); set ("mod1Dst", 1); set ("mod1Amt", 0.3f);
    set ("mod2Src", 2); set ("mod2Dst", 11); set ("mod2Amt", 0.5f);
    set ("mod3Src", 1); set ("mod3Via", 6); set ("mod3Dst", 3); set ("mod3Amt", 0.02f);
    set ("mod4Src", 5); set ("mod4Dst", 2); set ("mod4Amt", 0.2f);
    set ("drvOn", 1); set ("drvType", 0); set ("drvDrive", 0.5f);

    for (const bool features : { false, true })
    {
        auto v = values;
        if (! features)
            for (int i = master + 1; i < kNumParams; ++i) v[(size_t) i] = paramDefs()[(size_t) i].def;
        SynthEngine engine;
        engine.prepare (kSr);
        std::array<float, 512> L {}, R {};
        engine.setParams (makeEngineParams ([&v] (int i) { return v[(size_t) i]; }));
        for (int n = 20; n < 108; ++n) engine.noteOn (n, 100);
        engine.setModWheel (0.5);
        const int blocks = (int) (kSr * 10 / 512);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            engine.setParams (makeEngineParams ([&v] (int i) { return v[(size_t) i]; }));
            engine.setTransport (120.0, 0.0, false);
            engine.render (L.data(), R.data(), 512);
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("%-40s 88 voices: %6.0f ms for 10 s of audio  (%.1f%% of one core, %d voices active)\n",
                     features ? "LFOs x2 per voice + 4 routes + drive" : "v0.9 patch (no new features)",
                     ms, ms / 100.0, engine.activeVoices());
    }
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
