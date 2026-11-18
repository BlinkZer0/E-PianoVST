// ============================================================================
//  Web host — stands in for the plugin's C++ side when the UI runs in a
//  browser. It provides window.__JUCE__.backend with exactly the events the
//  JUCE editor uses (see PluginEditor.h), so vst/ui runs unchanged:
//    audio   Web Audio: AudioWorklet (or ScriptProcessor fallback on file://)
//            running dsp-worklet.js, the JS twin of SynthEngine.h
//    MIDI    Web MIDI (all inputs or one), incl. sustain, pitch bend, mod wheel
//    tempo   user-set BPM (the plugin uses the DAW's)
//    presets factory (FACTORY_JSON), this browser (localStorage), or a linked
//            folder on disk (File System Access API) — same .epreset format
// ============================================================================
(function () {
  'use strict';

  // ---------------------------------------------------------- the bridge ---
  const listeners = {};
  const handlers = {};
  const toUi = (ev, data) => queueMicrotask(() => (listeners[ev] || []).forEach(f => f(data)));
  window.__JUCE__ = {
    backend: {
      addEventListener(ev, fn) { (listeners[ev] = listeners[ev] || []).push(fn); },
      // handled synchronously so file pickers / permission prompts keep the click's user activation
      emitEvent(ev, data) { const h = handlers[ev]; if (h) h(data || {}); },
    },
  };
  const message = (text) => toUi('message', { text });
  const store = {
    get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
    set(k, v) { try { localStorage.setItem(k, v); return true; } catch (e) { return false; } },
  };

  // ------------------------------------------------------ factory presets ---
  // compact: [name, osc1, osc2, osc3, [a,d,s,r], [cut,res,env,dec], ampDb, {extra}?]
  const FACTORY = [];
  for (const cat of FACTORY_JSON.categories) {
    for (const p of cat.presets) {
      const v = {};
      for (let k = 0; k < 3; k++) {
        const o = p[k + 1], n = k + 1;
        v[`osc${n}On`] = o[0]; v[`osc${n}Wave`] = o[1]; v[`osc${n}Pitch`] = o[2];
        v[`osc${n}Fine`] = o[3]; v[`osc${n}Level`] = o[4];
      }
      [v.ampAttack, v.ampDecay, v.ampSustain, v.ampRelease] = p[4];
      [v.fltCutoff, v.fltReso, v.fltEnvAmt, v.fltDecay] = p[5];
      v.ampLevel = p[6];
      if (p[7]) Object.assign(v, p[7]);
      FACTORY.push({ id: `factory:${FACTORY.length}`, name: p[0], category: cat.name, factory: true, values: v });
    }
  }

  // ---------------------------------------------------------------- state ---
  const values = defaultParamValues();
  const samples = [null, null, null]; // oscillator samples: { name, rate, root, data: Float32Array }
  let current = { id: '', name: '', category: '' };
  let tempo = clamp(Number(store.get('epiano.tempo')) || 120, 20, 300);

  // Missing params -> default (so v0.9 presets stay neutral); master untouched.
  function presetValues(pv) {
    const out = {};
    for (const d of PARAM_DEFS) {
      if (d.noPreset) continue;
      let v = Number(pv[d.id]);
      if (pv[d.id] === undefined || pv[d.id] === null || isNaN(v)) v = d.def;
      v = clamp(v, d.min, d.max);
      if (d.step) v = Math.round(v / d.step) * d.step;
      out[d.id] = v;
    }
    return out;
  }
  // samplesJson: optional { "1": {name, rate, root, pcm16}, ... } embedded in user presets.
  // Presets without samples (all factory presets) leave loaded samples alone, like the plugin.
  function applyPreset(pv, info, samplesJson) {
    const all = presetValues(pv);
    Object.assign(values, all);
    audio.post({ type: 'params', values: all });
    if (samplesJson && typeof samplesJson === 'object') {
      for (let k = 0; k < 3; k++) { const s = sampleFromJson(samplesJson[String(k + 1)]); if (s) setSample(k, s); }
    }
    current = info;
    toUi('params', Object.assign({}, values));
    toUi('preset', current);
  }

  // start on the first factory preset, like a fresh plugin instance (before audio starts)
  Object.assign(values, presetValues(FACTORY[0].values));
  current = { id: FACTORY[0].id, name: FACTORY[0].name, category: FACTORY[0].category };

  // ---------------------------------------------------------------- audio ---
  const audio = {
    ctx: null, node: null, port: null, mode: '', error: '',
    post(m) { if (this.port) this.port.postMessage(m); },
    get running() { return !!this.ctx && this.ctx.state === 'running'; },

    async start() {
      const ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
      this.ctx = ctx;
      ctx.onstatechange = sendConfig;
      const src = `(${synthWorkletModule.toString()})();`;
      let built = null;
      if (ctx.audioWorklet && location.protocol !== 'file:') {
        try {
          const url = URL.createObjectURL(new Blob([src], { type: 'application/javascript' }));
          try { await ctx.audioWorklet.addModule(url); } finally { URL.revokeObjectURL(url); }
          const node = new AudioWorkletNode(ctx, 'epiano-synth', {
            numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
            processorOptions: { params: values },
          });
          node.port.onmessage = (e) => onDspMessage(e.data);
          built = { node, port: node.port, mode: 'AudioWorklet' };
        } catch (e) { console.warn('AudioWorklet unavailable, using ScriptProcessor', e); }
      }
      if (!built) built = this.scriptProcessor(ctx, src);
      this.node = built.node; this.port = built.port; this.mode = built.mode;
      this.node.connect(ctx.destination);
      this.post({ type: 'tempo', bpm: tempo });
      samples.forEach((s, k) => { if (s) postSample(k); }); // anything loaded before audio was ready
      sendConfig();
    },

    // Chrome/Edge block worklet modules on file:// — run the same DSP on the main thread
    scriptProcessor(ctx, src) {
      class AudioWorkletProcessorShim { constructor() { this.port = { postMessage: onDspMessage, onmessage: null }; } }
      let Processor = null;
      new Function('AudioWorkletProcessor', 'registerProcessor', 'sampleRate', src)(
        AudioWorkletProcessorShim, (name, cls) => { Processor = cls; }, ctx.sampleRate);
      const proc = new Processor({ processorOptions: { params: values } });
      const L128 = new Float32Array(128), R128 = new Float32Array(128);
      const node = ctx.createScriptProcessor(1024, 0, 2);
      node.onaudioprocess = (e) => {
        const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
        for (let off = 0; off < L.length; off += 128) {
          proc.process([], [[L128, R128]]);
          L.set(L128, off); R.set(R128, off);
        }
      };
      return { node, port: { postMessage: (m) => proc.port.onmessage({ data: m }) }, mode: 'ScriptProcessor' };
    },
  };

  function onDspMessage(m) { if (m.type === 'stats') toUi('stats', m); }

  function sendConfig() {
    const c = audio.ctx;
    let text;
    if (audio.error) text = `Audio error: ${audio.error}`;
    else if (!audio.running) text = 'Click anywhere to enable audio';
    else text = `Web · ${audio.mode} · ${(c.sampleRate / 1000).toFixed(1)} kHz · ~${(((c.baseLatency || 0) + (c.outputLatency || 0)) * 1000).toFixed(1)} ms`;
    toUi('config', { wrapper: 'Web', sampleRate: c ? c.sampleRate : 0, blockSize: 128, audio: text, running: audio.running });
  }

  const started = audio.start().catch((e) => { audio.error = e.message || String(e); console.error(e); sendConfig(); });
  function unlockAudio() {
    if (audio.running || audio.error) return;
    started.then(() => audio.ctx && audio.ctx.resume()).then(sendConfig).catch(() => {});
  }
  document.addEventListener('pointerdown', unlockAudio, true);
  document.addEventListener('keydown', unlockAudio, true);

  // -------------------------------------------------------------- samples ---
  // Same rules as vst/Source/SampleData.cpp: mono, peak-normalised, max 10 s,
  // root = WAV 'smpl' unity note (else C4). Stored in presets as base64 int16.
  const MAX_SAMPLE_SECONDS = 10;

  function postSample(k) {
    const s = samples[k];
    audio.post({ type: 'sample', osc: k, data: s ? s.data.slice() : null, rate: s ? s.rate : 0, root: s ? s.root : 60 });
  }

  function samplePeaks(s, points) {
    const out = [], n = s.data.length;
    for (let i = 0; i < points; i++) {
      const a = Math.floor(i * n / points), b = Math.max(a + 1, Math.floor((i + 1) * n / points));
      let mn = 0, mx = 0;
      for (let j = a; j < b && j < n; j++) { const x = s.data[j]; if (x < mn) mn = x; if (x > mx) mx = x; }
      out.push(Math.round(mn * 1000) / 1000, Math.round(mx * 1000) / 1000);
    }
    return out;
  }

  function sendSamples() {
    toUi('samples', { slots: samples.map(s => s ? { name: s.name, root: s.root, seconds: s.data.length / s.rate, peaks: samplePeaks(s, 100) } : null) });
  }

  function setSample(k, s) {
    samples[k] = s;
    postSample(k);
    sendSamples();
  }

  // WAV 'smpl' chunk -> MIDI unity note (null if absent / not a WAV)
  function wavRootNote(buf) {
    const v = new DataView(buf);
    const tag = (o) => String.fromCharCode(v.getUint8(o), v.getUint8(o + 1), v.getUint8(o + 2), v.getUint8(o + 3));
    if (buf.byteLength < 12 || tag(0) !== 'RIFF' || tag(8) !== 'WAVE') return null;
    for (let o = 12; o + 8 <= buf.byteLength;) {
      const size = v.getUint32(o + 4, true);
      if (tag(o) === 'smpl' && size >= 16 && o + 24 <= buf.byteLength) return Math.min(127, v.getUint32(o + 20, true));
      o += 8 + size + (size & 1);
    }
    return null;
  }

  // PCM / float WAV read directly (keeps the file's own sample rate, like the plugin).
  // Returns { sampleRate, length, numberOfChannels, getChannelData(c) } or null.
  function parseWav(buf) {
    const v = new DataView(buf);
    const tag = (o) => String.fromCharCode(v.getUint8(o), v.getUint8(o + 1), v.getUint8(o + 2), v.getUint8(o + 3));
    if (buf.byteLength < 12 || tag(0) !== 'RIFF' || tag(8) !== 'WAVE') return null;
    let fmt = null, dataOff = -1, dataLen = 0;
    for (let o = 12; o + 8 <= buf.byteLength;) {
      const id = tag(o), size = v.getUint32(o + 4, true);
      if (id === 'fmt ') {
        let format = v.getUint16(o + 8, true);
        if (format === 0xfffe && size >= 26) format = v.getUint16(o + 32, true); // WAVE_FORMAT_EXTENSIBLE
        fmt = { format, channels: v.getUint16(o + 10, true), rate: v.getUint32(o + 12, true), bits: v.getUint16(o + 22, true) };
      } else if (id === 'data') { dataOff = o + 8; dataLen = Math.min(size, buf.byteLength - dataOff); }
      o += 8 + size + (size & 1);
    }
    if (!fmt || dataOff < 0 || !fmt.channels || !fmt.rate) return null;
    const { format, channels, bits } = fmt, bps = bits >> 3;
    if (!((format === 1 && [8, 16, 24, 32].includes(bits)) || (format === 3 && (bits === 32 || bits === 64)))) return null;
    const frames = Math.floor(dataLen / (bps * channels));
    const chans = Array.from({ length: channels }, () => new Float32Array(frames));
    for (let i = 0, p = dataOff; i < frames; i++) {
      for (let c = 0; c < channels; c++, p += bps) {
        let x;
        if (format === 3) x = bits === 32 ? v.getFloat32(p, true) : v.getFloat64(p, true);
        else if (bits === 8) x = (v.getUint8(p) - 128) / 128;
        else if (bits === 16) x = v.getInt16(p, true) / 32768;
        else if (bits === 24) { let s = v.getUint8(p) | (v.getUint8(p + 1) << 8) | (v.getUint8(p + 2) << 16); if (s & 0x800000) s -= 0x1000000; x = s / 8388608; }
        else x = v.getInt32(p, true) / 2147483648;
        chans[c][i] = x;
      }
    }
    return { sampleRate: fmt.rate, length: frames, numberOfChannels: channels, getChannelData: (c) => chans[c] };
  }

  async function decodeSample(arrayBuffer, name) {
    const root = wavRootNote(arrayBuffer);
    let buf = parseWav(arrayBuffer);
    if (!buf) { // AIFF / FLAC / OGG / MP3 / unusual WAV: let the browser decode it
      const ctx = audio.ctx || new OfflineAudioContext(1, 1, 48000);
      try { buf = await ctx.decodeAudioData(arrayBuffer.slice(0)); }
      catch (e) { throw new Error(`Couldn't read audio from ${name} (try WAV, AIFF, FLAC, OGG or MP3)`); }
    }
    if (!buf.length) throw new Error(`${name} contains no audio`);
    const len = Math.min(buf.length, Math.floor(buf.sampleRate * MAX_SAMPLE_SECONDS));
    const data = new Float32Array(len);
    for (let c = 0; c < buf.numberOfChannels; c++) {
      const ch = buf.getChannelData(c);
      for (let i = 0; i < len; i++) data[i] += ch[i] / buf.numberOfChannels;
    }
    let peak = 0;
    for (let i = 0; i < len; i++) peak = Math.max(peak, Math.abs(data[i]));
    if (peak > 1e-6) for (let i = 0; i < len; i++) data[i] /= peak;
    return { name, rate: buf.sampleRate, root: root === null ? 60 : root, data, truncated: buf.length > len };
  }

  async function loadSampleBytes(k, arrayBuffer, name) {
    try {
      const s = await decodeSample(arrayBuffer, name);
      setSample(k, s);
      const id = `osc${k + 1}Smp`;
      if ((values[id] | 0) === 0) { // loading a sample switches that oscillator over to it
        values[id] = 1;
        audio.post({ type: 'param', id, value: 1 });
        toUi('params', { [id]: 1 });
      }
      message(s.truncated ? `${name} is longer than ${MAX_SAMPLE_SECONDS} s — using the first ${MAX_SAMPLE_SECONDS} s`
                          : `Loaded ${name} into OSC ${k + 1}`);
    } catch (e) { message(e.message || String(e)); }
  }

  function sampleToJson(s) {
    const pcm = new Int16Array(s.data.length);
    for (let i = 0; i < pcm.length; i++) pcm[i] = Math.round(clamp(s.data[i], -1, 1) * 32767);
    const bytes = new Uint8Array(pcm.buffer);
    let bin = '';
    for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return { name: s.name, rate: s.rate, root: s.root, pcm16: btoa(bin) };
  }
  function sampleFromJson(j) {
    if (!j || typeof j.pcm16 !== 'string') return null;
    const bin = atob(j.pcm16);
    const n = bin.length >> 1;
    if (n < 2) return null;
    const data = new Float32Array(n);
    for (let i = 0; i < n; i++) {
      let v = bin.charCodeAt(2 * i) | (bin.charCodeAt(2 * i + 1) << 8);
      if (v >= 0x8000) v -= 0x10000;
      data[i] = v / 32767;
    }
    return { name: String(j.name || 'sample'), rate: clamp(Number(j.rate) || 44100, 1000, 384000), root: clamp(Math.round(Number(j.root) || 60), 0, 127), data };
  }

  const sampleInput = document.createElement('input');
  sampleInput.type = 'file';
  sampleInput.accept = '.wav,.wave,.aif,.aiff,.flac,.ogg,.mp3,audio/*';
  sampleInput.style.display = 'none';
  document.addEventListener('DOMContentLoaded', () => document.body.appendChild(sampleInput));
  let sampleTarget = 0;
  sampleInput.addEventListener('change', async () => {
    const f = sampleInput.files && sampleInput.files[0];
    sampleInput.value = '';
    if (f) loadSampleBytes(sampleTarget, await f.arrayBuffer(), f.name);
  });

  // ---------------------------------------------------------------- notes ---
  const sounding = new Set();
  function noteOn(note, vel) {
    if (!audio.running) message('Audio is off — click anywhere on the page once to enable it');
    audio.post({ type: 'noteOn', note, vel });
    sounding.add(note);
    toUi('notes', { on: [note], off: [] });
  }
  function noteOff(note) {
    audio.post({ type: 'noteOff', note });
    if (sounding.delete(note)) toUi('notes', { on: [], off: [note] });
  }
  function allOff() {
    const off = [...sounding];
    sounding.clear();
    if (off.length) toUi('notes', { on: [], off });
  }

  // ----------------------------------------------------------------- MIDI ---
  const midi = { access: null, selected: store.get('epiano.midiInput') || 'all' };
  function midiInputs() {
    return midi.access ? Array.from(midi.access.inputs.values()).map(i => ({ id: i.id, name: i.name, state: i.state })) : [];
  }
  function bindMidi() {
    if (!midi.access) return;
    for (const input of midi.access.inputs.values()) {
      const on = midi.selected === 'all' || midi.selected === input.id;
      input.onmidimessage = on ? (e) => onMidi(e.data) : null;
    }
  }
  function onMidi(d) {
    const st = d[0];
    if (st >= 0xf0) return;
    const cmd = st & 0xf0;
    if (cmd === 0x90 && d[2] > 0) noteOn(d[1], d[2]);
    else if (cmd === 0x80 || cmd === 0x90) noteOff(d[1]);
    else if (cmd === 0xb0) {
      if (d[1] === 64) audio.post({ type: 'sustain', on: d[2] >= 64 });
      else if (d[1] === 1) audio.post({ type: 'modWheel', value: d[2] / 127 });
      else if (d[1] === 120) { audio.post({ type: 'panic' }); allOff(); }
      else if (d[1] === 123) { audio.post({ type: 'allNotesOff' }); allOff(); }
    } else if (cmd === 0xe0) {
      audio.post({ type: 'bend', value: (((d[2] << 7) | d[1]) - 8192) / 8192 });
    }
  }
  if (typeof navigator.requestMIDIAccess === 'function') {
    navigator.requestMIDIAccess({ sysex: false }).then((access) => {
      midi.access = access;
      access.onstatechange = () => { bindMidi(); toUi('midiInputs', { inputs: midiInputs(), selected: midi.selected }); };
      bindMidi();
      toUi('midiInputs', { inputs: midiInputs(), selected: midi.selected });
      const n = midiInputs().length;
      message(n ? `MIDI ready — ${n} input(s): ${midiInputs().map(i => i.name).join(', ')}` : 'MIDI ready — no devices found (plug one in, it appears automatically)');
    }).catch((e) => {
      toUi('midiInputs', { inputs: [], selected: 'all', error: true });
      message(`MIDI unavailable: ${e.message || e}`);
    });
  } else {
    queueMicrotask(() => toUi('midiInputs', { inputs: [], selected: 'all', error: true }));
  }

  // -------------------------------------------------------------- presets ---
  const PRESET_EXT = '.epreset';
  const LS_KEY = 'epiano.userPresets.v1';
  const local = {
    read() { try { return JSON.parse(store.get(LS_KEY)) || {}; } catch (e) { return {}; } },
    write(obj) { return store.set(LS_KEY, JSON.stringify(obj)); },
  };
  const localPresets = () => Object.keys(local.read()).sort((a, b) => a.localeCompare(b, undefined, { numeric: true }))
    .map(name => ({ id: `local:${name}`, name, category: 'User', factory: false }));

  const fsSupported = typeof window.showDirectoryPicker === 'function';
  const folder = { handle: null, granted: false, files: [], sig: '' };
  const idb = {
    open() {
      return new Promise((res, rej) => {
        const r = indexedDB.open('epiano', 1);
        r.onupgradeneeded = () => r.result.createObjectStore('kv');
        r.onsuccess = () => res(r.result);
        r.onerror = () => rej(r.error);
      });
    },
    async get(key) {
      const db = await this.open();
      return new Promise((res) => { const r = db.transaction('kv').objectStore('kv').get(key); r.onsuccess = () => res(r.result); r.onerror = () => res(undefined); });
    },
    async set(key, val) {
      const db = await this.open();
      return new Promise((res) => { const tx = db.transaction('kv', 'readwrite'); tx.objectStore('kv').put(val, key); tx.oncomplete = () => res(true); tx.onerror = () => res(false); });
    },
  };

  async function scanFolder() {
    if (!folder.handle || !folder.granted) return false;
    const files = [];
    async function walk(dir, path) {
      for await (const [name, h] of dir.entries()) {
        const rel = path ? `${path}/${name}` : name;
        if (h.kind === 'directory') await walk(h, rel);
        else if (name.toLowerCase().endsWith(PRESET_EXT)) {
          const f = await h.getFile();
          files.push({ id: `user:${rel}`, name: name.slice(0, -PRESET_EXT.length), category: path || 'User',
                       factory: false, handle: h, parent: dir, fileName: name, stamp: `${f.size}:${f.lastModified}` });
        }
      }
    }
    try { await walk(folder.handle, ''); } catch (e) { folder.granted = false; return true; }
    files.sort((a, b) => a.category.localeCompare(b.category) || a.name.localeCompare(b.name, undefined, { numeric: true }));
    const sig = files.map(f => `${f.id}|${f.stamp}`).join(';');
    if (sig === folder.sig) return false;
    folder.sig = sig;
    folder.files = files;
    return true;
  }

  function allPresets() { return [...FACTORY, ...folder.files, ...localPresets()]; }
  function sendPresetList() {
    toUi('presetList', {
      presets: allPresets().map(({ id, name, category, factory }) => ({ id, name, category, factory })),
      folder: folder.granted ? `${folder.handle.name} (linked folder)` : 'this browser — click FOLDER to link a folder on disk',
    });
  }

  async function linkFolder(handle, ask) {
    folder.handle = handle;
    folder.sig = '';
    let state = await handle.queryPermission({ mode: 'readwrite' });
    if (state !== 'granted' && ask) state = await handle.requestPermission({ mode: 'readwrite' });
    folder.granted = state === 'granted';
    await idb.set('presetFolder', handle);
    if (folder.granted) await scanFolder();
    sendPresetList();
    return folder.granted;
  }
  if (fsSupported && window.indexedDB) {
    idb.get('presetFolder').then(async (h) => {
      if (h && !(await linkFolder(h, false))) message(`Click FOLDER to reconnect the preset folder "${h.name}"`);
    }).catch(() => {});
  }
  setInterval(async () => { if (await scanFolder()) sendPresetList(); }, 2000); // autoload

  function parsePresetText(text, fallbackName) {
    let json;
    try { json = JSON.parse(text); } catch (e) { throw new Error(`Not a valid preset file: ${fallbackName}`); }
    const params = json && typeof json.params === 'object' ? json.params : null;
    if (!params || !PARAM_DEFS.some(d => !d.noPreset && d.id in params)) throw new Error(`No E-Piano settings found in ${fallbackName}`);
    return { name: String(json.name || '').trim() || fallbackName, params, samples: json.samples };
  }
  function presetText(name, category) {
    const params = {};
    for (const d of PARAM_DEFS) if (!d.noPreset) params[d.id] = +Number(values[d.id]).toPrecision(6);
    const out = { format: 'E-Piano Preset', version: 1, name, category, params };
    if (samples.some(Boolean)) {
      out.samples = {};
      samples.forEach((s, k) => { if (s) out.samples[String(k + 1)] = sampleToJson(s); });
    }
    return JSON.stringify(out, null, 2);
  }
  const legalName = (s) => s.trim().replace(/[\\/:*?"<>|]/g, '_').slice(0, 60);

  async function loadPresetById(id) {
    const p = allPresets().find(x => x.id === id);
    if (!p) { message('Preset not found (was it moved or deleted?)'); toUi('preset', current); return; }
    try {
      const info = { id, name: p.name, category: p.category };
      if (p.factory) applyPreset(p.values, info);
      else {
        const text = id.startsWith('local:') ? local.read()[p.name] : await (await p.handle.getFile()).text();
        const parsed = parsePresetText(text, p.name);
        applyPreset(parsed.params, info, parsed.samples);
      }
    } catch (e) { message(e.message || String(e)); toUi('preset', current); }
  }

  async function savePreset(rawName, overwrite) {
    const name = legalName(rawName);
    const result = (r) => toUi('saveResult', Object.assign({ name }, r));
    if (!name) return result({ ok: false, error: 'Enter a name for the preset' });
    try {
      if (folder.granted) {
        const fileName = name + PRESET_EXT;
        if (!overwrite) {
          let exists = true;
          try { await folder.handle.getFileHandle(fileName); } catch (e) { exists = false; }
          if (exists) return result({ ok: false, exists: true });
        }
        const w = await (await folder.handle.getFileHandle(fileName, { create: true })).createWritable();
        await w.write(presetText(name, 'User'));
        await w.close();
        await scanFolder();
        current = { id: `user:${fileName}`, name, category: 'User' };
      } else {
        const all = local.read();
        if (!overwrite && all[name]) return result({ ok: false, exists: true });
        all[name] = presetText(name, 'User');
        if (!local.write(all)) throw new Error('browser storage is unavailable (private window?)');
        current = { id: `local:${name}`, name, category: 'User' };
      }
      sendPresetList();
      toUi('preset', current);
      result({ ok: true });
    } catch (e) { result({ ok: false, error: e.message || String(e) }); }
  }

  async function deletePreset(id) {
    const p = allPresets().find(x => x.id === id);
    try {
      if (!p || p.factory) throw new Error('Only user presets can be deleted');
      if (id.startsWith('local:')) { const all = local.read(); delete all[p.name]; local.write(all); }
      else { await p.parent.removeEntry(p.fileName); await scanFolder(); }
      if (current.id === id) current = { id: '', name: current.name, category: 'User' };
      message(`Deleted preset "${p.name}"`);
    } catch (e) { message(e.message || String(e)); }
    sendPresetList();
    toUi('preset', current);
  }

  const fileInput = document.createElement('input');
  fileInput.type = 'file';
  fileInput.accept = `${PRESET_EXT},application/json`;
  fileInput.style.display = 'none';
  document.addEventListener('DOMContentLoaded', () => document.body.appendChild(fileInput));
  fileInput.addEventListener('change', async () => {
    const f = fileInput.files && fileInput.files[0];
    fileInput.value = '';
    if (!f) return;
    try {
      const p = parsePresetText(await f.text(), f.name.replace(/\.[^.]+$/, ''));
      applyPreset(p.params, { id: `file:${f.name}`, name: p.name, category: 'File' }, p.samples);
      message(`Loaded ${f.name}`);
    } catch (e) { message(e.message || String(e)); }
  });

  async function folderButton() {
    if (!fsSupported) { message('Linking a preset folder needs Chrome or Edge — presets are saved in this browser instead.'); return; }
    try {
      if (folder.handle && !folder.granted) { if (await linkFolder(folder.handle, true)) message(`Reconnected "${folder.handle.name}"`); return; }
      const handle = await window.showDirectoryPicker({ id: 'epiano-presets', mode: 'readwrite', startIn: 'documents' });
      await linkFolder(handle, true);
      message(`Preset folder linked: ${handle.name} — ${folder.files.length} preset(s). Link Documents\\E-Piano\\Presets to share presets with the VST.`);
    } catch (e) { if (e && e.name !== 'AbortError') message(`Folder: ${e.message || e}`); }
  }

  // ------------------------------------------------- UI -> host events ---
  Object.assign(handlers, {
    uiReady() {
      sendConfig();
      sendPresetList();
      toUi('params', Object.assign({}, values));
      toUi('preset', current);
      toUi('stats', { voices: 0, peak: 0, sustain: false, modWheel: 0, bpm: tempo });
      if (midi.access) toUi('midiInputs', { inputs: midiInputs(), selected: midi.selected });
      sendSamples();
    },
    paramMenu() {}, // no host automation menu in a browser
    loadSample({ osc }) { sampleTarget = clamp(osc | 0, 0, 2); sampleInput.click(); },
    dropSample({ osc, name, data }) {
      try {
        const bin = atob(data);
        const bytes = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
        loadSampleBytes(clamp(osc | 0, 0, 2), bytes.buffer, name);
      } catch (e) { message("Couldn't read the dropped file"); }
    },
    clearSample({ osc }) { setSample(clamp(osc | 0, 0, 2), null); },
    setParam({ id, value }) { if (id in values) { values[id] = value; audio.post({ type: 'param', id, value }); } },
    gesture() {},
    noteOn({ note, vel }) { noteOn(note, vel); },
    noteOff({ note }) { noteOff(note); },
    panic() { audio.post({ type: 'panic' }); allOff(); },
    modWheel({ value }) { audio.post({ type: 'modWheel', value }); },
    setTempo({ bpm }) { tempo = clamp(Number(bpm) || 120, 20, 300); store.set('epiano.tempo', String(tempo)); audio.post({ type: 'tempo', bpm: tempo }); },
    selectMidi({ id }) { midi.selected = id; store.set('epiano.midiInput', id); bindMidi(); },
    loadPreset({ id }) { loadPresetById(id); },
    savePreset({ name, overwrite }) { savePreset(name, overwrite); },
    deletePreset({ id }) { deletePreset(id); },
    loadPresetFile() { fileInput.click(); },
    openPresetFolder() { folderButton(); },
  });
})();
