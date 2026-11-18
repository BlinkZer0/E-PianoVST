#pragma once
// ============================================================================
//  Parameter table. Mirrors vst/ui/js/params.js (IDs, ranges, defaults, log
//  skew). The web build (web/index.html) carries the same table.
// ============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>

namespace epiano
{
enum ParamIndex
{
    osc1On, osc1Wave, osc1Pitch, osc1Fine, osc1Level,
    osc2On, osc2Wave, osc2Pitch, osc2Fine, osc2Level,
    osc3On, osc3Wave, osc3Pitch, osc3Fine, osc3Level,
    ampAttack, ampDecay, ampSustain, ampRelease, ampLevel,
    fltCutoff, fltReso, fltEnvAmt, fltDecay,
    master,
    kNumParams
};

enum class Kind { Float, Bool, Choice, Int };
enum class Unit { None, Seconds, Hz, Semitones, Cents, Percent, Decibels };

struct ParamDef
{
    const char* id;
    const char* hostName;
    Kind kind;
    float min, max, def;
    Unit unit;
    bool logSkew;
};

inline const juce::StringArray& waveNames()
{
    static const juce::StringArray names { "Sine", "Saw", "Square", "Triangle" };
    return names;
}

#define EPIANO_OSC(n, onDef)                                                                         \
    ParamDef { "osc" #n "On",    "Osc " #n " On",    Kind::Bool,   0.0f,    1.0f,   onDef, Unit::None,      false }, \
    ParamDef { "osc" #n "Wave",  "Osc " #n " Wave",  Kind::Choice, 0.0f,    3.0f,   0.0f,  Unit::None,      false }, \
    ParamDef { "osc" #n "Pitch", "Osc " #n " Pitch", Kind::Int,    -36.0f,  36.0f,  0.0f,  Unit::Semitones, false }, \
    ParamDef { "osc" #n "Fine",  "Osc " #n " Fine",  Kind::Int,    -100.0f, 100.0f, 0.0f,  Unit::Cents,     false }, \
    ParamDef { "osc" #n "Level", "Osc " #n " Level", Kind::Float,  0.0f,    1.0f,   0.7f,  Unit::Percent,   false }

inline const std::array<ParamDef, kNumParams>& paramDefs()
{
    static const std::array<ParamDef, kNumParams> defs { {
        EPIANO_OSC (1, 1.0f),
        EPIANO_OSC (2, 0.0f),
        EPIANO_OSC (3, 0.0f),
        { "ampAttack",  "Amp Attack",    Kind::Float, 0.001f, 5.0f,     0.005f, Unit::Seconds, true  },
        { "ampDecay",   "Amp Decay",     Kind::Float, 0.005f, 10.0f,    1.0f,   Unit::Seconds, true  },
        { "ampSustain", "Amp Sustain",   Kind::Float, 0.0f,   1.0f,     0.7f,   Unit::Percent, false },
        { "ampRelease", "Amp Release",   Kind::Float, 0.005f, 10.0f,    0.3f,   Unit::Seconds, true  },
        { "ampLevel",   "Amp Level",     Kind::Float, -30.0f, 6.0f,     0.0f,   Unit::Decibels, false },
        { "fltCutoff",  "Filter Cutoff", Kind::Float, 20.0f,  20000.0f, 8000.0f, Unit::Hz,     true  },
        { "fltReso",    "Filter Reso",   Kind::Float, 0.0f,   1.0f,     0.1f,   Unit::Percent, false },
        { "fltEnvAmt",  "Filter Env Amt",Kind::Float, 0.0f,   1.0f,     0.2f,   Unit::Percent, false },
        { "fltDecay",   "Filter Env Dec",Kind::Float, 0.005f, 5.0f,     0.4f,   Unit::Seconds, true  },
        { "master",     "Master",        Kind::Float, 0.0f,   1.0f,     0.7f,   Unit::Percent, false },
    } };
    return defs;
}
#undef EPIANO_OSC

inline juce::String formatValue (Unit unit, float v)
{
    switch (unit)
    {
        case Unit::Seconds:   return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms"
                                              : juce::String (v, 2) + " s";
        case Unit::Hz:        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                                  : juce::String (juce::roundToInt (v)) + " Hz";
        case Unit::Semitones: return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st";
        case Unit::Cents:     return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
        case Unit::Percent:   return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        case Unit::Decibels:  return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
        case Unit::None:
        default:              return juce::String (v, 2);
    }
}

// Same log mapping as paramToNorm/paramFromNorm in params.js
inline juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float start, float end, float n) { return start * std::pow (end / start, n); },
             [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
             [] (float start, float end, float v) { return juce::jlimit (start, end, v); } };
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& d : paramDefs())
    {
        const juce::ParameterID pid { d.id, 1 };
        const auto unit = d.unit;

        switch (d.kind)
        {
            case Kind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, d.hostName, d.def >= 0.5f));
                break;

            case Kind::Choice:
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, d.hostName, waveNames(), (int) d.def));
                break;

            case Kind::Int:
                layout.add (std::make_unique<juce::AudioParameterInt> (
                    pid, d.hostName, (int) d.min, (int) d.max, (int) d.def,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction (
                        [unit] (int v, int) { return formatValue (unit, (float) v); })));
                break;

            case Kind::Float:
            default:
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    pid, d.hostName,
                    d.logSkew ? logRange (d.min, d.max) : juce::NormalisableRange<float> (d.min, d.max),
                    d.def,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                        [unit] (float v, int) { return formatValue (unit, v); })));
                break;
        }
    }
    return layout;
}
} // namespace epiano
