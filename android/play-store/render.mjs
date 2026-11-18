// ============================================================================
//  Renders the Google Play listing graphics (and the Windows app icon) with
//  headless Chrome or Edge. No npm packages needed; Node 22+.
//
//    node android/play-store/render.mjs
//
//  Writes:
//    android/play-store/icon-512.png               Play store icon (512x512)
//    android/play-store/feature-graphic.png        Play feature graphic (1024x500)
//    android/play-store/screenshots/*.png          phone screenshots of web/index.html
//    vst/resources/icon.png                        Standalone / VST3 icon (1024x1024)
//
//  Run vst\webhost\build-web.ps1 first so the screenshots show the current UI.
// ============================================================================
import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync, existsSync, rmSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, '..', '..');
const browser = [
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
].find(existsSync);
if (!browser) throw new Error('Chrome or Edge not found');

const port = 9400 + Math.floor(Math.random() * 400);
const profile = mkdtempSync(join(tmpdir(), 'epiano-render-'));
const proc = spawn(browser, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`,
  '--hide-scrollbars', '--force-color-profile=srgb', 'about:blank'], { stdio: 'ignore' });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

let target;
for (let i = 0; i < 50 && !target; i++) {
  await sleep(200);
  try { target = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find((t) => t.type === 'page'); } catch {}
}
if (!target) throw new Error('browser did not start');
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
let nextId = 0;
const pending = new Map();
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
};
const cdp = (method, params = {}) => new Promise((res, rej) => {
  const id = ++nextId;
  pending.set(id, (m) => (m.error ? rej(new Error(`${method}: ${m.error.message}`)) : res(m.result)));
  ws.send(JSON.stringify({ id, method, params }));
});
const evaluate = async (expression) => {
  const r = await cdp('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || expression);
  return r.result.value;
};
await cdp('Page.enable');
await cdp('Runtime.enable');

async function open(url, { width, height, scale = 1, mobile = false }) {
  await cdp('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: scale, mobile });
  await cdp('Emulation.setTouchEmulationEnabled', { enabled: mobile, maxTouchPoints: mobile ? 5 : 1 });
  await cdp('Page.navigate', { url });
  await sleep(1500);
}
async function shot(file, clip) {
  mkdirSync(dirname(file), { recursive: true });
  const r = await cdp('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false, ...(clip ? { clip: { ...clip, scale: 1 } } : {}) });
  writeFileSync(file, Buffer.from(r.data, 'base64'));
  console.log('wrote', file.replace(repo, '.'));
}
const svgPage = (svg, css = '') => 'data:text/html;base64,' + Buffer.from(
  `<!doctype html><html><head><style>html,body{margin:0;background:transparent;overflow:hidden}svg{display:block;width:100vw;height:100vh;${css}}</style></head><body>${svg}</body></html>`).toString('base64');

try {
  const { readFileSync } = await import('node:fs');
  const iconSvg = readFileSync(join(here, 'art', 'icon.svg'), 'utf8');

  // Play icon: full square (Play applies its own rounded mask)
  await open(svgPage(iconSvg), { width: 512, height: 512 });
  await shot(join(here, 'icon-512.png'));

  // Windows icon: rounded square on transparent background
  await cdp('Emulation.setDefaultBackgroundColorOverride', { color: { r: 0, g: 0, b: 0, a: 0 } });
  await open(svgPage(iconSvg, 'border-radius:18%'), { width: 1024, height: 1024 });
  await shot(join(repo, 'vst', 'resources', 'icon.png'));
  await cdp('Emulation.setDefaultBackgroundColorOverride', {});

  // Feature graphic
  await open(pathToFileURL(join(here, 'art', 'feature-graphic.html')).href, { width: 1024, height: 500 });
  await shot(join(here, 'feature-graphic.png'));

  // Phone screenshots of the real app UI (touch device -> the app's compact layout)
  const app = pathToFileURL(join(repo, 'web', 'index.html')).href;
  const pick = (cat, name) => `(() => {
    const c = [...document.querySelectorAll('.cat-tab')].find(b => b.textContent.trim().toUpperCase() === ${JSON.stringify(cat.toUpperCase())});
    if (!c) throw new Error('category ${cat}'); c.click();
    const p = [...document.querySelectorAll('.preset-btn')].find(b => b.textContent.trim() === ${JSON.stringify(name)});
    if (!p) throw new Error('preset ${name}'); p.click(); return true; })()`;
  const page = (name) => `document.querySelector('.page-tab[data-page="${name}"]').click()`;
  const top = `window.scrollTo(0, 0)`;
  // Headless Chrome has no MIDI; show what a phone with nothing plugged in shows.
  // Raise the mod wheel a little (8 wheel steps of 8/127) so it reads as a control.
  const stage = `(() => {
    const s = document.querySelector('#midi-select');
    s.innerHTML = '<option>No MIDI devices</option>'; s.disabled = false;
    const w = document.querySelector('.wheel-svg');
    for (let i = 0; i < 8; i++) w.dispatchEvent(new WheelEvent('wheel', { deltaY: -100, cancelable: true }));
    return true; })()`;
  const outDir = join(here, 'screenshots');
  rmSync(outDir, { recursive: true, force: true });

  const portrait = { width: 360, height: 640, scale: 3, mobile: true };    // 1080 x 1920
  const landscape = { width: 640, height: 360, scale: 3, mobile: true };   // 1920 x 1080

  await open(app, portrait);
  await evaluate(stage);
  await evaluate(pick('Keys', 'Tine E-Piano')); await evaluate(page('synth')); await evaluate(top); await sleep(400);
  await shot(join(outDir, '1-phone-synth.png'));
  await evaluate(pick('Dubstep', 'Wub Machine')); await evaluate(page('mod')); await evaluate(top); await sleep(400);
  await shot(join(outDir, '2-phone-modulation.png'));
  await evaluate(pick('Jazz', 'Smoky Rhodes')); await evaluate(page('fx')); await evaluate(top); await sleep(400);
  await shot(join(outDir, '3-phone-drive-voice.png'));

  await open(app, landscape);
  await evaluate(stage);
  await evaluate(pick('Pads', 'Warm Pad')); await evaluate(page('synth')); await evaluate(top); await sleep(400);
  await shot(join(outDir, '4-phone-landscape.png'));
} finally {
  ws.close();
  proc.kill();
  await sleep(300);
  try { rmSync(profile, { recursive: true, force: true }); } catch {}
}
