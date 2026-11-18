#pragma once
// ============================================================================
//  User samples for the oscillators (Sample mode: One-shot / Loop / Cycle).
//  Mono, peak-normalised, max kMaxSeconds. Stored inside projects and .epreset
//  files as { name, rate, root, pcm16: <standard base64 of int16 LE> } so they
//  travel with the preset and load identically in the web build.
// ============================================================================
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

namespace epiano
{
constexpr double kMaxSampleSeconds = 10.0;

struct SampleData
{
    juce::String name;
    double rate = 44100.0;
    int root = 60;                 // MIDI note the file plays back at unshifted (WAV 'smpl' unity note, else C4)
    std::vector<float> data;       // mono, peak-normalised

    double seconds() const { return rate > 0 ? (double) data.size() / rate : 0.0; }
};

using SamplePtr = std::shared_ptr<const SampleData>;

// Decoding (wav / aiff / flac / ogg via JUCE). Returns nullptr + error on failure.
SamplePtr decodeSampleFile (const juce::File& file, juce::String& error);
SamplePtr decodeSampleMemory (const void* data, size_t size, const juce::String& name, juce::String& error);

// Preset / state serialisation
juce::var sampleToVar (const SampleData& s);
SamplePtr sampleFromVar (const juce::var& v);

// Waveform overview for the UI: `points` (min, max) pairs
juce::Array<juce::var> samplePeaks (const SampleData& s, int points);
} // namespace epiano
