#pragma once
// ============================================================================
//  DSP engine. Its JavaScript twin is vst/webhost/dsp-worklet.js — the two are
//  kept line-for-line equivalent and checked by vst/tests/run-parity.ps1.
//
//  Per voice:  3 osc -> SVF low-pass -> amp env x velocity x amp-mod -> DRIVE -> pan
//  Global:     sum -> (Amp level x Master, smoothed) -> soft clip
//
//  Control rate: every kChunk (32) samples the modulation matrix is evaluated
//  and new per-voice targets (pitch, osc levels, filter g/k, amplitude, pan)
//  are computed; every one of them is ramped linearly per sample so nothing
//  steps or clicks.
//
//  No allocation, no locks, no JUCE dependency.
// ============================================================================
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace epiano
{
constexpr int    kNumVoices      = 88;
constexpr int    kNumOsc         = 3;
constexpr int    kNumLfo         = 2;
constexpr int    kNumModSlots    = 8;
constexpr int    kChunk          = 32;
constexpr double kVoiceGain      = 0.16;  // per-voice headroom
constexpr double kPitchBendRange = 2.0;   // semitones
constexpr double kSilence        = 1e-4;  // -80 dB
constexpr double kPi             = 3.141592653589793;
constexpr double kTwoPi          = 6.283185307179586;
constexpr double kSqrt2          = 1.4142135623730951;
constexpr double kLn2            = 0.6931471805599453;
const double     kLn1000         = std::log (1000.0);

// What a mod amount of 1.0 means for each destination
constexpr double kCutoffModOctaves = 5.0;   // log-frequency domain
constexpr double kPitchModSemis    = 12.0;

// Choice indices (must match the choice lists in Params.h / params.js)
enum ModSource { srcNone, srcLfo1, srcLfo2, srcAmpEnv, srcFltEnv, srcVelocity, srcModWheel, kNumModSources };
enum ModDest   { dstNone, dstCutoff, dstReso, dstPitchAll, dstOsc1Pitch, dstOsc2Pitch, dstOsc3Pitch,
                 dstOsc1Level, dstOsc2Level, dstOsc3Level, dstAmp, dstPan, kNumModDests };
enum LfoWave   { lfoSine, lfoTriangle, lfoSquare, lfoSawUp, lfoSawDown, lfoSampleHold };
enum DriveType { driveSoft, driveHard, driveFold, driveCrush };
enum VoiceMode { modePoly, modeMono, modeLegato };

// Synced LFO divisions, in quarter-note beats (order = lfoNDiv choice list)
constexpr double kLfoDivBeats[] = { 4.0, 3.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0,
                                    0.375, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0 };
constexpr int kNumLfoDivs = 15;

// Parameter snapshot, taken once per processBlock.
struct EngineParams
{
    struct Osc { bool on = false; int wave = 0; double semis = 0; double level = 0; };
    struct Lfo
    {
        bool on = false; int wave = lfoSine; double rate = 2; bool sync = false; int div = 8;
        bool perVoice = false; bool retrig = false; double phase = 0; /* 0..1 */ double amount = 1;
    };
    struct ModSlot { bool on = true; int src = srcNone, via = srcNone, dst = dstNone; double amount = 0; };
    struct Drive { bool on = false; int type = driveSoft; double drive = 0.3, tone = 1, mix = 1, outGain = 1; };

    std::array<Osc, kNumOsc> osc;
    double attack = 0.005, decay = 1, sustain = 0.7, release = 0.3;
    double cutoff = 8000, reso = 0.1, envAmt = 0.2, fltDecay = 0.4;
    double ampGain = 1.0;   // patch level (linear)
    double master = 0.7;    // global output level
    std::array<Lfo, kNumLfo> lfo;
    std::array<ModSlot, kNumModSlots> mod;
    Drive drive;
    int voiceMode = modePoly;
    bool glideOn = false;
    double glideTime = 0.1;
    bool glideLegatoOnly = false;
};

// Deterministic RNG (identical sequence in the JS engine: Math.imul LCG)
struct Rng
{
    uint32_t s = 0x2545F491u;
    void seed (uint32_t v) { s = v; }
    uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
    double uni() { return (double) (next() >> 8) * (1.0 / 16777216.0); } // [0, 1)
    double bi()  { return uni() * 2.0 - 1.0; }                            // [-1, 1)
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
                level += (c.sus - level) * 0.002;
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
// TPT state-variable low-pass. g/k are passed per sample so cutoff and
// resonance can be modulated smoothly (the TPT structure stays stable).
struct SVF
{
    double ic1 = 0, ic2 = 0;

    void reset() { ic1 = ic2 = 0; }

    static double gFor (double fc, double sr)
    {
        fc = std::min (std::max (fc, 20.0), sr * 0.45);
        return std::tan (kPi * fc / sr);
    }
    static double kFor (double reso) { return 2 - 1.9 * reso; } // Q 0.5 .. 10

    double process (double x, double g, double k)
    {
        const double a1 = 1 / (1 + g * (g + k));
        const double a2 = g * a1;
        const double a3 = g * a2;
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        return v2;
    }
};

//==============================================================================
struct Lfo
{
    double phase = 0, held = 0, out = 0;
    bool primed = false;

    void start (double p, Rng& rng)         { phase = p; held = rng.bi(); primed = false; } // fresh voice
    void retrigger (double p, Rng& rng)     { phase = p; held = rng.bi(); }                 // keep smoothing

    double shape (int wave, double offset) const
    {
        double t = phase + offset;
        t -= std::floor (t);
        switch (wave)
        {
            case lfoTriangle:   { double t3 = t + 0.25; if (t3 >= 1) t3 -= 1; return 1 - 4 * std::abs (t3 - 0.5); }
            case lfoSquare:     return t < 0.5 ? 1.0 : -1.0;
            case lfoSawUp:      return 2 * t - 1;
            case lfoSawDown:    return 1 - 2 * t;
            case lfoSampleHold: return held;
            default:            return std::sin (kTwoPi * t);
        }
    }

    void advance (double inc, Rng& rng)
    {
        phase += inc;
        if (phase >= 1) { phase -= std::floor (phase); held = rng.bi(); }
    }

    // ~2 ms smoothing: square / S&H steps don't click, sine is unaffected
    double smooth (double target, double coef)
    {
        if (! primed) { out = target; primed = true; }
        else out += (target - out) * coef;
        return out;
    }
};

//==============================================================================
struct DriveState
{
    double x1 = 0, F1 = 0, lp = 0, hold = 0;
    int holdLeft = 0;
    void reset() { x1 = F1 = lp = hold = 0; holdLeft = 0; }
};

inline double logCosh (double x)
{
    x = std::abs (x);
    return x + std::log1p (std::exp (-2 * x)) - kLn2;
}

inline double hardClip (double x) { return x < -1 ? -1 : (x > 1 ? 1 : x); }
inline double hardClipAnti (double x) { const double a = std::abs (x); return a <= 1 ? 0.5 * x * x : a - 0.5; }

// triangle wavefolder: identity on [-1, 1], folds back beyond
inline double fold (double x)
{
    double t = (x + 1) * 0.25;
    t -= std::floor (t);
    return 1 - 4 * std::abs (t - 0.5);
}

// First-order antiderivative anti-aliasing (ADAA) for soft / hard clipping
inline double adaa (DriveState& st, double x, bool hard)
{
    const double F = hard ? hardClipAnti (x) : logCosh (x);
    const double dx = x - st.x1;
    const double y = std::abs (dx) < 1e-6 ? (hard ? hardClip (0.5 * (x + st.x1)) : std::tanh (0.5 * (x + st.x1)))
                                          : (F - st.F1) / dx;
    st.x1 = x;
    st.F1 = F;
    return y;
}

//==============================================================================
struct VoiceCtl { double dt[kNumOsc] {}, lvl[kNumOsc] {}, g = 0, k = 2, amp = 0, panL = 1, panR = 1; };

struct Voice
{
    std::array<Oscillator, kNumOsc> osc;
    std::array<Lfo, kNumLfo> lfo;
    Envelope env;
    SVF filter;
    DriveState drive;
    int note = -1;
    int velocity = 0;
    double velGain = 0;
    double fEnv = 0;
    uint64_t age = 0;
    bool held = false;      // key physically down
    bool sustained = false; // key up, held by sustain pedal
    double pitch = 60, glideFrom = 60, glideTo = 60, glidePos = 1; // pitch in (fractional) MIDI notes
    VoiceCtl cur;
    bool snap = true;       // first chunk after a fresh start: no ramp from stale values

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
        ctlSmooth    = 1 - std::exp (-kChunk / (0.005 * sr)); // ~5 ms, per chunk
        lfoSmooth    = 1 - std::exp (-kChunk / (0.002 * sr)); // ~2 ms
        wheelSmooth  = 1 - std::exp (-kChunk / (0.010 * sr)); // ~10 ms
        rng.seed (0x2545F491u);
        for (auto& l : lfoG) { l = Lfo{}; }
        slotAmt.fill (0);
        firstParams = true;
        panic();
    }

    void setParams (const EngineParams& p)
    {
        params = p;
        if (firstParams)
        {
            masterGain = p.master * p.ampGain;
            drvGain = driveGainFor (p.drive.drive); drvMix = p.drive.mix; drvOut = p.drive.outGain;
            firstParams = false;
        }
    }

    // Host transport (VST3). Without a host it stays at 120 BPM / not playing.
    void setTransport (double bpmIn, double ppqIn, bool playingIn)
    {
        bpm = bpmIn > 0 ? bpmIn : 120.0;
        ppqAtBlock = ppqIn;
        playing = playingIn;
        samplesIntoBlock = 0;
    }

    void noteOn (int note, int vel)
    {
        if (vel <= 0) { noteOff (note); return; }
        const int heldBefore = keyCount;
        pushKey (note);

        if (params.voiceMode == modePoly)
        {
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
            const bool glide = params.glideOn && lastNote >= 0 && (! params.glideLegatoOnly || heldBefore > 0);
            startVoice (*v, note, vel, glide ? lastPitch : (double) note);
            if (heldBefore == 0) retriggerGlobalLfos();
        }
        else
        {
            Voice& v = voices[0];
            const bool overlapping = heldBefore > 0 && v.active() && v.env.stage != Envelope::Release;
            const bool glide = params.glideOn && lastNote >= 0 && (overlapping || ! params.glideLegatoOnly);
            if (params.voiceMode == modeLegato && overlapping)
            {
                setVoicePitch (v, note, glide);      // legato: new pitch, envelopes keep going
                v.note = note;
                v.held = true;
                v.sustained = false;
            }
            else
            {
                startVoice (v, note, vel, glide ? (v.active() ? v.pitch : lastPitch) : (double) note);
                retriggerGlobalLfos();
            }
        }
        lastNote = note;
        lastPitch = note;
    }

    void noteOff (int note)
    {
        removeKey (note);
        const bool mono = params.voiceMode != modePoly;
        for (int i = mono ? 1 : 0; i < kNumVoices; ++i)   // poly (and leftovers from poly when in mono)
        {
            auto& v = voices[(size_t) i];
            if (v.note == note && v.held && v.active())
            {
                if (sustainPedal) { v.held = false; v.sustained = true; }
                else v.release();
            }
        }
        if (! mono) return;

        Voice& v = voices[0];
        if (! (v.active() && v.held && v.note == note)) return;
        if (keyCount > 0)
        {
            const int back = keys[(size_t) (keyCount - 1)];   // return to the last key still held
            setVoicePitch (v, back, params.glideOn);
            v.note = back;
            lastNote = back;
            lastPitch = back;
        }
        else if (sustainPedal) { v.held = false; v.sustained = true; }
        else v.release();
    }

    void setSustain (bool on)
    {
        sustainPedal = on;
        if (! on) for (auto& v : voices) if (v.sustained) v.release();
    }

    void setPitchBend (double v) { bend = v; }
    void setModWheel (double v)  { wheelTarget = std::min (std::max (v, 0.0), 1.0); }

    void allNotesOff()
    {
        sustainPedal = false;
        keyCount = 0;
        for (auto& v : voices) if (v.active()) v.release();
    }

    void panic()
    {
        sustainPedal = false;
        keyCount = 0;
        for (auto& v : voices)
        {
            v.env.kill(); v.held = v.sustained = false; v.filter.reset(); v.drive.reset();
        }
    }

    // Writes (does not add) numSamples into L (and R if non-null; mono sums).
    void render (float* L, float* R, int numSamples)
    {
        int pos = 0;
        while (pos < numSamples)
        {
            const int n = std::min (kChunk, numSamples - pos);
            std::fill (mixL.begin(), mixL.begin() + n, 0.0);
            std::fill (mixR.begin(), mixR.begin() + n, 0.0);
            updateControl (n);

            int count = 0;
            for (auto& v : voices)
                if (v.active()) { ++count; renderVoice (v, n); }
            active = count;

            const double target = params.master * params.ampGain;
            double g = masterGain;
            for (int i = 0; i < n; ++i)
            {
                g += (target - g) * masterSmooth;
                if (R != nullptr)
                {
                    const float yl = (float) std::tanh (mixL[(size_t) i] * g);
                    const float yr = (float) std::tanh (mixR[(size_t) i] * g);
                    L[pos + i] = yl;
                    R[pos + i] = yr;
                    peak = std::max (peak, std::max (std::abs (yl), std::abs (yr)));
                }
                else
                {
                    const float y = (float) std::tanh (0.5 * (mixL[(size_t) i] + mixR[(size_t) i]) * g);
                    L[pos + i] = y;
                    peak = std::max (peak, std::abs (y));
                }
            }
            masterGain = g;
            pos += n;
            samplesIntoBlock += n;
        }
    }

    int activeVoices() const { return active; }
    bool sustainDown() const { return sustainPedal; }
    double modWheel() const { return wheelTarget; }
    double tempo() const { return bpm; }
    float takePeak() { const float p = peak; peak = 0; return p; }

private:
    struct ActiveSlot { int src, via, dst; double amt; };

    static double driveGainFor (double drive) { return std::pow (10.0, drive * 36.0 / 20.0); } // 0..+36 dB

    double lfoRateHz (const EngineParams::Lfo& l) const
    {
        if (! l.sync) return l.rate;
        const int d = std::min (std::max (l.div, 0), kNumLfoDivs - 1);
        return (bpm / 60.0) / kLfoDivBeats[d];
    }

    void pushKey (int note)
    {
        removeKey (note);
        if (keyCount < 128) keys[(size_t) keyCount++] = note;
    }

    void removeKey (int note)
    {
        int w = 0;
        for (int r = 0; r < keyCount; ++r)
            if (keys[(size_t) r] != note) keys[(size_t) w++] = keys[(size_t) r];
        keyCount = w;
    }

    void retriggerGlobalLfos()
    {
        for (int i = 0; i < kNumLfo; ++i)
            if (! params.lfo[(size_t) i].perVoice && params.lfo[(size_t) i].retrig)
                lfoG[(size_t) i].retrigger (0.0, rng);
    }

    void setVoicePitch (Voice& v, int note, bool glide)
    {
        v.glideFrom = v.pitch;
        v.glideTo = note;
        v.glidePos = glide ? 0.0 : 1.0;
        if (! glide) v.pitch = note;
    }

    void startVoice (Voice& v, int note, int vel, double fromPitch)
    {
        const bool fresh = ! v.active();
        if (fresh)
        {
            // sine/tri start at phase 0 (clean attack), saw/square random (wider stacks)
            for (int k = 0; k < kNumOsc; ++k)
            {
                const int w = params.osc[(size_t) k].wave;
                v.osc[(size_t) k].reset (w == 1 || w == 2 ? rng.uni() : 0.0);
            }
            v.filter.reset();
            v.drive.reset();
            for (int i = 0; i < kNumLfo; ++i)
                v.lfo[(size_t) i].start (params.lfo[(size_t) i].retrig ? 0.0 : rng.uni(), rng);
            v.snap = true;
        }
        else
        {
            for (int i = 0; i < kNumLfo; ++i)
                if (params.lfo[(size_t) i].retrig) v.lfo[(size_t) i].retrigger (0.0, rng);
        }
        v.note = note;
        v.velocity = vel;
        v.velGain = 0.1 + 0.9 * std::pow (vel / 127.0, 1.5);
        v.age = ++ageCounter;
        v.held = true;
        v.sustained = false;
        v.fEnv = 1;
        v.glideFrom = fromPitch;
        v.glideTo = note;
        v.glidePos = fromPitch == (double) note ? 1.0 : 0.0;
        v.pitch = fromPitch;
        v.env.gateOn();
    }

    // Once per chunk: envelope coefs, global LFOs, smoothed globals, active mod slots.
    void updateControl (int n)
    {
        const auto& P = params;
        envc.atkInc  = 1 / (std::max (P.attack, 0.0005) * sr);
        envc.decCoef = expCoef (P.decay, sr);
        envc.sus     = P.sustain;
        envc.relCoef = expCoef (P.release, sr);
        fEnvBlockCoef = std::pow (expCoef (P.fltDecay, sr), n);
        wheel += (wheelTarget - wheel) * wheelSmooth;

        for (int i = 0; i < kNumLfo; ++i)
        {
            const auto& lp = P.lfo[(size_t) i];
            lfoInc[(size_t) i] = lfoRateHz (lp) * n / sr;
            if (lp.perVoice) continue;
            auto& g = lfoG[(size_t) i];
            const bool locked = lp.sync && playing && ! lp.retrig;
            if (locked) // free-running + synced + host playing: lock the phase to the song position
            {
                const double beats = kLfoDivBeats[std::min (std::max (lp.div, 0), kNumLfoDivs - 1)];
                double ph = (ppqAtBlock + samplesIntoBlock / sr * bpm / 60.0) / beats;
                ph -= std::floor (ph);
                if (ph < g.phase - 0.5) g.held = rng.bi(); // wrapped
                g.phase = ph;
            }
            const double raw = lp.on ? g.shape (lp.wave, lp.phase) * lp.amount : 0.0;
            lfoGlobal[(size_t) i] = g.smooth (raw, lfoSmooth);
            if (! locked) g.advance (lfoInc[(size_t) i], rng);
        }

        const auto& D = P.drive;
        drvGain += (driveGainFor (D.drive) - drvGain) * ctlSmooth;
        drvMix  += (D.mix - drvMix) * ctlSmooth;
        drvOut  += (D.outGain - drvOut) * ctlSmooth;
        toneCoef = 1 - std::exp (-kTwoPi * (200.0 * std::pow (100.0, D.tone)) / sr);
        crushQ = std::pow (2.0, (16.0 - 14.0 * D.drive) - 1.0);
        crushHold = 1 + (int) (D.drive * D.drive * 16.0);
        crushGain = 1 + 3 * D.drive;

        numActive = 0;
        for (int s = 0; s < kNumModSlots; ++s)
        {
            const auto& m = P.mod[(size_t) s];
            const bool routed = m.src != srcNone && m.dst != dstNone;
            const double target = (m.on && routed) ? m.amount : 0.0;
            slotAmt[(size_t) s] += (target - slotAmt[(size_t) s]) * ctlSmooth;
            if (routed && std::abs (slotAmt[(size_t) s]) > 1e-6)
                activeSlots[(size_t) numActive++] = { m.src, m.via, m.dst, slotAmt[(size_t) s] };
        }
    }

    void renderVoice (Voice& v, int n)
    {
        const auto& P = params;

        // ---- modulation sources for this voice --------------------------------
        double src[kNumModSources];
        src[srcNone] = 0;
        for (int i = 0; i < kNumLfo; ++i)
        {
            const auto& lp = P.lfo[(size_t) i];
            src[srcLfo1 + i] = lp.perVoice
                ? v.lfo[(size_t) i].smooth (lp.on ? v.lfo[(size_t) i].shape (lp.wave, lp.phase) * lp.amount : 0.0, lfoSmooth)
                : lfoGlobal[(size_t) i];
        }
        src[srcAmpEnv]   = v.env.level;
        src[srcFltEnv]   = v.fEnv;
        src[srcVelocity] = v.velocity / 127.0;
        src[srcModWheel] = wheel;

        // ---- modulation matrix: sum every route into its destination --------
        double dst[kNumModDests] = {};
        for (int a = 0; a < numActive; ++a)
        {
            const auto& s = activeSlots[(size_t) a];
            double m = src[s.src] * s.amt;
            if (s.via != srcNone)
            {
                double w = src[s.via];
                if (s.via == srcLfo1 || s.via == srcLfo2) w = 0.5 * (w + 1); // bipolar -> 0..1
                m *= w;
            }
            dst[s.dst] += m;
        }

        // ---- targets for the end of this chunk -------------------------------
        VoiceCtl T;
        const double base = v.pitch - 69 + bend * kPitchBendRange + dst[dstPitchAll] * kPitchModSemis;
        for (int k = 0; k < kNumOsc; ++k)
        {
            const auto& o = P.osc[(size_t) k];
            const double semis = base + o.semis + dst[dstOsc1Pitch + k] * kPitchModSemis;
            T.dt[k] = std::min (440.0 * std::pow (2.0, semis / 12.0) / sr, 0.45);
            T.lvl[k] = o.on ? o.level * std::min (std::max (1 + dst[dstOsc1Level + k], 0.0), 2.0) : 0.0;
        }
        const double velN   = v.velocity / 127.0;
        const double envOct = P.envAmt * 7 * v.fEnv * (0.35 + 0.65 * velN);
        const double keyOct = (v.pitch - 60) / 24.0;
        T.g = SVF::gFor (P.cutoff * std::pow (2.0, envOct + keyOct + dst[dstCutoff] * kCutoffModOctaves), sr);
        T.k = SVF::kFor (std::min (std::max (P.reso + dst[dstReso], 0.0), 1.0));
        T.amp = v.velGain * std::min (std::max (1 + dst[dstAmp], 0.0), 2.0);
        const double pan = std::min (std::max (dst[dstPan], -1.0), 1.0);
        const double ang = (pan + 1) * kPi / 4;            // centre = unity in both channels
        T.panL = std::min (1.0, kSqrt2 * std::cos (ang));
        T.panR = std::min (1.0, kSqrt2 * std::sin (ang));

        if (v.snap) { v.cur = T; v.snap = false; }

        // ---- per-sample ramps ------------------------------------------------
        const double inv = 1.0 / n;
        double dt0 = v.cur.dt[0], dt1 = v.cur.dt[1], dt2 = v.cur.dt[2];
        double l0 = v.cur.lvl[0], l1 = v.cur.lvl[1], l2 = v.cur.lvl[2];
        double g = v.cur.g, k = v.cur.k, amp = v.cur.amp, pl = v.cur.panL, pr = v.cur.panR;
        const double ddt0 = (T.dt[0] - dt0) * inv, ddt1 = (T.dt[1] - dt1) * inv, ddt2 = (T.dt[2] - dt2) * inv;
        const double dl0 = (T.lvl[0] - l0) * inv, dl1 = (T.lvl[1] - l1) * inv, dl2 = (T.lvl[2] - l2) * inv;
        const double dg = (T.g - g) * inv, dk = (T.k - k) * inv, damp = (T.amp - amp) * inv;
        const double dpl = (T.panL - pl) * inv, dpr = (T.panR - pr) * inv;
        const bool on0 = P.osc[0].on, on1 = P.osc[1].on, on2 = P.osc[2].on;
        const int w0 = P.osc[0].wave, w1 = P.osc[1].wave, w2 = P.osc[2].wave;
        const bool drv = P.drive.on;

        for (int i = 0; i < n; ++i)
        {
            dt0 += ddt0; dt1 += ddt1; dt2 += ddt2;
            l0 += dl0; l1 += dl1; l2 += dl2;
            g += dg; k += dk; amp += damp; pl += dpl; pr += dpr;

            double s = 0;
            if (on0) s += l0 * v.osc[0].next (w0, dt0);
            if (on1) s += l1 * v.osc[1].next (w1, dt1);
            if (on2) s += l2 * v.osc[2].next (w2, dt2);
            double y = v.filter.process (s, g, k) * v.env.next (envc) * amp;
            if (drv) y = applyDrive (v.drive, y);
            y *= kVoiceGain;
            mixL[(size_t) i] += y * pl;
            mixR[(size_t) i] += y * pr;
            if (! v.env.active()) break;
        }
        v.cur = T;

        // ---- advance per-voice state to the next chunk ------------------------
        v.fEnv *= fEnvBlockCoef;
        for (int i = 0; i < kNumLfo; ++i)
            if (P.lfo[(size_t) i].perVoice) v.lfo[(size_t) i].advance (lfoInc[(size_t) i], rng);
        if (v.glidePos < 1)
        {
            v.glidePos = std::min (1.0, v.glidePos + n / (std::max (P.glideTime, 0.001) * sr));
            v.pitch = v.glideFrom + (v.glideTo - v.glideFrom) * v.glidePos;
        }
        if (! v.env.active()) { v.filter.reset(); v.drive.reset(); v.held = v.sustained = false; }
    }

    double applyDrive (DriveState& st, double x)
    {
        double wet;
        switch (params.drive.type)
        {
            case driveHard: wet = adaa (st, x * drvGain, true); break;
            case driveFold: wet = fold (x * drvGain); break;
            case driveCrush:
                if (--st.holdLeft <= 0)
                {
                    const double c = hardClip (x * crushGain);
                    st.hold = std::floor (c * crushQ + 0.5) / crushQ;
                    st.holdLeft = crushHold;
                }
                wet = st.hold;
                break;
            default: wet = adaa (st, x * drvGain, false); break;
        }
        st.lp += (wet - st.lp) * toneCoef;
        return (x + (st.lp - x) * drvMix) * drvOut;
    }

    std::array<Voice, kNumVoices> voices;
    std::array<double, kChunk> mixL {}, mixR {};
    std::array<int, 128> keys {};
    int keyCount = 0;
    EngineParams params;
    EnvCoefs envc;
    Rng rng;
    std::array<Lfo, kNumLfo> lfoG;
    std::array<double, kNumLfo> lfoGlobal {}, lfoInc {};
    std::array<double, kNumModSlots> slotAmt {};
    std::array<ActiveSlot, kNumModSlots> activeSlots {};
    int numActive = 0;
    double sr = 48000, bend = 0, masterGain = 0.7, masterSmooth = 0.001, fEnvBlockCoef = 0;
    double ctlSmooth = 0.5, lfoSmooth = 0.5, wheelSmooth = 0.5, wheel = 0, wheelTarget = 0;
    double drvGain = 1, drvMix = 1, drvOut = 1, toneCoef = 1, crushQ = 32768, crushGain = 1;
    int crushHold = 1;
    double bpm = 120, ppqAtBlock = 0;
    bool playing = false;
    int samplesIntoBlock = 0;
    int lastNote = -1;
    double lastPitch = 60;
    uint64_t ageCounter = 0;
    bool sustainPedal = false, firstParams = true;
    int active = 0;
    float peak = 0;
};
} // namespace epiano
