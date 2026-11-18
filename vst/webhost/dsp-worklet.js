// ============================================================================
//  E-Piano DSP engine — JavaScript twin of vst/Source/SynthEngine.h.
//  Kept line-for-line equivalent (same control rate, same RNG, same maths) and
//  checked against the C++ build by vst/tests/run-parity.ps1.
//
//  This whole function is stringified and loaded as an AudioWorklet module
//  (or run on the main thread as a ScriptProcessor fallback), so it must not
//  reference anything outside itself.
// ============================================================================
function synthWorkletModule() {
  'use strict';

  const NUM_VOICES = 88, NUM_OSC = 3, NUM_LFO = 2, NUM_MOD_SLOTS = 8, CHUNK = 32;
  const VOICE_GAIN = 0.16, PITCH_BEND_RANGE = 2, SILENCE = 1e-4;
  const PI = Math.PI, TWO_PI = 2 * Math.PI, SQRT2 = Math.SQRT2, LN2 = Math.LN2;
  const LN_1000 = Math.log(1000);
  const CUTOFF_MOD_OCT = 5, PITCH_MOD_SEMIS = 12;

  // choice indices (match Params.h / params.js)
  const SRC_NONE = 0, SRC_LFO1 = 1, SRC_LFO2 = 2, SRC_AMPENV = 3, SRC_FLTENV = 4, SRC_VEL = 5, SRC_WHEEL = 6, NUM_SRC = 7;
  const DST_NONE = 0, DST_CUTOFF = 1, DST_RESO = 2, DST_PITCH_ALL = 3, DST_OSC1_PITCH = 4,
        DST_OSC1_LEVEL = 7, DST_AMP = 10, DST_PAN = 11, NUM_DST = 12;
  const LFO_SINE = 0, LFO_TRI = 1, LFO_SQUARE = 2, LFO_SAW_UP = 3, LFO_SAW_DOWN = 4, LFO_SH = 5;
  const DRIVE_SOFT = 0, DRIVE_HARD = 1, DRIVE_FOLD = 2, DRIVE_CRUSH = 3;
  const MODE_POLY = 0, MODE_LEGATO = 2;
  const LFO_DIV_BEATS = [4, 3, 2, 4 / 3, 1.5, 1, 2 / 3, 0.75, 0.5, 1 / 3, 0.375, 0.25, 1 / 6, 0.125, 1 / 12];

  const clampv = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
  const expCoef = (seconds, sr) => Math.exp(-LN_1000 / (Math.max(seconds, 0.0005) * sr));

  // Deterministic RNG — same sequence as epiano::Rng (C++)
  class Rng {
    constructor() { this.s = 0x2545F491; }
    seed(v) { this.s = v >>> 0; }
    next() { this.s = (Math.imul(this.s, 1664525) + 1013904223) >>> 0; return this.s; }
    uni() { return (this.next() >>> 8) * (1 / 16777216); }
    bi() { return this.uni() * 2 - 1; }
  }

  function polyBlep(t, dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
  }

  class Oscillator {
    constructor() { this.phase = 0; }
    reset(p) { this.phase = p; }
    next(wave, dt) {
      const t = this.phase;
      let y;
      switch (wave) {
        case 1: y = 2 * t - 1 - polyBlep(t, dt); break;
        case 2: { let t2 = t + 0.5; if (t2 >= 1) t2 -= 1; y = (t < 0.5 ? 1 : -1) + polyBlep(t, dt) - polyBlep(t2, dt); break; }
        case 3: { let t3 = t + 0.25; if (t3 >= 1) t3 -= 1; y = 1 - 4 * Math.abs(t3 - 0.5); break; }
        default: y = Math.sin(TWO_PI * t);
      }
      let p = t + dt; if (p >= 1) p -= 1;
      this.phase = p;
      return y;
    }
  }

  // stage: 0 idle, 1 attack, 2 decay, 3 sustain, 4 release
  class Envelope {
    constructor() { this.stage = 0; this.level = 0; }
    gateOn() { this.stage = 1; }
    gateOff() { if (this.stage !== 0) this.stage = 4; }
    kill() { this.stage = 0; this.level = 0; }
    active() { return this.stage !== 0; }
    next(c) {
      switch (this.stage) {
        case 1:
          this.level += c.atkInc;
          if (this.level >= 1) { this.level = 1; this.stage = 2; }
          break;
        case 2:
          this.level = c.sus + (this.level - c.sus) * c.decCoef;
          if (Math.abs(this.level - c.sus) < SILENCE) { this.level = c.sus; this.stage = c.sus < SILENCE ? 0 : 3; }
          break;
        case 3:
          this.level += (c.sus - this.level) * 0.002;
          if (c.sus < SILENCE && this.level < SILENCE) { this.level = 0; this.stage = 0; }
          break;
        case 4:
          this.level *= c.relCoef;
          if (this.level < SILENCE) { this.level = 0; this.stage = 0; }
          break;
      }
      return this.level;
    }
  }

  class SVF {
    constructor() { this.ic1 = 0; this.ic2 = 0; }
    reset() { this.ic1 = 0; this.ic2 = 0; }
    static gFor(fc, sr) { fc = Math.min(Math.max(fc, 20), sr * 0.45); return Math.tan(PI * fc / sr); }
    static kFor(reso) { return 2 - 1.9 * reso; }
    process(x, g, k) {
      const a1 = 1 / (1 + g * (g + k));
      const a2 = g * a1;
      const a3 = g * a2;
      const v3 = x - this.ic2;
      const v1 = a1 * this.ic1 + a2 * v3;
      const v2 = this.ic2 + a2 * this.ic1 + a3 * v3;
      this.ic1 = 2 * v1 - this.ic1;
      this.ic2 = 2 * v2 - this.ic2;
      return v2;
    }
  }

  class Lfo {
    constructor() { this.phase = 0; this.held = 0; this.out = 0; this.primed = false; }
    start(p, rng) { this.phase = p; this.held = rng.bi(); this.primed = false; }
    retrigger(p, rng) { this.phase = p; this.held = rng.bi(); }
    shape(wave, offset) {
      let t = this.phase + offset;
      t -= Math.floor(t);
      switch (wave) {
        case LFO_TRI: { let t3 = t + 0.25; if (t3 >= 1) t3 -= 1; return 1 - 4 * Math.abs(t3 - 0.5); }
        case LFO_SQUARE: return t < 0.5 ? 1 : -1;
        case LFO_SAW_UP: return 2 * t - 1;
        case LFO_SAW_DOWN: return 1 - 2 * t;
        case LFO_SH: return this.held;
        default: return Math.sin(TWO_PI * t);
      }
    }
    advance(inc, rng) {
      this.phase += inc;
      if (this.phase >= 1) { this.phase -= Math.floor(this.phase); this.held = rng.bi(); }
    }
    smooth(target, coef) {
      if (!this.primed) { this.out = target; this.primed = true; }
      else this.out += (target - this.out) * coef;
      return this.out;
    }
  }

  class DriveState {
    constructor() { this.reset(); }
    reset() { this.x1 = 0; this.F1 = 0; this.lp = 0; this.hold = 0; this.holdLeft = 0; }
  }
  function logCosh(x) { x = Math.abs(x); return x + Math.log1p(Math.exp(-2 * x)) - LN2; }
  function hardClip(x) { return x < -1 ? -1 : (x > 1 ? 1 : x); }
  function hardClipAnti(x) { const a = Math.abs(x); return a <= 1 ? 0.5 * x * x : a - 0.5; }
  function fold(x) { let t = (x + 1) * 0.25; t -= Math.floor(t); return 1 - 4 * Math.abs(t - 0.5); }
  function adaa(st, x, hard) {
    const F = hard ? hardClipAnti(x) : logCosh(x);
    const dx = x - st.x1;
    const y = Math.abs(dx) < 1e-6 ? (hard ? hardClip(0.5 * (x + st.x1)) : Math.tanh(0.5 * (x + st.x1)))
                                  : (F - st.F1) / dx;
    st.x1 = x; st.F1 = F;
    return y;
  }

  function makeCtl() { return { dt: [0, 0, 0], lvl: [0, 0, 0], g: 0, k: 2, amp: 0, panL: 1, panR: 1 }; }
  function copyCtl(d, s) {
    for (let i = 0; i < 3; i++) { d.dt[i] = s.dt[i]; d.lvl[i] = s.lvl[i]; }
    d.g = s.g; d.k = s.k; d.amp = s.amp; d.panL = s.panL; d.panR = s.panR;
  }

  class Voice {
    constructor() {
      this.osc = [new Oscillator(), new Oscillator(), new Oscillator()];
      this.lfo = [new Lfo(), new Lfo()];
      this.env = new Envelope();
      this.filter = new SVF();
      this.drive = new DriveState();
      this.note = -1; this.velocity = 0; this.velGain = 0; this.fEnv = 0; this.age = 0;
      this.held = false; this.sustained = false;
      this.pitch = 60; this.glideFrom = 60; this.glideTo = 60; this.glidePos = 1;
      this.cur = makeCtl(); this.snap = true;
    }
    active() { return this.env.stage !== 0; }
    release() { this.held = false; this.sustained = false; this.env.gateOff(); }
  }

  // Raw parameter values (by id) -> engine snapshot. Mirrors EngineParamsFromValues.h.
  function makeEngineParams(v, P) {
    const ch = (id, max) => clampv(Math.round(v[id]), 0, max);
    for (let k = 0; k < 3; k++) {
      const n = k + 1, o = P.osc[k];
      o.on = v[`osc${n}On`] >= 0.5; o.wave = ch(`osc${n}Wave`, 3);
      o.semis = v[`osc${n}Pitch`] + v[`osc${n}Fine`] / 100; o.level = v[`osc${n}Level`];
    }
    P.attack = v.ampAttack; P.decay = v.ampDecay; P.sustain = v.ampSustain; P.release = v.ampRelease;
    P.cutoff = v.fltCutoff; P.reso = v.fltReso; P.envAmt = v.fltEnvAmt; P.fltDecay = v.fltDecay;
    P.ampGain = Math.pow(10, v.ampLevel / 20); P.master = v.master;
    for (let i = 0; i < NUM_LFO; i++) {
      const n = i + 1, l = P.lfo[i];
      l.on = v[`lfo${n}On`] >= 0.5; l.wave = ch(`lfo${n}Wave`, 5); l.rate = v[`lfo${n}Rate`];
      l.sync = v[`lfo${n}Sync`] >= 0.5; l.div = ch(`lfo${n}Div`, LFO_DIV_BEATS.length - 1);
      l.perVoice = ch(`lfo${n}Mode`, 1) === 1; l.retrig = ch(`lfo${n}Trig`, 1) === 1;
      l.phase = v[`lfo${n}Phase`] / 360; l.amount = v[`lfo${n}Amount`];
    }
    for (let s = 0; s < NUM_MOD_SLOTS; s++) {
      const n = s + 1, m = P.mod[s];
      m.on = v[`mod${n}On`] >= 0.5; m.src = ch(`mod${n}Src`, NUM_SRC - 1); m.via = ch(`mod${n}Via`, NUM_SRC - 1);
      m.dst = ch(`mod${n}Dst`, NUM_DST - 1); m.amount = v[`mod${n}Amt`];
    }
    const D = P.drive;
    D.on = v.drvOn >= 0.5; D.type = ch('drvType', 3); D.drive = v.drvDrive; D.tone = v.drvTone; D.mix = v.drvMix;
    D.outGain = Math.pow(10, v.drvOut / 20);
    P.voiceMode = ch('voiceMode', 2); P.glideOn = v.glideOn >= 0.5; P.glideTime = v.glideTime;
    P.glideLegatoOnly = ch('glideMode', 1) === 1;
    return P;
  }
  function newEngineParams() {
    return {
      osc: [0, 1, 2].map(() => ({ on: false, wave: 0, semis: 0, level: 0 })),
      lfo: [0, 1].map(() => ({ on: false, wave: 0, rate: 2, sync: false, div: 8, perVoice: false, retrig: false, phase: 0, amount: 1 })),
      mod: Array.from({ length: NUM_MOD_SLOTS }, () => ({ on: true, src: 0, via: 0, dst: 0, amount: 0 })),
      drive: { on: false, type: 0, drive: 0.3, tone: 1, mix: 1, outGain: 1 },
      attack: 0.005, decay: 1, sustain: 0.7, release: 0.3, cutoff: 8000, reso: 0.1, envAmt: 0.2, fltDecay: 0.4,
      ampGain: 1, master: 0.7, voiceMode: 0, glideOn: false, glideTime: 0.1, glideLegatoOnly: false,
    };
  }

  class SynthEngine {
    constructor(sampleRate) {
      this.voices = Array.from({ length: NUM_VOICES }, () => new Voice());
      this.mixL = new Float64Array(CHUNK); this.mixR = new Float64Array(CHUNK);
      this.keys = new Int32Array(128); this.keyCount = 0;
      this.params = newEngineParams();
      this.envc = { atkInc: 0, decCoef: 0, sus: 0, relCoef: 0 };
      this.rng = new Rng();
      this.lfoG = [new Lfo(), new Lfo()];
      this.lfoGlobal = [0, 0]; this.lfoInc = [0, 0];
      this.slotAmt = new Float64Array(NUM_MOD_SLOTS);
      this.activeSlots = Array.from({ length: NUM_MOD_SLOTS }, () => ({ src: 0, via: 0, dst: 0, amt: 0 }));
      this.numActive = 0;
      this.src = new Float64Array(NUM_SRC); this.dst = new Float64Array(NUM_DST);
      this.T = makeCtl();
      this.bend = 0; this.masterGain = 0.7; this.fEnvBlockCoef = 0;
      this.wheel = 0; this.wheelTarget = 0;
      this.drvGain = 1; this.drvMix = 1; this.drvOut = 1; this.toneCoef = 1; this.crushQ = 32768; this.crushGain = 1; this.crushHold = 1;
      this.bpm = 120; this.ppqAtBlock = 0; this.playing = false; this.samplesIntoBlock = 0;
      this.lastNote = -1; this.lastPitch = 60; this.ageCounter = 0;
      this.sustainPedal = false; this.firstParams = true; this.active = 0; this.peak = 0;
      this.prepare(sampleRate);
    }

    prepare(sr) {
      this.sr = sr;
      this.masterSmooth = 1 - Math.exp(-1 / (0.02 * sr));
      this.ctlSmooth = 1 - Math.exp(-CHUNK / (0.005 * sr));
      this.lfoSmooth = 1 - Math.exp(-CHUNK / (0.002 * sr));
      this.wheelSmooth = 1 - Math.exp(-CHUNK / (0.010 * sr));
      this.rng.seed(0x2545F491);
      this.lfoG = [new Lfo(), new Lfo()];
      this.slotAmt.fill(0);
      this.firstParams = true;
      this.panic();
    }

    setParams(P) {
      this.params = P;
      if (this.firstParams) {
        this.masterGain = P.master * P.ampGain;
        this.drvGain = SynthEngine.driveGainFor(P.drive.drive); this.drvMix = P.drive.mix; this.drvOut = P.drive.outGain;
        this.firstParams = false;
      }
    }

    setTransport(bpm, ppq, playing) {
      this.bpm = bpm > 0 ? bpm : 120; this.ppqAtBlock = ppq; this.playing = playing; this.samplesIntoBlock = 0;
    }

    static driveGainFor(drive) { return Math.pow(10, drive * 36 / 20); }

    lfoRateHz(l) {
      if (!l.sync) return l.rate;
      return (this.bpm / 60) / LFO_DIV_BEATS[clampv(l.div, 0, LFO_DIV_BEATS.length - 1)];
    }

    pushKey(note) { this.removeKey(note); if (this.keyCount < 128) this.keys[this.keyCount++] = note; }
    removeKey(note) {
      let w = 0;
      for (let r = 0; r < this.keyCount; r++) if (this.keys[r] !== note) this.keys[w++] = this.keys[r];
      this.keyCount = w;
    }

    retriggerGlobalLfos() {
      for (let i = 0; i < NUM_LFO; i++) {
        const l = this.params.lfo[i];
        if (!l.perVoice && l.retrig) this.lfoG[i].retrigger(0, this.rng);
      }
    }

    setVoicePitch(v, note, glide) {
      v.glideFrom = v.pitch; v.glideTo = note; v.glidePos = glide ? 0 : 1;
      if (!glide) v.pitch = note;
    }

    noteOn(note, vel) {
      if (vel <= 0) { this.noteOff(note); return; }
      const P = this.params;
      const heldBefore = this.keyCount;
      this.pushKey(note);
      if (P.voiceMode === MODE_POLY) {
        let v = null;
        for (const x of this.voices) if (x.active() && x.note === note) { v = x; break; }
        if (!v) for (const x of this.voices) if (!x.active()) { v = x; break; }
        if (!v) {
          for (const x of this.voices) {
            if (!v) { v = x; continue; }
            const xr = x.env.stage === 4, vr = v.env.stage === 4;
            if ((xr && !vr) || (xr === vr && x.age < v.age)) v = x;
          }
        }
        const glide = P.glideOn && this.lastNote >= 0 && (!P.glideLegatoOnly || heldBefore > 0);
        this.startVoice(v, note, vel, glide ? this.lastPitch : note);
        if (heldBefore === 0) this.retriggerGlobalLfos();
      } else {
        const v = this.voices[0];
        const overlapping = heldBefore > 0 && v.active() && v.env.stage !== 4;
        const glide = P.glideOn && this.lastNote >= 0 && (overlapping || !P.glideLegatoOnly);
        if (P.voiceMode === MODE_LEGATO && overlapping) {
          this.setVoicePitch(v, note, glide);
          v.note = note; v.held = true; v.sustained = false;
        } else {
          this.startVoice(v, note, vel, glide ? (v.active() ? v.pitch : this.lastPitch) : note);
          this.retriggerGlobalLfos();
        }
      }
      this.lastNote = note;
      this.lastPitch = note;
    }

    noteOff(note) {
      this.removeKey(note);
      const mono = this.params.voiceMode !== MODE_POLY;
      for (let i = mono ? 1 : 0; i < NUM_VOICES; i++) {
        const v = this.voices[i];
        if (v.note === note && v.held && v.active()) {
          if (this.sustainPedal) { v.held = false; v.sustained = true; } else v.release();
        }
      }
      if (!mono) return;
      const v = this.voices[0];
      if (!(v.active() && v.held && v.note === note)) return;
      if (this.keyCount > 0) {
        const back = this.keys[this.keyCount - 1];
        this.setVoicePitch(v, back, this.params.glideOn);
        v.note = back; this.lastNote = back; this.lastPitch = back;
      } else if (this.sustainPedal) { v.held = false; v.sustained = true; }
      else v.release();
    }

    setSustain(on) {
      this.sustainPedal = on;
      if (!on) for (const v of this.voices) if (v.sustained) v.release();
    }
    setPitchBend(v) { this.bend = v; }
    setModWheel(v) { this.wheelTarget = clampv(v, 0, 1); }
    allNotesOff() {
      this.sustainPedal = false; this.keyCount = 0;
      for (const v of this.voices) if (v.active()) v.release();
    }
    panic() {
      this.sustainPedal = false; this.keyCount = 0;
      for (const v of this.voices) { v.env.kill(); v.held = v.sustained = false; v.filter.reset(); v.drive.reset(); }
    }

    startVoice(v, note, vel, fromPitch) {
      const P = this.params, rng = this.rng;
      if (!v.active()) {
        for (let k = 0; k < NUM_OSC; k++) {
          const w = P.osc[k].wave;
          v.osc[k].reset(w === 1 || w === 2 ? rng.uni() : 0);
        }
        v.filter.reset();
        v.drive.reset();
        for (let i = 0; i < NUM_LFO; i++) v.lfo[i].start(P.lfo[i].retrig ? 0 : rng.uni(), rng);
        v.snap = true;
      } else {
        for (let i = 0; i < NUM_LFO; i++) if (P.lfo[i].retrig) v.lfo[i].retrigger(0, rng);
      }
      v.note = note; v.velocity = vel;
      v.velGain = 0.1 + 0.9 * Math.pow(vel / 127, 1.5);
      v.age = ++this.ageCounter;
      v.held = true; v.sustained = false; v.fEnv = 1;
      v.glideFrom = fromPitch; v.glideTo = note; v.glidePos = fromPitch === note ? 1 : 0; v.pitch = fromPitch;
      v.env.gateOn();
    }

    updateControl(n) {
      const P = this.params, sr = this.sr, e = this.envc;
      e.atkInc = 1 / (Math.max(P.attack, 0.0005) * sr);
      e.decCoef = expCoef(P.decay, sr);
      e.sus = P.sustain;
      e.relCoef = expCoef(P.release, sr);
      this.fEnvBlockCoef = Math.pow(expCoef(P.fltDecay, sr), n);
      this.wheel += (this.wheelTarget - this.wheel) * this.wheelSmooth;

      for (let i = 0; i < NUM_LFO; i++) {
        const lp = P.lfo[i];
        this.lfoInc[i] = this.lfoRateHz(lp) * n / sr;
        if (lp.perVoice) continue;
        const g = this.lfoG[i];
        const locked = lp.sync && this.playing && !lp.retrig;
        if (locked) {
          const beats = LFO_DIV_BEATS[clampv(lp.div, 0, LFO_DIV_BEATS.length - 1)];
          let ph = (this.ppqAtBlock + this.samplesIntoBlock / sr * this.bpm / 60) / beats;
          ph -= Math.floor(ph);
          if (ph < g.phase - 0.5) g.held = this.rng.bi();
          g.phase = ph;
        }
        const raw = lp.on ? g.shape(lp.wave, lp.phase) * lp.amount : 0;
        this.lfoGlobal[i] = g.smooth(raw, this.lfoSmooth);
        if (!locked) g.advance(this.lfoInc[i], this.rng);
      }

      const D = P.drive;
      this.drvGain += (SynthEngine.driveGainFor(D.drive) - this.drvGain) * this.ctlSmooth;
      this.drvMix += (D.mix - this.drvMix) * this.ctlSmooth;
      this.drvOut += (D.outGain - this.drvOut) * this.ctlSmooth;
      this.toneCoef = 1 - Math.exp(-TWO_PI * (200 * Math.pow(100, D.tone)) / sr);
      this.crushQ = Math.pow(2, (16 - 14 * D.drive) - 1);
      this.crushHold = 1 + Math.trunc(D.drive * D.drive * 16);
      this.crushGain = 1 + 3 * D.drive;

      this.numActive = 0;
      for (let s = 0; s < NUM_MOD_SLOTS; s++) {
        const m = P.mod[s];
        const routed = m.src !== SRC_NONE && m.dst !== DST_NONE;
        const target = (m.on && routed) ? m.amount : 0;
        this.slotAmt[s] += (target - this.slotAmt[s]) * this.ctlSmooth;
        if (routed && Math.abs(this.slotAmt[s]) > 1e-6) {
          const a = this.activeSlots[this.numActive++];
          a.src = m.src; a.via = m.via; a.dst = m.dst; a.amt = this.slotAmt[s];
        }
      }
    }

    renderVoice(v, n) {
      const P = this.params, sr = this.sr, src = this.src, dst = this.dst, T = this.T;

      src[SRC_NONE] = 0;
      for (let i = 0; i < NUM_LFO; i++) {
        const lp = P.lfo[i];
        src[SRC_LFO1 + i] = lp.perVoice
          ? v.lfo[i].smooth(lp.on ? v.lfo[i].shape(lp.wave, lp.phase) * lp.amount : 0, this.lfoSmooth)
          : this.lfoGlobal[i];
      }
      src[SRC_AMPENV] = v.env.level;
      src[SRC_FLTENV] = v.fEnv;
      src[SRC_VEL] = v.velocity / 127;
      src[SRC_WHEEL] = this.wheel;

      dst.fill(0);
      for (let a = 0; a < this.numActive; a++) {
        const s = this.activeSlots[a];
        let m = src[s.src] * s.amt;
        if (s.via !== SRC_NONE) {
          let w = src[s.via];
          if (s.via === SRC_LFO1 || s.via === SRC_LFO2) w = 0.5 * (w + 1);
          m *= w;
        }
        dst[s.dst] += m;
      }

      const base = v.pitch - 69 + this.bend * PITCH_BEND_RANGE + dst[DST_PITCH_ALL] * PITCH_MOD_SEMIS;
      for (let k = 0; k < NUM_OSC; k++) {
        const o = P.osc[k];
        const semis = base + o.semis + dst[DST_OSC1_PITCH + k] * PITCH_MOD_SEMIS;
        T.dt[k] = Math.min(440 * Math.pow(2, semis / 12) / sr, 0.45);
        T.lvl[k] = o.on ? o.level * Math.min(Math.max(1 + dst[DST_OSC1_LEVEL + k], 0), 2) : 0;
      }
      const velN = v.velocity / 127;
      const envOct = P.envAmt * 7 * v.fEnv * (0.35 + 0.65 * velN);
      const keyOct = (v.pitch - 60) / 24;
      T.g = SVF.gFor(P.cutoff * Math.pow(2, envOct + keyOct + dst[DST_CUTOFF] * CUTOFF_MOD_OCT), sr);
      T.k = SVF.kFor(Math.min(Math.max(P.reso + dst[DST_RESO], 0), 1));
      T.amp = v.velGain * Math.min(Math.max(1 + dst[DST_AMP], 0), 2);
      const pan = Math.min(Math.max(dst[DST_PAN], -1), 1);
      const ang = (pan + 1) * PI / 4;
      T.panL = Math.min(1, SQRT2 * Math.cos(ang));
      T.panR = Math.min(1, SQRT2 * Math.sin(ang));

      if (v.snap) { copyCtl(v.cur, T); v.snap = false; }

      const inv = 1 / n, c = v.cur;
      let dt0 = c.dt[0], dt1 = c.dt[1], dt2 = c.dt[2], l0 = c.lvl[0], l1 = c.lvl[1], l2 = c.lvl[2];
      let g = c.g, k = c.k, amp = c.amp, pl = c.panL, pr = c.panR;
      const ddt0 = (T.dt[0] - dt0) * inv, ddt1 = (T.dt[1] - dt1) * inv, ddt2 = (T.dt[2] - dt2) * inv;
      const dl0 = (T.lvl[0] - l0) * inv, dl1 = (T.lvl[1] - l1) * inv, dl2 = (T.lvl[2] - l2) * inv;
      const dg = (T.g - g) * inv, dk = (T.k - k) * inv, damp = (T.amp - amp) * inv;
      const dpl = (T.panL - pl) * inv, dpr = (T.panR - pr) * inv;
      const on0 = P.osc[0].on, on1 = P.osc[1].on, on2 = P.osc[2].on;
      const w0 = P.osc[0].wave, w1 = P.osc[1].wave, w2 = P.osc[2].wave;
      const [o0, o1, o2] = v.osc;
      const env = v.env, envc = this.envc, filt = v.filter, drv = P.drive.on;
      const mixL = this.mixL, mixR = this.mixR;

      for (let i = 0; i < n; i++) {
        dt0 += ddt0; dt1 += ddt1; dt2 += ddt2;
        l0 += dl0; l1 += dl1; l2 += dl2;
        g += dg; k += dk; amp += damp; pl += dpl; pr += dpr;

        let s = 0;
        if (on0) s += l0 * o0.next(w0, dt0);
        if (on1) s += l1 * o1.next(w1, dt1);
        if (on2) s += l2 * o2.next(w2, dt2);
        let y = filt.process(s, g, k) * env.next(envc) * amp;
        if (drv) y = this.applyDrive(v.drive, y);
        y *= VOICE_GAIN;
        mixL[i] += y * pl;
        mixR[i] += y * pr;
        if (!env.active()) break;
      }
      copyCtl(v.cur, T);

      v.fEnv *= this.fEnvBlockCoef;
      for (let i = 0; i < NUM_LFO; i++) if (P.lfo[i].perVoice) v.lfo[i].advance(this.lfoInc[i], this.rng);
      if (v.glidePos < 1) {
        v.glidePos = Math.min(1, v.glidePos + n / (Math.max(P.glideTime, 0.001) * sr));
        v.pitch = v.glideFrom + (v.glideTo - v.glideFrom) * v.glidePos;
      }
      if (!env.active()) { filt.reset(); v.drive.reset(); v.held = v.sustained = false; }
    }

    applyDrive(st, x) {
      let wet;
      switch (this.params.drive.type) {
        case DRIVE_HARD: wet = adaa(st, x * this.drvGain, true); break;
        case DRIVE_FOLD: wet = fold(x * this.drvGain); break;
        case DRIVE_CRUSH:
          if (--st.holdLeft <= 0) {
            const c = hardClip(x * this.crushGain);
            st.hold = Math.floor(c * this.crushQ + 0.5) / this.crushQ;
            st.holdLeft = this.crushHold;
          }
          wet = st.hold;
          break;
        default: wet = adaa(st, x * this.drvGain, false);
      }
      st.lp += (wet - st.lp) * this.toneCoef;
      return (x + (st.lp - x) * this.drvMix) * this.drvOut;
    }

    // Writes numSamples into L / R (R may be null -> mono sum)
    render(L, R, offset, numSamples) {
      let pos = 0;
      while (pos < numSamples) {
        const n = Math.min(CHUNK, numSamples - pos);
        this.mixL.fill(0, 0, n); this.mixR.fill(0, 0, n);
        this.updateControl(n);
        let count = 0;
        for (const v of this.voices) if (v.active()) { count++; this.renderVoice(v, n); }
        this.active = count;

        const target = this.params.master * this.params.ampGain;
        let g = this.masterGain, peak = this.peak;
        for (let i = 0; i < n; i++) {
          g += (target - g) * this.masterSmooth;
          if (R) {
            const yl = Math.fround(Math.tanh(this.mixL[i] * g)), yr = Math.fround(Math.tanh(this.mixR[i] * g));
            L[offset + pos + i] = yl; R[offset + pos + i] = yr;
            peak = Math.max(peak, Math.abs(yl), Math.abs(yr));
          } else {
            const y = Math.fround(Math.tanh(0.5 * (this.mixL[i] + this.mixR[i]) * g));
            L[offset + pos + i] = y;
            peak = Math.max(peak, Math.abs(y));
          }
        }
        this.masterGain = g; this.peak = peak;
        pos += n;
        this.samplesIntoBlock += n;
      }
    }
  }

  // ---------------------------------------------------------- processor ---
  class SynthProcessor extends AudioWorkletProcessor {
    constructor(options) {
      super();
      const init = (options && options.processorOptions && options.processorOptions.params) || {};
      this.values = Object.assign({}, init);
      this.engine = new SynthEngine(sampleRate);
      this.P = newEngineParams();
      this.dirty = true;
      this.bpm = 120;
      this.blockCount = 0;
      this.port.onmessage = (e) => this.onMessage(e.data);
    }

    onMessage(m) {
      const e = this.engine;
      switch (m.type) {
        case 'param': this.values[m.id] = m.value; this.dirty = true; break;
        case 'params': Object.assign(this.values, m.values); this.dirty = true; break;
        case 'noteOn': this.applyParams(); e.noteOn(m.note, m.vel); break;
        case 'noteOff': this.applyParams(); e.noteOff(m.note); break;
        case 'sustain': e.setSustain(!!m.on); break;
        case 'bend': e.setPitchBend(m.value); break;
        case 'modWheel': e.setModWheel(m.value); break;
        case 'tempo': this.bpm = m.bpm; break;
        case 'allNotesOff': e.allNotesOff(); break;
        case 'panic': e.panic(); break;
      }
    }

    applyParams() {
      if (!this.dirty) return;
      this.engine.setParams(makeEngineParams(this.values, this.P));
      this.dirty = false;
    }

    process(inputs, outputs) {
      const out = outputs[0];
      const L = out[0];
      if (!L) return true;
      const R = out.length > 1 ? out[1] : null;
      this.applyParams();
      this.engine.setTransport(this.bpm, 0, false);
      this.engine.render(L, R, 0, L.length);

      if (++this.blockCount >= 16) { // ~43 ms @ 48k
        const e = this.engine;
        this.port.postMessage({ type: 'stats', voices: e.active, peak: e.peak, sustain: e.sustainPedal,
                                modWheel: e.wheelTarget, bpm: this.bpm });
        this.blockCount = 0;
        e.peak = 0;
      }
      return true;
    }
  }

  registerProcessor('epiano-synth', SynthProcessor);
}
