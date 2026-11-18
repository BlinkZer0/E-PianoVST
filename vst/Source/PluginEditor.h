#pragma once

#include <bitset>
#include "PluginProcessor.h"

// Hosts the HTML/SVG UI (vst/ui, compiled in as BinaryData) in a WebView2 and
// bridges it to the processor:
//   UI -> C++ events : uiReady, setParam{id,value}, gesture{id,begin},
//                      noteOn{note,vel}, noteOff{note}, panic,
//                      loadPreset{id}, savePreset{name,overwrite}, deletePreset{id},
//                      loadPresetFile, openPresetFolder
//   C++ -> UI events : params{id:value...}, notes{on[],off[]},
//                      stats{voices,peak,sustain}, config{...},
//                      presetList{presets[],folder}, preset{id,name,category},
//                      saveResult{ok,exists,name,error}, message{text}
class EPianoEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EPianoEditor (EPianoProcessor&);
    ~EPianoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::WebBrowserComponent::Options makeBrowserOptions();
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    int indexForId (const juce::String& id) const;
    void onUiReady();
    void onSetParam (const juce::var&);
    void onGesture (const juce::var&);
    void sendConfig();
    void sendPresetList();
    void sendPreset();
    void sendMessage (const juce::String& text);
    void chooseAndLoadPresetFile();

    EPianoProcessor& processor;

    std::array<float, epiano::kNumParams> lastSent {};
    std::array<bool, epiano::kNumParams> inGesture {};
    std::bitset<128> lastNotes;
    int lastPresetVersion = -1;
    int ticks = 0;
    bool uiReady = false;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::WebBrowserComponent browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EPianoEditor)
};
