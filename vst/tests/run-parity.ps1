# ============================================================================
#  DSP parity: renders every scenario in tests/parity_scenarios.json with the
#  C++ engine (EPianoTests --parity) and the JS engine (webhost/dsp-worklet.js
#  in headless Edge), then compares RMS per window (8 per channel) + peak.
#  Usage: powershell -ExecutionPolicy Bypass -File run-parity.ps1 [-Tolerance 0.001]
# ============================================================================
param([double]$Tolerance = 0.001)
$ErrorActionPreference = 'Stop'

$vst = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $env:LOCALAPPDATA 'EPianoVST\build\EPianoTests_artefacts\Release\EPianoTests.exe'
if (-not (Test-Path $exe)) { throw "Build EPianoTests first: $exe" }
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw 'Microsoft Edge not found (needed to run the JS engine headless)' }

$scenarioFile = Join-Path $PSScriptRoot 'parity_scenarios.json'
$tmp = Join-Path $env:TEMP 'epiano-parity'
New-Item -ItemType Directory -Force $tmp | Out-Null

# ---- C++ -------------------------------------------------------------------
Push-Location $PSScriptRoot
$cppJson = & $exe --parity 'parity_scenarios.json' | Out-String
Pop-Location
$cpp = $cppJson | ConvertFrom-Json

# ---- JS (same scenarios, same factory presets) -------------------------------
$read = { param($p) [IO.File]::ReadAllText((Join-Path $vst $p)) }
$page = @'
<!doctype html><html><head><title>running</title></head><body><pre id="out"></pre>
<script>PARAMS_JS</script>
<script>const FACTORY_JSON = FACTORY;</script>
<script>const SCENARIOS = SCENARIO_JSON;</script>
<script>DSP_JS</script>
<script>
try {
  let Proc; const sampleRate = 48000;
  class AudioWorkletProcessor { constructor() { this.port = { postMessage() {}, onmessage: null }; } }
  const registerProcessor = (n, c) => { Proc = c; };
  eval('(' + synthWorkletModule.toString() + ')()');

  const factory = {};
  for (const c of FACTORY_JSON.categories) for (const p of c.presets) {
    const v = {};
    for (let k = 0; k < 3; k++) { const o = p[k + 1], n = k + 1;
      v[`osc${n}On`] = o[0]; v[`osc${n}Wave`] = o[1]; v[`osc${n}Pitch`] = o[2]; v[`osc${n}Fine`] = o[3]; v[`osc${n}Level`] = o[4]; }
    [v.ampAttack, v.ampDecay, v.ampSustain, v.ampRelease] = p[4];
    [v.fltCutoff, v.fltReso, v.fltEnvAmt, v.fltDecay] = p[5];
    v.ampLevel = p[6];
    if (p[7]) Object.assign(v, p[7]);
    factory[c.name + '/' + p[0]] = v;
  }

  const out = [];
  for (const sc of SCENARIOS) {
    const values = defaultParamValues();
    if (sc.preset) Object.assign(values, factory[sc.preset]);
    Object.assign(values, sc.params || {});
    const p = new Proc({ processorOptions: { params: values } });
    p.bpm = sc.bpm || 120;
    const L = new Float32Array(128), R = new Float32Array(128);
    const W = 8, per = sc.blocks * 128 / W, sl = new Float64Array(W), sr = new Float64Array(W);
    let peak = 0;
    const send = (m) => p.port.onmessage({ data: m });
    for (let b = 0; b < sc.blocks; b++) {
      for (const ev of sc.events || []) {
        if (ev[0] !== b) continue;
        if (ev[1] === 'on') send({ type: 'noteOn', note: ev[2], vel: ev[3] });
        else if (ev[1] === 'off') send({ type: 'noteOff', note: ev[2] });
        else if (ev[1] === 'wheel') send({ type: 'modWheel', value: ev[2] });
        else if (ev[1] === 'sustain') send({ type: 'sustain', on: !!ev[2] });
        else if (ev[1] === 'bend') send({ type: 'bend', value: ev[2] });
      }
      p.process([], [[L, R]]);
      for (let i = 0; i < 128; i++) {
        const w = Math.min(W - 1, Math.floor((b * 128 + i) / per));
        sl[w] += L[i] * L[i]; sr[w] += R[i] * R[i];
        peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i]));
      }
    }
    out.push({ name: sc.name, rmsL: [...sl].map(x => Math.sqrt(x / per)), rmsR: [...sr].map(x => Math.sqrt(x / per)), peak });
  }
  document.getElementById('out').textContent = JSON.stringify(out);
  document.title = 'done';
} catch (e) { document.title = 'ERROR ' + (e.stack || e); }
</script></body></html>
'@
$page = $page.Replace('PARAMS_JS', (& $read 'ui\js\params.js')).
              Replace('FACTORY;', (& $read 'presets\factory_presets.json') + ';').
              Replace('SCENARIO_JSON', [IO.File]::ReadAllText($scenarioFile)).
              Replace('DSP_JS', (& $read 'webhost\dsp-worklet.js'))
$pagePath = Join-Path $tmp 'parity.html'
[IO.File]::WriteAllText($pagePath, $page, [Text.UTF8Encoding]::new($false))
$domFile = Join-Path $tmp 'dom.txt'
Start-Process -FilePath $edge -Wait -RedirectStandardOutput $domFile -RedirectStandardError (Join-Path $tmp 'err.txt') -ArgumentList @(
    '--headless=new', '--disable-gpu', "--user-data-dir=$tmp\profile", '--virtual-time-budget=120000', '--dump-dom',
    ('file:///' + ($pagePath -replace '\\', '/')))
$dom = [IO.File]::ReadAllText($domFile)
if ($dom -match '(?s)<title>ERROR(.*?)</title>') { throw "JS engine error: $([Net.WebUtility]::HtmlDecode($Matches[1]))" }
if ($dom -notmatch '(?s)<pre id="out">(.*?)</pre>') { throw 'JS run produced no output' }
$js = [Net.WebUtility]::HtmlDecode($Matches[1]) | ConvertFrom-Json

# ---- compare -------------------------------------------------------------------
$fail = 0
Write-Host ('{0,-48} {1,12} {2,12}  {3}' -f 'scenario', 'max rel err', 'C++ peak', 'result')
for ($i = 0; $i -lt $cpp.Count; $i++) {
    $c = $cpp[$i]; $j = $js[$i]
    $worst = 0.0
    foreach ($ch in 'rmsL', 'rmsR') {
        for ($w = 0; $w -lt 8; $w++) {
            $a = [double]$c.$ch[$w]; $b = [double]$j.$ch[$w]
            $err = [Math]::Abs($a - $b) / [Math]::Max([Math]::Max($a, $b), 1e-4)
            if ($err -gt $worst) { $worst = $err }
        }
    }
    $pk = [Math]::Abs([double]$c.peak - [double]$j.peak) / [Math]::Max([double]$c.peak, 1e-4)
    if ($pk -gt $worst) { $worst = $pk }
    $ok = $worst -le $Tolerance
    if (-not $ok) { $fail++ }
    Write-Host ('{0,-48} {1,12:E2} {2,12:F4}  {3}' -f $c.name, $worst, [double]$c.peak, $(if ($ok) { 'ok' } else { 'FAIL' }))
}
if ($fail -eq 0) { Write-Host "PARITY OK ($($cpp.Count) scenarios, tolerance $Tolerance)" ; exit 0 }
Write-Host "PARITY FAILED in $fail scenario(s)"; exit 1
