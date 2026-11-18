#include "PresetManager.h"
#include "BinaryData.h"

namespace epiano
{
namespace
{
float clampParam (int index, float v)
{
    const auto& d = paramDefs()[(size_t) index];
    return juce::jlimit (d.min, d.max, v);
}
} // namespace

PresetManager::PresetManager()
{
    loadFactory();
}

//==============================================================================
// Factory file format (compact):
//   preset = [name, osc1, osc2, osc3, [a,d,s,r], [cutoff,reso,envAmt,envDecay], ampLevelDb, {extra}?]
//   osc    = [on, wave, pitch, fine, level]
//   extra  = optional { "paramId": value } for LFO / matrix / drive / voice params
// Anything not given loads at its default (= neutral), so v0.9 presets are unchanged.
void PresetManager::loadFactory()
{
    const auto json = juce::JSON::parse (juce::String::fromUTF8 (BinaryData::factory_presets_json,
                                                                 BinaryData::factory_presets_jsonSize));
    if (const auto* cats = json["categories"].getArray())
    {
        for (const auto& cat : *cats)
        {
            const auto* list = cat["presets"].getArray();
            if (list == nullptr) continue;

            for (const auto& p : *list)
            {
                if (! p.isArray() || p.size() < 7) continue;
                PresetData d;
                d.name = p[0].toString();
                d.category = cat["name"].toString();
                for (int k = 0; k < 3; ++k)
                    for (int j = 0; j < 5; ++j)
                        d.values[(size_t) (osc1On + k * 5 + j)] = clampParam (osc1On + k * 5 + j, (float) p[k + 1][j]);

                const int env[] { ampAttack, ampDecay, ampSustain, ampRelease };
                const int flt[] { fltCutoff, fltReso, fltEnvAmt, fltDecay };
                for (int j = 0; j < 4; ++j)
                {
                    d.values[(size_t) env[j]] = clampParam (env[j], (float) p[4][j]);
                    d.values[(size_t) flt[j]] = clampParam (flt[j], (float) p[5][j]);
                }
                d.values[ampLevel] = clampParam (ampLevel, (float) p[6]);

                // optional 8th element: { "paramId": value } for everything added after v0.9
                if (p.size() > 7 && p[7].isObject())
                    for (int i = 0; i < kNumParams; ++i)
                        if (p[7].hasProperty (paramDefs()[(size_t) i].id))
                            d.values[(size_t) i] = clampParam (i, (float) p[7][juce::Identifier (paramDefs()[(size_t) i].id)]);

                factoryPresets.push_back (std::move (d));
            }
        }
    }

    jassert (factoryPresets.size() >= 80);
    if (factoryPresets.empty()) // never leave the host with zero programs
    {
        PresetData init;
        init.name = "Init";
        init.category = "Factory";
        factoryPresets.push_back (init);
    }
}

//==============================================================================
juce::File PresetManager::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("E-Piano")
        .getChildFile ("Presets");
}

juce::String PresetManager::idForFile (const juce::File& f) const
{
    const auto root = userFolder();
    if (! f.isAChildOf (root)) return {};
    return "user:" + f.getRelativePathFrom (root).replaceCharacter ('\\', '/');
}

juce::String PresetManager::categoryForFile (const juce::File& f) const
{
    const auto root = userFolder();
    const auto parent = f.getParentDirectory();
    if (parent == root || ! f.isAChildOf (root)) return "User";
    return parent.getRelativePathFrom (root).replaceCharacter ('\\', '/');
}

bool PresetManager::rescanIfChanged()
{
    const auto root = userFolder();
    root.createDirectory();

    const auto files = root.findChildFiles (juce::File::findFiles, true, juce::String ("*") + fileExtension);

    juce::String sig;
    for (const auto& f : files)
        sig << f.getFullPathName() << '|' << f.getSize() << '|' << f.getLastModificationTime().toMilliseconds() << ';';

    if (scanned && sig == signature) return false;
    scanned = true;
    signature = sig;

    user.clear();
    for (const auto& f : files)
        user.push_back ({ idForFile (f), f.getFileNameWithoutExtension(), categoryForFile (f), f });

    std::sort (user.begin(), user.end(), [] (const PresetEntry& a, const PresetEntry& b)
    {
        if (a.category != b.category) return a.category.compareNatural (b.category) < 0;
        return a.name.compareNatural (b.name) < 0;
    });
    return true;
}

const PresetEntry* PresetManager::findUser (const juce::String& id) const
{
    for (const auto& e : user)
        if (e.id == id) return &e;
    return nullptr;
}

juce::File PresetManager::fileForName (const juce::String& name) const
{
    return userFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + fileExtension);
}

//==============================================================================
// User file format:
//   { "format": "E-Piano Preset", "version": 1, "name": "...", "category": "...",
//     "params": { "<paramId>": value, ... } }      (master is never stored)
bool PresetManager::readFile (const juce::File& f, PresetData& d, juce::String& error)
{
    juce::var json;
    if (! f.existsAsFile() || juce::JSON::parse (f.loadFileAsString(), json).failed() || ! json.isObject())
    {
        error = "Not a valid preset file: " + f.getFileName();
        return false;
    }
    const auto params = json["params"];
    int found = 0;
    if (params.isObject())
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            if (i == master) continue;
            const juce::Identifier id (paramDefs()[(size_t) i].id);
            if (params.hasProperty (id))
            {
                d.values[(size_t) i] = clampParam (i, (float) params[id]);
                ++found;
            }
        }
    }
    if (found == 0)
    {
        error = "No E-Piano settings found in " + f.getFileName();
        return false;
    }
    d.name = json["name"].toString().trim();
    if (d.name.isEmpty()) d.name = f.getFileNameWithoutExtension();
    d.category = json["category"].toString();
    return true;
}

bool PresetManager::writeFile (const juce::File& f, const PresetData& d, juce::String& error)
{
    auto* params = new juce::DynamicObject();
    for (int i = 0; i < kNumParams; ++i)
    {
        if (i == master || std::isnan (d.values[(size_t) i])) continue;
        // round to 6 significant digits so the files stay readable
        const double v = juce::String (d.values[(size_t) i], 6).getDoubleValue();
        params->setProperty (paramDefs()[(size_t) i].id, v);
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "E-Piano Preset");
    root->setProperty ("version", 1);
    root->setProperty ("name", d.name);
    root->setProperty ("category", d.category);
    root->setProperty ("params", juce::var (params));

    if (! f.getParentDirectory().createDirectory() || ! f.replaceWithText (juce::JSON::toString (juce::var (root))))
    {
        error = "Couldn't write " + f.getFullPathName();
        return false;
    }
    return true;
}
} // namespace epiano
