# E-Piano — v0.9

E-Piano is a 3-oscillator, 88-voice polyphonic synthesizer. It comes in two builds that share the same sound engine, controls and presets:

| Build | Where | What it is |
|---|---|---|
| **VST3 plugin** | [`vst/`](vst/README.md) | C++ / JUCE 8 instrument plugin for any VST3 DAW, plus a Standalone app |
| **Web build** | [`web/index.html`](web/index.html) | One self-contained HTML file that runs in Chrome or Edge with Web MIDI |

## What it can do

**Sound engine**
- 88-voice polyphony. Playing the same note again retriggers its voice. When all voices are busy it takes over the oldest released voice first, then the oldest voice.
- 3 oscillators per voice, each with:
  - **Wave**: sine, saw, square or triangle (anti-aliased with PolyBLEP)
  - **Pitch**: ±36 semitones
  - **Fine**: ±100 cents
  - **Level**
  - an on/off switch
- **Amp envelope**: Attack, Decay, Sustain, Release, plus **Amp**, the preset's own level (−30 to +6 dB).
- **Filter**: 12 dB/oct low-pass (state-variable) with Cutoff and Resonance. Its filter envelope (Env Amt, Env Dec) responds to velocity, and the cutoff partly follows the key you play.
- **Output**: Master volume with a soft clipper, a voice counter, and a level meter with a clip light.

**Playing**
- **MIDI**: note on/off with velocity, sustain pedal (CC64), pitch bend (±2 semitones), and all-notes-off / all-sound-off. All MIDI channels are accepted.
- **On-screen keyboard**: 24 keys drawn in SVG, in inverted colours (black naturals, white sharps), with octave shift. Clicking lower on a key gives a higher velocity, and you can drag across keys for a glissando.
- **Computer keyboard**: `A W S E D F T G Y H U J K O L P ; '` play notes, and `Z` / `X` shift the octave.
- **Knobs** (all SVG): drag up or down (Shift for fine), mouse wheel, arrow keys, and double-click to reset.
- **PANIC** silences everything immediately.

**Presets**
- **80 factory presets** in 8 categories of 10: Keys, Pads, Brass, Organ, Bass, Bells, Strings, Leads. Each one's level was set by rendering it, so switching presets keeps a steady volume.
- A category browser, ‹ › arrows to step through every preset, and a `*` after the name when you've changed something since loading.
- **Save / Load / Folder** using `.epreset` files (plain JSON). The plugin and the web build use the same format, so presets move freely between them.

## VST3 plugin (`vst/`)

```
vst\build.bat      build VST3 + Standalone (Release)
vst\install.bat    copy E-Piano.vst3 to C:\Program Files\Common Files\VST3
```

- Loads as an instrument with stereo out and MIDI in. All 25 parameters can be automated, and a knob drag records as one automation move. MIDI events are applied at the exact sample they arrive.
- The 80 factory presets also appear in the DAW's program/preset menu. The project saves every setting plus the selected preset.
- User presets live in **`Documents\E-Piano\Presets`**, shared by every DAW and every instance. New, renamed and deleted files show up automatically within about 2 seconds, and each subfolder becomes a category. Deleting a preset moves it to the Recycle Bin.
- The UI is the same HTML/SVG as the web build, shown inside the plugin through WebView2 and scaled to the window size.
- Requirements: Windows, Visual Studio 2022 or 2026 (C++ workload), Git. JUCE 8.0.17 downloads automatically on the first build.

Build details, the bridge between the UI and C++, tests and troubleshooting tips are in [vst/README.md](vst/README.md).

## Web build (`web/index.html`)

- **One file**: UI, DSP and all 80 presets are inside it. Open it directly in Chrome or Edge, or run `start.bat` to serve it from `http://localhost:8080`.
- Audio switches on with your first click or key press. The bottom-right corner shows the audio path:
  - **AudioWorklet** when served by `start.bat` (lowest latency)
  - **ScriptProcessor** when opened straight from disk (same DSP, a bit more latency)
- **MIDI**: pick your keyboard under *MIDI IN*. The browser asks for MIDI permission the first time.
- **Presets**:
  - **FOLDER** links a folder on disk (Chrome/Edge). Link `Documents\E-Piano\Presets` to share presets with the plugin. Linked folders autoload the same way the plugin's does. After reopening the page, one click on **RECONNECT** grants access again.
  - With no linked folder, **SAVE** stores presets in this browser (the *Browser* tab).
  - **LOAD** opens any `.epreset` file.

## Where we are (v0.9)

**Done**
- Web prototype → VST3 port finished. The C++ and JavaScript DSP match within about 1% on every factory preset.
- The VST3 builds cleanly, installs, and runs in a DAW (confirmed by hand).
- Presets: 80 level-matched factory presets, save/load, a shared preset folder with autoload, and host program support.
- Automated checks:
  - preset system console tests (`EPianoTests`): factory data, the save → load round trip, and bad or partial files
  - DSP parity between C++ and JS
  - Amp level gain checks
  - headless-browser UI tests of the web build

**Known limitations**
- Windows only for now. The plugin code is portable JUCE, but no macOS build has been set up or tested.
- The web build relies on Chrome or Edge for Web MIDI and folder linking. Other browsers can play but only store presets in the browser.
- The user-folder preset list in the plugin hasn't been stress-tested with very large libraries; it rescans the whole folder every 2 seconds.

**Ideas for 1.0**
- Modulation: an LFO for vibrato and tremolo, and a mod wheel mapping
- Effects: chorus, delay and reverb, which suit the e-piano and pad presets
- Per-oscillator pulse width or wave shaping, and unison / stereo spread
- Velocity sensitivity control, and a glide / mono mode for leads and basses
- A macOS (AU/VST3) build and an installer

## Repository layout

```
web/index.html            web build (single file)
start.bat, serve.ps1      optional local server for the web build
vst/                      VST3 plugin (JUCE 8): Source/, ui/, presets/, tests/
```
