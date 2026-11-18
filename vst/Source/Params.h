#pragma once
// ============================================================================
//  Parameter table. Mirrors vst/ui/js/params.js (IDs, ranges, defaults, log
//  skew, choice lists). The web build (web/index.html) is generated from that.
//
//  New parameters are only ever APPENDED (indices + IDs of existing ones never
//  change) and default to a neutral value, so v0.9 presets/projects load and
//  sound exactly as before.
// ============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <vector>

namespace epiano
{
constexpr int kNumLfos = 2;
constexpr int kNumModSlotsParam = 8;
constexpr int kModSlotFields = 5; // On, Src, Via, Dst, Amt
constexpr int kLfoFields = 9;     // On, Wave, Rate, Sync, Div, Mode, Trig, Phase, Amount

enum ParamIndex
{
    // ---- v0.9 -------------------------------------------------------------
    osc1On, osc1Wave, osc1Pitch, osc1Fine, osc1Level,
    osc2On, osc2Wave, osc2Pitch, osc2Fine, osc2Level,
    osc3On, osc3Wave, osc3Pitch, osc3Fine, osc3Level,
    ampAttack, ampDecay, ampSustain, ampRelease, ampLevel,
    fltCutoff, fltReso, fltEnvAmt, fltDecay,
    master,
    // ---- modulation -------------------------------------------------------
    lfo1On, lfo1Wave, lfo1Rate, lfo1Sync, lfo1Div, lfo1Mode, lfo1Trig, lfo1Phase, lfo1Amount,
    lfo2On, lfo2Wave, lfo2Rate, lfo2Sync, lfo2Div, lfo2Mode, lfo2Trig, lfo2Phase, lfo2Amount,
    mod1On, // slot s field f = mod1On + s * kModSlotFields + f
    modLast = mod1On + kNumModSlotsParam * kModSlotFields - 1,
    // ---- drive ------------------------------------------------------------
    drvOn, drvType, drvDrive, drvTone, drvMix, drvOut,
    // ---- voice ------------------------------------------------------------
    voiceMode, glideOn, glideTime, glideMode,
    kNumParams
};

inline int lfoParam (int lfo, int field)  { return lfo1On + lfo * kLfoFields + field; }
inline int modParam (int slot, int field) { return mod1On + slot * kModSlotFields + field; }

enum class Kind { Float, Bool, Choice, Int };
enum class Unit { None, Seconds, Hz, LfoHz, Semitones, Cents, Percent, BipolarPercent, Decibels, Degrees };

struct ParamDef
{
    juce::String id;
    juce::String hostName;
    Kind kind;
    float min, max, def;
    Unit unit;
    bool logSkew;
    juce::StringArray choices; // Kind::Choice only
};

// Choice lists — order is the stored value; must match params.js and SynthEngine.h enums.
inline juce::StringArray oscWaveChoices()   { return { "Sine", "Saw", "Square", "Triangle" }; }
inline juce::StringArray lfoWaveChoices()   { return { "Sine", "Triangle", "Square", "Saw Up", "Saw Down", "S&H" }; }
inline juce::StringArray lfoDivChoices()    { return { "1/1", "1/2D", "1/2", "1/2T", "1/4D", "1/4", "1/4T", "1/8D", "1/8", "1/8T",
                                                       "1/16D", "1/16", "1/16T", "1/32", "1/32T" }; }
inline juce::StringArray lfoModeChoices()   { return { "Global", "Voice" }; }
inline juce::StringArray lfoTrigChoices()   { return { "Free", "Note" }; }
inline juce::StringArray modSrcChoices()    { return { "None", "LFO 1", "LFO 2", "Amp Env", "Filter Env", "Velocity", "Mod Wheel" }; }
inline juce::StringArray modDstChoices()    { return { "None", "Cutoff", "Resonance", "Pitch (All)", "Osc 1 Pitch", "Osc 2 Pitch",
                                                       "Osc 3 Pitch", "Osc 1 Level", "Osc 2 Level", "Osc 3 Level", "Amplitude", "Pan" }; }
inline juce::StringArray driveTypeChoices() { return { "Soft", "Hard", "Fold", "Crush" }; }
inline juce::StringArray voiceModeChoices() { return { "Poly", "Mono", "Legato" }; }
inline juce::StringArray glideModeChoices() { return { "Always", "Legato" }; }

inline const std::vector<ParamDef>& paramDefs()
{
    static const std::vector<ParamDef> defs = []
    {
        std::vector<ParamDef> d;
        auto add = [&d] (juce::String id, juce::String name, Kind kind, float mn, float mx, float def,
                         Unit unit = Unit::None, bool log = false, juce::StringArray choices = {})
        {
            d.push_back ({ id, name, kind, mn, mx, def, unit, log, choices });
        };
        auto choice = [&add] (juce::String id, juce::String name, juce::StringArray c, int def)
        {
            add (id, name, Kind::Choice, 0.0f, (float) (c.size() - 1), (float) def, Unit::None, false, c);
        };

        for (int n = 1; n <= 3; ++n)
        {
            const juce::String o = "osc" + juce::String (n), on = "Osc " + juce::String (n);
            add (o + "On", on + " On", Kind::Bool, 0, 1, n == 1 ? 1.0f : 0.0f);
            choice (o + "Wave", on + " Wave", oscWaveChoices(), 0);
            add (o + "Pitch", on + " Pitch", Kind::Int, -36, 36, 0, Unit::Semitones);
            add (o + "Fine", on + " Fine", Kind::Int, -100, 100, 0, Unit::Cents);
            add (o + "Level", on + " Level", Kind::Float, 0, 1, 0.7f, Unit::Percent);
        }
        add ("ampAttack",  "Amp Attack",     Kind::Float, 0.001f, 5.0f,     0.005f,  Unit::Seconds, true);
        add ("ampDecay",   "Amp Decay",      Kind::Float, 0.005f, 10.0f,    1.0f,    Unit::Seconds, true);
        add ("ampSustain", "Amp Sustain",    Kind::Float, 0.0f,   1.0f,     0.7f,    Unit::Percent);
        add ("ampRelease", "Amp Release",    Kind::Float, 0.005f, 10.0f,    0.3f,    Unit::Seconds, true);
        add ("ampLevel",   "Amp Level",      Kind::Float, -30.0f, 6.0f,     0.0f,    Unit::Decibels);
        add ("fltCutoff",  "Filter Cutoff",  Kind::Float, 20.0f,  20000.0f, 8000.0f, Unit::Hz, true);
        add ("fltReso",    "Filter Reso",    Kind::Float, 0.0f,   1.0f,     0.1f,    Unit::Percent);
        add ("fltEnvAmt",  "Filter Env Amt", Kind::Float, 0.0f,   1.0f,     0.2f,    Unit::Percent);
        add ("fltDecay",   "Filter Env Dec", Kind::Float, 0.005f, 5.0f,     0.4f,    Unit::Seconds, true);
        add ("master",     "Master",         Kind::Float, 0.0f,   1.0f,     0.7f,    Unit::Percent);

        for (int n = 1; n <= kNumLfos; ++n)
        {
            const juce::String l = "lfo" + juce::String (n), ln = "LFO " + juce::String (n);
            add (l + "On", ln + " On", Kind::Bool, 0, 1, 0);
            choice (l + "Wave", ln + " Wave", lfoWaveChoices(), 0);
            add (l + "Rate", ln + " Rate", Kind::Float, 0.02f, 40.0f, 2.0f, Unit::LfoHz, true);
            add (l + "Sync", ln + " Sync", Kind::Bool, 0, 1, 0);
            choice (l + "Div", ln + " Division", lfoDivChoices(), 8); // 1/8
            choice (l + "Mode", ln + " Mode", lfoModeChoices(), 0);
            choice (l + "Trig", ln + " Trigger", lfoTrigChoices(), 0);
            add (l + "Phase", ln + " Phase", Kind::Float, 0, 360, 0, Unit::Degrees);
            add (l + "Amount", ln + " Amount", Kind::Float, 0, 1, 1, Unit::Percent);
        }
        for (int n = 1; n <= kNumModSlotsParam; ++n)
        {
            const juce::String m = "mod" + juce::String (n), mn = "Mod " + juce::String (n);
            add (m + "On", mn + " On", Kind::Bool, 0, 1, 1);
            choice (m + "Src", mn + " Source", modSrcChoices(), 0);
            choice (m + "Via", mn + " Via", modSrcChoices(), 0);
            choice (m + "Dst", mn + " Destination", modDstChoices(), 0);
            add (m + "Amt", mn + " Amount", Kind::Float, -1, 1, 0, Unit::BipolarPercent);
        }
        add ("drvOn", "Drive On", Kind::Bool, 0, 1, 0);
        choice ("drvType", "Drive Type", driveTypeChoices(), 0);
        add ("drvDrive", "Drive", Kind::Float, 0, 1, 0.3f, Unit::Percent);
        add ("drvTone", "Drive Tone", Kind::Float, 0, 1, 1.0f, Unit::Percent);
        add ("drvMix", "Drive Mix", Kind::Float, 0, 1, 1.0f, Unit::Percent);
        add ("drvOut", "Drive Output", Kind::Float, -24, 6, 0, Unit::Decibels);

        choice ("voiceMode", "Voice Mode", voiceModeChoices(), 0);
        add ("glideOn", "Glide On", Kind::Bool, 0, 1, 0);
        add ("glideTime", "Glide Time", Kind::Float, 0.001f, 5.0f, 0.1f, Unit::Seconds, true);
        choice ("glideMode", "Glide Mode", glideModeChoices(), 0);

        jassert ((int) d.size() == kNumParams);
        return d;
    }();
    return defs;
}

inline juce::String formatValue (Unit unit, float v)
{
    switch (unit)
    {
        case Unit::Seconds:   return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms"
                                              : juce::String (v, 2) + " s";
        case Unit::Hz:        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                                  : juce::String (juce::roundToInt (v)) + " Hz";
        case Unit::LfoHz:     return juce::String (v, v < 1.0f ? 3 : (v < 10.0f ? 2 : 1)) + " Hz";
        case Unit::Semitones: return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st";
        case Unit::Cents:     return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
        case Unit::Percent:   return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        case Unit::BipolarPercent: return (v > 0.004f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%";
        case Unit::Decibels:  return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
        case Unit::Degrees:   return juce::String (juce::roundToInt (v)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
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
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, d.hostName, d.choices, (int) d.def));
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
