// Real application EXE/CEF regression. Own configuration, profile and cooperative close.
// ICAX_CEF_GPU_RUNTIME selects a complete isolated runtime; ICAX_CEF_GPU_PORT and
// ICAX_ARTIFACT_DIR select the private CDP port and output directory.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createHash, randomUUID } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
const repository = fileURLToPath(new URL('../../../', import.meta.url));
const runtime = resolve(process.env.ICAX_CEF_GPU_RUNTIME || resolve(repository, 'output/tests/cef-gpu-native/Debug'));
const output = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repository, 'output/tests/cef-gpu-regression', randomUUID()));
const port = Number(process.env.ICAX_CEF_GPU_PORT || 9434);
assert.ok(Number.isInteger(port) && port > 1024 && port < 65536);
const executable = resolve(runtime, 'TubeDesigner.exe');
for (const path of [executable, resolve(runtime, 'CefUIContainer.dll'), resolve(runtime, 'apps/tube-designer/webpage/sketchArea.mjs')]) assert.ok(existsSync(path), path);
assert.equal(await fetch(`http://127.0.0.1:${port}/json/version`).then(() => true, () => false), false, 'Private CDP port already in use');
mkdirSync(resolve(output, 'Setting'), { recursive: true });
const fixtureURL = new URL('./TubeDesignerCefGpu.fixture.mjs', import.meta.url).href;
writeFileSync(resolve(output, 'fixture.html'), `<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="${pathToFileURL(resolve(runtime, 'apps/_shared/workbench/styles/laser3dcam.css')).href}"><div id="app" class="tube-designer-workspace"></div><script type="module">try { const {mountCefGpuFixture}=await import(${JSON.stringify(fixtureURL)});await mountCefGpuFixture(${JSON.stringify(pathToFileURL(runtime + '/').href)}); } catch(error) { window.__cefGpuError=error.stack; throw error; }</script>`);
writeFileSync(resolve(output, 'Setting/UIContainer.Setting'), `type=cef\nmodulePath=CefUIContainer.dll\nstartURL=${pathToFileURL(resolve(output, 'fixture.html')).href}\nremoteDebuggingPort=${port}\nsdoPollIntervalMS=16\n`);
const report = { passed: false, runtime, output, port, executable, executableSha256: createHash('sha256').update(readFileSync(executable)).digest('hex'),
  privateUserData: resolve(output, 'UserData'), noForcedTermination: true, pageErrors: [] };
const save = () => writeFileSync(resolve(output, 'result.json'), JSON.stringify(report, null, 2));
const psLiteral = value => "'" + value.replaceAll("'", "''") + "'";
const launchScript = resolve(output, 'start-owned.ps1'), launchResult = resolve(output, `launch-${randomUUID()}.json`);
writeFileSync(launchScript, `$ErrorActionPreference='Stop'\n$taskProcess=Start-Process -FilePath ${psLiteral(executable)} -WorkingDirectory ${psLiteral(output)} -WindowStyle Hidden -RedirectStandardOutput ${psLiteral(resolve(output, 'application-stdout.log'))} -RedirectStandardError ${psLiteral(resolve(output, 'application-stderr.log'))} -Environment @{ICAX_USER_DATA_ROOT=${psLiteral(report.privateUserData)};__COMPAT_LAYER='RunAsInvoker'} -PassThru\n@{pid=$taskProcess.Id;startTime=$taskProcess.StartTime.ToUniversalTime().ToString('o')}|ConvertTo-Json -Compress|Set-Content -LiteralPath ${psLiteral(launchResult)}\n`);
// The compatibility layer runs this test with the caller's existing token; the
// application EXE manifest and its requested privileges remain unmodified.
const launcher = spawn('pwsh', ['-NoProfile', '-File', launchScript], { windowsHide: true, stdio: 'ignore' });
launcher.unref();
for (let attempt = 0; attempt < 100 && !existsSync(launchResult); attempt++) await new Promise(done => setTimeout(done, 100));
assert.ok(existsSync(launchResult), 'Owned launcher did not report its process identity');
Object.assign(report, JSON.parse(readFileSync(launchResult, 'utf8').replace(/^\uFEFF/, ''))); save();
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs');
let browser, page;
const summarize = values => { const sorted = values.toSorted((a, b) => a - b); return { count: sorted.length, max: sorted.at(-1) || 0,
  p95: sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * .95))] || 0 }; };
try {
  for (let attempt = 0; attempt < 100; attempt++) {
    if (await fetch(`http://127.0.0.1:${port}/json/version`).then(r => r.ok, () => false)) break;
    await new Promise(done => setTimeout(done, 100));
  }
  browser = await chromium.connectOverCDP(`http://127.0.0.1:${port}`);
  page = browser.contexts()[0].pages().find(candidate => candidate.url() === pathToFileURL(resolve(output, 'fixture.html')).href);
  assert.ok(page, 'Owned fixture page must exist');
  page.on('pageerror', error => report.pageErrors.push(error.message));
  await page.waitForFunction(() => window.__cefGpuReady || window.__cefGpuError, {}, { timeout: 20000 });
  assert.equal(await page.evaluate(() => window.__cefGpuError), undefined);
  const session = await browser.newBrowserCDPSession();
  report.systemBefore = await session.send('SystemInfo.getInfo');
  report.webgl = await page.evaluate(() => { const canvas = document.createElement('canvas'), gl = canvas.getContext('webgl2');
    if (!gl) return { available: false }; const debug = gl.getExtension('WEBGL_debug_renderer_info');
    return { available: true, renderer: gl.getParameter(debug.UNMASKED_RENDERER_WEBGL), vendor: gl.getParameter(debug.UNMASKED_VENDOR_WEBGL) }; });
  assert.equal(report.webgl.available, true);
  assert.doesNotMatch(report.webgl.renderer, /SwiftShader|Software|Disabled/i);
  assert.equal(report.systemBefore.gpu.auxAttributes.processCrashCount, 0);
  assert.doesNotMatch(report.systemBefore.commandLine, /--disable-gpu(?:\s|$)|--disable-gpu-compositing/);
  const point = async (x, y) => page.locator('[data-tube-sketch-canvas]').evaluate((svg, [x, y]) => {
    const p = svg.createSVGPoint(); p.x = x; p.y = y; const screen = p.matrixTransform(svg.getScreenCTM()); return { x: screen.x, y: screen.y };
  }, [x, y]);
  await page.locator('[data-sketch-command="sketch.circle"]').click();
  const center = await point(450, 145); await page.mouse.click(center.x, center.y);
  await page.evaluate(() => { window.__cefGpuOriginalSvg = document.querySelector('[data-tube-sketch-canvas]'); window.__cefGpuMetrics.active = true; });
  const end = await point(700, 190); await page.mouse.move(end.x, end.y, { steps: 30 });
  await page.evaluate(() => new Promise(done => requestAnimationFrame(() => requestAnimationFrame(done))));
  report.rubberband = await page.evaluate(() => { window.__cefGpuMetrics.active = false; return {
    preview: document.querySelector('[data-tube-sketch-draft-preview]').innerHTML,
    svgRetained: window.__cefGpuOriginalSvg === document.querySelector('[data-tube-sketch-canvas]'), metrics: window.__cefGpuMetrics }; });
  assert.ok(report.rubberband.preview);
  assert.equal(report.rubberband.svgRetained, true);
  assert.equal(report.rubberband.metrics.nativeCalls, 0);
  assert.ok(report.rubberband.metrics.updates.length > 2);
  await page.screenshot({ path: resolve(output, 'circle-rubberband.png') });
  await page.mouse.click(end.x, end.y);
  report.committed = await page.evaluate(() => ({ count: window.__cefGpuFixture.draft.entities.length, last: window.__cefGpuFixture.draft.entities.at(-1) }));
  assert.equal(report.committed.count, 97); assert.equal(report.committed.last.kind, 'circle'); assert.ok(report.committed.last.radius > 0);
  report.systemAfter = await session.send('SystemInfo.getInfo');
  assert.equal(report.systemAfter.gpu.auxAttributes.processCrashCount, 0);
  assert.deepEqual(report.pageErrors, []);
  const times = report.rubberband.metrics.updates;
  report.performance = { callbacks: summarize(report.rubberband.metrics.callbacks), frames: summarize(report.rubberband.metrics.frames),
    latency: summarize(report.rubberband.metrics.latency), intervals: summarize(times.slice(1).map((time, i) => time - times[i])), longTasks: report.rubberband.metrics.longTasks };
  report.passed = true;
} catch (error) { report.error = error.stack; process.exitCode = 1; }
finally {
  if (page) await page.close().catch(error => { report.closeError = error.message; });
  if (browser) await browser.close().catch(() => {});
  const checkScript = `$ErrorActionPreference='Stop';try { $taskProcess=Get-Process -Id ${report.pid} -ErrorAction Stop; if(!$taskProcess.WaitForExit(15000)) { @{exited=$false}|ConvertTo-Json -Compress; exit 1 } } catch [Microsoft.PowerShell.Commands.ProcessCommandException] {} ; @{exited=$true}|ConvertTo-Json -Compress`;
  const closed = spawnSync('pwsh', ['-NoProfile', '-Command', checkScript], { windowsHide: true, encoding: 'utf8', timeout: 20000 });
  report.cooperativeExit = { status: closed.status, output: closed.stdout.trim(), error: closed.stderr.trim() };
  const shutdownLog = resolve(report.privateUserData, 'Logs/ApplicationShutdown.log');
  if (existsSync(shutdownLog)) report.shutdownLog = readFileSync(shutdownLog, 'utf8');
  const cefLog = resolve(report.privateUserData, 'Logs/cef.log');
  if (existsSync(cefLog)) { report.cefLog = cefLog; report.gpuCrashLines = readFileSync(cefLog, 'utf8').split(/\r?\n/).filter(line => /GPU process exited unexpectedly|GPU process has crashed/.test(line)); }
  if (closed.status !== 0 || !report.shutdownLog?.includes('run-return end') || report.gpuCrashLines?.length) { report.passed = false; process.exitCode = 1; }
  save();
  console.log(JSON.stringify({ passed: report.passed, output, renderer: report.webgl?.renderer, performance: report.performance, error: report.error, cooperativeExit: report.cooperativeExit }));
}
