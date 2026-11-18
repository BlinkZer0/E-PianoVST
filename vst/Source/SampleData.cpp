#include "SampleData.h"

namespace epiano
{
namespace
{
SamplePtr decodeReader (std::unique_ptr<juce::AudioFormatReader> reader, const juce::String& name, juce::String& error)
{
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0)
    {
        error = "Couldn't read audio from " + name + " (supported: WAV, AIFF, FLAC, OGG)";
        return nullptr;
    }
    const auto maxLen = (juce::int64) (reader->sampleRate * kMaxSampleSeconds);
    const int len = (int) std::min (reader->lengthInSamples, maxLen);
    const int chans = (int) juce::jlimit (1u, 8u, reader->numChannels);

    juce::AudioBuffer<float> buf (chans, len);
    reader->read (&buf, 0, len, 0, true, true);

    auto s = std::make_shared<SampleData>();
    s->name = name;
    s->rate = reader->sampleRate;
    s->data.resize ((size_t) len);
    float peak = 0;
    for (int i = 0; i < len; ++i)
    {
        float sum = 0;
        for (int c = 0; c < chans; ++c) sum += buf.getSample (c, i);
        s->data[(size_t) i] = sum / (float) chans;
        peak = std::max (peak, std::abs (s->data[(size_t) i]));
    }
    if (peak > 1e-6f)
        for (auto& x : s->data) x /= peak;

    const auto unity = reader->metadataValues.getValue ("MidiUnityNote", "");
    s->root = unity.isNotEmpty() ? juce::jlimit (0, 127, unity.getIntValue()) : 60;

    if (reader->lengthInSamples > maxLen)
        error = name + " is longer than " + juce::String ((int) kMaxSampleSeconds) + " s - using the first "
              + juce::String ((int) kMaxSampleSeconds) + " s";
    return s;
}

juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager fm;
    static const bool registered = [] { fm.registerBasicFormats(); return true; }();
    juce::ignoreUnused (registered);
    return fm;
}
} // namespace

SamplePtr decodeSampleFile (const juce::File& file, juce::String& error)
{
    return decodeReader (std::unique_ptr<juce::AudioFormatReader> (formats().createReaderFor (file)),
                         file.getFileName(), error);
}

SamplePtr decodeSampleMemory (const void* data, size_t size, const juce::String& name, juce::String& error)
{
    auto stream = std::make_unique<juce::MemoryInputStream> (data, size, true);
    return decodeReader (std::unique_ptr<juce::AudioFormatReader> (formats().createReaderFor (std::move (stream))),
                         name, error);
}

juce::var sampleToVar (const SampleData& s)
{
    std::vector<int16_t> pcm (s.data.size());
    for (size_t i = 0; i < s.data.size(); ++i)
        pcm[i] = (int16_t) std::lround (juce::jlimit (-1.0f, 1.0f, s.data[i]) * 32767.0f);
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", s.name);
    o->setProperty ("rate", s.rate);
    o->setProperty ("root", s.root);
    o->setProperty ("pcm16", juce::Base64::toBase64 (pcm.data(), pcm.size() * sizeof (int16_t)));
    return juce::var (o);
}

SamplePtr sampleFromVar (const juce::var& v)
{
    if (! v.isObject()) return nullptr;
    juce::MemoryOutputStream bytes;
    if (! juce::Base64::convertFromBase64 (bytes, v["pcm16"].toString())) return nullptr;
    const auto n = bytes.getDataSize() / sizeof (int16_t);
    if (n < 2) return nullptr;
    auto s = std::make_shared<SampleData>();
    s->name = v["name"].toString();
    s->rate = juce::jlimit (1000.0, 384000.0, (double) v.getProperty ("rate", 44100.0));
    s->root = juce::jlimit (0, 127, (int) v.getProperty ("root", 60));
    s->data.resize (n);
    const auto* p = static_cast<const uint8_t*> (bytes.getData());
    for (size_t i = 0; i < n; ++i)
        s->data[i] = (float) (int16_t) (p[2 * i] | (p[2 * i + 1] << 8)) / 32767.0f;
    return s;
}

juce::Array<juce::var> samplePeaks (const SampleData& s, int points)
{
    juce::Array<juce::var> out;
    const int n = (int) s.data.size();
    for (int i = 0; i < points; ++i)
    {
        const int a = (int) ((juce::int64) i * n / points), b = std::max (a + 1, (int) ((juce::int64) (i + 1) * n / points));
        float mn = 0, mx = 0;
        for (int j = a; j < b && j < n; ++j) { mn = std::min (mn, s.data[(size_t) j]); mx = std::max (mx, s.data[(size_t) j]); }
        out.add (std::round (mn * 1000) / 1000);
        out.add (std::round (mx * 1000) / 1000);
    }
    return out;
}
} // namespace epiano
