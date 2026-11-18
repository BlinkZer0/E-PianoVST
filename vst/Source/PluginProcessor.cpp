#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace epiano;

EPianoProcessor::EPianoProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EPianoState", createParameterLayout())
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& id = paramDefs()[(size_t) i].id;
        params[(size_t) i] = apvts.getParameter (id);
        rawValues[(size_t) i] = apvts.getRawParameterValue (id);
        jassert (params[(size_t) i] != nullptr && rawValues[(size_t) i] != nullptr);
    }
    setCurrentProgram (0); // a fresh instance starts on the first factory preset
}

//==============================================================================
void EPianoProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    keyboardState.reset();
}

bool EPianoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

EngineParams EPianoProcessor::readParams() const
{
    return makeEngineParams ([this] (int i) { return paramValue (i); });
}

void EPianoProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())                                   engine.noteOn (m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())                             engine.noteOff (m.getNoteNumber());
    else if (m.isSustainPedalOn())                      engine.setSustain (true);
    else if (m.isSustainPedalOff())                     engine.setSustain (false);
    else if (m.isPitchWheel())                          engine.setPitchBend ((m.getPitchWheelValue() - 8192) / 8192.0);
    else if (m.isControllerOfType (1))                  engine.setModWheel (m.getControllerValue() / 127.0);
    else if (m.isAllSoundOff())                         engine.panic();
    else if (m.isAllNotesOff() || m.isResetAllControllers()) engine.allNotesOff();
}

void EPianoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    if (panicRequested.exchange (false))
        engine.panic();
    if (const float w = uiModWheel.exchange (-1.0f); w >= 0.0f)   // mod wheel dragged in the UI
        engine.setModWheel (w);

    // Merges notes played on the UI keyboard into `midi`, and tracks all
    // sounding notes so the editor can light up the keys.
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    engine.setParams (readParams());

    // Host tempo + song position (tempo-synced LFOs). No play head -> 120 BPM.
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            if (const auto b = pos->getBpm()) bpm = *b;
            if (const auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }
    }
    engine.setTransport (bpm, ppq, playing);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    // Sample-accurate: render up to each MIDI event, then apply it.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, n, meta.samplePosition);
        if (t > pos)
        {
            engine.render (L + pos, R != nullptr ? R + pos : nullptr, t - pos);
            pos = t;
        }
        handleMidi (meta.getMessage());
    }
    if (pos < n)
        engine.render (L + pos, R != nullptr ? R + pos : nullptr, n - pos);

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    statVoices.store (engine.activeVoices());
    statSustain.store (engine.sustainDown());
    statModWheel.store ((float) engine.modWheel());
    statBpm.store ((float) engine.tempo());
    const float pk = engine.takePeak();
    if (pk > statPeak.load()) statPeak.store (pk);
}

//==============================================================================
// Sets every parameter except master. Anything the preset doesn't specify
// (e.g. an older file saved before a parameter existed) goes to its default.
void EPianoProcessor::applyPreset (const PresetData& preset)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        if (i == master) continue;
        const float stored = preset.values[(size_t) i];
        const float v = std::isnan (stored) ? paramDefs()[(size_t) i].def : stored;
        auto* p = params[(size_t) i];
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void EPianoProcessor::setPresetInfo (const juce::String& id, const juce::String& name, const juce::String& category)
{
    {
        const juce::ScopedLock sl (presetLock);
        presetInfo = { id, name, category };
    }
    ++presetVersion;
}

EPianoProcessor::PresetInfo EPianoProcessor::currentPreset() const
{
    const juce::ScopedLock sl (presetLock);
    return presetInfo;
}

void EPianoProcessor::setCurrentProgram (int index)
{
    index = juce::jlimit (0, getNumPrograms() - 1, index);
    currentProgram = index;
    const auto& preset = presetManager.factory()[(size_t) index];
    applyPreset (preset);
    setPresetInfo ("factory:" + juce::String (index), preset.name, preset.category);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

const juce::String EPianoProcessor::getProgramName (int index)
{
    if (index < 0 || index >= getNumPrograms()) return {};
    const auto& p = presetManager.factory()[(size_t) index];
    return p.category + " - " + p.name;
}

bool EPianoProcessor::loadPresetById (const juce::String& id, juce::String& error)
{
    if (id.startsWith ("factory:"))
    {
        const int index = id.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (index < 0 || index >= getNumPrograms()) { error = "Unknown preset"; return false; }
        setCurrentProgram (index);
        return true;
    }

    presetManager.rescanIfChanged();
    if (const auto* entry = presetManager.findUser (id))
    {
        PresetData data;
        if (! PresetManager::readFile (entry->file, data, error)) return false;
        applyPreset (data);
        setPresetInfo (entry->id, entry->name, entry->category);
        return true;
    }
    error = "Preset not found (was it moved or deleted?)";
    return false;
}

bool EPianoProcessor::loadPresetFile (const juce::File& file, juce::String& error)
{
    PresetData data;
    if (! PresetManager::readFile (file, data, error)) return false;
    applyPreset (data);
    const auto userId = presetManager.idForFile (file);
    if (userId.isNotEmpty())
        setPresetInfo (userId, file.getFileNameWithoutExtension(), presetManager.categoryForFile (file));
    else
        setPresetInfo ("file:" + file.getFullPathName(), data.name, "File");
    return true;
}

bool EPianoProcessor::saveUserPreset (const juce::String& rawName, bool overwrite, bool& alreadyExists, juce::String& error)
{
    alreadyExists = false;
    const auto name = rawName.trim();
    if (name.isEmpty()) { error = "Enter a name for the preset"; return false; }

    const auto file = presetManager.fileForName (name);
    if (file.existsAsFile() && ! overwrite) { alreadyExists = true; return false; }

    PresetData data;
    data.name = name;
    data.category = "User";
    for (int i = 0; i < kNumParams; ++i)
        if (i != master) data.values[(size_t) i] = paramValue (i);

    if (! PresetManager::writeFile (file, data, error)) return false;
    presetManager.rescanIfChanged();
    setPresetInfo (presetManager.idForFile (file), file.getFileNameWithoutExtension(), presetManager.categoryForFile (file));
    return true;
}

bool EPianoProcessor::deleteUserPreset (const juce::String& id, juce::String& error)
{
    presetManager.rescanIfChanged();
    const auto* entry = presetManager.findUser (id);
    if (entry == nullptr) { error = "Only user presets can be deleted"; return false; }
    const auto file = entry->file;
    if (! file.moveToTrash() && ! file.deleteFile()) { error = "Couldn't delete " + file.getFullPathName(); return false; }
    presetManager.rescanIfChanged();
    if (currentPreset().id == id)                       // keep the sound, drop the link to the file
        setPresetInfo ({}, currentPreset().name, "User");
    return true;
}

//==============================================================================
void EPianoProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    const auto info = currentPreset();
    state.setProperty ("program", currentProgram.load(), nullptr);
    state.setProperty ("presetId", info.id, nullptr);
    state.setProperty ("presetName", info.name, nullptr);
    state.setProperty ("presetCategory", info.category, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void EPianoProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
            auto name = state.getProperty ("presetName").toString();
            if (name.isEmpty()) // project saved by an older build: fall back to its program
            {
                const auto& p = presetManager.factory()[(size_t) currentProgram.load()];
                setPresetInfo ("factory:" + juce::String (currentProgram.load()), p.name, p.category);
            }
            else
            {
                setPresetInfo (state.getProperty ("presetId").toString(), name,
                               state.getProperty ("presetCategory").toString());
            }
            apvts.replaceState (state);
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* EPianoProcessor::createEditor()
{
    return new EPianoEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EPianoProcessor();
}
