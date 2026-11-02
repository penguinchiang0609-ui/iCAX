// Production drawing editor + scope-aware native scene/resources. Test-only
// scroll spacers exercise both panes without inventing modelling records.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetRoot, browserReportDirectory, importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
import { startSideSketchNativeBridge } from './fixtures/sideSketchNativeBridge.mjs';

const output = browserReportDirectory('part-drawing-array-skip');
const runtime = resolve(process.env.ICAX_DRAWING_NATIVE_RUNTIME || 'src/x64/Debug');
const report = { status: 'running', nativeBusinessResponsesMocked: false, checks: [], screenshots: [],
  scope: 'Deployed production drawing UI/DOM/WebGL modules and registered scene CAD services in an isolated native host. Scroll spacers are test-only. Standalone CEF application was not controlled.' };
let nativeBridge, browser, page;
const nativeCalls = [], browserErrors = [];
async function rpc(scope, method, payload = {}) {
  method=method.replace(/^TubeDesigner\./,'');
  const request={scope,method,payload},started=performance.now();
  try {
    let result;
    if(scope==='native')result=await nativeBridge.rpc({action:method,payload});
    else if(scope==='inspection'&&method==='ResetScene')result=await nativeBridge.rpc({action:'reset'});
    else if(scope==='inspection'&&method==='ReadResource')result=await nativeBridge.rpc({action:'resource',payload});
    else if(scope==='inspection'&&method==='GetResourceInfo') {
      try {await nativeBridge.rpc({action:'resource',payload});result={exists:true};}
      catch(error) {if(!/Real resource not present/.test(error.message))throw error;result={exists:false};}
    } else result=await nativeBridge.invoke(method,payload,scope);
    nativeCalls.push({request,response:{ok:true,keys:Object.keys(result),toolCount:result.toolCount},milliseconds:performance.now()-started});
    return result;
  } catch(error) {nativeCalls.push({request,response:{ok:false,error:error.message}});throw error;}
}
try {
  mkdirSync(output,{recursive:true});
  nativeBridge=await startSideSketchNativeBridge(output);
  const loaded=JSON.parse(execFileSync('powershell.exe',['-NoProfile','-Command',
    '(Get-Process -Id '+nativeBridge.processId+' -ErrorAction Stop).Modules | Where-Object { $_.ModuleName -in @(\'TubeDesigner.dll\',\'OpenCascadeResourceImport.dll\') } | ForEach-Object { [pscustomobject]@{name=$_.ModuleName;path=$_.FileName} } | ConvertTo-Json -Compress'],{encoding:'utf8'}));
  report.runtimeModules=Object.fromEntries(loaded.map(module=>[module.name,module.path]));
  report.nativeModule={path:report.runtimeModules['TubeDesigner.dll'],
    sha256:createHash('sha256').update(readFileSync(report.runtimeModules['TubeDesigner.dll'])).digest('hex')};
  assert.equal(resolve(report.nativeModule.path).toLowerCase(),resolve(output,'runtime','TubeDesigner.dll').toLowerCase());
  assert.equal(report.nativeModule.sha256,nativeBridge.dllHash);
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
    + readFileSync(resolve(browserAssetRoot, 'apps/_shared/workbench/styles/workbench.css'), 'utf8') + tubeDesignerCss });
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
      if (!view.tubeDesignerPartDrawing) { preview.disposePartDrawingPreview(mount); dialogs.replaceChildren(); return; }
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
  assert.equal(await page.evaluate(() => window.fixture.state.previewMode), 'finished');
  assert.ok(await page.evaluate(() => window.fixture.state.preview.geometry?.url));
  await page.locator('[data-drawing-preview-mode="tools"]').click(); await ready();
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
  const cancelledPreview = await page.evaluate(() => ({ key: window.fixture.state.previewResourceKey,
    geometry: window.fixture.state.preview.geometry }));
  await page.getByRole('button', { name: '取消', exact: true }).click(); await idle();
  await page.waitForFunction(key => window.fixture.calls.some(call => call.method === 'TubeDesigner.ReleasePartDrawingPreview'
    && call.payload.previewResourceKey === key && call.result?.released), cancelledPreview.key);
  assert.equal((await rpc('inspection', 'GetResourceInfo', { url: cancelledPreview.geometry.url })).exists, false);
  assert.equal(await countCreates(), 0);
  report.checks.push('Closing disposes the real viewport and releases its native runtime geometry scope without creating a part');
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
  assert.equal(parts.length, 1); const part = parts[0], recipe = created.payload;
  assert.equal(part.entityId, created.result.partEntityId);
  assert.ok(part.properties['tubeDesigner.cadDocument']?.documentResourceId);
  assert.equal(part.properties['tubeDesigner.partDrawing'], undefined);
  assert.equal(recipe.features.length, 1);
  assert.deepEqual(recipe.features[0].skippedInstances, ['0:1', '1:2']);
  assert.equal(recipe.features[0].arrayCount, 3); assert.equal(recipe.features[0].rowCount, 2);
  assert.equal(recipe.features[0].length, 144); assert.equal(recipe.features[0].roll, 17);
  const document = await rpc('scene', 'GetTubeCadDocument', { partEntityId: part.entityId,
    resourceVersion: part.manufacturingGeometryResourceVersion });
  assert.ok(document.geometry.SolidNodes.length > 2);
  assert.ok(document.geometry.SolidNodes.some(node => node.ID === document.geometry.BaseNodeID));
  assert.ok(part.manufacturingGeometryResourceId);
  const manufacturingInfo = await rpc('native', 'geometry', { partEntityId: part.entityId });
  assert.equal(manufacturingInfo.valid, true, 'The saved manufacturing BRep must exist and be valid');
  const documentMatch=await rpc('native','cad-document-geometry',{partEntityId:part.entityId});
  assert.ok(documentMatch.directedDifferenceVolume<1e-5,'The canonical skipped array CAD document must reproduce its published manufacturing BRep');
  const finalResource = await rpc('inspection', 'ReadResource', { url: part.thumbnailGeometryResourceId,
    version: Number(part.thumbnailGeometryResourceVersion) || 0 });
  assert.ok(finalResource.bytes > 100, 'Final native display mesh must be readable');
  const reread = await rpc('scene', 'GetTubeCadDocument', { partEntityId: part.entityId,
    resourceVersion: part.manufacturingGeometryResourceVersion });
  assert.deepEqual(reread.geometry, document.geometry);
  assert.equal(reread.revision, document.revision);
  assert.equal(await countCreates(), 1);
  assert.equal((await rpc('scene', 'List')).tubeDesigner.nestingGroups.flatMap(g => g.parts ?? []).length, 1);
  report.saved = { entityId: part.entityId, skippedInstances: recipe.features[0].skippedInstances, arrayCount: 3, rowCount: 2,
    document, documentMatch, manufacturingGeometryResourceId: part.manufacturingGeometryResourceId, manufacturingInfo,
    thumbnailGeometryResourceId: part.thumbnailGeometryResourceId, displayResourceBytes: finalResource.bytes };
  report.checks.push('Confirm creates exactly one part from the canonical skipped array and publishes its complete neutral CAD document and real manufacturing/display resources');
  // Saved-part event routing and Apply are covered by the unified native browser suite.
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
  if (browser) await browser.close();
  await nativeBridge?.close();
}
