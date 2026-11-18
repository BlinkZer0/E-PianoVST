# ============================================================================
#  Packages the Windows release:  dist\E-Piano-<version>-Windows-x64.zip
#    E-Piano.exe        Standalone app (no DAW needed)
#    E-Piano.vst3\      VST3 plugin bundle
#    README.txt         install notes
#  Usage:  vst\build.bat  then  powershell -ExecutionPolicy Bypass -File vst\package.ps1
# ============================================================================
$ErrorActionPreference = 'Stop'
$vst  = $PSScriptRoot
$repo = Split-Path $vst -Parent
$version = (Select-String -Path (Join-Path $vst 'CMakeLists.txt') -Pattern 'project\(EPianoVST VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$art  = Join-Path $env:LOCALAPPDATA 'EPianoVST\build\EPianoVST_artefacts\Release'
$exe  = Join-Path $art 'Standalone\E-Piano.exe'
$vst3 = Join-Path $art 'VST3\E-Piano.vst3'
foreach ($p in $exe, $vst3) { if (-not (Test-Path $p)) { throw "Not built yet: $p  (run vst\build.bat)" } }

# The release must not need the Visual C++ Redistributable.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -property installationPath
$dumpbin = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object Name | Select-Object -Last 1 |
           ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe' }
if ($dumpbin -and (Test-Path $dumpbin)) {
    $bin = Join-Path $vst3 'Contents\x86_64-win\E-Piano.vst3'
    foreach ($f in $exe, $bin) {
        $deps = & $dumpbin /dependents $f | Select-String -Pattern '(MSVCP|VCRUNTIME)\d+'
        if ($deps) { throw "$f links the MSVC runtime dynamically ($($deps -join ', ')). Rebuild with vst\build.bat." }
    }
}

$dist  = Join-Path $repo 'dist'
$name  = "E-Piano-$version-Windows-x64"
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
Copy-Item -Recurse $vst3 $stage

@"
E-Piano $version for Windows (64-bit)
=====================================

3-oscillator, 88-voice synthesizer. 120 factory presets, two LFOs, an 8-route
mod matrix, drive, mono/legato glide and WAV sample playback.

STANDALONE APP
  Run E-Piano.exe. No installation needed.
  Options > Audio/MIDI Settings: pick your audio device and MIDI keyboard.

VST3 PLUGIN
  Copy the E-Piano.vst3 folder to:
      C:\Program Files\Common Files\VST3
  then rescan plugins in your DAW. It loads as an instrument.

REQUIREMENTS
  Windows 10 or 11, 64-bit.
  Microsoft Edge WebView2 Runtime (built into Windows 11 and current Windows 10;
  otherwise get it from https://developer.microsoft.com/microsoft-edge/webview2/).

PRESETS
  Your presets are saved in Documents\E-Piano\Presets and are shared by the
  Standalone app and the plugin in every DAW. The .epreset files also load in
  the web build and the Android app.

More: https://github.com/BlinkZer0/E-PianoVST
"@ | Set-Content -Path (Join-Path $stage 'README.txt') -Encoding utf8

$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Remove-Item -Recurse -Force $stage
Write-Host "Wrote $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
