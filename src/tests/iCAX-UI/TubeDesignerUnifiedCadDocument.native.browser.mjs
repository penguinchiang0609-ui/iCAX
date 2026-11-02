// Current production punch entry in Edge -> real scene SDOs -> neutral
// document/resources -> fresh project repository on reopen. The original 2D
// and 3D workspaces are covered by CadExistingWorkbenches.native.browser.mjs.
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { resolve, sep } from 'node:path';
import { tubeDesignerCss } from '../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs';
import { startSideSketchNativeBridge, repositoryRoot } from './fixtures/sideSketchNativeBridge.mjs';

const output = resolve(repositoryRoot, 'output/tests/punch-cad-document-browser');
const sourceRoot = resolve(repositoryRoot, 'src');
mkdirSync(output, { recursive: true });
const bridge = await startSideSketchNativeBridge(output);
const results = [], errors = [], external = [], servedSources = new Map();
let browser, page, failure;
const get = (id, extra = {}) => bridge.invoke('GetTubeCadDocument', { partEntityId: id, ...extra });
const circularCut = read => read.geometry.SolidNodes.find(item => item.ID !== read.geometry.BaseNodeID
  && item.Data.type === 'SExtrudedRegionNode' && item.Data.value.Section.Boundaries.length === 1
  && item.Data.value.Section.Boundaries[0].Curves[0]?.Segment.Curve.type === 'Circle2');
const radius = sketch => sketch.geometry.value.Boundaries[0].Curves[0].Segment.Curve.value.Radius;

try {
  const imported = await bridge.invoke('ImportNestingPart', { sourcePath: resolve(repositoryRoot,
    'samples/tube-geometry/05_double_cavity_blind_hole.step') });
  const id = imported.partEntityId;
  const original = await get(id);
  const holeId = circularCut(original).ID;
  const originalRadius = circularCut(original).Data.value.Section.Boundaries[0].Curves[0].Segment.Curve.value.Radius;
  const descriptor = JSON.parse(readFileSync(resolve(sourceRoot, 'apps/tube-designer/templates/profile/rect/profile.json'), 'utf8'));
  const parameters = { ...Object.fromEntries(descriptor.parameters.map(item => [item.key, item.defaultValue])),
    width: 80, depth: 60, wallThickness: 3, cornerRadius: 0, innerRadius: 0 };
  const evaluated = await bridge.invoke('EvaluateProfilePackage', { profileRef: { scope: 'system', id: 'rect' }, parameters }, 'product');
  const circleTool = (await bridge.invoke('GetPartDrawingTools')).tools.find(item => item.id === 'circle');
  const authored = await bridge.invoke('AddNestingPunchPart', {
    name: '真实 UI 四弧圆孔', quantity: 1,
    drawing: { schemaVersion: 1, length: 400, section: { source: 'library', profile: evaluated.profile } }, ends: {},
    features: [{ id: 'authored-circle', type: 'circle', toolTarget: circleTool.target,
      toolRef: { scope: 'system', id: 'circle' }, toolParameters: { ...circleTool.defaultParameters, diameter: 10 },
      station: 140, face: 'top', offset: 0, layoutDatum: 'base' }],
  });
  const authoredRead = await get(authored.partEntityId);
  const authoredCut = circularCut(authoredRead);
  assert.equal(authoredCut.Data.value.Section.Boundaries[0].Curves.length, 4);
  const scene = await bridge.invoke('List');
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE
    || 'file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs');
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
  page = await browser.newPage({ viewport: { width: 1560, height: 820 } });
  page.setDefaultTimeout(45000);
  page.on('pageerror', error => errors.push(error.message));
  await page.exposeFunction('cadNativeRpc', bridge.rpc);
  await page.route('**/*', route => {
    const url = new URL(route.request().url());
    if (url.origin !== 'http://unified-cad-native.test') { external.push(url.href); return route.abort(); }
    if (url.pathname === '/') return route.fulfill({ contentType: 'text/html',
      body: '<!doctype html><html lang="zh-CN"><meta charset="utf-8"><body><div id="app" class="tube-designer-workspace"></div></body></html>' });
    const file = resolve(sourceRoot, url.pathname.replace(/^\/src\//, ''));
    if (!url.pathname.startsWith('/src/') || !file.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(file)) return route.abort();
    const body = readFileSync(file, 'utf8');
    const hash = createHash('sha256').update(body).digest('hex');
    if (servedSources.has(url.pathname)) assert.equal(servedSources.get(url.pathname), hash, 'Production source changed during the browser run');
    servedSources.set(url.pathname, hash);
    return route.fulfill({ contentType: 'text/javascript', body });
  });
  await page.goto('http://unified-cad-native.test');
  await page.addStyleTag({ content: '*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif}'
    + readFileSync(resolve(sourceRoot, 'apps/_shared/workbench/styles/workbench.css'), 'utf8') + tubeDesignerCss
    + '#app{height:820px;position:relative}.fixture-pane{height:150px;width:260px;overflow:auto}.fixture-content{height:800px}' });
  // Force actual overflow only for the interaction regression. Remove this
  // separate stylesheet before the production-layout visual QA screenshot.
  const interactionStyle = await page.addStyleTag({ content:
    '.tube-cad-sidebar>[role=tree]{min-height:850px}.tube-cad-property-section{padding-bottom:250px}'
    + '.tube-cad-curve-fields{max-height:65px;overflow:auto}' });
  await page.evaluate(async ({ scene, id }) => {
    const cad = await import('/src/apps/tube-designer/webpage/tubeCadEditor.mjs');
    const { handleDesignerAreaAction } = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const parts = await import('/src/apps/tube-designer/webpage/partsArea.mjs');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const mount = document.querySelector('#app');
    const view = { activeAreaId: 'nesting', pending: false, scene, tubeDesignerActivePartId: id,
      tubeDesignerActiveNestingPartId: id, tubeDesignerNestingSelectedPartIds: [id], tubeDesignerNestingSelectionKind: 'part' };
    const f = window.fixture = { view, cad, pending: 0, calls: [], failures: [], resources: [], renders: 0, patches: 0, viewports: [] };
    Object.defineProperty(f, 'state', { get() { return view.tubeDesignerCadEditor; } });
    const invoke = scope => async (method, payload, options) => {
      const record = { method, scope, payload: structuredClone(payload) }; f.calls.push(record);
      options?.onReport?.({ message: '真实原生 CAD 计算中', completed: 0, total: 1 });
      try {
        const result = await window.cadNativeRpc({ action: 'invoke', scope, method: method.replace(/^TubeDesigner\./, ''), payload });
        record.result = result;
        if (method === 'TubeDesigner.ApplyTubeCadDocument' && f.holdNextApply) {
          f.holdNextApply = false; f.nativeResponseWaiting = true;
          await new Promise(resolve => { f.releaseResponse = resolve; }); f.nativeResponseWaiting = false;
        }
        return result;
      } catch (error) { record.error = error.message; throw error; }
    };
    const resources = { async get(url, options) {
      const version = Number(options?.headers?.get('ICAX-Resource-Version') || 0);
      const result = await window.cadNativeRpc({ action: 'resource', payload: { url, version } });
      f.resources.push({ url, version, bytes: result.bytes });
      return new Response(Uint8Array.from(atob(result.base64), char => char.charCodeAt(0)));
    } };
    const context = f.context = { mount, sceneProxy: { invoke: invoke('scene'), resources }, productProxy: { invoke: invoke('product') } };
    // The buttons below are rendered by the production nesting viewport. The
    // surrounding app shell is isolated so it cannot touch desktop projects.
    const template = document.createElement('template');
    template.innerHTML = parts.renderNestingViewportOverlay(context, view);
    const productionEntries = template.content.querySelector('.tube-designer-cutting-scene-actions').outerHTML;
    mount.innerHTML = '<main id="main-workbench">' + productionEntries
      + '<aside class="cam-context-pane fixture-pane"><div class="fixture-content">左栏滚动</div></aside>'
      + '<aside class="cam-info-pane fixture-pane"><div class="fixture-content">右栏滚动</div></aside></main>';
    const ops = f.ops = {
      createPartDrawingViewport(options) { const viewport = createThreeViewport(options); f.viewport = viewport; f.viewports.push(viewport); return viewport; },
      appendProjectLog() {}, showNotice() {},
      renderProject() {
        f.renders++;
        if (cad.patchTubeCadEditor(view, mount)) f.patches++;
        else if (view.tubeDesignerCadEditor) mount.insertAdjacentHTML('beforeend', cad.renderTubeCadEditor(view));
        cad.attachTubeCadEditor(context, view, mount, ops);
      },
    };
    f.switchActivePart = id => {
      view.tubeDesignerActivePartId = id; view.tubeDesignerActiveNestingPartId = id;
      view.tubeDesignerNestingSelectedPartIds = [id];
      const template = document.createElement('template');
      template.innerHTML = parts.renderNestingViewportOverlay(context, view);
      mount.querySelector('.tube-designer-cutting-scene-actions').replaceWith(
        template.content.querySelector('.tube-designer-cutting-scene-actions'));
    };
    f.dispatch = async (action, target) => {
      if (!action?.startsWith('tube-designer-')) return;
      f.pending++;
      try { return await handleDesignerAreaAction(context, view, action, target, ops); }
      catch (error) { f.failures.push({ action, error: error.message }); }
      finally { f.pending--; }
    };
    mount.addEventListener('click', event => {
      const target = event.target.closest('[data-cam-action]');
      if (target && !target.disabled) void f.dispatch(target.dataset.camAction, target);
    });
    mount.addEventListener('change', event => void f.dispatch(event.target.dataset.camChangeAction, event.target));
    f.render = ops.renderProject;
  }, { scene, id });
  const idle = () => page.waitForFunction(() => !window.fixture.pending && !window.fixture.state?.loading
    && !window.fixture.state?.saving, null, { timeout: 180000 });
  const action = suffix => page.locator(`[data-cam-action="tube-designer-cad-${suffix}"]`);
  const select = nodeId => page.locator(`[data-cad-node="${nodeId}"][data-cam-action="tube-designer-cad-select"]`).click();
  const field = key => page.locator(`[data-cad-field="${key}"]`);
  const input = async (locator, value) => { await locator.fill(String(value)); await locator.dispatchEvent('change'); await idle(); };
  const draftReady = () => page.waitForFunction(() => {
    const f = window.fixture, state = f.state, preview = state?.preview;
    return !state?.previewPending && preview?.draftRevision === state?.draftRevision
      && f.viewport?.geometryObjects?.has(preview.geometry.url);
  }, null, { timeout: 180000 });
  const assertUI = async () => {
    assert.deepEqual(await page.evaluate(() => window.fixture.failures), []);
    assert.equal(await page.evaluate(() => window.fixture.state?.error || ''), '');
  };
  const nativeEquivalent = async (label, partEntityId = id) => {
    const published = await bridge.rpc({ action: 'geometry', payload: { partEntityId } });
    const document = await bridge.rpc({ action: 'cad-document-geometry', payload: { partEntityId } });
    assert.equal(published.valid, true); assert.equal(document.valid, true);
    assert.ok(document.directedDifferenceVolume <= Math.max(1e-3, published.volume * 1e-8), label);
    return { published, document };
  };

  // Click the production punch entry, edit a real STEP feature, and
  // continue interacting while the native save response is in flight.
  await page.locator('[data-cam-action="tube-designer-punch-open"]').click(); await idle(); await assertUI();
  assert.equal(await page.evaluate(() => window.fixture.state.mode), 'punch');
  await select(holeId); await idle();
  assert.equal(Number(await field('radius').inputValue()), originalRadius * 2);
  const originalFrame = (await get(id, { nodeId: holeId })).sketch.constructionFrame;
  const movedX = originalFrame.Location.X + 12;
  await input(field('radius'), (originalRadius + 1) * 2);
  await input(field('location-X'), movedX); await draftReady(); await assertUI();
  assert.deepEqual(await get(id), original, 'The real native draft leaves the complete persisted document unchanged');
  await page.locator('.tube-cad-properties details').filter({ has: page.locator('summary', { hasText: '精确轮廓曲线' }) }).locator('summary').first().click();
  await page.evaluate(() => { window.fixture.holdNextApply = true; });
  await action('save').click();
  await page.waitForFunction(() => window.fixture.nativeResponseWaiting, null, { timeout: 180000 });
  await page.evaluate(() => {
    const f = window.fixture, input = document.querySelector('[data-cad-field=radius]');
    input.focus({ preventScroll: true }); input.setSelectionRange(0, 1);
    const selectors = ['.cam-context-pane', '.cam-info-pane', '.tube-cad-sidebar', '.tube-cad-properties', '.tube-cad-curve-fields'];
    const positions = [185, 265, 245, 175, 45];
    const nodes = selectors.map(selector => document.querySelector(selector));
    nodes.forEach((node, index) => { node.scrollTop = positions[index]; });
    f.interaction = { input, canvas: document.querySelector('[data-tube-cad-viewport] .icax-three-viewport-canvas'),
      nodes, selectors, positions: nodes.map(node => node.scrollTop), value: input.value,
      selection: [input.selectionStart, input.selectionEnd], workbench: document.querySelector('#main-workbench'), probes: 0 };
    input.addEventListener('cad-listener-probe', () => { f.interaction.probes++; });
    f.releaseResponse();
  });
  await idle(); await assertUI();
  const retained = await page.evaluate(() => {
    const f = window.fixture, old = f.interaction, input = document.querySelector('[data-cad-field=radius]');
    input.dispatchEvent(new Event('cad-listener-probe'));
    return { input: input === old.input, focus: document.activeElement === input, value: input.value === old.value,
      selection: JSON.stringify([input.selectionStart, input.selectionEnd]) === JSON.stringify(old.selection),
      canvas: !!old.canvas?.isConnected && old.canvas === document.querySelector('[data-tube-cad-viewport] .icax-three-viewport-canvas'),
      workbench: old.workbench === document.querySelector('#main-workbench'),
      nodes: old.selectors.every((selector, index) => old.nodes[index] === document.querySelector(selector)),
      scroll: old.nodes.every((node, index) => Math.abs(node.scrollTop - old.positions[index]) < 1),
      positions: old.positions, listener: old.probes === 1 };
  });
  for (const key of ['input', 'focus', 'value', 'selection', 'canvas', 'workbench', 'nodes', 'scroll', 'listener']) assert.equal(retained[key], true, key);
  assert.ok(retained.positions.every(value => value > 0),
    'Both outer panes, editor sidebars and nested curve fields actually scrolled: ' + JSON.stringify(retained.positions));
  const afterEdit = await get(id, { nodeId: holeId });
  assert.equal(radius(afterEdit.sketch), originalRadius + 1);
  assert.equal(afterEdit.sketch.constructionFrame.Location.X, movedX);
  await page.waitForFunction(() => window.fixture.viewport?.sceneObjects.size > 0 && window.fixture.resources.length > 0,
    null, { timeout: 180000 });
  results.push({ case: 'production punch edit/save retains current focus, selection, listeners, GPU canvas and five scroll containers', retained,
    ...await nativeEquivalent('punch published geometry agrees with neutral document') });
  await page.screenshot({ path: resolve(output, 'punch-native-edited.png') });
  await page.getByRole('button', { name: '完成', exact: true }).click(); await idle();

  // The production punch entry writes another cut into that same document.
  await page.locator('[data-cam-action="tube-designer-punch-open"]').click(); await idle(); await assertUI();
  assert.equal(await page.evaluate(() => window.fixture.state.mode), 'punch');
  await input(page.locator('[data-cad-new-hole-field=diameter]'), 8);
  await input(page.locator('[data-cad-new-hole-field=station]'), 60);
  await input(page.locator('[data-cad-new-hole-field=offset]'), 15);
  await action('add-hole').click(); await idle(); await assertUI();
  const newHoleId = await page.evaluate(() => window.fixture.state.selected);
  await draftReady(); await assertUI();
  const beforePunchSave = await get(id);
  assert.equal(beforePunchSave.revision, afterEdit.revision);
  assert.equal(beforePunchSave.geometry.SolidNodes.some(item => item.ID === newHoleId), false,
    'Native punch draft preview is visible without publishing the new cut');
  await action('save').click(); await idle(); await assertUI();
  const afterPunch = await get(id, { nodeId: holeId });
  assert.equal(afterPunch.documentResourceId, original.documentResourceId);
  assert.equal(afterPunch.sketch.constructionFrame.Location.X, movedX);
  assert.equal(radius(afterPunch.sketch), originalRadius + 1);
  assert.ok(afterPunch.geometry.SolidNodes.some(item => item.ID === newHoleId));
  results.push({ case: 'production punch entry/add/save retains imported geometry and prior diameter/position edits', newHoleId,
    ...await nativeEquivalent('punch geometry') });
  await page.screenshot({ path: resolve(output, 'punch-native-saved.png') });
  await page.getByRole('button', { name: '完成', exact: true }).click(); await idle();

  const file = 'unified-cad-real-browser.ictd';
  await bridge.rpc({ action: 'save', payload: { file } });
  const before = await get(id);
  const reopened = await bridge.rpc({ action: 'open', payload: { file } });
  assert.deepEqual(await get(id), before);
  await page.evaluate(scene => { window.fixture.view.scene = scene; }, reopened.snapshot);
  await page.locator('[data-cam-action="tube-designer-punch-open"]').click(); await idle(); await assertUI();
  await select(holeId); await idle();
  assert.equal(Number(await field('radius').inputValue()), (originalRadius + 1) * 2);
  await input(field('radius'), (originalRadius + 1.5) * 2); await draftReady(); await assertUI();
  await action('save').click(); await idle(); await assertUI();
  assert.equal(radius((await get(id, { nodeId: holeId })).sketch), originalRadius + 1.5);
  results.push({ case: 'fresh native project reopen -> production punch entry -> continued edit/save', ...await nativeEquivalent('reopen continuation') });
  await page.getByRole('button', { name: '完成', exact: true }).click(); await idle();

  // A normal generated circle consists of four exact circular arcs. Edit its
  // diameter through the real punch controls and retain every original range.
  await page.evaluate(id => window.fixture.switchActivePart(id), authored.partEntityId);
  await page.locator('[data-cam-action="tube-designer-punch-open"]').click(); await idle(); await assertUI();
  await select(authoredCut.ID); await idle();
  assert.ok(Math.abs(Number(await field('radius').inputValue()) - 10) < 1e-8);
  await input(field('radius'), 12); await draftReady(); await assertUI();
  assert.equal((await get(authored.partEntityId)).revision, authoredRead.revision,
    'Four-arc diameter preview remains a draft');
  await action('save').click(); await idle(); await assertUI();
  const authoredEdited = await get(authored.partEntityId);
  const oldCurves = authoredCut.Data.value.Section.Boundaries[0].Curves;
  const newCurves = authoredEdited.geometry.SolidNodes.find(item => item.ID === authoredCut.ID).Data.value.Section.Boundaries[0].Curves;
  assert.equal(newCurves.length, 4);
  assert.deepEqual(newCurves.map(item => item.Segment.Range), oldCurves.map(item => item.Segment.Range));
  assert.deepEqual(newCurves.map(item => item.Segment.Curve.value.Placement), oldCurves.map(item => item.Segment.Curve.value.Placement));
  assert.ok(newCurves.every(item => Math.abs(item.Segment.Curve.value.Radius - 6) < 1e-9));
  const authoredGeometry = await nativeEquivalent('authored four-arc diameter', authored.partEntityId);
  const expectedAuthoredVolume = (80 * 60 - 74 * 54) * 400 - Math.PI * 36 * 3;
  assert.ok(Math.abs(authoredGeometry.published.volume - expectedAuthoredVolume) < 1e-3);
  results.push({ case: 'ordinary punch creation -> actual diameter edit/save changes all four exact arcs while preserving ranges/placements',
    id: authored.partEntityId, exactRanges: newCurves.map(item => item.Segment.Range), diameter: 12,
    expectedVolume: expectedAuthoredVolume, ...authoredGeometry });
  await page.screenshot({ path: resolve(output, 'authored-four-arc-diameter.png') });
  await interactionStyle.evaluate(node => node.remove());
  await page.evaluate(() => {
    for (const node of document.querySelectorAll('#app, #app *')) { node.scrollTop = 0; node.scrollLeft = 0; }
    window.scrollTo(0, 0);
  });
  await page.screenshot({ path: resolve(output, 'production-layout-authored-four-arc-diameter.png') });
  const finalPreview = await page.evaluate(() => ({ key: window.fixture.state.previewResourceKey,
    geometry: window.fixture.state.preview?.geometry }));
  assert.ok(finalPreview.geometry?.url);
  await page.getByRole('button', { name: '完成', exact: true }).click(); await idle();
  await page.waitForFunction(key => window.fixture.calls.some(call =>
    call.method === 'TubeDesigner.ReleaseTubeCadDocumentPreview'
      && call.payload.previewResourceKey === key && call.result?.released), finalPreview.key, { timeout: 180000 });
  await assert.rejects(bridge.rpc({ action: 'resource', payload: finalPreview.geometry }),
    'The last actual UI preview session releases its mesh after closing');
  assert.deepEqual(errors, []); assert.deepEqual(external, []);
  const ui = await page.evaluate(() => ({ calls: window.fixture.calls, resources: window.fixture.resources,
    patches: window.fixture.patches, renders: window.fixture.renders, failures: window.fixture.failures }));
  assert.ok(ui.calls.filter(call => call.method === 'TubeDesigner.ApplyTubeCadDocument').length >= 4);
  assert.ok(ui.calls.filter(call => call.method === 'TubeDesigner.PreviewTubeCadDocument' && call.result?.geometry?.url).length >= 2,
    'Actual punch changes show independently evaluated native draft GPU resources');
  assert.ok(ui.calls.some(call => call.method === 'TubeDesigner.ReleaseTubeCadDocumentPreview' && call.result?.released),
    'Closing an editor releases its real preview session resources');
  assert.equal(ui.calls.filter(call => /\.(ApplyPartDrawing|ApplyPunchWizard|SavePartSketch|GetPartDrawing|RecoverImportedPartDrawing)$/.test(call.method)).length, 0);
  results.push({ case: 'all actual punch saves use the shared neutral CAD scene API', ...ui });
  console.log('Punch CAD real browser/native acceptance passed: production punch entry, events, resources, retained interaction, project reopen and continued editing.');
} catch (error) { failure = error; throw error; }
finally {
  if (failure && page) await page.screenshot({ path: resolve(output, 'failure.png') }).catch(() => {});
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: !failure, error: failure?.stack,
    buildPhase: process.env.ICAX_CAD_BUILD_PHASE || 'unspecified', completeProductAcceptanceClaim: false,
    tubeDesignerDllSha256: bridge.dllHash, importerDllSha256: bridge.importerHash,
    browserUIExercised: true, editorCoverage: ['punch'], twoAndThreeDimensionalWorkspacesCoveredBy: 'TubeDesignerCadExistingWorkbenches.native.browser.mjs',
    nativeBusinessResponsesMocked: false, filePickerExercised: false,
    standaloneDesktopExercised: false, servedProductionSources: Object.fromEntries(servedSources),
    results, errors, externalRequests: external, requests: bridge.requests }, null, 2));
  await browser?.close(); await bridge.close();
}
