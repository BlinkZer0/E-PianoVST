# E-Piano — VST3 plugin (JUCE 8)

This folder is the source of truth for E-Piano:
- **C++ DSP:** the plugin's sound engine.
- **HTML/SVG UI:** shown inside the plugin through WebView2.
- **Web host:** lets the same UI run in a browser. The single-file web build (`../web/index.html`) is generated from here.

## Build

```
build.bat            VST3 + Standalone (Release)
build.bat Debug
install.bat          copies E-Piano.vst3 to C:\Program Files\Common Files\VST3 (asks for admin)
powershell -ExecutionPolicy Bypass -File webhost\build-web.ps1    regenerate ..\web\index.html
```

Requirements: Visual Studio 2022 or 2026 with the C++ workload, Git, and the WebView2 runtime (built into Windows 11). On first build, CMake downloads JUCE 8.0.17. Build files go to `%LOCALAPPDATA%\EPianoVST`, so OneDrive doesn't sync them:

- `...\build\EPianoVST_artefacts\Release\VST3\E-Piano.vst3`
- `...\build\EPianoVST_artefacts\Release\Standalone\E-Piano.exe` is the quickest way to test without a DAW. Use *Options → Audio/MIDI Settings* to pick the audio device and MIDI input.

To open the project in Visual Studio, open `%LOCALAPPDATA%\EPianoVST\build\EPianoVST.slnx` after one build.

## Signal flow

```
per voice:  OSC 1-3 → SVF low-pass → amp env × velocity × Amplitude mod → DRIVE → pan
global:     Σ voices → Amp level × Master (smoothed) → soft clip → out (stereo)
```

Modulation runs at a 32-sample control rate. Each target ramps linearly per sample in between: pitch, oscillator levels, filter g/k coefficients, amplitude and pan. Nothing steps or clicks, and per-sample cost stays low across 88 voices.

## In the DAW

- Loads as an instrument (Instrument|Synth) with stereo out and MIDI in.
- **96 automatable parameters.** Knob drags record as single automation gestures.
- **Right-click any control** for the host's parameter menu: FL Studio's *Create automation clip* / *Link to controller*, Cubase's automation menu, and so on. The editor asks for it with `getHostContext()->getContextMenuForParameter()`. Hosts without one (e.g. the Standalone app) get a small fallback menu with *Reset to default*. Shift+right-click still opens DevTools.
- **Programs:** the 120 factory presets appear in the DAW's preset/program menu.
- **MIDI:** note on/off with velocity, sustain (CC64), **mod wheel (CC1)**, pitch bend (±2 st), all notes off/all sound off. Events are applied at the exact sample they arrive.
- **Host tempo** drives synced LFOs. While the transport plays, a *Global + Free + Sync* LFO locks to the song position, so a wobble lands on the beat every time.
- **State:** the project saves every setting plus the selected preset.

## Features

| Section | Controls |
|---|---|
| **LFO 1 / LFO 2** | On · Wave (Sine, Triangle, Square, Saw Up, Saw Down, S&H) · Rate 0.02–40 Hz, or **Sync** to 1/1 … 1/32 (dotted and triplet included) · Amount · Phase · **Mode** Global / Voice · **Trigger** Free / Note |
| **Mod matrix** | 8 routes, each with On · Source · **× Via** · Destination · Amount (±, shown in the destination's units) |
| **Drive** | On · Type (Soft, Hard, Fold, Crush) · Drive (0–36 dB) · Tone · Mix · Output |
| **Voice** | Mode (Poly, Mono, Legato) · Glide on/off · Glide time · Glide mode (Always, Legato only) |
| **Samples** | Per oscillator: Sample mode (Waveform, One-shot, Loop, Cycle) · LOAD WAV / drag-and-drop · clear |

**Sample details**
- **Formats:** WAV, AIFF, FLAC and OGG are decoded by JUCE.
- **Processing:** mixed to mono, peak-normalised, capped at 10 s. Root = the WAV `smpl` chunk's unity note, otherwise C4.
- **Playback:** 4-point Hermite interpolation. *One-shot* restarts on every trigger, *Loop* repeats the whole file, and *Cycle* treats the file as one waveform cycle.
- **Fallback:** sample mode with no sample loaded plays the waveform.
- **Audio-thread handoff:** the message thread posts a new buffer through an atomic pointer. The audio thread swaps it in at the start of a block and parks the old one, which the message thread then frees. No locks or deallocation happen on the audio thread.
- **Storage:** samples are saved in project state and `.epreset` files as `"samples": { "1": { name, rate, root, pcm16 } }`, where `pcm16` is standard base64 of 16-bit little-endian PCM. The web build reads and writes the same format.

**Mod matrix details**
- **Sources:** LFO 1, LFO 2, Amp Env, Filter Env, Velocity, Mod Wheel.
- **Destinations:** Cutoff (±5 octaves in log frequency), Resonance, Pitch (All), Osc 1/2/3 Pitch (±12 st), Osc 1/2/3 Level, Amplitude, Pan.
- **Combining:** several routes can hit the same destination; they add up.
- **Via:** multiplies a route by a second source. *LFO 1 × Mod Wheel → Cutoff* means the wheel sets how deep the wobble is.

**LFO details**
- **Global** LFOs share one phase across all voices (wobble bass).
- **Voice** LFOs run separately per voice. In Free mode each voice starts at a random phase; in Note mode the phase resets on every note.
- Square and S&H outputs get about 2 ms of smoothing so they don't click.

**Drive details**
- Soft and Hard use first-order anti-derivative anti-aliasing (ADAA).
- Drive sits after the amp envelope, so harder playing breaks up more, like an amp.

**Voice details**
- Mono and Legato use a last-note-priority key stack. Releasing a key returns to the previous held note, and the sustain pedal is respected.
- Legato doesn't retrigger the envelopes.

**Backward compatibility**
- Every parameter added since v0.9 defaults to neutral: LFOs off, routes empty, drive off, Poly, glide off. Pan's centre position is unity in both channels.
- So v0.9 presets, projects and `.epreset` files load and sound as before. The parity tests below include v0.9 presets to keep it that way.

## Presets

- **Factory:** 120 presets in 12 categories, built in from [presets/factory_presets.json](presets/factory_presets.json):
  - Keys, Pads, Brass, Organ, Bass, Bells, Strings, Leads (10 each, unchanged since v0.9)
  - **Dubstep**: Wub Machine, Triplet Wobble, Half-Time Womp, Growl Yoi, Stutter Wobble, Mod Wheel Wobble, Laser Wob, Riddim Bass, Talking Bass, Neuro Reese Wobble. All are tempo-synced, mono-legato with glide, and most put extra wobble or resonance on the mod wheel
  - **Dubstep 2**: Brostep Screech, Sub Drop Wobble, Vowel Wub, Ripper Bass, Tearout Growl, Slow Womp 70, Wobble Chords (poly), Gated Wob, Metallic Wob, Deep Space Wob
  - **Jazz**: Smoky Rhodes, Jazz Organ B3, Vibes Tremolo, Walking Upright, Brushed Pad, Muted Trumpet, Flute Solo, Jazz Guitar, Wurly Comp, Late Night Keys. Velocity opens the filter; tremolo and Leslie-style motion come from the LFOs; the trumpet and flute put vibrato on the mod wheel
  - **Motion**: Classic Wobble Bass, Deep Dubstep Bass, Reese Bass, Rhythmic Bass, Vibrato Lead, Tremolo Pad, Modulated Synth Pad, Distorted Bass, S&H Bleeps, Auto-Pan Crunch Keys

  Their Amp levels were set by rendering each one (`EPianoTests --loudness`). Many Motion presets put extra depth on the mod wheel.
- **User folder:** `Documents\E-Piano\Presets`, shared by every DAW and every instance.
  - Scanned on open and every 2 seconds, so new, renamed and deleted files show up on their own.
  - Subfolders become category tabs.
  - **SAVE** writes `<name>.epreset` and asks before replacing a file. **LOAD** opens any `.epreset`. **FOLDER** opens the folder in Explorer. The **×** on a user preset moves it to the Recycle Bin.
- File format (plain JSON). Any parameter a file leaves out loads at its default:
  ```json
  { "format": "E-Piano Preset", "version": 1, "name": "My Sound", "category": "User",
    "params": { "osc1Wave": 1, "fltCutoff": 300, "lfo1On": 1, "mod1Src": 1, "mod1Dst": 1, "mod1Amt": 0.45 } }
  ```

## Tests

Build the test tool, then run it from `%LOCALAPPDATA%\EPianoVST\build\EPianoTests_artefacts\Release\`:

```
cmake --build %LOCALAPPDATA%\EPianoVST\build --config Release --target EPianoTests
```

| Command | Checks |
|---|---|
| `EPianoTests.exe` | 120 factory presets, ranges, v0.9 presets carry no new settings, neutral defaults, save → load round trip, old / partial / bad files, samples embedded in presets, WAV decoding with root note |
| `powershell -File tests\run-parity.ps1` | **C++ vs JS DSP parity**: renders the 23 scenarios in [tests/parity_scenarios.json](tests/parity_scenarios.json) (v0.9 presets, Dubstep, Dubstep 2 and Jazz presets, sample One-shot / Loop / Cycle, every LFO mode, matrix sources and Via, all drive types, mono/legato/glide, sustain, an 88-voice flood) in both engines and compares RMS per window. Currently agree to within 5×10⁻⁵ |
| `EPianoTests.exe --bench` | worst case, 88 voices with both LFOs per voice + 4 routes + drive: about 13% of one core (v0.9 patch: about 5%) |
| `EPianoTests.exe --loudness` | loudness of every factory preset (level matching) |

## Layout

```
CMakeLists.txt
Source/
  Params.h                   parameter table (96 params; mirrors ui/js/params.js)
  SampleData.*               sample decoding (WAV/AIFF/FLAC/OGG), preset/state serialisation
  SynthEngine.h              DSP: oscillators, ADSR, SVF, LFOs, mod matrix, drive, voice modes, glide (no JUCE)
  EngineParamsFromValues.h   parameter values -> engine snapshot (plugin + tests)
  PresetManager.*            factory presets, user folder scan, .epreset read/write
  PluginProcessor.*          APVTS, MIDI, host tempo/transport, programs, state
  PluginEditor.*             WebView2 host + JS <-> C++ bridge
ui/                          the UI (HTML/CSS/SVG/JS), embedded in the binary
  js/app.js                  UI wiring (shared by plugin and web build)
  js/knob.js, controls.js    SVG knob, toggle, segmented, dropdown, mod wheel
  js/engine-juce.js          the bridge (JUCE native integration)
webhost/
  dsp-worklet.js             JS twin of SynthEngine.h (web audio engine)
  web-host.js                provides the bridge in a browser: Web Audio, Web MIDI, tempo, presets
  build-web.ps1              assembles ../web/index.html
presets/factory_presets.json
tests/                       PresetTests.cpp, EngineTools.cpp (--parity/--bench/--loudness), parity_scenarios.json, run-parity.ps1
```

### Bridge events

| UI → host | host → UI (30 Hz, only changes) |
|---|---|
| `uiReady`, `setParam {id,value}`, `gesture {id,begin}` | `params {id:value,…}` |
| `noteOn {note,vel}`, `noteOff {note}`, `panic`, `modWheel {value}` | `notes {on:[],off:[]}` (host MIDI + UI keys) |
| `loadPreset {id}`, `savePreset {name,overwrite}`, `deletePreset {id}` | `presetList {presets,folder}`, `preset {id,name,category}` |
| `loadPresetFile`, `openPresetFolder`, `paramMenu {id,x,y}`, `loadSample {osc}`, `dropSample {osc,name,data}`, `clearSample {osc}` | `saveResult {ok,exists,name,error}`, `message {text}`, `samples {slots}` |
| *web only:* `setTempo {bpm}`, `selectMidi {id}` | `samples {slots}`, `stats {voices,peak,sustain,modWheel,bpm}`, `config {…}`, *web only:* `midiInputs` |

## Troubleshooting

- **Live UI editing:** set the environment variable `EPIANO_UI_DIR` to this repo's `vst\ui` folder before starting the DAW or Standalone. The UI then loads from disk: edit HTML/CSS/JS and just reopen the plugin window.
- **DevTools:** Shift + right-click in the plugin window, then *Inspect*.
- **Blank window:** the WebView2 runtime is missing or blocked. Its data folder is `%TEMP%\EPianoVST-WebView2`; deleting it is safe.
- The status bar shows the plugin format, sample rate and block size, and the last notes received. The header shows the host tempo that synced LFOs follow.
