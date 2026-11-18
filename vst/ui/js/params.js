// ============================================================================
//  Parameter definitions (plugin copy). Must match vst/Source/Params.h.
//  The web build (web/index.html) carries the same table.
// ============================================================================
'use strict';

const WAVE_NAMES = ['Sine', 'Saw', 'Square', 'Triangle'];
const NUM_VOICES = 88;

function oscParams(n) {
  return [
    { id: `osc${n}On`,    name: 'On',    group: `osc${n}`, type: 'bool',   min: 0,   max: 1,   step: 1, def: n === 1 ? 1 : 0 },
    { id: `osc${n}Wave`,  name: 'Wave',  group: `osc${n}`, type: 'choice', min: 0,   max: 3,   step: 1, def: 0, choices: WAVE_NAMES },
    { id: `osc${n}Pitch`, name: 'Pitch', group: `osc${n}`, min: -36,  max: 36,  step: 1, def: 0, unit: 'st', bipolar: true },
    { id: `osc${n}Fine`,  name: 'Fine',  group: `osc${n}`, min: -100, max: 100, step: 1, def: 0, unit: 'ct', bipolar: true },
    { id: `osc${n}Level`, name: 'Level', group: `osc${n}`, min: 0,    max: 1,   def: 0.7, unit: '%' },
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
  if (d.type === 'choice') return d.choices[v | 0];
  if (d.type === 'bool') return v >= 0.5 ? 'On' : 'Off';
  switch (d.unit) {
    case 's':  return v < 1 ? `${Math.round(v * 1000)} ms` : `${v.toFixed(2)} s`;
    case 'Hz': return v >= 1000 ? `${(v / 1000).toFixed(v >= 10000 ? 1 : 2)} kHz` : `${Math.round(v)} Hz`;
    case 'st': return `${v > 0 ? '+' : ''}${v} st`;
    case 'ct': return `${v > 0 ? '+' : ''}${v} ct`;
    case '%':  return `${Math.round(v * 100)}%`;
    case 'dB': return `${v > 0.05 ? '+' : ''}${v.toFixed(1)} dB`;
    default:   return String(v);
  }
}
