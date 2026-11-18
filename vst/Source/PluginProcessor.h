#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Params.h"
#include "PresetManager.h"
#include "SynthEngine.h"
#include "EngineParamsFromValues.h"

class EPianoProcessor : public juce::AudioProcessor
{
public:
    EPianoProcessor();
    ~EPianoProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // The 80 factory presets are exposed to the host as programs.
    int getNumPrograms() override { return (int) presetManager.factory().size(); }
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Presets (message thread)
    struct PresetInfo { juce::String id, name, category; };
    PresetInfo currentPreset() const;
    std::atomic<int> presetVersion { 0 };   // bumps whenever currentPreset() changes

    bool loadPresetById (const juce::String& id, juce::String& error);
    bool loadPresetFile (const juce::File& file, juce::String& error);
    bool saveUserPreset (const juce::String& name, bool overwrite, bool& alreadyExists, juce::String& error);
    bool deleteUserPreset (const juce::String& id, juce::String& error);

    epiano::PresetManager presetManager;

    //==============================================================================
    // Used by the editor (message thread)
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // UI notes in, all sounding notes out
    juce::RangedAudioParameter* param (int index) const { return params[(size_t) index]; }
    float paramValue (int index) const { return rawValues[(size_t) index]->load(); }
    void requestPanic() { panicRequested = true; }
    void setUiModWheel (float v) { uiModWheel = juce::jlimit (0.0f, 1.0f, v); }

    std::atomic<int>   statVoices { 0 };
    std::atomic<float> statPeak { 0.0f };
    std::atomic<bool>  statSustain { false };
    std::atomic<float> statModWheel { 0.0f };
    std::atomic<float> statBpm { 120.0f };

private:
    void applyPreset (const epiano::PresetData&);
    void setPresetInfo (const juce::String& id, const juce::String& name, const juce::String& category);
    epiano::EngineParams readParams() const;
    void handleMidi (const juce::MidiMessage&);

    std::array<juce::RangedAudioParameter*, epiano::kNumParams> params {};
    std::array<std::atomic<float>*, epiano::kNumParams> rawValues {};
    epiano::SynthEngine engine;
    std::atomic<int> currentProgram { 0 };
    std::atomic<bool> panicRequested { false };
    std::atomic<float> uiModWheel { -1.0f };   // -1 = no pending change

    mutable juce::CriticalSection presetLock;
    PresetInfo presetInfo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EPianoProcessor)
};
