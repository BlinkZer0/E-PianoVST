# E-Piano — v1.0

E-Piano is a 3-oscillator, 88-voice polyphonic synthesizer. It comes in two builds that share the same sound engine, controls and presets:

| Build | Where | What it is |
|---|---|---|
| **VST3 plugin** | [`vst/`](vst/README.md) | C++ / JUCE 8 instrument plugin for any VST3 DAW, plus a Standalone app |
| **Web build** | [`web/index.html`](https://blinkzer0.github.io/E-PianoVST) | One self-contained HTML file that runs in Chrome or Edge with Web MIDI |

![E-Piano: preset browser, three oscillators with sample loading, amp envelope, filter, output, and the inverted 24-key keyboard with mod wheel](docs/Screenshot%202026-11-17%20195810.png)

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
- **Output**: Master volume with a soft clipper, a voice counter, and a level meter with a clip light. Output is stereo.

**Modulation** (MODULATION page)
- **Two LFOs**:
  - Waves: Sine, Triangle, Square, Saw Up, Saw Down, S&H.
  - Rate in Hz, or synced to tempo from 1/1 to 1/32, including dotted and triplet.
  - Amount and Phase controls.
  - **Global** (all voices move together, e.g. wobble bass) or **per Voice** (each note has its own LFO, e.g. pads).
  - **Free**-running or **Note**-triggered.
- **Mod matrix** with 8 routes: Source × Via → Destination with Amount.
  - Sources: LFO 1/2, Amp Env, Filter Env, Velocity, Mod Wheel.
  - Destinations: Cutoff (moves in octaves, so it sounds musical), Resonance, Pitch for all oscillators or each one, Osc levels, Amplitude, Pan.
  - *Via* lets a second source scale a route, e.g. the mod wheel controls vibrato depth.
- **Mod wheel** (CC1), plus a draggable on-screen wheel next to the keys.

**Drive · Voice** (DRIVE · VOICE page)
- **Drive** per voice after the filter: Soft, Hard, Fold or Crush, with Drive, Tone, Mix and Output. Soft and Hard are anti-aliased.
- **Voice mode**: Poly, Mono or Legato, plus **glide** (time, and either always or only when notes overlap).

**Samples** (SYNTH page, under each oscillator)
- Any oscillator can play a **WAV sample** instead of its waveform. Click **LOAD WAV** or drag a file onto the oscillator panel (AIFF, FLAC and OGG work too; the web build also takes MP3).
- Modes:
  - **One-shot**: plays once per note, restarting on every key press.
  - **Loop**: repeats the whole file.
  - **Cycle**: the file is a single waveform cycle, pitched like an oscillator (good for single-cycle wavetable files).
- Pitch follows the keyboard from the file's root note. A WAV's own root-note setting is used if it has one; otherwise C4. Pitch and Fine retune as usual, and the filter, envelopes, LFOs, matrix and drive all apply.
- Samples are mixed to mono, normalised, and limited to 10 s. They're saved **inside** projects and `.epreset` files, so presets with samples work on any machine and in both builds.

**Playing**
- **MIDI**: note on/off with velocity, sustain pedal (CC64), mod wheel (CC1), pitch bend (±2 semitones), and all-notes-off / all-sound-off. All MIDI channels are accepted.
- **On-screen keyboard**: 24 keys drawn in SVG, in inverted colours (black naturals, white sharps), with octave shift. Clicking lower on a key gives a higher velocity, and you can drag across keys for a glissando.
- **Computer keyboard**: `A W S E D F T G Y H U J K O L P ; '` play notes, and `Z` / `X` shift the octave.
- **Knobs** (all SVG): drag up or down (Shift for fine), mouse wheel, arrow keys, and double-click to reset.
- **PANIC** silences everything immediately.

**Presets**
- **120 factory presets** in 12 categories of 10: Keys, Pads, Brass, Organ, Bass, Bells, Strings, Leads, **Dubstep** and **Dubstep 2** (twenty tempo-synced wobbles: 1/8, triplet, half-time, stutter, growl, laser, riddim, talking, screech, sub-drop, vowel, tearout, metallic and Reese wobbles, plus poly wobble chords, most with extra depth on the mod wheel), **Jazz** (Rhodes and Wurly with tremolo, B3 organ with a Leslie-style effect, tremolo vibes, walking upright bass, brushed pad, muted trumpet and flute with vibrato on the mod wheel, jazz guitar, late-night keys), plus **Motion**, which shows off the new features: wobble, dubstep, Reese and rhythmic basses, a vibrato lead, tremolo and modulated pads, distorted bass, S&H bleeps and auto-pan keys. Each preset's level was set by rendering it, so switching presets keeps a steady volume.
- Presets, projects and `.epreset` files from v0.9 sound exactly as before in v1.0: everything new defaults to off or neutral.
- A category browser, ‹ › arrows to step through every preset, and a `*` after the name when you've changed something since loading.
- **Save / Load / Folder** using `.epreset` files (plain JSON). The plugin and the web build use the same format, so presets move freely between them.

## VST3 plugin (`vst/`)

```
vst\build.bat      build VST3 + Standalone (Release)
vst\install.bat    copy E-Piano.vst3 to C:\Program Files\Common Files\VST3
```

- Loads as an instrument with stereo out and MIDI in. All 96 parameters can be automated, and a knob drag records as one automation move. MIDI events are applied at the exact sample they arrive.
- **Automating in FL Studio** (or any DAW with VST3 parameter menus): **right-click a knob, switch or dropdown** to get the DAW's own menu (*Create automation clip*, *Link to controller*…). Or move a control and use FL's *Tools → Last tweaked*.
- Synced LFOs follow the DAW's tempo, and lock to the beat while the transport plays.
- The 120 factory presets also appear in the DAW's program/preset menu. The project saves every setting plus the selected preset.
- User presets live in **`Documents\E-Piano\Presets`**, shared by every DAW and every instance. New, renamed and deleted files show up automatically within about 2 seconds, and each subfolder becomes a category. Deleting a preset moves it to the Recycle Bin.
- The UI is the same HTML/SVG as the web build, shown inside the plugin through WebView2 and scaled to the window size.
- Requirements: Windows, Visual Studio 2022 or 2026 (C++ workload), Git. JUCE 8.0.17 downloads automatically on the first build.

Build details, the bridge between the UI and C++, tests and troubleshooting tips are in [vst/README.md](vst/README.md).

## Web build (`web/index.html`)

- **One file**: UI, DSP and all 120 presets are inside it. Open it directly in Chrome or Edge, or run `start.bat` to serve it from `http://localhost:8080`.
- It's **generated from the plugin's own UI** by `vst\webhost\build-web.ps1`, so it has every feature of the plugin. Its JS sound engine is checked against the C++ one by the parity test.
- **BPM** in the header sets the tempo for synced LFOs. The plugin uses the DAW's tempo instead.
- Audio switches on with your first click or key press. The bottom-right corner shows the audio path:
  - **AudioWorklet** when served by `start.bat` (lowest latency)
  - **ScriptProcessor** when opened straight from disk (same DSP, a bit more latency)
- **MIDI**: pick your keyboard under *MIDI IN*. The browser asks for MIDI permission the first time.
- **Presets**:
  - **FOLDER** links a folder on disk (Chrome/Edge). Link `Documents\E-Piano\Presets` to share presets with the plugin. Linked folders autoload the same way the plugin's does. After reopening the page, one click on **RECONNECT** grants access again.
  - With no linked folder, **SAVE** stores presets in this browser (the *User* tab). After reopening the page, click **FOLDER** once to reconnect a linked folder.
  - **LOAD** opens any `.epreset` file.

## Release history

**v1.0: modulation, drive, samples**
- **Modulation:** two LFOs (tempo sync, Global or per-Voice, Free or Note trigger), an 8-route mod matrix with *Via*, and mod wheel support with an on-screen wheel.
- **Sound:** per-voice drive (Soft, Hard, Fold, Crush), Mono and Legato modes with glide, and stereo pan.
- **Samples:** WAV sample playback per oscillator (One-shot, Loop, Cycle), saved inside projects and presets.
- **Presets:** 120 level-matched factory presets. New since v0.9: Motion, Dubstep, Dubstep 2 and Jazz.
- **DAW automation:** right-click any control for the DAW's own parameter menu, including FL Studio's *Create automation clip*.
- **Under the hood:**
  - The C++ and JS engines are line-for-line twins with a shared deterministic RNG. They agree to within 5×10⁻⁵ on 23 test scenarios, down from about 1% before.
  - Worst case (88 voices, both LFOs per voice, 4 routes and drive) uses about 13% of one CPU core.
- **Automated checks:** preset tests (`EPianoTests`), C++ vs JS parity (`vst\tests\run-parity.ps1`), a performance benchmark (`--bench`), and headless-browser UI tests of the web build.
- **Still to confirm by hand:** the right-click automation menu and sample loading in FL Studio. Everything else in v1.0 has been built and tested automatically.

**v0.9: first plugin release**
- Web prototype ported to VST3. 80 level-matched presets, save/load, a shared preset folder with autoload, and host programs.
- Builds, installs, and runs in a DAW (confirmed by hand).

**Known limitations**
- Windows only for now. The plugin code is portable JUCE, but no macOS build has been set up or tested.
- The web build relies on Chrome or Edge for Web MIDI and folder linking. Other browsers can play but only store presets in the browser.
- The user-folder preset list in the plugin hasn't been stress-tested with very large libraries; it rescans the whole folder every 2 seconds.

**Ideas after 1.0**
- Effects: chorus, delay and reverb, which suit the e-piano and pad presets
- Per-oscillator pulse width or wave shaping, and unison / stereo spread
- More mod sources and destinations (aftertouch, LFO rate, drive amount). The matrix is table-driven, so each one is a small addition.
- A macOS (AU/VST3) build and an installer

## Repository layout

```
web/index.html            web build (single file, generated: vst\webhost\build-web.ps1)
start.bat, serve.ps1      optional local server for the web build
vst/                      VST3 plugin (JUCE 8): Source/, ui/, webhost/, presets/, tests/
```
