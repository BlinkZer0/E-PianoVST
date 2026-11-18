// Console tests for the preset system (no audio device / window needed).
// Build + run:  cmake --build <build> --config Release --target EPianoTests
#include <juce_core/juce_core.h>
#include "PresetManager.h"

using namespace epiano;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int runParity (const juce::File&);
int runLoudness();
int runBench();

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init; // not strictly needed, keeps JUCE leak checks quiet

    if (argc >= 3 && juce::String (argv[1]) == "--parity")
        return runParity (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[2])));
    if (argc >= 2 && juce::String (argv[1]) == "--loudness")
        return runLoudness();
    if (argc >= 2 && juce::String (argv[1]) == "--bench")
        return runBench();

    PresetManager pm;
    const auto& f = pm.factory();
    std::printf ("factory presets: %d\n", (int) f.size());
    CHECK (f.size() == 120);
    CHECK ((int) paramDefs().size() == kNumParams && kNumParams == 96);

    // 12 categories x 10. The v0.9 parameters are always present and in range;
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
    CHECK (cats.size() == 12);
    CHECK (f[100].category == "Dubstep 2" && f[100].name == "Brostep Screech");
    CHECK (f[110].category == "Jazz" && f[110].name == "Smoky Rhodes" && f[110].values[lfo1On] == 1.0f);
    CHECK (f[90].category == "Dubstep" && f[90].name == "Wub Machine" && f[90].values[lfo1Sync] == 1.0f);
    CHECK (f[0].name == "Tine E-Piano");
    CHECK (f[50].name == "Glass Bells" && f[50].values[osc3Pitch] == 31.0f); // needs the +/-36 range

    // every factory preset gives the mod wheel a job (as Source or Via of a matrix route)
    for (size_t pi = 0; pi < f.size(); ++pi)
    {
        bool wheel = false;
        for (int s = 0; s < kNumModSlotsParam; ++s)
            wheel = wheel || f[pi].values[(size_t) modParam (s, 1)] == 6.0f || f[pi].values[(size_t) modParam (s, 2)] == 6.0f;
        if (! wheel) std::printf ("no mod wheel route: %s/%s\n", f[pi].category.toRawUTF8(), f[pi].name.toRawUTF8());
        CHECK (wheel);
    }

    // the original 80 only gained mod-wheel routes (LFO 2 + matrix), which are silent
    // with the wheel at 0: LFO 1, drive, voice and sample settings stay unset -> sound unchanged
    for (size_t pi = 0; pi < 80; ++pi)
        for (int i = master + 1; i < kNumParams; ++i)
            if (i < lfo2On || i >= drvOn)
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

    // samples: embedded in the preset, round-trip within 16-bit precision
    {
        auto s = std::make_shared<SampleData>();
        s->name = "test.wav"; s->rate = 44100; s->root = 57;
        for (int i = 0; i < 1000; ++i) s->data.push_back ((float) std::sin (i * 0.05) * 0.9f);
        PresetData withSample = f[0];
        withSample.name = "With Sample";
        withSample.samples[1] = s;
        CHECK (PresetManager::writeFile (tmp, withSample, err));
        PresetData back;
        CHECK (PresetManager::readFile (tmp, back, err));
        CHECK (back.samples[0] == nullptr && back.samples[2] == nullptr && back.samples[1] != nullptr);
        if (auto r = back.samples[1])
        {
            CHECK (r->name == "test.wav" && r->rate == 44100 && r->root == 57 && r->data.size() == 1000);
            float worst = 0;
            for (size_t i = 0; i < r->data.size(); ++i) worst = std::max (worst, std::abs (r->data[i] - s->data[i]));
            CHECK (worst < 1.0f / 32767.0f);
        }

        // decoding a real WAV file: written with JUCE, read back mono + peak-normalised, smpl root note honoured
        const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("epiano-test.wav");
        {
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (wav);
            const auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (16)
                                  .withMetadataValues ({ { "MidiUnityNote", "48" }, { "NumSampleLoops", "0" } });
            auto w = fmt.createWriterFor (out, opts);
            CHECK (w != nullptr);
            juce::AudioBuffer<float> b (2, 4800);
            for (int i = 0; i < 4800; ++i) { b.setSample (0, i, 0.25f * (float) std::sin (i * 0.1)); b.setSample (1, i, 0.25f * (float) std::sin (i * 0.1)); }
            if (w) w->writeFromAudioSampleBuffer (b, 0, 4800);
        }
        juce::String msg;
        auto decoded = decodeSampleFile (wav, msg);
        CHECK (decoded != nullptr);
        if (decoded)
        {
            float peak = 0;
            for (auto x : decoded->data) peak = std::max (peak, std::abs (x));
            CHECK (decoded->data.size() == 4800 && decoded->rate == 48000 && decoded->root == 48);
            CHECK (std::abs (peak - 1.0f) < 1e-3f);
        }
        wav.deleteFile();
        tmp.replaceWithText ("not audio");
        CHECK (decodeSampleFile (tmp, msg) == nullptr && msg.isNotEmpty());
    }

    // garbage is rejected with a message
    tmp.replaceWithText ("not json");
    PresetData bad;
    CHECK (! PresetManager::readFile (tmp, bad, err) && err.isNotEmpty());
    tmp.deleteFile();

    std::printf ("user preset folder: %s\n", PresetManager::userFolder().getFullPathName().toRawUTF8());
    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
