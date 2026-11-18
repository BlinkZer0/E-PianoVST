// ============================================================================
//  Parameter definitions. Must match vst/Source/Params.h exactly (IDs, ranges,
//  defaults, choice lists, order). The web build (web/index.html) is generated
//  from this file. New parameters are only appended, with neutral defaults.
// ============================================================================
'use strict';

const WAVE_NAMES = ['Sine', 'Saw', 'Square', 'Triangle'];
const LFO_WAVES = ['Sine', 'Triangle', 'Square', 'Saw Up', 'Saw Down', 'S&H'];
const LFO_DIVS = ['1/1', '1/2D', '1/2', '1/2T', '1/4D', '1/4', '1/4T', '1/8D', '1/8', '1/8T',
                  '1/16D', '1/16', '1/16T', '1/32', '1/32T'];
const LFO_MODES = ['Global', 'Voice'];
const LFO_TRIGS = ['Free', 'Note'];
const MOD_SOURCES = ['None', 'LFO 1', 'LFO 2', 'Amp Env', 'Filter Env', 'Velocity', 'Mod Wheel'];
const MOD_DESTS = ['None', 'Cutoff', 'Resonance', 'Pitch (All)', 'Osc 1 Pitch', 'Osc 2 Pitch', 'Osc 3 Pitch',
                   'Osc 1 Level', 'Osc 2 Level', 'Osc 3 Level', 'Amplitude', 'Pan'];
const DRIVE_TYPES = ['Soft', 'Hard', 'Fold', 'Crush'];
const VOICE_MODES = ['Poly', 'Mono', 'Legato'];
const GLIDE_MODES = ['Always', 'Legato'];
const NUM_VOICES = 88;
const NUM_MOD_SLOTS = 8;

const choiceParam = (id, name, group, choices, def) =>
  ({ id, name, group, type: 'choice', min: 0, max: choices.length - 1, step: 1, def, choices });
const boolParam = (id, name, group, def) =>
  ({ id, name, group, type: 'bool', min: 0, max: 1, step: 1, def });

function oscParams(n) {
  return [
    boolParam(`osc${n}On`, 'On', `osc${n}`, n === 1 ? 1 : 0),
    choiceParam(`osc${n}Wave`, 'Wave', `osc${n}`, WAVE_NAMES, 0),
    { id: `osc${n}Pitch`, name: 'Pitch', group: `osc${n}`, min: -36,  max: 36,  step: 1, def: 0, unit: 'st', bipolar: true },
    { id: `osc${n}Fine`,  name: 'Fine',  group: `osc${n}`, min: -100, max: 100, step: 1, def: 0, unit: 'ct', bipolar: true },
    { id: `osc${n}Level`, name: 'Level', group: `osc${n}`, min: 0,    max: 1,   def: 0.7, unit: '%' },
  ];
}

function lfoParams(n) {
  const g = `lfo${n}`;
  return [
    boolParam(`${g}On`, 'On', g, 0),
    choiceParam(`${g}Wave`, 'Wave', g, LFO_WAVES, 0),
    { id: `${g}Rate`, name: 'Rate', group: g, min: 0.02, max: 40, def: 2, unit: 'lfohz', skew: 'log' },
    boolParam(`${g}Sync`, 'Sync', g, 0),
    choiceParam(`${g}Div`, 'Rate', g, LFO_DIVS, 8),
    choiceParam(`${g}Mode`, 'Mode', g, LFO_MODES, 0),
    choiceParam(`${g}Trig`, 'Trigger', g, LFO_TRIGS, 0),
    { id: `${g}Phase`, name: 'Phase', group: g, min: 0, max: 360, def: 0, unit: 'deg' },
    { id: `${g}Amount`, name: 'Amount', group: g, min: 0, max: 1, def: 1, unit: '%' },
  ];
}

function modSlotParams(n) {
  const g = 'matrix';
  return [
    boolParam(`mod${n}On`, 'On', g, 1),
    choiceParam(`mod${n}Src`, 'Source', g, MOD_SOURCES, 0),
    choiceParam(`mod${n}Via`, 'Via', g, MOD_SOURCES, 0),
    choiceParam(`mod${n}Dst`, 'Destination', g, MOD_DESTS, 0),
    { id: `mod${n}Amt`, name: 'Amount', group: g, min: -1, max: 1, def: 0, unit: 'bip', bipolar: true },
  ];
}

const PARAM_DEFS = [
  ...oscParams(1),
  ...oscParams(2),
  ...oscParams(3),
  { id: 'ampAttack',  name: 'Attack',  group: 'amp',    min: 0.001, max: 5,     def: 0.005, unit: 's', skew: 'log' },
  { id: 'ampDecay',   name: 'Decay',   group: 'amp',    min: 0.005, max: 10,    def: 1.0,   unit: 's', skew: 'log' },
  { id: 'ampSustain', name: 'Sustain', group: 'amp',    min: 0,     max: 1,     def: 0.7,   unit: '%' },
  { id: 'ampRelease', name: 'Release', group: 'amp',    min: 0.005, max: 10,    def: 0.3,   unit: 's', skew: 'log' },
  { id: 'ampLevel',   name: 'Amp',     group: 'amp',    min: -30,   max: 6,     def: 0,     unit: 'dB' },
  { id: 'fltCutoff',  name: 'Cutoff',  group: 'filter', min: 20,    max: 20000, def: 8000,  unit: 'Hz', skew: 'log' },
  { id: 'fltReso',    name: 'Reso',    group: 'filter', min: 0,     max: 1,     def: 0.1,   unit: '%' },
  { id: 'fltEnvAmt',  name: 'Env Amt', group: 'filter', min: 0,     max: 1,     def: 0.2,   unit: '%' },
  { id: 'fltDecay',   name: 'Env Dec', group: 'filter', min: 0.005, max: 5,     def: 0.4,   unit: 's', skew: 'log' },
  { id: 'master',     name: 'Master',  group: 'output', min: 0,     max: 1,     def: 0.7,   unit: '%', noPreset: true },
  ...lfoParams(1),
  ...lfoParams(2),
  ...Array.from({ length: NUM_MOD_SLOTS }, (_, i) => modSlotParams(i + 1)).flat(),
  boolParam('drvOn', 'On', 'drive', 0),
  choiceParam('drvType', 'Type', 'drive', DRIVE_TYPES, 0),
  { id: 'drvDrive', name: 'Drive',  group: 'drive', min: 0,   max: 1, def: 0.3, unit: '%' },
  { id: 'drvTone',  name: 'Tone',   group: 'drive', min: 0,   max: 1, def: 1,   unit: '%' },
  { id: 'drvMix',   name: 'Mix',    group: 'drive', min: 0,   max: 1, def: 1,   unit: '%' },
  { id: 'drvOut',   name: 'Output', group: 'drive', min: -24, max: 6, def: 0,   unit: 'dB' },
  choiceParam('voiceMode', 'Mode', 'voice', VOICE_MODES, 0),
  boolParam('glideOn', 'Glide', 'voice', 0),
  { id: 'glideTime', name: 'Time', group: 'voice', min: 0.001, max: 5, def: 0.1, unit: 's', skew: 'log' },
  choiceParam('glideMode', 'Glide Mode', 'voice', GLIDE_MODES, 0),
];

const PARAM_BY_ID = Object.fromEntries(PARAM_DEFS.map(d => [d.id, d]));

function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

function defaultParamValues() {
  return Object.fromEntries(PARAM_DEFS.map(d => [d.id, d.def]));
}

// value <-> 0..1 normalized (same mapping the VST host will see)
function paramToNorm(d, v) {
  if (d.skew === 'log') return clamp(Math.log(v / d.min) / Math.log(d.max / d.min), 0, 1);
  return clamp((v - d.min) / (d.max - d.min), 0, 1);
}

function paramFromNorm(d, n) {
  n = clamp(n, 0, 1);
  let v = d.skew === 'log' ? d.min * Math.pow(d.max / d.min, n) : d.min + n * (d.max - d.min);
  if (d.step) v = Math.round(v / d.step) * d.step;
  return clamp(v, d.min, d.max);
}

function formatParam(d, v) {
  if (d.type === 'choice') return d.choices[clamp(Math.round(v), 0, d.choices.length - 1)];
  if (d.type === 'bool') return v >= 0.5 ? 'On' : 'Off';
  switch (d.unit) {
    case 's':     return v < 1 ? `${Math.round(v * 1000)} ms` : `${v.toFixed(2)} s`;
    case 'Hz':    return v >= 1000 ? `${(v / 1000).toFixed(v >= 10000 ? 1 : 2)} kHz` : `${Math.round(v)} Hz`;
    case 'lfohz': return `${v.toFixed(v < 1 ? 3 : v < 10 ? 2 : 1)} Hz`;
    case 'st':    return `${v > 0 ? '+' : ''}${v} st`;
    case 'ct':    return `${v > 0 ? '+' : ''}${v} ct`;
    case '%':     return `${Math.round(v * 100)}%`;
    case 'bip':   return `${v > 0.004 ? '+' : ''}${Math.round(v * 100)}%`;
    case 'dB':    return `${v > 0.05 ? '+' : ''}${v.toFixed(1)} dB`;
    case 'deg':   return `${Math.round(v)}°`;
    default:      return String(v);
  }
}
