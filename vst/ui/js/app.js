// ============================================================================
//  Plugin UI wiring. Differs from the web build (web/index.html):
//   - engine is the JUCE bridge (C++ owns all parameter state)
//   - no power button / Web MIDI (the DAW provides audio + MIDI)
//   - host automation, program changes and host MIDI notes flow back in
//   - the whole panel scales to fit the plugin window
// ============================================================================
(function () {
  'use strict';

  const $ = (sel) => document.querySelector(sel);
  function el(tag, cls, html) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (html !== undefined) e.innerHTML = html;
    return e;
  }

  // ---------------------------------------------------------------- state ---
  const values = defaultParamValues();      // replaced by the plugin's state on connect
  let presetList = [];                      // [{id, name, category, factory}] factory first, then user
  let presetFolder = '';
  let current = { id: '', name: '—', category: '' };
  let presetDirty = false;
  let activeCat = null;                     // category tab being shown

  const engine = new JuceEngine();
  const knobs = {};
  const toggles = {};
  const screens = {};
  const dragging = new Set();   // params being dragged in the UI
  const lastLocal = {};         // id -> time of last UI-side change

  // ---------------------------------------------------- parameter plumbing ---
  function applyValue(id, v) {
    values[id] = v;
    if (knobs[id]) knobs[id].setValue(v);
    if (toggles[id]) toggles[id](v);
  }

  // A change made in this UI
  function setParam(id, v) {
    lastLocal[id] = performance.now();
    applyValue(id, v);
    engine.setParam(id, v);
    if (!PARAM_BY_ID[id].noPreset && !presetDirty) { presetDirty = true; renderPresetName(); }
    refreshScreens(PARAM_BY_ID[id].group);
  }

  // Changes coming from the plugin (automation, presets, state restore)
  engine.on('params', (obj) => {
    const now = performance.now();
    const groups = new Set();
    for (const id in obj) {
      if (!PARAM_BY_ID[id]) continue;
      // don't fight the user's hand: skip params being dragged or changed here in the last 300 ms
      if (dragging.has(id) || (lastLocal[id] !== undefined && now - lastLocal[id] < 300)) continue;
      applyValue(id, obj[id]);
      groups.add(PARAM_BY_ID[id].group);
    }
    groups.forEach(g => refreshScreens(g));
  });

  // ------------------------------------------------------------ presets ----
  const esc = (s) => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

  engine.on('presetList', ({ presets, folder }) => {
    presetList = presets;
    presetFolder = folder;
    if (!activeCat || !categories().includes(activeCat)) activeCat = current.category || (presets[0] && presets[0].category);
    renderBrowser();
  });

  engine.on('preset', (p) => {
    const changed = p.id !== current.id || p.name !== current.name;
    current = p;
    if (changed) presetDirty = false;
    if (p.category && categories().includes(p.category)) activeCat = p.category;
    renderPresetName();
    renderBrowser();
  });

  engine.on('message', ({ text }) => logMidi(text));

  function categories() {
    const cats = [];
    for (const p of presetList) if (!cats.includes(p.category)) cats.push(p.category);
    if (!presetList.some(p => !p.factory) && !cats.includes('User')) cats.push('User');
    return cats;
  }

  function loadPreset(p) {
    presetDirty = false;
    current = { id: p.id, name: p.name, category: p.category };
    for (const id in lastLocal) delete lastLocal[id]; // let all incoming values through
    renderPresetName();
    renderBrowser();
    engine.loadPreset(p.id); // plugin applies the values and sends them back
  }

  function stepPreset(dir) {
    if (!presetList.length) return;
    const i = presetList.findIndex(p => p.id === current.id);
    const next = i < 0 ? 0 : (i + dir + presetList.length) % presetList.length;
    loadPreset(presetList[next]);
  }

  function renderPresetName() {
    $('#preset-name').textContent = current.name + (presetDirty ? ' *' : '');
    $('#preset-cat').textContent = (current.category || 'Voice').toUpperCase();
  }

  const tabsEl = $('#cat-tabs');
  const gridEl = $('#preset-grid');
  function renderBrowser() {
    const cats = categories();
    tabsEl.innerHTML = '';
    for (const c of cats) {
      const t = el('button', 'cat-tab', esc(c));
      t.type = 'button';
      t.setAttribute('role', 'tab');
      t.setAttribute('aria-selected', c === activeCat);
      t.classList.toggle('active', c === activeCat);
      t.classList.toggle('has-current', c === current.category);
      t.addEventListener('click', () => { activeCat = c; renderBrowser(); });
      tabsEl.appendChild(t);
    }

    gridEl.innerHTML = '';
    const items = presetList.filter(p => p.category === activeCat);
    if (!items.length) {
      gridEl.appendChild(el('div', 'grid-empty',
        `No user presets yet. Click <b>SAVE</b> to store the current sound.<br>
         Preset files in <span class="path">${esc(presetFolder)}</span> show up here automatically.`));
      return;
    }
    for (const p of items) {
      const b = el('button', 'preset-btn', `<span class="pb-name">${esc(p.name)}</span>`);
      b.type = 'button';
      b.title = p.name;
      b.classList.toggle('selected', p.id === current.id);
      b.addEventListener('click', () => loadPreset(p));
      if (!p.factory) {
        const del = el('span', 'pb-del', '×');
        del.title = 'Delete preset';
        del.addEventListener('click', (e) => {
          e.stopPropagation();
          openModal({
            title: 'Delete preset',
            body: `Move <b>${esc(p.name)}</b> to the Recycle Bin?`,
            ok: 'DELETE',
            danger: true,
            onOk: () => engine.deletePreset(p.id),
          });
        });
        b.appendChild(del);
      }
      gridEl.appendChild(b);
    }
  }

  // Save: ask for a name, confirm before replacing an existing file
  function savePrompt() {
    openModal({
      title: 'Save preset',
      body: `Saved to <span class="path">${esc(presetFolder)}</span>`,
      input: current.category === 'User' || !current.id.startsWith('factory:') ? current.name : `${current.name} (copy)`,
      ok: 'SAVE',
      onOk: (name) => { if (name.trim()) engine.savePreset(name.trim(), false); },
    });
  }
  engine.on('saveResult', ({ ok, exists, name, error }) => {
    if (ok) { presetDirty = false; renderPresetName(); logMidi(`Saved preset "${name}"`); }
    else if (exists) {
      openModal({
        title: 'Replace preset?',
        body: `A preset named <b>${esc(name)}</b> already exists. Replace it?`,
        ok: 'REPLACE',
        danger: true,
        onOk: () => engine.savePreset(name, true),
      });
    } else logMidi(`Save failed: ${error}`);
  });

  $('#preset-prev').addEventListener('click', () => stepPreset(-1));
  $('#preset-next').addEventListener('click', () => stepPreset(1));
  $('#preset-save').addEventListener('click', savePrompt);
  $('#preset-load').addEventListener('click', () => engine.loadPresetFile());
  $('#preset-folder').addEventListener('click', () => engine.openPresetFolder());

  // ------------------------------------------------------------- modal -----
  const modal = $('#modal'), modalInput = $('#modal-input'), modalOk = $('#modal-ok');
  let modalOnOk = null;
  function openModal({ title, body, input, ok, danger, onOk }) {
    $('#modal-title').textContent = title;
    $('#modal-body').innerHTML = body || '';
    modalOk.textContent = ok || 'OK';
    modalOk.classList.toggle('danger', !!danger);
    modalInput.style.display = input === undefined ? 'none' : '';
    modalInput.value = input || '';
    modalOnOk = onOk;
    modal.classList.remove('hidden');
    if (input !== undefined) { modalInput.focus(); modalInput.select(); } else modalOk.focus();
  }
  function closeModal(accept) {
    if (modal.classList.contains('hidden')) return;
    modal.classList.add('hidden');
    const fn = modalOnOk;
    modalOnOk = null;
    if (accept && fn) fn(modalInput.value);
  }
  modalOk.addEventListener('click', () => closeModal(true));
  $('#modal-cancel').addEventListener('click', () => closeModal(false));
  modal.addEventListener('pointerdown', (e) => { if (e.target === modal) closeModal(false); });
  modal.addEventListener('keydown', (e) => {
    e.stopPropagation(); // typing a name must not play notes
    if (e.key === 'Enter') { e.preventDefault(); closeModal(true); }
    else if (e.key === 'Escape') { e.preventDefault(); closeModal(false); }
  });
  modal.addEventListener('keyup', (e) => e.stopPropagation());

  // ------------------------------------------------------------- screens ---
  function wavePath(wave, w, h) {
    const N = 240, cycles = 2, pad = 5, pts = [];
    for (let i = 0; i <= N; i++) {
      const x = i / N, t = (x * cycles) % 1;
      let y;
      if (wave === 1) y = 2 * t - 1;
      else if (wave === 2) y = t < 0.5 ? 1 : -1;
      else if (wave === 3) y = 1 - 4 * Math.abs(((t + 0.25) % 1) - 0.5);
      else y = Math.sin(2 * Math.PI * t);
      pts.push(`${(x * w).toFixed(1)},${(h / 2 - y * (h / 2 - pad)).toFixed(1)}`);
    }
    return 'M' + pts.join(' L');
  }

  function adsrPath(a, d, s, r, w, h) {
    const pad = 5, seg = (w - 2 * pad) * 0.26;
    const tw = (t) => seg * (0.08 + 0.92 * Math.log10(1 + t * 100) / Math.log10(1 + 1000));
    const top = pad, bot = h - pad, ys = bot - s * (bot - top);
    let x = pad;
    const p = [`M${x},${bot}`];
    x += tw(a); p.push(`L${x.toFixed(1)},${top}`);
    x += tw(d); p.push(`L${x.toFixed(1)},${ys.toFixed(1)}`);
    x = w - pad - tw(r); p.push(`L${x.toFixed(1)},${ys.toFixed(1)}`);
    p.push(`L${w - pad},${bot}`);
    return p.join(' ');
  }

  function filterPath(cut, reso, w, h) {
    const k = 2 - 1.9 * reso, N = 120, pts = [];
    for (let i = 0; i <= N; i++) {
      const f = 20 * Math.pow(1000, i / N), x = f / cut;
      const mag = 1 / Math.sqrt((1 - x * x) ** 2 + (k * x) ** 2);
      const db = clamp(20 * Math.log10(mag), -36, 24);
      const y = 6 + (24 - db) / 60 * (h - 10);
      pts.push(`${(i / N * w).toFixed(1)},${y.toFixed(1)}`);
    }
    return 'M' + pts.join(' L');
  }

  function makeScreen(parent) {
    const s = el('div', 'screen', `
      <svg viewBox="0 0 200 48" preserveAspectRatio="none"><path class="scr-grid" d="M0 24 H200"/><path class="scr-line" d=""/></svg>
      <div class="scr-text"></div>`);
    parent.appendChild(s);
    return { path: s.querySelector('.scr-line'), text: s.querySelector('.scr-text'), root: s };
  }

  function refreshScreens(group) {
    for (let n = 1; n <= 3; n++) {
      const g = `osc${n}`;
      if (group && group !== g) continue;
      const scr = screens[g], on = values[`${g}On`] >= 0.5, w = values[`${g}Wave`] | 0;
      scr.path.setAttribute('d', wavePath(w, 200, 48));
      scr.text.textContent = on
        ? `${WAVE_NAMES[w].toUpperCase()}  ${formatParam(PARAM_BY_ID[`${g}Pitch`], values[`${g}Pitch`])}  ${formatParam(PARAM_BY_ID[`${g}Fine`], values[`${g}Fine`])}`
        : 'OFF';
      scr.root.closest('.panel').classList.toggle('off', !on);
    }
    if (!group || group === 'amp') {
      screens.amp.path.setAttribute('d', adsrPath(values.ampAttack, values.ampDecay, values.ampSustain, values.ampRelease, 200, 48));
    }
    if (!group || group === 'filter') {
      screens.filter.path.setAttribute('d', filterPath(values.fltCutoff, values.fltReso, 200, 48));
      screens.filter.text.textContent = `LP12  ${formatParam(PARAM_BY_ID.fltCutoff, values.fltCutoff)}`;
    }
  }

  // ------------------------------------------------------------- panels ----
  function addKnob(row, id) {
    const k = new Knob(PARAM_BY_ID[id], values[id],
      (v) => setParam(id, v),
      (begin) => {
        if (begin) { dragging.add(id); engine.beginGesture(id); }
        else { dragging.delete(id); engine.endGesture(id); }
      });
    knobs[id] = k;
    row.appendChild(k.el);
  }

  function makePanel(title, extraHeader) {
    const p = el('div', 'panel');
    const h = el('div', 'panel-head', `<span class="panel-title">${title}</span>`);
    if (extraHeader) h.appendChild(extraHeader);
    p.appendChild(h);
    return p;
  }

  const oscRow = $('#osc-row');
  for (let n = 1; n <= 3; n++) {
    const id = `osc${n}On`;
    const btn = el('button', 'power', '<span class="led"></span><span>ON</span>');
    btn.type = 'button';
    btn.setAttribute('aria-label', `Oscillator ${n} on/off`);
    btn.addEventListener('click', () => setParam(id, values[id] >= 0.5 ? 0 : 1));
    toggles[id] = (v) => { btn.classList.toggle('on', v >= 0.5); btn.setAttribute('aria-pressed', v >= 0.5); };
    toggles[id](values[id]);

    const panel = makePanel(`OSC ${n}`, btn);
    screens[`osc${n}`] = makeScreen(panel);
    const row = el('div', 'knob-row');
    ['Wave', 'Pitch', 'Fine', 'Level'].forEach(f => addKnob(row, `osc${n}${f}`));
    panel.appendChild(row);
    oscRow.appendChild(panel);
  }

  const modRow = $('#mod-row');
  {
    const amp = makePanel('AMP ENVELOPE');
    screens.amp = makeScreen(amp);
    screens.amp.text.textContent = 'ADSR';
    const row = el('div', 'knob-row');
    ['ampAttack', 'ampDecay', 'ampSustain', 'ampRelease', 'ampLevel'].forEach(id => addKnob(row, id));
    amp.appendChild(row);
    modRow.appendChild(amp);

    const flt = makePanel('FILTER');
    screens.filter = makeScreen(flt);
    const frow = el('div', 'knob-row');
    ['fltCutoff', 'fltReso', 'fltEnvAmt', 'fltDecay'].forEach(id => addKnob(frow, id));
    flt.appendChild(frow);
    modRow.appendChild(flt);

    const out = makePanel('OUTPUT');
    out.classList.add('output-panel');
    out.appendChild(el('div', 'screen voice-screen', `
      <div class="vs-row"><span class="vs-label">VOICES</span><span class="vs-value"><b id="voice-count">0</b>/${NUM_VOICES}</span></div>
      <div class="meter"><div id="meter-fill"></div><div class="meter-clip"></div></div>`));
    const orow = el('div', 'knob-row');
    addKnob(orow, 'master');
    out.appendChild(orow);
    modRow.appendChild(out);
  }

  // ---------------------------------------------------------- notes / UI ---
  const monitor = $('#midi-monitor');
  const midiLed = $('#midi-led');
  let ledTimer = 0;
  function logMidi(text) { monitor.textContent = text; }
  function flashLed() {
    midiLed.classList.add('on');
    clearTimeout(ledTimer);
    ledTimer = setTimeout(() => midiLed.classList.remove('on'), 90);
  }

  const piano = new Piano($('#piano'), {
    numKeys: 24,
    baseNote: 48,
    onNoteOn: (n, v) => noteOn(n, v),
    onNoteOff: (n) => noteOff(n),
  });

  function noteOn(note, vel) {
    engine.noteOn(note, vel);
    piano.setNote(note, true);
  }
  function noteOff(note) {
    engine.noteOff(note);
    piano.setNote(note, false);
  }

  // All sounding notes (host MIDI + this keyboard), reported by the plugin
  engine.on('notes', ({ on, off }) => {
    off.forEach(n => piano.setNote(n, false));
    on.forEach(n => piano.setNote(n, true));
    flashLed();
    if (on.length) logMidi(`NOTE ON   ${on.map(n => `${midiNoteName(n)} (${n})`).join('  ')}`);
    else if (off.length) logMidi(`NOTE OFF  ${off.map(n => `${midiNoteName(n)} (${n})`).join('  ')}`);
  });

  function setOctave(base) {
    base = clamp(base, 12, 96);
    for (const [k, n] of heldKeys) { noteOff(n); heldKeys.delete(k); }
    piano.setBase(base);
    $('#octave-readout').textContent = `${midiNoteName(base)}–${midiNoteName(base + 23)}`;
  }
  $('#oct-down').addEventListener('click', () => setOctave(piano.base - 12));
  $('#oct-up').addEventListener('click', () => setOctave(piano.base + 12));

  // Computer keyboard: two rows, A = C
  const KEYMAP = { a: 0, w: 1, s: 2, e: 3, d: 4, f: 5, t: 6, g: 7, y: 8, h: 9, u: 10, j: 11,
                   k: 12, o: 13, l: 14, p: 15, ';': 16, "'": 17 };
  const heldKeys = new Map();
  window.addEventListener('keydown', (e) => {
    if (e.repeat || e.ctrlKey || e.metaKey || e.altKey) return;
    if (/^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName) || !modal.classList.contains('hidden')) return;
    const key = e.key.toLowerCase();
    if (key === 'z') { setOctave(piano.base - 12); return; }
    if (key === 'x') { setOctave(piano.base + 12); return; }
    if (key in KEYMAP && !heldKeys.has(key)) {
      const note = piano.base + KEYMAP[key];
      heldKeys.set(key, note);
      noteOn(note, 100);
    }
  });
  window.addEventListener('keyup', (e) => {
    const key = e.key.toLowerCase();
    if (heldKeys.has(key)) { noteOff(heldKeys.get(key)); heldKeys.delete(key); }
  });
  window.addEventListener('blur', () => {
    for (const [, n] of heldKeys) noteOff(n);
    heldKeys.clear();
  });

  $('#panic-btn').addEventListener('click', () => {
    engine.panic();
    piano.clear();
    heldKeys.clear();
    logMidi('PANIC — all voices killed');
  });

  // ------------------------------------------------------------- stats -----
  const meterFill = $('#meter-fill');
  const meterClip = document.querySelector('.meter-clip');
  const sustainLed = $('#sustain-led');
  let clipTimer = 0;
  engine.on('stats', ({ voices, peak, sustain }) => {
    $('#voice-count').textContent = voices;
    sustainLed.classList.toggle('on', !!sustain);
    const db = 20 * Math.log10(Math.max(peak, 1e-5));
    meterFill.style.width = `${clamp((db + 60) / 60, 0, 1) * 100}%`;
    if (peak > 0.95) {
      meterClip.classList.add('on');
      clearTimeout(clipTimer);
      clipTimer = setTimeout(() => meterClip.classList.remove('on'), 800);
    }
  });

  engine.on('config', ({ sampleRate, blockSize, wrapper }) => {
    const sr = sampleRate > 0 ? `${(sampleRate / 1000).toFixed(1)} kHz · ${blockSize} smp` : 'not playing yet';
    $('#audio-info').textContent = `${wrapper} · ${sr}`;
    $('#audio-led').classList.add('on');
    logMidi('Connected — play your MIDI controller, the keys, or A–\' on the keyboard');
  });

  // --------------------------------------------------------- fit window ----
  const synth = $('.synth');
  function fit() {
    const w = synth.offsetWidth + 24, h = synth.offsetHeight + 24;
    const s = Math.min(window.innerWidth / w, window.innerHeight / h);
    synth.style.transform = `translateX(-50%) scale(${s})`;
  }
  window.addEventListener('resize', fit);

  // No browser context menu in the plugin; Shift+right-click still opens it (Inspect = devtools).
  document.addEventListener('contextmenu', (e) => { if (engine.connected && !e.shiftKey) e.preventDefault(); });

  // -------------------------------------------------------------- init -----
  renderPresetName();
  renderBrowser();
  refreshScreens();
  setOctave(48);
  fit();
  if (engine.connected) engine.uiReady();
  else logMidi('Not running inside the plugin — no audio engine connected (UI preview only)');
})();
