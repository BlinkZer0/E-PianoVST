# E-Piano for Android

The web build (`web/index.html`) packaged as a native Android app for Google Play, with **MIDI working on the phone**.

## How it works

| Piece | What it does |
|---|---|
| `MainActivity.java` | A full-screen WebView that works in portrait and landscape (the UI switches to a scrolling phone layout with the keyboard pinned to the bottom). It serves the bundled page from `https://appassets.androidplatform.net/assets/index.html`, which counts as a secure context, so AudioWorklet and localStorage work. It also handles the file picker for **LOAD WAV** and `.epreset` loading. |
| `MidiBridge.java` | Native MIDI through `android.media.midi`. Android's WebView has **no Web MIDI**, so this opens every MIDI input device and splits the byte stream into messages (running status included; SysEx and real-time bytes dropped). It forwards messages to the page in batches and picks up devices plugged in or removed while the app runs. |
| `assets/android-midi.js` | Provides `navigator.requestMIDIAccess()` on top of `MidiBridge`, so the web host's MIDI code runs unchanged: input list, input selection, sustain, mod wheel, pitch bend, hot-plug. |
| `bundleWebApp` (Gradle task) | Copies `../web/index.html` into the app's assets at build time and inserts the shim at the top of `<head>`. Run `vst\webhost\build-web.ps1` first if you changed the UI or DSP. |

**MIDI devices that work**
- **USB MIDI keyboards** (class-compliant) through a USB-C or OTG adapter. No permission prompt is needed, and they appear as soon as they're plugged in.
- **Bluetooth LE MIDI** devices, once Android has connected them. Android has no built-in pairing screen for MIDI, so connect the device first with a free app such as *MIDI BLE Connect*. It then shows up in E-Piano.

The touch keyboard is multitouch, and the app also installs on devices without MIDI support.

## Build

You need **Android Studio** (it includes the JDK, Android SDK and Gradle).

1. *File → Open* → choose this `android/` folder. Let it sync; it downloads Gradle 8.13 and AGP 8.11.
2. Debug on a phone: enable *Developer options → USB debugging*, plug in the phone and click **Run**.
3. Release for Play: *Build → Generate Signed App Bundle / APK → Android App Bundle*, or from a terminal:
   ```
   gradlew bundleRelease      → app/build/outputs/bundle/release/app-release.aab
   gradlew assembleRelease    → app/build/outputs/apk/release/app-release.apk  (sideloading)
   ```
   Without Android Studio: set `JAVA_HOME` to a JDK 17 and put `sdk.dir=C\:/path/to/Android/Sdk` in `local.properties` (git-ignored), then run `gradlew assembleDebug`. Install the result with `adb install -r app/build/outputs/apk/debug/app-debug.apk`.

### Signing

Create an upload key once and **back it up**. If you lose it, you have to ask Google to reset it.

```
keytool -genkeypair -v -keystore upload-key.jks -keyalg RSA -keysize 2048 -validity 10000 -alias upload
```

Put `upload-key.jks` in `android/` (one has already been generated there; its passwords are in `keystore.properties`), then copy `keystore.properties.example` to `keystore.properties` and fill in the passwords. Git ignores both files.

## Publishing on Google Play

1. You need a [Play Console](https://play.google.com/console) developer account (one-time $25 fee). **New personal accounts** must run a closed test with at least 12 testers for 14 days before they can publish to production.
2. Create the app and upload `app-release.aab`. Enable **Play App Signing** (the default).
3. Store listing: a 512×512 icon, a 1024×500 feature graphic and at least 2 phone screenshots. Fill in the content rating questionnaire. For *Data safety*, the app collects nothing: presets and samples stay on the device, and there's no network use.
4. For every update, raise `versionCode` (and `versionName`) in `app/build.gradle`.

**Application ID:** `com.blinkzer0.epiano`. It can never change once the app is published, so edit `applicationId` in `app/build.gradle` before the first upload if you want a different one.
