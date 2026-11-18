# Google Play listing: E-Piano 1.0

Everything the Play Console asks for, ready to paste. Graphics are in this folder; regenerate them with `node android/play-store/render.mjs`.

## Files to upload

| Console field | File | Spec |
|---|---|---|
| App bundle | `android/app/build/outputs/bundle/release/app-release.aab` | signed with the upload key |
| ReTrace mapping (App bundle explorer → Downloads → Deobfuscation file) | `android/app/build/outputs/mapping/release/mapping.txt` | optional, makes crash reports readable |
| App icon | `icon-512.png` | 512×512 PNG |
| Feature graphic | `feature-graphic.png` | 1024×500 PNG |
| Phone screenshots | `screenshots/1-phone-synth.png` … `4-phone-landscape.png` | 1080×1920 and 1920×1080 |

## Store listing

**App name** (30 max)
```
E-Piano Synth
```

**Short description** (80 max)
```
3-oscillator synth with USB & Bluetooth MIDI, 120 presets and a touch keyboard
```

**Full description** (4000 max)
```
E-Piano is a 3-oscillator, 88-voice polyphonic synthesizer for your phone or tablet. Plug in a USB MIDI keyboard or connect a Bluetooth LE MIDI controller and play, or use the multitouch keyboard on screen.

120 FACTORY PRESETS
Electric pianos, pads, brass, organs, basses, bells, strings, leads, jazz keys, tempo-synced dubstep wobbles and more, all level-matched so switching presets keeps a steady volume. Every preset responds to the mod wheel: vibrato, rotary shimmer, tremolo or an opening filter.

SOUND ENGINE
• 3 oscillators per voice: sine, saw, square or triangle (anti-aliased), ±36 semitones, fine tune and level
• Any oscillator can play your own WAV, AIFF, FLAC, OGG or MP3 sample: one-shot, loop or single-cycle wavetable
• Amp envelope (ADSR) and a resonant 12 dB low-pass filter with its own envelope
• Drive per voice: soft, hard, fold or bit-crush
• Poly, mono and legato modes with glide

MODULATION
• Two LFOs with six waveforms, free rate or tempo sync (1/1 to 1/32, dotted and triplet), global or per voice
• 8-route mod matrix: LFOs, envelopes, velocity and mod wheel to cutoff, resonance, pitch, levels, amplitude and pan, with a second "via" source to scale any route

PLAYING
• USB MIDI keyboards (class compliant, through a USB-C or OTG adapter) appear as soon as they are plugged in
• Bluetooth LE MIDI controllers, once connected in Android
• Velocity, sustain pedal, mod wheel and pitch bend
• On-screen keyboard with octave shift and a draggable mod wheel
• Portrait and landscape layouts

PRESETS
Save your own sounds, load .epreset files, and move them freely between E-Piano on Android, the free web version and the E-Piano VST3 plugin for Windows DAWs.

No ads, no accounts, no tracking. E-Piano needs no internet connection and collects no data.
```

**App category:** Music & Audio
**Tags:** Music, Synthesizer, Piano, Keyboard instruments, MIDI (pick what the Console offers)
**Contact email:** your developer email (required)
**Website:** https://github.com/BlinkZer0/E-PianoVST
**Privacy policy URL:** https://blinkzer0.github.io/E-PianoVST/privacy.html (goes live when `docs/privacy.html` is pushed)

## App content (Policy → App content)

| Section | Answer |
|---|---|
| Privacy policy | https://blinkzer0.github.io/E-PianoVST/privacy.html |
| Ads | No, the app contains no ads |
| App access | All functionality is available without special access |
| Content rating | Questionnaire category **"All other app types"**; answer No to every question (no violence, sexual content, language, controlled substances, gambling, user interaction or sharing, location sharing, digital purchases). Expected rating: Everyone / PEGI 3 |
| Target audience | **13 and over** (or 18+). Choosing an under-13 age group brings in the Families policy requirements |
| News app | No |
| Data safety | **No data collected, no data shared.** The app requests no permissions (merged manifest has none, not even internet); presets and samples stay on the device |
| Government app / Financial features / Health | No / None / None |
| Advertising ID | No (the app does not use it; on target SDK 33+ declare "No") |

## Release notes (en-US, 500 max)
```
E-Piano 1.0
• 120 factory presets, now all with mod wheel response
• Two LFOs, an 8-route mod matrix, drive, mono/legato glide
• Play your own samples on any oscillator
• USB and Bluetooth LE MIDI, sustain, mod wheel, pitch bend
• Phone layouts for portrait and landscape
```

## Before the first production release

1. **New personal developer accounts** must run a **closed test with at least 12 testers for 14 continuous days** before production access is granted. Upload the same AAB to a Closed testing track first.
2. **Play App Signing:** keep the default (Google holds the app signing key; you keep `upload-key.jks`). Back up `upload-key.jks` and `keystore.properties` somewhere outside this folder.
3. **Every later upload:** raise `versionCode` (and `versionName`) in `android/app/build.gradle`, run `vst\webhost\build-web.ps1` if the UI or DSP changed, then `gradlew bundleRelease`.
