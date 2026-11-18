// Console tests for the preset system (no audio device / window needed).
// Build + run:  cmake --build <build> --config Release --target EPianoTests
#include <juce_core/juce_core.h>
#include "PresetManager.h"

using namespace epiano;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    juce::ScopedJuceInitialiser_GUI init; // not strictly needed, keeps JUCE leak checks quiet

    PresetManager pm;
    const auto& f = pm.factory();
    std::printf ("factory presets: %d\n", (int) f.size());
    CHECK (f.size() == 80);

    // 8 categories x 10, every value present and in range
    juce::StringArray cats;
    for (const auto& p : f)
    {
        cats.addIfNotAlreadyThere (p.category);
        for (int i = 0; i < kNumParams; ++i)
        {
            if (i == master) { CHECK (std::isnan (p.values[(size_t) i])); continue; }
            const auto& d = paramDefs()[(size_t) i];
            CHECK (! std::isnan (p.values[(size_t) i]));
            CHECK (p.values[(size_t) i] >= d.min && p.values[(size_t) i] <= d.max);
        }
    }
    std::printf ("categories: %s\n", cats.joinIntoString (", ").toRawUTF8());
    CHECK (cats.size() == 8);
    CHECK (f[0].name == "Tine E-Piano");
    CHECK (f[50].name == "Glass Bells" && f[50].values[osc3Pitch] == 31.0f); // needs the +/-36 range

    // save -> load round trip
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("epiano-test.epreset");
    juce::String err;
    PresetData out = f[13];
    out.name = "Round Trip";
    CHECK (PresetManager::writeFile (tmp, out, err));
    PresetData in;
    CHECK (PresetManager::readFile (tmp, in, err));
    CHECK (in.name == "Round Trip");
    for (int i = 0; i < kNumParams; ++i)
        if (i != master) CHECK (std::abs (in.values[(size_t) i] - out.values[(size_t) i]) < 1e-4f * std::max (1.0f, std::abs (out.values[(size_t) i])));

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
