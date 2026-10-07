// Production drawing editor + scope-aware native scene/resources. Test-only
// scroll spacers exercise both panes without inventing modelling records.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, mkdirSync, copyFileSync } from 'node:fs';
import { resolve, delimiter } from 'node:path';
import { browserAssetRoot, browserReportDirectory, importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';

const output = browserReportDirectory('part-drawing-array-skip');
const runtime = resolve(process.env.ICAX_DRAWING_NATIVE_RUNTIME || 'src/x64/Debug');
const sourceBridge = resolve(process.env.ICAX_DRAWING_NATIVE_BRIDGE
  || 'output/tests/punch-native-incremental/probe-host-v2/BatchExcelAutomationScopeBridge.exe');
const isolatedNative = resolve(output, 'native-host');
const bridge = resolve(isolatedNative, 'BatchExcelAutomationScopeBridge.exe');
const report = { status: 'running', nativeBusinessResponsesMocked: false, checks: [], screenshots: [],
  scope: 'Deployed production drawing UI/DOM/WebGL modules in a browser fixture with an isolated actual native scene. Scroll spacers are test-only. Standalone CEF application was not controlled.' };
let host, browser, page, sequence = 0, stderr = '';
const waiting = new Map(), nativeCalls = [], browserErrors = [];
function rpc(scope, method, payload = {}) {
  const request = { id: ++sequence, scope, method: method.replace(/^TubeDesigner\./, ''), payload };
  return new Promise((complete, reject) => {
    const timer = setTimeout(() => { waiting.delete(request.id); reject(new Error('Native timeout: ' + method)); }, 120000);
    waiting.set(request.id, { timer, request, started: performance.now(), complete, reject });
    host.stdin.write(JSON.stringify(request) + '\n');
  });
}
function summarizeResponse(response) {
  if (!response.ok) return { ok: false, error: response.error };
  const r = response.result ?? {};
  return { ok: true, keys: Object.keys(r), resultError: r.resultError, toolCount: r.toolCount,
    partEntityId: r.partEntityId, baseGeometry: r.baseGeometry,
    ...(r.base64 ? { resource: { url: r.url, version: r.version, bytes: r.bytes } } : {}),
    toolPreviews: r.toolPreviews?.map(t => ({ key: t.key, target: t.target, geometry: t.geometry })) };
}
try {
  mkdirSync(isolatedNative, { recursive: true });
  if (sourceBridge !== bridge) copyFileSync(sourceBridge, bridge);
  const currentDll = resolve(runtime, 'TubeDesigner.dll');
  copyFileSync(currentDll, resolve(isolatedNative, 'TubeDesigner.dll'));
  const expectedDllHash = createHash('sha256').update(readFileSync(currentDll)).digest('hex');
  const env = { ...process.env, ICAX_AUTOMATION_USER_DATA: resolve(output, 'native-user-data') };
  mkdirSync(env.ICAX_AUTOMATION_USER_DATA, { recursive: true });
  const priorPath = Object.entries(env).find(([k]) => k.toLowerCase() === 'path')?.[1] ?? '';
  for (const key of Object.keys(env)) if (key.toLowerCase() === 'path') delete env[key];
  env.Path = runtime + delimiter + priorPath;
  host = spawn(bridge, [], { cwd: runtime, env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
  host.stderr.on('data', bytes => { stderr += bytes; });
  const abort = error => { for (const job of waiting.values()) { clearTimeout(job.timer); job.reject(error); } waiting.clear(); };
  host.on('error', abort); host.on('exit', code => abort(new Error('Native host exited: ' + code)));
  createInterface({ input: host.stdout }).on('line', line => {
    let response; try { response = JSON.parse(line); } catch { stderr += line + '\n'; return; }
    const job = waiting.get(response.id); if (!job) return;
    waiting.delete(response.id); clearTimeout(job.timer);
    nativeCalls.push({ request: job.request, response: summarizeResponse(response), milliseconds: performance.now() - job.started });
    response.ok ? job.complete(response.result) : job.reject(new Error(response.error));
  });
  await rpc('inspection', 'ResetScene');
  report.runtimeModules = (await rpc('inspection', 'GetRuntimeModules')).modules;
  report.nativeModule = { path: report.runtimeModules['TubeDesigner.dll'],
    sha256: createHash('sha256').update(readFileSync(report.runtimeModules['TubeDesigner.dll'])).digest('hex') };
  assert.equal(resolve(report.nativeModule.path).toLowerCase(), resolve(isolatedNative, 'TubeDesigner.dll').toLowerCase());
  assert.equal(report.nativeModule.sha256, expectedDllHash, 'Actual native module must match the selected current runtime DLL');
  const id = 'round', descriptor = JSON.parse(readFileSync(resolve(browserAssetRoot, `apps/tube-designer/templates/profile/${id}/profile.json`), 'utf8'));
  descriptor.display = JSON.parse(readFileSync(resolve(browserAssetRoot, `apps/tube-designer/templates/profile/${id}/display.json`), 'utf8'));
  const defaultParameters = Object.fromEntries(descriptor.parameters.map(p => [p.key, p.defaultValue]));
  const profile = { id, name: descriptor.displayName, descriptor, defaultParameters,
    previewProfile: (await rpc('product', 'EvaluateProfilePackage', { profileRef: { scope: 'system', id }, parameters: defaultParameters })).profile };
  const scene = await rpc('scene', 'List');
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } }); page.setDefaultTimeout(45000);
  page.on('pageerror', error => browserErrors.push(error.message));
  await page.exposeFunction('nativeDrawingRpc', rpc);
  await page.route('http://drawing-skip.test/**', serveBrowserAsset);
  await page.goto('http://drawing-skip.test/');
  const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
  await page.addStyleTag({ content: '*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,Arial,sans-serif}'
    + readFileSync(resolve(browserAssetRoot, 'apps/_shared/workbench/styles/laser3dcam.css'), 'utf8') + tubeDesignerCss });
  await page.evaluate(async ({ profile, scene }) => {
    const drawing = await import('/src/apps/tube-designer/webpage/partDrawing.mjs');
    const preview = await import('/src/apps/tube-designer/webpage/partDrawingPreview.mjs');
    const dom = await import('/src/apps/tube-designer/webpage/partDrawingDom.mjs');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    document.body.innerHTML = '<main id="mount" class="tube-designer-workspace"><section id="background"><button data-cam-action="tube-designer-drawing-open">三维绘制零件</button></section><div id="dialogs"></div></main>';
    const mount = document.querySelector('#mount'), dialogs = document.querySelector('#dialogs');
    const view = { activeAreaId: 'nesting', pending: false, scene, tubeDesignerSystemProfiles: [profile], tubeDesignerSelectedProfileId: 'system:round',
      tubeDesignerTemplateProfiles: [], tubeDesignerUserData: { profiles: [] } };
    const f = window.fixture = { view, drawing, preview, dom, calls: [], failures: [], resourceReads: [], pending: 0, patches: 0, mounts: 0,
      holdNextResource: false, heldResource: null, addScrollSpacers: false };
    Object.defineProperty(f, 'state', { get() { return view.tubeDesignerPartDrawing?.state; } });
    const invoke = scope => async (method, payload, options) => {
      const call = { scope, method, payload: structuredClone(payload) }; f.calls.push(call);
      try { const result = await window.nativeDrawingRpc(scope, method, payload); call.result = result; return result; }
      catch (error) { call.error = error.message; throw error; }
    };
    const context = f.context = { mount, sceneProxy: { invoke: invoke('scene'), resources: { async get(url, options) {
      const version = Number(options?.headers?.get('ICAX-Resource-Version') || 0);
      const result = await window.nativeDrawingRpc('inspection', 'ReadResource', { url, version });
      f.resourceReads.push({ url, version, bytes: result.bytes });
      if (f.holdNextResource) {
        f.holdNextResource = false;
        await new Promise(release => { f.heldResource = { url, version, release }; });
      }
      return new Response(Uint8Array.from(atob(result.base64), c => c.charCodeAt(0)));
    } } }, productProxy: { invoke: invoke('product') } };
    const ops = f.ops = { createPartDrawingViewport(options) { return f.viewport = createThreeViewport(options); }, showNotice() {}, renderProject() {
      if (!view.tubeDesignerPartDrawing) { dialogs.replaceChildren(); return; }
      let html = drawing.renderPartDrawingDialog(view);
      if (f.addScrollSpacers) {
        const template = document.createElement('template'); template.innerHTML = html;
        const left = document.createElement('div'); left.id = 'test-tree-spacer'; left.style.height = '1300px';
        template.content.querySelector('.td-draw-tree [role="tree"]').append(left);
        const nested = document.createElement('div'); nested.id = 'test-nested-scroll'; nested.style.cssText = 'height:70px;overflow:auto';
        nested.innerHTML = '<div style="height:650px"></div>';
        const right = document.createElement('div'); right.id = 'test-right-spacer'; right.style.height = '1000px';
        template.content.querySelector('.td-draw-property-scroll').append(nested, right);
        html = template.innerHTML;
      }
      if (dom.patchPartDrawingDom(view, mount, html)) f.patches++;
      else { f.mounts++; dialogs.innerHTML = html; dom.rememberPartDrawingDom(view, mount); }
      preview.attachPartDrawingPreview(context, view, mount, ops);
      drawing.attachPartDrawingEditor(context, view, mount, ops);
    } };
    f.dispatch = async (action, target) => { if (!action?.startsWith('tube-designer-drawing-')) return;
      f.pending++; try { return await drawing.handlePartDrawingAction(context, view, action, target, ops); }
      catch (error) { f.failures.push({ action, error: error.message }); } finally { f.pending--; } };
    document.addEventListener('change', e => void f.dispatch(e.target.dataset.camChangeAction, e.target));
    document.addEventListener('click', e => { const target = e.target.closest('[data-cam-action]'); if (target && !target.disabled) void f.dispatch(target.dataset.camAction, target); });
    f.render = ops.renderProject;
  }, { profile, scene });
  const action = name => page.locator(`[data-cam-action="tube-designer-drawing-${name}"]`);
  const command = name => page.locator(`[data-drawing-command="${name}"]`);
  const field = key => page.locator(`[data-tube-designer-punch-field="${key}"]`);
  const ready = async () => {
    await page.waitForFunction(() => { const f = window.fixture, s = f.state;
      return s && !f.pending && !f.view.pending && !s.previewPending && !s.previewRenderPending
        && s.preview?.revision === s.revision && document.querySelector('[data-part-drawing-preview-ready="true"]'); });
    assert.equal(await page.evaluate(() => window.fixture.state.error), '');
  };
  const idle = () => page.waitForFunction(() => !window.fixture.pending && !window.fixture.view.pending);
  const change = async (locator, value) => { await locator.fill(String(value)); await locator.press('Tab'); await ready(); };
  const select = async (key, value) => { await field(key).selectOption(value); await ready(); };
  const screenshot = async name => { const path = resolve(output, name + '.png'); await page.screenshot({ path }); report.screenshots.push(path); };
  const countCreates = () => page.evaluate(() => window.fixture.calls.filter(c => /\.(Add|Apply)PartDrawing$/.test(c.method)).length);
  await action('open').click(); await ready();
  await command('branch').click(); await ready();
  await change(page.locator('[data-drawing-section="branch"][data-drawing-parameter="width"]'), 8);
  await change(field('station'), 100);
  await select('arrayDimension', 'two');
  await change(field('arrayCount'), 3); await change(field('arraySpacing'), 100);
  await change(field('rowCount'), 2); await change(field('rowSpacing'), 45);
  await select('drawingArrayMode', 'round');
  assert.equal(await field('skipInstancesText').count(), 1);
  await change(field('skipInstancesText'), '2:1, 3:2');
  const arraySnapshot = () => page.evaluate(() => {
    const f = window.fixture, s = f.state, payload = f.calls.findLast(c => c.method === 'TubeDesigner.PreviewPartDrawing')?.payload;
    const feature = payload.features[0], tool = s.preview.toolPreviews.find(t => t.key === s.features[0].id);
    const geometry = f.viewport.geometryObjects.get(tool.geometry.url);
    return { feature, toolCount: s.preview.toolCount, toolPreviews: s.preview.toolPreviews.length,
      positions: geometry.getAttribute('position').count, indices: geometry.index?.count ?? 0,
      visible: f.viewport.sceneObjects.get(`drawing:feature:${s.features[0].id}`)?.visible === true };
  });
  report.initial = await arraySnapshot();
  assert.deepEqual(report.initial.feature.skippedInstances, ['0:1', '1:2']);
  assert.equal(report.initial.toolCount, 4); assert.ok(report.initial.positions > 0 && report.initial.indices > 0); assert.equal(report.initial.visible, true);
  report.checks.push('Actual regular 3 x 2 circumferential array skips 2:1 and 3:2, transports canonical zero-based pairs and displays exactly four native tool positions');
  await screenshot('01-two-dimensional-skip-native-preview');

  // The native resource is already calculated/read, then held before browser
  // consumption. Interaction continues after that point, not at request start.
  await page.evaluate(() => {
    const f = window.fixture; f.addScrollSpacers = true; f.render();
    f.canvas = document.querySelector('.icax-three-viewport-canvas'); f.skipInput = document.querySelector('[data-tube-designer-punch-field="skipInstancesText"]');
    f.left = document.querySelector('.td-draw-tree [role="tree"]'); f.right = document.querySelector('.td-draw-property-scroll'); f.nested = document.querySelector('#test-nested-scroll');
    f.inputEvents = 0; f.canvasEvents = 0; f.skipInput.addEventListener('test-retained', () => f.inputEvents++); f.canvas.addEventListener('test-retained', () => f.canvasEvents++);
    f.holdNextResource = true;
  });
  await field('skipInstancesText').fill('2:1'); await field('skipInstancesText').press('Tab');
  await page.waitForFunction(() => window.fixture.heldResource && !window.fixture.view.pending);
  await field('skipInstancesText').focus(); await field('skipInstancesText').fill('2:1 ');
  await page.evaluate(() => {
    const f = window.fixture; f.skipInput.setSelectionRange(1, 4, 'backward');
    f.left.scrollTop = 210; f.right.scrollTop = 125; f.nested.scrollTop = 95;
    f.latestScroll = [f.left.scrollTop, f.right.scrollTop, f.nested.scrollTop]; f.heldResource.release();
  });
  await ready();
  report.continuity = await page.evaluate(() => {
    const f = window.fixture; f.skipInput.dispatchEvent(new Event('test-retained')); f.canvas.dispatchEvent(new Event('test-retained'));
    return { input: f.skipInput === document.querySelector('[data-tube-designer-punch-field="skipInstancesText"]'),
      canvas: f.canvas === document.querySelector('.icax-three-viewport-canvas'), focus: document.activeElement === f.skipInput,
      value: f.skipInput.value, selection: [f.skipInput.selectionStart, f.skipInput.selectionEnd, f.skipInput.selectionDirection],
      scroll: [f.left.scrollTop, f.right.scrollTop, f.nested.scrollTop], expectedScroll: f.latestScroll,
      nested: f.nested === document.querySelector('#test-nested-scroll'), listeners: [f.inputEvents, f.canvasEvents] };
  });
  for (const k of ['input', 'canvas', 'focus', 'nested']) assert.equal(report.continuity[k], true, k);
  assert.equal(report.continuity.value, '2:1 '); assert.deepEqual(report.continuity.selection, [1, 4, 'backward']);
  assert.deepEqual(report.continuity.scroll, report.continuity.expectedScroll); assert.ok(report.continuity.scroll.every(x => x > 0));
  assert.deepEqual(report.continuity.listeners, [1, 1]);
  report.checks.push('A held actual resource reply retains the latest skip text/caret, both panes and nested scroll, original input/canvas nodes and listeners');
  await field('skipInstancesText').press('Tab'); await ready();
  for (const text of ['2:1, 2:1', '4:1', '*:*']) {
    const writes = await countCreates();
    await field('skipInstancesText').fill(text); await field('skipInstancesText').press('Tab'); await idle();
    assert.ok(await page.evaluate(() => window.fixture.state.error));
    assert.equal(await countCreates(), writes);
  }
  await change(field('skipInstancesText'), '2:1, 3:2');
  await select('arrayDimension', 'one');
  assert.equal(await page.evaluate(() => window.fixture.state.preview.toolCount), 3);
  await change(field('skipInstancesText'), '2');
  report.oneDimensional = await arraySnapshot();
  assert.equal(report.oneDimensional.toolCount, 2);
  assert.deepEqual(report.oneDimensional.feature.skippedInstances, ['0:1']);
  await select('arrayDimension', 'none');
  assert.equal(await page.evaluate(() => window.fixture.state.preview.toolCount), 1);
  await select('arrayDimension', 'two');
  assert.equal(await field('skipInstancesText').inputValue(), '2:1, 3:2');
  assert.equal(await page.evaluate(() => window.fixture.state.preview.toolCount), 4);
  report.checks.push('Duplicate, out-of-range and all-skipped input is rejected; one-dimensional ordinal skips work, and inactive dimensional drafts remain intact without blocking another array mode');

  await change(field('skipInstancesText'), '2:*');
  report.wildcards = [await arraySnapshot()];
  assert.equal(report.wildcards[0].toolCount, 4);
  assert.deepEqual(report.wildcards[0].feature.skippedInstances, ['0:1', '1:1']);
  await change(field('skipInstancesText'), '*:2');
  report.wildcards.push(await arraySnapshot());
  assert.equal(report.wildcards[1].toolCount, 3);
  assert.deepEqual(report.wildcards[1].feature.skippedInstances, ['1:0', '1:1', '1:2']);
  await change(field('skipInstancesText'), '2:1, 3:2');
  await page.evaluate(() => { const f = window.fixture; f.addScrollSpacers = false; f.render(); document.querySelector('.td-draw-property-scroll').scrollTop = 10000; });
  await screenshot('02-skip-field-before-final-create');
  assert.equal(await countCreates(), 0);
  await page.getByRole('button', { name: '取消', exact: true }).click(); await idle();
  assert.equal(await countCreates(), 0);
  report.checks.push('Wildcard row/column skips work through the same native recipe; cancelling the drawing makes zero creation calls');

  await action('open').click(); await ready(); await command('branch').click(); await ready();
  await change(page.locator('[data-drawing-section="branch"][data-drawing-parameter="width"]'), 8);
  await change(field('length'), 144); await change(field('roll'), 17);
  await change(field('station'), 100); await select('arrayDimension', 'two');
  await change(field('arrayCount'), 3); await change(field('arraySpacing'), 100); await change(field('rowCount'), 2);
  await select('drawingArrayMode', 'round'); await change(field('rowSpacing'), 45); await change(field('skipInstancesText'), '2:1, 3:2');
  await action('apply').click(); await idle();
  const persistedScene = await rpc('scene', 'List');
  const created = await page.evaluate(() => window.fixture.calls.findLast(c => c.method === 'TubeDesigner.AddPartDrawing'));
  assert.ok(created?.result?.partEntityId, JSON.stringify(created)); assert.equal(created.error, undefined); assert.equal(await countCreates(), 1);
  const parts = persistedScene.tubeDesigner.nestingGroups.flatMap(g => g.parts ?? []);
  assert.equal(parts.length, 1); const part = parts[0], recipe = part.properties['tubeDesigner.partDrawing'];
  assert.equal(part.entityId, created.result.partEntityId); assert.equal(recipe.features.length, 1);
  assert.deepEqual(recipe.features[0].skippedInstances, ['0:1', '1:2']);
  assert.equal(recipe.features[0].arrayCount, 3); assert.equal(recipe.features[0].rowCount, 2);
  const branchFields = ['angle', 'azimuth', 'roll', 'offsetY', 'offsetZ', 'length', 'direction'];
  const placementValues = item => Object.fromEntries(branchFields.map(key => [key, item[key]]));
  assert.deepEqual(placementValues(recipe.features[0]), placementValues(created.payload.features[0]),
    'Saved drawing recipe must retain every supplied branch placement field, before any editor defaults');
  assert.equal(recipe.features[0].length, 144); assert.equal(recipe.features[0].roll, 17);
  assert.ok(part.manufacturingGeometryResourceId);
  const manufacturingInfo = await rpc('inspection', 'GetResourceInfo', { url: part.manufacturingGeometryResourceId });
  assert.equal(manufacturingInfo.exists, true, 'The saved manufacturing BRep must exist');
  const finalResource = await rpc('inspection', 'ReadResource', { url: part.thumbnailGeometryResourceId,
    version: Number(part.thumbnailGeometryResourceVersion) || 0 });
  assert.ok(finalResource.bytes > 100, 'Final native display mesh must be readable');
  report.saved = { entityId: part.entityId, skippedInstances: recipe.features[0].skippedInstances, arrayCount: 3, rowCount: 2,
    manufacturingGeometryResourceId: part.manufacturingGeometryResourceId, manufacturingInfo,
    thumbnailGeometryResourceId: part.thumbnailGeometryResourceId, displayResourceBytes: finalResource.bytes };
  report.checks.push('Confirm creates exactly one actual part with the canonical skipped pairs persisted in its dedicated drawing recipe and real manufacturing resource');
  await page.evaluate(({ scene, partId }) => {
    const f = window.fixture; f.view.scene = scene;
    const button = document.createElement('button'); button.textContent = '编辑已保存的二维阵列';
    button.dataset.camAction = 'tube-designer-drawing-open'; button.dataset.tubeDesignerPartId = partId;
    document.querySelector('#background').append(button);
  }, { scene: persistedScene, partId: part.entityId });
  await page.getByRole('button', { name: '编辑已保存的二维阵列', exact: true }).click(); await ready();
  await page.locator(`[data-drawing-node="${recipe.features[0].id}"]`).click(); await ready();
  assert.equal(await field('arrayDimension').inputValue(), 'two');
  assert.equal(await field('skipInstancesText').inputValue(), '2:1, 3:2');
  report.reopened = await arraySnapshot();
  assert.deepEqual(placementValues(report.reopened.feature), placementValues(created.payload.features[0]),
    'Reopened production preview must keep all persisted branch placement values exactly');
  assert.deepEqual(await page.evaluate(() => {
    const feature = window.fixture.state.features[0];
    return Object.fromEntries(['angle', 'azimuth', 'roll', 'offsetY', 'offsetZ', 'length', 'direction'].map(key => [key, feature[key]]));
  }), placementValues(created.payload.features[0]), 'Production reopened model must retain branch placement values');
  assert.equal(report.reopened.toolCount, 4); assert.deepEqual(report.reopened.feature.skippedInstances, ['0:1', '1:2']);
  assert.equal(report.reopened.visible, true); assert.ok(report.reopened.positions > 0);
  await field('skipInstancesText').scrollIntoViewIfNeeded();
  await screenshot('03-persisted-part-reopened-two-dimensional-skip');
  await page.getByRole('button', { name: '取消', exact: true }).click(); await idle();
  assert.equal(await countCreates(), 1);
  assert.equal((await rpc('scene', 'List')).tubeDesigner.nestingGroups.flatMap(g => g.parts ?? []).length, 1);
  report.checks.push('The actual persisted List part reopens through the production drawing action with the two-dimensional mode, original skip text and four real visible tools; cancelling it performs no second write');
  report.status = 'passed';
  assert.deepEqual(browserErrors, []); assert.deepEqual(await page.evaluate(() => window.fixture.failures), []);
  assert.equal(nativeCalls.filter(c => c.response.ok === false).length, 0);
  console.log('PartDrawing array skip actual native/browser PASS: ' + report.checks.length + ' groups. ' + output);
} catch (error) {
  report.status = 'failed'; report.error = error.message; report.stack = error.stack;
  if (page) { const path = resolve(output, 'failure.png'); await page.screenshot({ path }).catch(() => {}); report.screenshots.push(path);
    report.failureState = await page.evaluate(() => ({ state: window.fixture?.state, failures: window.fixture?.failures })).catch(() => null); }
  throw error;
} finally {
  report.nativeRequests = nativeCalls; report.browserErrors = browserErrors;
  writeFileSync(resolve(output, 'report.json'), JSON.stringify(report, null, 2));
  writeFileSync(resolve(output, 'native-stderr.log'), stderr);
  if (browser) await browser.close();
  if (host?.exitCode == null) { host.stdin.end(); host.kill(); }
}
