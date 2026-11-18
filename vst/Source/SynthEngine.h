#pragma once
// ============================================================================
//  DSP engine — line-for-line twin of the JS engine in web/index.html.
//    Oscillator  : PolyBLEP sine / saw / square / triangle
//    Envelope    : linear attack, exponential decay/release ADSR
//    SVF         : TPT state-variable low-pass (Cytomic / Simper)
//    Voice       : 3 oscillators + filter + amp env + filter env
//    SynthEngine : 88-voice allocator + render loop
//  No JUCE dependency, no allocation on the audio thread.
// ============================================================================
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace epiano
{
constexpr int    kNumVoices      = 88;
constexpr int    kNumOsc         = 3;
constexpr double kVoiceGain      = 0.16;  // per-voice headroom
constexpr double kPitchBendRange = 2.0;   // semitones
constexpr double kSilence        = 1e-4;  // -80 dB
constexpr int    kChunk          = 32;    // max samples between control-rate updates
constexpr double kTwoPi          = 6.283185307179586;
const double     kLn1000         = std::log (1000.0);

// Parameter snapshot, taken once per processBlock.
struct EngineParams
{
    struct Osc { bool on = false; int wave = 0; double semis = 0; double level = 0; };
    std::array<Osc, kNumOsc> osc;
    double attack = 0.005, decay = 1, sustain = 0.7, release = 0.3;
    double cutoff = 8000, reso = 0.1, envAmt = 0.2, fltDecay = 0.4;
    double ampGain = 1.0;   // patch level (linear, from the Amp dB knob)
    double master = 0.7;    // global output level
};

inline double expCoef (double seconds, double sr)
{
    return std::exp (-kLn1000 / (std::max (seconds, 0.0005) * sr));
}

inline double polyBlep (double t, double dt)
{
    if (t < dt)     { t /= dt;           return t + t - t * t - 1.0; }
    if (t > 1 - dt) { t = (t - 1) / dt;  return t * t + t + t + 1.0; }
    return 0.0;
}

//==============================================================================
struct Oscillator
{
    double phase = 0;

    void reset (double p) { phase = p; }

    double next (int wave, double dt)
    {
        const double t = phase;
        double y;
        switch (wave)
        {
            case 1: // saw
                y = 2 * t - 1 - polyBlep (t, dt);
                break;
            case 2: // square
            {
                double t2 = t + 0.5; if (t2 >= 1) t2 -= 1;
                y = (t < 0.5 ? 1.0 : -1.0) + polyBlep (t, dt) - polyBlep (t2, dt);
                break;
            }
            case 3: // triangle, phase-shifted to start at 0
            {
                double t3 = t + 0.25; if (t3 >= 1) t3 -= 1;
                y = 1 - 4 * std::abs (t3 - 0.5);
                break;
            }
            default: // sine
                y = std::sin (kTwoPi * t);
        }
        double p = t + dt; if (p >= 1) p -= 1;
        phase = p;
        return y;
    }
};

//==============================================================================
struct EnvCoefs { double atkInc = 0, decCoef = 0, sus = 0, relCoef = 0; };

struct Envelope
{
    enum Stage { Idle = 0, Attack, Decay, Sustain, Release };
    int stage = Idle;
    double level = 0;

    void gateOn()  { stage = Attack; }                 // from current level: no click on retrigger
    void gateOff() { if (stage != Idle) stage = Release; }
    void kill()    { stage = Idle; level = 0; }
    bool active() const { return stage != Idle; }

    double next (const EnvCoefs& c)
    {
        switch (stage)
        {
            case Attack:
                level += c.atkInc;
                if (level >= 1) { level = 1; stage = Decay; }
                break;
            case Decay:
                level = c.sus + (level - c.sus) * c.decCoef;
                if (std::abs (level - c.sus) < kSilence)
                {
                    level = c.sus;
                    stage = c.sus < kSilence ? Idle : Sustain;
                }
                break;
            case Sustain:
                level += (c.sus - level) * 0.002;           // smooth sustain-knob moves
                if (c.sus < kSilence && level < kSilence) { level = 0; stage = Idle; }
                break;
            case Release:
                level *= c.relCoef;
                if (level < kSilence) { level = 0; stage = Idle; }
                break;
            default: break;
        }
        return level;
    }
};

//==============================================================================
struct SVF
{
    double ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0;

    void reset() { ic1 = ic2 = 0; }

    void set (double fc, double reso, double sr)
    {
        fc = std::min (std::max (fc, 20.0), sr * 0.45);
        const double g = std::tan (3.141592653589793 * fc / sr);
        const double k = 2 - 1.9 * reso; // Q 0.5 .. 10
        a1 = 1 / (1 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    double process (double x)
    {
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        return v2;
    }
};

//==============================================================================
struct Voice
{
    std::array<Oscillator, kNumOsc> osc;
    Envelope env;
    SVF filter;
    int note = -1;
    int velocity = 0;
    double velGain = 0;
    double fEnv = 0;
    uint64_t age = 0;
    bool held = false;      // key physically down
    bool sustained = false; // key up, held by sustain pedal

    bool active() const { return env.active(); }

    void release() { held = sustained = false; env.gateOff(); }
};

//==============================================================================
class SynthEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        masterSmooth = 1 - std::exp (-1 / (0.02 * sr));
        panic();
    }

    void setParams (const EngineParams& p)
    {
        params = p;
        if (firstParams) { masterGain = p.master * p.ampGain; firstParams = false; }
    }

    void noteOn (int note, int vel)
    {
        if (vel <= 0) { noteOff (note); return; }
        Voice* v = nullptr;
        for (auto& x : voices) if (x.active() && x.note == note) { v = &x; break; }   // retrigger
        if (v == nullptr) for (auto& x : voices) if (! x.active()) { v = &x; break; } // free voice
        if (v == nullptr)                                                              // steal
        {
            for (auto& x : voices)
            {
                if (v == nullptr) { v = &x; continue; }
                const bool xr = x.env.stage == Envelope::Release, vr = v->env.stage == Envelope::Release;
                if ((xr && ! vr) || (xr == vr && x.age < v->age)) v = &x;
            }
        }
        startVoice (*v, note, vel);
    }

    void noteOff (int note)
    {
        for (auto& v : voices)
        {
            if (v.note == note && v.held && v.active())
            {
                if (sustainPedal) { v.held = false; v.sustained = true; }
                else v.release();
            }
        }
    }

    void setSustain (bool on)
    {
        sustainPedal = on;
        if (! on) for (auto& v : voices) if (v.sustained) v.release();
    }

    void setPitchBend (double v) { bend = v; }

    void allNotesOff()
    {
        sustainPedal = false;
        for (auto& v : voices) if (v.active()) v.release();
    }

    void panic()
    {
        sustainPedal = false;
        for (auto& v : voices) { v.env.kill(); v.held = v.sustained = false; v.filter.reset(); }
    }

    // Writes (does not add) numSamples into L (and R if non-null).
    void render (float* L, float* R, int numSamples)
    {
        int pos = 0;
        while (pos < numSamples)
        {
            const int n = std::min (kChunk, numSamples - pos);
            std::fill (mix.begin(), mix.begin() + n, 0.0);
            updateBlockParams (n);

            int count = 0;
            for (auto& v : voices)
                if (v.active()) { ++count; renderVoice (v, n); }
            active = count;

            // output gain = patch amp level * master, smoothed together (no zipper noise)
            const double target = params.master * params.ampGain;
            double g = masterGain;
            for (int i = 0; i < n; ++i)
            {
                g += (target - g) * masterSmooth;
                const float y = (float) std::tanh (mix[(size_t) i] * g);
                L[pos + i] = y;
                if (R != nullptr) R[pos + i] = y;
                peak = std::max (peak, std::abs (y));
            }
            masterGain = g;
            pos += n;
        }
    }

    int activeVoices() const { return active; }
    bool sustainDown() const { return sustainPedal; }
    float takePeak() { const float p = peak; peak = 0; return p; }

private:
    void startVoice (Voice& v, int note, int vel)
    {
        if (! v.active())
        {
            // sine/tri start at phase 0 (clean attack), saw/square random (wider stacks)
            for (int k = 0; k < kNumOsc; ++k)
            {
                const int w = params.osc[(size_t) k].wave;
                v.osc[(size_t) k].reset (w == 1 || w == 2 ? random01() : 0.0);
            }
            v.filter.reset();
        }
        v.note = note;
        v.velocity = vel;
        v.velGain = 0.1 + 0.9 * std::pow (vel / 127.0, 1.5);
        v.age = ++ageCounter;
        v.held = true;
        v.sustained = false;
        v.fEnv = 1;
        v.env.gateOn();
    }

    void updateBlockParams (int n)
    {
        envc.atkInc  = 1 / (std::max (params.attack, 0.0005) * sr);
        envc.decCoef = expCoef (params.decay, sr);
        envc.sus     = params.sustain;
        envc.relCoef = expCoef (params.release, sr);
        fEnvBlockCoef = std::pow (expCoef (params.fltDecay, sr), n);
    }

    void renderVoice (Voice& v, int n)
    {
        const double base = v.note - 69 + bend * kPitchBendRange;
        double dt[kNumOsc], lvl[kNumOsc];
        int wave[kNumOsc];
        for (int k = 0; k < kNumOsc; ++k)
        {
            const auto& o = params.osc[(size_t) k];
            dt[k]   = std::min (440.0 * std::pow (2.0, (base + o.semis) / 12.0) / sr, 0.45);
            lvl[k]  = o.on ? o.level : 0.0;
            wave[k] = o.wave;
        }

        // Filter: cutoff * envelope (velocity-scaled) * half key-tracking
        const double velN   = v.velocity / 127.0;
        const double envOct = params.envAmt * 7 * v.fEnv * (0.35 + 0.65 * velN);
        const double keyOct = (v.note - 60) / 24.0;
        v.filter.set (params.cutoff * std::pow (2.0, envOct + keyOct), params.reso, sr);
        v.fEnv *= fEnvBlockCoef;

        const double amp = kVoiceGain * v.velGain;
        for (int i = 0; i < n; ++i)
        {
            double s = 0;
            if (lvl[0] > 0) s += lvl[0] * v.osc[0].next (wave[0], dt[0]);
            if (lvl[1] > 0) s += lvl[1] * v.osc[1].next (wave[1], dt[1]);
            if (lvl[2] > 0) s += lvl[2] * v.osc[2].next (wave[2], dt[2]);
            s = v.filter.process (s);
            mix[(size_t) i] += s * v.env.next (envc) * amp;
            if (! v.env.active()) break;
        }
        if (! v.env.active()) { v.filter.reset(); v.held = v.sustained = false; }
    }

    double random01()
    {
        rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
        return (double) (rng >> 11) * (1.0 / 9007199254740992.0);
    }

    std::array<Voice, kNumVoices> voices;
    std::array<double, kChunk> mix {};
    EngineParams params;
    EnvCoefs envc;
    double sr = 48000, bend = 0, masterGain = 0.7, masterSmooth = 0.001, fEnvBlockCoef = 0;
    uint64_t ageCounter = 0, rng = 0x9E3779B97F4A7C15ULL;
    bool sustainPedal = false, firstParams = true;
    int active = 0;
    float peak = 0;
};
} // namespace epiano
