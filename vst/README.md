# E-Piano — VST3 port (JUCE 8)

This is a port of the web prototype in `../web`. The DSP is translated line for line into C++. The UI is the same HTML/SVG, shown inside the plugin through WebView2.

## Build

```
build.bat            (Release)
build.bat Debug
install.bat          copies E-Piano.vst3 to C:\Program Files\Common Files\VST3 (asks for admin)
```

Requirements: Visual Studio 2022 or 2026 with the C++ workload, Git, and the WebView2 runtime (built into Windows 11). On first build, CMake downloads JUCE 8.0.17. Build files go to `%LOCALAPPDATA%\EPianoVST`, so OneDrive doesn't sync them:

- `...\build\EPianoVST_artefacts\Release\VST3\E-Piano.vst3`
- `...\build\EPianoVST_artefacts\Release\Standalone\E-Piano.exe` is the quickest way to test without a DAW. Use *Options → Audio/MIDI Settings* to pick the audio device and MIDI input.

To open the project in Visual Studio, open `%LOCALAPPDATA%\EPianoVST\build\EPianoVST.slnx` (`.sln` on older versions) after one build.

## In the DAW

- Loads as an instrument (Instrument|Synth) with stereo out and MIDI in.
- All 25 controls are automatable. Knob drags are recorded as single automation gestures.
- **Amp** (Amp Envelope panel, −30 to +6 dB) is the preset's own level and is saved with presets. **Master** is your global output level and presets never change it.
- The 80 factory presets are host programs, so they also appear in the DAW's preset/program menu.
- MIDI: note on/off with velocity, sustain (CC64), pitch bend (±2 st), all notes off/all sound off. Events are sample-accurate.
- State, including the selected preset, is saved with the project.

## Presets

- **Factory:** 80 presets in 8 categories (Keys, Pads, Brass, Organ, Bass, Bells, Strings, Leads), built into the plugin from [presets/factory_presets.json](presets/factory_presets.json). Their Amp levels were set by rendering each one, so switching presets doesn't jump in volume.
- **User folder:** `Documents\E-Piano\Presets`, shared by every DAW and every instance. The plugin scans it on open and checks it every 2 seconds, so files you copy in, rename or delete show up on their own. Files in the folder itself appear under **User**, and each subfolder becomes its own category tab.
- **SAVE** writes `<name>.epreset` into that folder and asks before replacing an existing file. **LOAD** opens any `.epreset` file from anywhere. **FOLDER** opens the folder in Explorer. Hovering a user preset shows **×**, which moves the file to the Recycle Bin.
- The arrows next to the preset name step through all presets: factory first, then user.
- File format (plain JSON, easy to edit or share):
  ```json
  { "format": "E-Piano Preset", "version": 1, "name": "My Sound", "category": "User",
    "params": { "osc1On": 1, "osc1Wave": 0, "osc1Pitch": 0, "ampLevel": -1.5, "fltCutoff": 2500 } }
  ```
  Any parameter missing from a file loads at its default, so older or hand-written files still work.

## Tests

`cmake --build %LOCALAPPDATA%\EPianoVST\build --config Release --target EPianoTests`, then run `EPianoTests.exe` from `EPianoTests_artefacts\Release`. It checks the factory presets, the save/load round trip, and how bad or partial files are handled.

## Layout

```
CMakeLists.txt
Source/
  Params.h          parameter table (mirrors ui/js/params.js)
  PresetManager.*   factory presets, user folder scan, .epreset read/write
  SynthEngine.h     DSP: Oscillator, Envelope, SVF, Voice, 88-voice engine (no JUCE)
  PluginProcessor.* APVTS, MIDI handling, programs, state
  PluginEditor.*    WebView2 host + JS <-> C++ bridge
ui/                 copy of the web UI, embedded in the binary
  js/engine-juce.js the bridge to C++ (the web build uses Web Audio instead)
  js/app.js         adapted: no power button / Web MIDI, receives host automation + notes
  js/knob.js        + automation gesture callbacks
```

### Bridge events

| UI → C++ | C++ → UI (30 Hz, only changes) |
|---|---|
| `uiReady`, `setParam {id,value}`, `gesture {id,begin}` | `params {id:value,…}` |
| `noteOn {note,vel}`, `noteOff {note}`, `panic` | `notes {on:[],off:[]}` (host MIDI + UI keys) |
| `loadPreset {id}`, `savePreset {name,overwrite}`, `deletePreset {id}` | `presetList {presets,folder}`, `preset {id,name,category}` |
| `loadPresetFile`, `openPresetFolder` | `saveResult {ok,exists,name,error}`, `message {text}`, `stats`, `config` |

## Troubleshooting

- **Live UI editing:** set the environment variable `EPIANO_UI_DIR` to this repo's `vst\ui` folder before starting the DAW or Standalone. The UI then loads from disk, so you can edit HTML/CSS/JS and just reopen the plugin window without rebuilding.
- **DevTools:** Shift + right-click in the plugin window, then *Inspect*.
- **Blank window:** the WebView2 runtime is missing or blocked. Its data folder is `%TEMP%\EPianoVST-WebView2`; deleting it is safe.
- The status bar shows the plugin format, sample rate and block size, and the last notes received.
