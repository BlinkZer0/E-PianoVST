// ============================================================================
//  UI wiring — shared by the plugin and the web build.
//   - talks only to JuceEngine (engine-juce.js). In the plugin that is JUCE's
//     native bridge to C++; in the web build vst/webhost/web-host.js provides
//     the same bridge on top of Web Audio + Web MIDI.
//   - the host owns all parameter state; automation, preset loads and MIDI
//     notes flow back in as events
//   - the whole panel scales to fit the window
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
  const bound = {};             // id -> [controls with setValue()]
  const screens = {};
  const dragging = new Set();   // params being dragged in the UI
  const lastLocal = {};         // id -> time of last UI-side change

  // ---------------------------------------------------- parameter plumbing ---
  function applyValue(id, v) {
    values[id] = v;
    if (knobs[id]) knobs[id].setValue(v);
    (bound[id] || []).forEach(c => c.setValue(v));
  }

  // A change made in this UI
  function setParam(id, v) {
    lastLocal[id] = performance.now();
    applyValue(id, v);
    engine.setParam(id, v);
    if (!PARAM_BY_ID[id].noPreset && !presetDirty) { presetDirty = true; renderPresetName(); }
    refreshScreens(PARAM_BY_ID[id].group);
  }

  // Bind a non-knob control (ToggleButton / Segmented / SelectBox) to a param
  function control(Cls, id, ...extra) {
    const c = new Cls(PARAM_BY_ID[id], values[id], (v) => setParam(id, v), ...extra);
    (bound[id] = bound[id] || []).push(c);
    return c;
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
    if (changed) { presetDirty = false; shownSlots.clear(); refreshMatrix(); }
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
    if (!group || group === 'lfo1' || group === 'lfo2') refreshLfos();
    if (!group || group === 'matrix') refreshMatrix();
    if (!group || group === 'drive') refreshDrive();
    if (!group || group === 'voice') refreshVoice();
    refreshPageLeds();
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
    const btn = control(ToggleButton, `osc${n}On`).el;
    btn.setAttribute('aria-label', `Oscillator ${n} on/off`);

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

  // ===================================================== MODULATION page ====
  const LFO_DIV_BEATS = [4, 3, 2, 4 / 3, 1.5, 1, 2 / 3, 0.75, 0.5, 1 / 3, 0.375, 0.25, 1 / 6, 0.125, 1 / 12];
  const SH_PATTERN = [0.55, -0.35, 0.9, -0.8, 0.15, -0.55, 0.7, -0.1];
  let tempo = 120;

  function lfoShape(wave, t, cycle) {
    t -= Math.floor(t);
    switch (wave) {
      case 1: return 1 - 4 * Math.abs(((t + 0.25) % 1) - 0.5);
      case 2: return t < 0.5 ? 1 : -1;
      case 3: return 2 * t - 1;
      case 4: return 1 - 2 * t;
      case 5: return SH_PATTERN[((cycle % SH_PATTERN.length) + SH_PATTERN.length) % SH_PATTERN.length];
      default: return Math.sin(2 * Math.PI * t);
    }
  }
  function lfoPath(wave, offset, w, h) {
    const N = 200, pad = 6, pts = [];
    for (let i = 0; i <= N; i++) {
      const x = i / N, t = x * 2 + offset, y = lfoShape(wave, t, Math.floor(t));
      pts.push(`${(x * w).toFixed(1)},${(h / 2 - y * (h / 2 - pad)).toFixed(1)}`);
    }
    return 'M' + pts.join(' L');
  }
  function lfoRateHz(g) {
    if (values[`${g}Sync`] >= 0.5) return (tempo / 60) / LFO_DIV_BEATS[values[`${g}Div`] | 0];
    return values[`${g}Rate`];
  }

  const modPage = $('#modpage-row');
  const lfoUi = [];
  for (let n = 1; n <= 2; n++) {
    const g = `lfo${n}`;
    const panel = makePanel(`LFO ${n}`, control(ToggleButton, `${g}On`).el);
    panel.classList.add('lfo-panel');
    const scr = makeScreen(panel);
    const head = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    head.setAttribute('class', 'scr-head');
    scr.root.querySelector('svg').appendChild(head);
    const row = el('div', 'knob-row');
    addKnob(row, `${g}Wave`);
    const rateSlot = el('div', 'rate-slot');
    addKnob(rateSlot, `${g}Rate`);
    addKnob(rateSlot, `${g}Div`);
    row.appendChild(rateSlot);
    addKnob(row, `${g}Amount`);
    addKnob(row, `${g}Phase`);
    panel.appendChild(row);
    const sw = el('div', 'switch-row');
    sw.appendChild(control(ToggleButton, `${g}Sync`, 'SYNC').el);
    sw.appendChild(control(Segmented, `${g}Mode`).el);
    sw.appendChild(control(Segmented, `${g}Trig`).el);
    panel.appendChild(sw);
    modPage.appendChild(panel);
    lfoUi.push({ g, panel, scr, head, rateSlot, phase: 0 });
  }

  function refreshLfos() {
    for (const u of lfoUi) {
      const g = u.g, on = values[`${g}On`] >= 0.5, sync = values[`${g}Sync`] >= 0.5;
      const wave = values[`${g}Wave`] | 0;
      u.rateSlot.children[0].style.display = sync ? 'none' : '';
      u.rateSlot.children[1].style.display = sync ? '' : 'none';
      u.scr.path.setAttribute('d', lfoPath(wave, values[`${g}Phase`] / 360, 200, 48));
      const rate = sync ? LFO_DIVS[values[`${g}Div`] | 0] : formatParam(PARAM_BY_ID[`${g}Rate`], values[`${g}Rate`]);
      const mode = values[`${g}Mode`] >= 0.5 ? 'VOICE' : 'GLOBAL';
      u.scr.text.textContent = on ? `${LFO_WAVES[wave].toUpperCase()}  ${rate}  ${mode}${values[`${g}Trig`] >= 0.5 ? ' · NOTE' : ''}` : 'OFF';
      u.panel.classList.toggle('off', !on);
    }
  }

  // live playhead (visual only; the audio LFOs run in the engine)
  let lastFrame = performance.now();
  function animateLfos(now) {
    const dt = Math.min(0.1, (now - lastFrame) / 1000);
    lastFrame = now;
    if (activePage === 'mod') {
      for (const u of lfoUi) {
        if (values[`${u.g}On`] < 0.5) { u.head.setAttribute('d', ''); continue; }
        u.phase = (u.phase + lfoRateHz(u.g) * dt) % 1;
        const x = (u.phase * 100).toFixed(1);
        u.head.setAttribute('d', `M${x} 0 V48`);
      }
    }
    requestAnimationFrame(animateLfos);
  }
  requestAnimationFrame(animateLfos);

  // ---- mod matrix ------------------------------------------------------------
  const addBtn = el('button', 'pd-btn mx-add', '+ ADD');
  addBtn.type = 'button';
  addBtn.title = 'Add a modulation route';
  const matrix = makePanel('MOD MATRIX', addBtn);
  matrix.classList.add('matrix-panel');
  matrix.appendChild(el('div', 'mx-head',
    '<span></span><span>SOURCE</span><span>× VIA</span><span>DESTINATION</span><span>AMOUNT</span><span></span>'));
  const mxRows = el('div', 'mx-rows');
  const mxEmpty = el('div', 'mx-empty',
    'No routes yet. Click <b>+ ADD</b>. Try <i>LFO 1 → Cutoff</i> for wobble, <i>LFO 1 × Mod Wheel → Pitch (All)</i> for vibrato on the wheel, or <i>Velocity → Cutoff</i> for touch.');
  const NONE_SRC = ['—', ...MOD_SOURCES.slice(1)], NONE_DST = ['—', ...MOD_DESTS.slice(1)];
  const shownSlots = new Set();
  const mxUi = [];
  for (let s = 1; s <= NUM_MOD_SLOTS; s++) {
    const r = el('div', 'mx-row');
    const onBtn = control(ToggleButton, `mod${s}On`, '').el;
    onBtn.classList.add('mx-on');
    onBtn.title = 'Enable / bypass this route';
    r.appendChild(onBtn);
    r.appendChild(control(SelectBox, `mod${s}Src`, NONE_SRC).el);
    r.appendChild(control(SelectBox, `mod${s}Via`, NONE_SRC).el);
    r.appendChild(control(SelectBox, `mod${s}Dst`, NONE_DST).el);
    const amt = el('div', 'mx-amt');
    addKnob(amt, `mod${s}Amt`);
    knobs[`mod${s}Amt`].el.classList.add('knob-sm');
    const amtText = el('span', 'mx-amt-text');
    amt.appendChild(amtText);
    r.appendChild(amt);
    const rm = el('button', 'mx-del', '×');
    rm.type = 'button';
    rm.title = 'Remove route';
    rm.addEventListener('click', () => removeRoute(s));
    r.appendChild(rm);
    mxRows.appendChild(r);
    mxUi.push({ s, r, amtText });
  }
  matrix.appendChild(mxRows);
  matrix.appendChild(mxEmpty);
  modPage.appendChild(matrix);

  const slotUsed = (s) => values[`mod${s}Src`] !== 0 || values[`mod${s}Dst`] !== 0 || shownSlots.has(s);
  function addRoute() {
    const free = mxUi.find(u => !slotUsed(u.s));
    if (!free) { logMidi('All 8 modulation routes are in use'); return; }
    const s = free.s;
    shownSlots.add(s);
    setParam(`mod${s}On`, 1);
    setParam(`mod${s}Src`, 1);
    setParam(`mod${s}Via`, 0);
    setParam(`mod${s}Dst`, 1);
    setParam(`mod${s}Amt`, 0.25);
  }
  function removeRoute(s) {
    shownSlots.delete(s);
    setParam(`mod${s}Src`, 0);
    setParam(`mod${s}Via`, 0);
    setParam(`mod${s}Dst`, 0);
    setParam(`mod${s}Amt`, 0);
    setParam(`mod${s}On`, 1);
  }
  addBtn.addEventListener('click', addRoute);

  // amount in the destination's own units
  function amountText(dst, amt) {
    const sign = amt > 0.0005 ? '+' : amt < -0.0005 ? '−' : '';
    const a = Math.abs(amt);
    if (dst === 1) return `${sign}${(a * 5).toFixed(2)} oct`;
    if (dst >= 3 && dst <= 6) {
      const st = a * 12;
      return st < 1 ? `${sign}${Math.round(st * 100)} ct` : `${sign}${st.toFixed(2)} st`;
    }
    return `${sign}${Math.round(a * 100)}%`;
  }
  function refreshMatrix() {
    let any = false;
    for (const u of mxUi) {
      const used = slotUsed(u.s);
      any = any || used;
      u.r.style.display = used ? '' : 'none';
      u.r.classList.toggle('off', values[`mod${u.s}On`] < 0.5);
      u.amtText.textContent = amountText(values[`mod${u.s}Dst`] | 0, values[`mod${u.s}Amt`]);
    }
    mxEmpty.style.display = any ? 'none' : '';
    addBtn.disabled = mxUi.every(u => slotUsed(u.s));
  }

  // ==================================================== DRIVE · VOICE page ===
  const fxPage = $('#fxpage-row');
  const drivePanel = makePanel('DRIVE', control(ToggleButton, 'drvOn').el);
  drivePanel.classList.add('drive-panel');
  screens.drive = makeScreen(drivePanel);
  {
    const typeRow = el('div', 'switch-row');
    typeRow.appendChild(control(Segmented, 'drvType').el);
    drivePanel.appendChild(typeRow);
    const row = el('div', 'knob-row');
    ['drvDrive', 'drvTone', 'drvMix', 'drvOut'].forEach(id => addKnob(row, id));
    drivePanel.appendChild(row);
  }
  fxPage.appendChild(drivePanel);

  const voicePanel = makePanel('VOICE');
  voicePanel.classList.add('voice-panel');
  screens.voice = makeScreen(voicePanel);
  {
    const r1 = el('div', 'labeled-row', '<span class="ctl-label">MODE</span>');
    r1.appendChild(control(Segmented, 'voiceMode').el);
    const r2 = el('div', 'labeled-row', '<span class="ctl-label">GLIDE</span>');
    r2.appendChild(control(ToggleButton, 'glideOn').el);
    r2.appendChild(control(Segmented, 'glideMode').el);
    const row = el('div', 'knob-row');
    addKnob(row, 'glideTime');
    voicePanel.append(r1, r2, row);
  }
  fxPage.appendChild(voicePanel);

  function driveCurve(type, drive, mix, outDb) {
    const g = Math.pow(10, drive * 36 / 20), out = Math.pow(10, outDb / 20), N = 120, pts = [];
    const q = Math.pow(2, (16 - 14 * drive) - 1);
    for (let i = 0; i <= N; i++) {
      const x = -1 + 2 * i / N;
      let y;
      if (type === 1) y = clamp(x * g, -1, 1);
      else if (type === 2) { let t = (x * g + 1) * 0.25; t -= Math.floor(t); y = 1 - 4 * Math.abs(t - 0.5); }
      else if (type === 3) y = Math.floor(clamp(x * (1 + 3 * drive), -1, 1) * q + 0.5) / q;
      else y = Math.tanh(x * g);
      y = clamp((x + (y - x) * mix) * out, -1.15, 1.15);
      pts.push(`${(i / N * 200).toFixed(1)},${(24 - y * 19).toFixed(1)}`);
    }
    return 'M' + pts.join(' L');
  }
  function refreshDrive() {
    const on = values.drvOn >= 0.5, type = values.drvType | 0;
    screens.drive.path.setAttribute('d', driveCurve(type, values.drvDrive, values.drvMix, values.drvOut));
    screens.drive.text.textContent = on ? `${DRIVE_TYPES[type].toUpperCase()}  +${Math.round(values.drvDrive * 36)} dB` : 'OFF';
    drivePanel.classList.toggle('off', !on);
  }
  function refreshVoice() {
    const mode = values.voiceMode | 0, glide = values.glideOn >= 0.5;
    const t = values.glideTime;
    const span = glide ? 12 + 150 * Math.log10(1 + t * 100) / Math.log10(501) : 0;
    const x0 = 24;
    screens.voice.path.setAttribute('d', `M0,36 L${x0},36 L${(x0 + Math.max(span, 0.5)).toFixed(1)},12 L200,12`);
    screens.voice.text.textContent = `${VOICE_MODES[mode].toUpperCase()} · GLIDE ${glide ? formatParam(PARAM_BY_ID.glideTime, t) + (values.glideMode >= 0.5 ? ' LEGATO' : '') : 'OFF'}`;
  }

  // ========================================================== page tabs ===
  const pageTabs = Array.from(document.querySelectorAll('.page-tab'));
  const pageEls = Array.from(document.querySelectorAll('.page'));
  const PAGE_HINTS = {
    synth: 'Oscillators · amp envelope · filter · output',
    mod: '× Via scales a route by a second source, e.g. Mod Wheel',
    fx: 'Drive runs per voice after the filter · Mono / Legato use the last note held',
  };
  let activePage = 'synth';
  try { activePage = localStorage.getItem('epiano.page') || 'synth'; } catch (e) { /* storage blocked */ }
  function showPage(p) {
    if (!PAGE_HINTS[p]) p = 'synth';
    activePage = p;
    pageEls.forEach(pg => pg.classList.toggle('active', pg.dataset.page === p));
    pageTabs.forEach(t => { t.classList.toggle('active', t.dataset.page === p); t.setAttribute('aria-selected', t.dataset.page === p); });
    $('#page-hint').textContent = PAGE_HINTS[p];
    try { localStorage.setItem('epiano.page', p); } catch (e) { /* storage blocked */ }
  }
  pageTabs.forEach(t => t.addEventListener('click', () => showPage(t.dataset.page)));

  function refreshPageLeds() {
    const lfo = values.lfo1On >= 0.5 || values.lfo2On >= 0.5;
    const route = mxUi.some(u => values[`mod${u.s}On`] >= 0.5 && values[`mod${u.s}Src`] && values[`mod${u.s}Dst`] && values[`mod${u.s}Amt`]);
    $('#mod-led').classList.toggle('on', lfo || route);
    $('#fx-led').classList.toggle('on', values.drvOn >= 0.5 || (values.voiceMode | 0) !== 0 || values.glideOn >= 0.5);
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
  engine.on('stats', ({ voices, peak, sustain, modWheel, bpm }) => {
    $('#voice-count').textContent = voices;
    sustainLed.classList.toggle('on', !!sustain);
    if (typeof modWheel === 'number') wheel.setValue(modWheel);
    if (typeof bpm === 'number' && bpm > 0) {
      tempo = bpm;
      if (document.activeElement !== bpmInput) bpmInput.value = String(Math.round(bpm * 10) / 10);
    }
    const db = 20 * Math.log10(Math.max(peak, 1e-5));
    meterFill.style.width = `${clamp((db + 60) / 60, 0, 1) * 100}%`;
    if (peak > 0.95) {
      meterClip.classList.add('on');
      clearTimeout(clipTimer);
      clipTimer = setTimeout(() => meterClip.classList.remove('on'), 800);
    }
  });

  let connectedOnce = false;
  engine.on('config', ({ sampleRate, blockSize, wrapper, audio, running }) => {
    const web = wrapper === 'Web';
    document.body.classList.toggle('web', web);
    bpmInput.readOnly = !web;
    bpmInput.title = web ? 'Tempo for synced LFOs' : 'Host tempo (synced LFOs follow your DAW)';
    const sr = sampleRate > 0 ? `${(sampleRate / 1000).toFixed(1)} kHz · ${blockSize} smp` : 'not playing yet';
    $('#audio-info').textContent = audio || `${wrapper} · ${sr}`;
    $('#audio-led').classList.toggle('on', running !== false);
    if (!connectedOnce) logMidi('Connected — play your MIDI controller, the keys, or A–\' on the keyboard');
    connectedOnce = true;
    fit();
  });

  // ---- mod wheel, tempo, MIDI input (web build) --------------------------------
  const wheel = new ModWheel((v) => engine.modWheel(v));
  $('#wheel-slot').replaceWith(wheel.el);

  const bpmInput = $('#bpm-input');
  bpmInput.addEventListener('change', () => {
    const v = clamp(Number(bpmInput.value) || 120, 20, 300);
    bpmInput.value = String(v);
    tempo = v;
    engine.setTempo(v);
  });

  const midiSelect = $('#midi-select');
  engine.on('midiInputs', ({ inputs, selected, error }) => {
    midiSelect.innerHTML = '';
    if (error) { midiSelect.appendChild(new Option('MIDI unavailable', 'all')); midiSelect.disabled = true; return; }
    midiSelect.disabled = false;
    midiSelect.appendChild(new Option(inputs.length ? `All inputs (${inputs.length})` : 'No MIDI devices', 'all'));
    inputs.forEach(d => midiSelect.appendChild(new Option(d.name + (d.state === 'connected' ? '' : ' (off)'), d.id)));
    midiSelect.value = Array.from(midiSelect.options).some(o => o.value === selected) ? selected : 'all';
  });
  midiSelect.addEventListener('change', () => engine.selectMidi(midiSelect.value));

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
  showPage(activePage);
  refreshScreens();
  setOctave(48);
  fit();
  if (engine.connected) engine.uiReady();
  else logMidi('Not running inside the plugin — no audio engine connected (UI preview only)');
})();
