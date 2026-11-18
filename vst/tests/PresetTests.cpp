// Console tests for the preset system (no audio device / window needed).
// Build + run:  cmake --build <build> --config Release --target EPianoTests
#include <juce_core/juce_core.h>
#include "PresetManager.h"

using namespace epiano;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int runParity (const juce::File&);
int runLoudness();

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init; // not strictly needed, keeps JUCE leak checks quiet

    if (argc >= 3 && juce::String (argv[1]) == "--parity")
        return runParity (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[2])));
    if (argc >= 2 && juce::String (argv[1]) == "--loudness")
        return runLoudness();

    PresetManager pm;
    const auto& f = pm.factory();
    std::printf ("factory presets: %d\n", (int) f.size());
    CHECK (f.size() == 90);
    CHECK ((int) paramDefs().size() == kNumParams && kNumParams == 93);

    // 9 categories x 10. The v0.9 parameters are always present and in range;
    // newer ones are either absent (NaN -> neutral default) or in range.
    juce::StringArray cats;
    for (const auto& p : f)
    {
        cats.addIfNotAlreadyThere (p.category);
        for (int i = 0; i < kNumParams; ++i)
        {
            const float v = p.values[(size_t) i];
            if (i == master) { CHECK (std::isnan (v)); continue; }
            const auto& d = paramDefs()[(size_t) i];
            if (i < master) CHECK (! std::isnan (v));
            if (! std::isnan (v)) CHECK (v >= d.min && v <= d.max);
        }
    }
    std::printf ("categories: %s\n", cats.joinIntoString (", ").toRawUTF8());
    CHECK (cats.size() == 9);
    CHECK (f[0].name == "Tine E-Piano");
    CHECK (f[50].name == "Glass Bells" && f[50].values[osc3Pitch] == 31.0f); // needs the +/-36 range

    // the original 80 carry no modulation / drive / voice settings at all -> sound unchanged
    for (size_t pi = 0; pi < 80; ++pi)
        for (int i = master + 1; i < kNumParams; ++i)
            CHECK (std::isnan (f[pi].values[(size_t) i]));

    // the new Motion presets do use the new features
    CHECK (f[80].category == "Motion" && f[80].name == "Classic Wobble Bass");
    CHECK (f[80].values[lfo1On] == 1.0f && f[80].values[voiceMode] == 1.0f && f[80].values[modParam (1, 2)] == 6.0f);

    // neutral defaults for everything added after v0.9
    CHECK (paramDefs()[lfo1On].def == 0 && paramDefs()[lfo2On].def == 0 && paramDefs()[drvOn].def == 0);
    CHECK (paramDefs()[voiceMode].def == 0 && paramDefs()[glideOn].def == 0);
    for (int s = 0; s < kNumModSlotsParam; ++s)
        CHECK (paramDefs()[(size_t) modParam (s, 1)].def == 0 && paramDefs()[(size_t) modParam (s, 3)].def == 0
               && paramDefs()[(size_t) modParam (s, 4)].def == 0);

    // a v0.9 user preset file (no new params) reads fine; new params stay unset
    {
        const auto old = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("epiano-v09.epreset");
        old.replaceWithText (R"({"format":"E-Piano Preset","version":1,"name":"Old","category":"User","params":{"osc1On":1,"osc1Wave":1,"osc1Pitch":0,"osc1Fine":0,"osc1Level":0.8,"ampLevel":-1.5,"fltCutoff":2500}})");
        PresetData d;
        juce::String e;
        CHECK (PresetManager::readFile (old, d, e));
        CHECK (d.values[osc1Wave] == 1.0f && d.values[ampLevel] == -1.5f);
        for (int i = master + 1; i < kNumParams; ++i) CHECK (std::isnan (d.values[(size_t) i]));
        old.deleteFile();
    }

    // save -> load round trip
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("epiano-test.epreset");
    juce::String err;
    for (const size_t src : { (size_t) 13, (size_t) 81, (size_t) 86 }) // v0.9 pad, Motion presets with LFO/matrix/drive/voice
    {
        PresetData out = f[src];
        out.name = "Round Trip";
        CHECK (PresetManager::writeFile (tmp, out, err));
        PresetData in;
        CHECK (PresetManager::readFile (tmp, in, err));
        CHECK (in.name == "Round Trip");
        for (int i = 0; i < kNumParams; ++i)
        {
            if (i == master) continue;
            const float a = out.values[(size_t) i], b = in.values[(size_t) i];
            if (std::isnan (a)) CHECK (std::isnan (b)); // unset stays unset
            else CHECK (std::abs (b - a) < 1e-4f * std::max (1.0f, std::abs (a)));
        }
    }

    // older/partial file: missing params stay NaN (-> defaults when applied)
    tmp.replaceWithText (R"({"name":"Partial","params":{"osc1Wave":2,"fltCutoff":99999}})");
    PresetData partial;
    CHECK (PresetManager::readFile (tmp, partial, err));
    CHECK (partial.values[osc1Wave] == 2.0f);
    CHECK (partial.values[fltCutoff] == 20000.0f);          // clamped to range
    CHECK (std::isnan (partial.values[ampLevel]));

    // garbage is rejected with a message
    tmp.replaceWithText ("not json");
    PresetData bad;
    CHECK (! PresetManager::readFile (tmp, bad, err) && err.isNotEmpty());
    tmp.deleteFile();

    std::printf ("user preset folder: %s\n", PresetManager::userFolder().getFullPathName().toRawUTF8());
    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
