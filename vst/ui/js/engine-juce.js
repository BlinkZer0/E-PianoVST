// ============================================================================
//  JUCE bridge — the plugin's replacement for the web version's WebAudioEngine.
//  Talks to the C++ processor through JUCE 8's native integration
//  (window.__JUCE__.backend), which JUCE injects into the page.
// ============================================================================
'use strict';

class JuceEngine {
  constructor() {
    this.backend = (window.__JUCE__ && window.__JUCE__.backend) || null;
    this.connected = !!this.backend;
    this.ready = true;
    this.handlers = {};
    if (this.backend) {
      for (const ev of ['params', 'notes', 'stats', 'config', 'presetList', 'preset', 'saveResult', 'message', 'midiInputs']) {
        this.backend.addEventListener(ev, (data) => { if (this.handlers[ev]) this.handlers[ev](data); });
      }
    }
  }

  on(event, fn) { this.handlers[event] = fn; }
  emit(id, payload) { if (this.backend) this.backend.emitEvent(id, payload || {}); }

  start() { return Promise.resolve(); }
  uiReady() { this.emit('uiReady'); }

  // parameters
  setParam(id, value) { this.emit('setParam', { id, value }); }
  beginGesture(id) { this.emit('gesture', { id, begin: true }); }
  endGesture(id) { this.emit('gesture', { id, begin: false }); }

  // notes
  noteOn(note, vel) { this.emit('noteOn', { note, vel }); }
  noteOff(note) { this.emit('noteOff', { note }); }
  panic() { this.emit('panic'); }
  modWheel(value) { this.emit('modWheel', { value }); }

  // web build only (the plugin takes tempo + MIDI from the host)
  setTempo(bpm) { this.emit('setTempo', { bpm }); }
  selectMidi(id) { this.emit('selectMidi', { id }); }

  // presets
  loadPreset(id) { this.emit('loadPreset', { id }); }
  savePreset(name, overwrite) { this.emit('savePreset', { name, overwrite: !!overwrite }); }
  deletePreset(id) { this.emit('deletePreset', { id }); }
  loadPresetFile() { this.emit('loadPresetFile'); }
  openPresetFolder() { this.emit('openPresetFolder'); }
}
