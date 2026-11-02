// Production creation and existing-part entry events -> the same sketch/drawing
// workbenches -> real scene SDO, SDK/GPU resources and a fresh .ictd repository.
// This isolated browser host does not operate a desktop host or file picker.
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { resolve, sep } from 'node:path';
import { tubeDesignerCss } from '../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs';
import { planarCadDisplayCurve } from '../../apps/_shared/workbench/sketch/planarCurveDisplay.mjs';
import { startSideSketchNativeBridge, repositoryRoot } from './fixtures/sideSketchNativeBridge.mjs';

const sourceRoot = resolve(repositoryRoot, 'src');
const caseSelection = process.env.ICAX_CAD_WORKBENCH_CASES || 'all';
assert.ok(['all', 'side-boundaries', 'seam-preview', 'late-preview'].includes(caseSelection), 'Select a declared actual-workbench case group');
const sideBoundaryOnly = caseSelection !== 'all', seamPreviewOnly = ['seam-preview', 'late-preview'].includes(caseSelection);
const output = resolve(repositoryRoot, 'output/tests/cad-existing-workbenches-native-browser' + (sideBoundaryOnly ? '-' + caseSelection : ''));
const buildPhase = process.env.ICAX_CAD_ACCEPTANCE_BUILD_PHASE || 'final';
mkdirSync(output, { recursive: true });
const bridge = await startSideSketchNativeBridge(output);
const checks = [], errors = [], external = [], assets = new Map(), loadedModules = [];
const recordCheck = check => { checks.push(check); console.log(`PASS ${checks.length}: ${check.case}`); };
let browser, page, failure;
const parts = scene => scene.tubeDesigner?.nestingGroups?.flatMap(group => group.parts ?? []) ?? [];
const get = (id, authoringView) => bridge.invoke('GetTubeCadDocument', { partEntityId: id, ...(authoringView ? { authoringView } : {}) });
const near = (actual, expected, title) => assert.ok(Math.abs(actual - expected) < 1e-6, `${title}: ${actual} versus ${expected}`);
const sameDocument = (before, after) => {
  assert.equal(after.partEntityId, before.partEntityId);
  assert.equal(after.documentResourceId, before.documentResourceId, 'Editing preserves the document resource identity');
  assert.notEqual(after.revision, before.revision, 'A real CAD apply advances the authoritative revision');
  assert.ok(after.resourceVersion > before.resourceVersion);
};
const solids = document => new Map(document.geometry.SolidNodes.map(node => [node.ID, node]));
const preserve = (before, after, ids) => {
  const old = solids(before), current = solids(after);
  for (const id of ids) assert.deepEqual(current.get(id), old.get(id), `Unedited exact node ${id} survives another workbench`);
};
const roundedBase = document => {
  const base = solids(document).get(document.geometry.BaseNodeID);
  assert.equal(base.Data.type, 'SExtrudedRegionNode');
  const loops = base.Data.value.Section.Boundaries;
  assert.equal(loops.length, 2);
  const evidence = loops.map(loop => {
    const curves = loop.Curves.map(planarCadDisplayCurve);
    for (let i = 0; i < curves.length; i++) {
      const end = curves[i].points.at(-1), start = curves[(i + 1) % curves.length].points[0];
      near(end[0], start[0], 'Exact corner/line X closure');
      near(end[1], start[1], 'Exact corner/line Y closure');
    }
    const points = curves.flatMap(curve => curve.points);
    const bounds = [0, 1].map(axis => [Math.min(...points.map(p => p[axis])), Math.max(...points.map(p => p[axis]))]);
    const radii = loop.Curves.filter(curve => curve.Segment.Curve.type === 'Circle2').map(curve => curve.Segment.Curve.value.Radius);
    assert.equal(radii.length, 4, 'A rounded rectangular loop has four exact arcs');
    return { curves: curves.length, radii, width: bounds[0][1] - bounds[0][0], depth: bounds[1][1] - bounds[1][0] };
  });
  const outer = evidence.find(loop => Math.abs(loop.width - 60) < 1e-6);
  assert.ok(outer, JSON.stringify(evidence)); near(outer.depth, 40, 'Rounded tube depth');
  outer.radii.forEach(radius => near(radius, 3, 'Rounded tube outer radius'));
  return evidence;
};

try {
  if (process.env.ICAX_EXISTING_CAD_EXPECTED_DLL_SHA256)
    assert.equal(bridge.dllHash.toLowerCase(), process.env.ICAX_EXISTING_CAD_EXPECTED_DLL_SHA256.toLowerCase(), 'Run the new authoring DLL, never an older acceptance result');
  const loaded = JSON.parse(execFileSync('powershell.exe', ['-NoProfile', '-Command',
    `(Get-Process -Id ${bridge.processId}).Modules | Where-Object { $_.ModuleName -in @('TubeDesigner.dll','OpenCascadeResourceImport.dll') } | Select-Object ModuleName,FileName | ConvertTo-Json -Compress`],
  { windowsHide: true, encoding: 'utf8' }));
  for (const module of Array.isArray(loaded) ? loaded : [loaded]) {
    const sha256 = createHash('sha256').update(readFileSync(module.FileName)).digest('hex');
    loadedModules.push({ name: module.ModuleName, path: module.FileName, sha256 });
    assert.equal(sha256, module.ModuleName === 'TubeDesigner.dll' ? bridge.dllHash : bridge.importerHash,
      'The executing process loads the exact copied native module');
  }
  assert.equal(loadedModules.length, 2, 'Both authoring and geometry service DLLs are loaded in the real bridge');
  const profiles = [];
  for (const id of ['rect', 'round']) {
    const descriptor = JSON.parse(readFileSync(resolve(sourceRoot, `apps/tube-designer/templates/profile/${id}/profile.json`), 'utf8'));
    const parameters = Object.fromEntries(descriptor.parameters.map(parameter => [parameter.key, parameter.defaultValue]));
    const evaluated = await bridge.invoke('EvaluateProfilePackage', { profileRef: { scope: 'system', id }, parameters }, 'product');
    profiles.push({ id, name: descriptor.displayName, descriptor, defaultParameters: parameters, previewProfile: evaluated.profile });
  }
  const scene = await bridge.invoke('List'); assert.equal(parts(scene).length, 0);
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE
    || 'file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs');
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
  page = await browser.newPage({ viewport: { width: 1660, height: 1020 } });
  page.setDefaultTimeout(45000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('dialog', dialog => dialog.accept());
  await page.exposeFunction('cadWorkbenchNativeRpc', bridge.rpc);
  await page.route('**/*', route => {
    const url = new URL(route.request().url());
    if (url.origin !== 'http://cad-workbenches-native.test') { external.push(url.href); return route.abort(); }
    if (url.pathname === '/') return route.fulfill({ contentType: 'text/html',
      body: '<!doctype html><html lang="zh-CN"><meta charset="utf-8"><body><div id="app" class="tube-designer-workspace"></div></body></html>' });
    const file = resolve(sourceRoot, url.pathname.replace(/^\/src\//, ''));
    if (!url.pathname.startsWith('/src/') || !file.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(file)) return route.abort();
    const content = readFileSync(file);
    const hash = createHash('sha256').update(content).digest('hex');
    if (assets.has(file)) assert.equal(assets.get(file), hash, 'Production source changed during this acceptance run');
    assets.set(file, hash);
    return route.fulfill({ contentType: 'text/javascript', body: content });
  });
  await page.goto('http://cad-workbenches-native.test');
  await page.addStyleTag({ content: '*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif}'
    + readFileSync(resolve(sourceRoot, 'apps/_shared/workbench/styles/workbench.css'), 'utf8') + tubeDesignerCss
    + '#app{height:1020px;position:relative}.fixture-pane{height:150px;width:260px;overflow:auto}.fixture-content{height:900px}' });
  await page.evaluate(async ({ profiles, scene }) => {
    const sketch = await import('/src/apps/tube-designer/webpage/sketchArea.mjs');
    const standard = await import('/src/apps/tube-designer/webpage/nestingStandardPart.mjs');
    const drawing = await import('/src/apps/tube-designer/webpage/partDrawing.mjs');
    const drawingDom = await import('/src/apps/tube-designer/webpage/partDrawingDom.mjs');
    const drawingPreview = await import('/src/apps/tube-designer/webpage/partDrawingPreview.mjs');
    const sidePreview = await import('/src/apps/tube-designer/webpage/sideSketchPreview.mjs');
    const parts = await import('/src/apps/tube-designer/webpage/partsArea.mjs');
    const actions = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const { getRibbonDefinition } = await import('/src/apps/tube-designer/webpage/ribbonDefinition.mjs');
    const { renderDesignerOperationOverlay } = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const mount = document.querySelector('#app');
    const view = { activeAreaId: 'nesting', pending: false, scene, tubeDesignerSystemProfiles: profiles,
      tubeDesignerSelectedProfileId: 'system:rect', tubeDesignerTemplateProfiles: [], tubeDesignerUserData: { profiles: [] },
      tubeDesignerNestingSelectionKind: 'part', tubeDesignerNestingSelectedPartIds: [] };
    const f = window.fixture = { view, sketch, drawing, sidePreview, pending: 0, calls: [], resources: [], failures: [], patches: 0, renders: 0, viewports: [] };
    Object.defineProperty(f, 'side', { get() { return view.tubeDesignerSketch; } });
    Object.defineProperty(f, 'draft', { get() { return f.side?.sideByPart?.[f.side.targetPartId]; } });
    Object.defineProperty(f, 'draw', { get() { return view.tubeDesignerPartDrawing?.state; } });
    const invoke = scope => async (method, payload, options) => {
      const record = { method, scope, payload: structuredClone(payload) }; f.calls.push(record);
      options?.onReport?.({ message: '真实原生 CAD 工作区计算中', completed: 0, total: 1 });
      try {
        const result = await window.cadWorkbenchNativeRpc({ action: 'invoke', scope, method: method.replace(/^TubeDesigner\./, ''), payload });
        record.result = result;
        if (method === 'TubeDesigner.PreviewTubeCadDocument' && f.holdNextPreview) {
          f.holdNextPreview = false; f.nativeResponseWaiting = true;
          await new Promise(resolve => { f.releaseResponse = resolve; }); f.nativeResponseWaiting = false;
        }
        return result;
      } catch (error) { record.error = error.message; throw error; }
    };
    const resources = { async get(url, options) {
      const version = Number(options?.headers?.get('ICAX-Resource-Version') || 0);
      const result = await window.cadWorkbenchNativeRpc({ action: 'resource', payload: { url, version } });
      f.resources.push({ url, version, bytes: result.bytes });
      return new Response(Uint8Array.from(atob(result.base64), char => char.charCodeAt(0)));
    } };
    const context = f.context = { mount, sceneProxy: { invoke: invoke('scene'), resources }, productProxy: { invoke: invoke('product') },
      actions: { async selectRibbonTab(id) { f.ribbonTab = id; } } };
    const ribbon = getRibbonDefinition().tabs.flatMap(tab => tab.groups.flatMap(group => group.commands));
    const newButtons = ['nesting.draw-2d-part', 'nesting.draw-part'].map(id => {
      const command = ribbon.find(command => command.id === id); if (!command) throw new Error('Missing production creation command ' + id);
      return `<button type="button" data-fixture-ribbon-command="${id}">${command.title}</button>`;
    }).join('');
    const entries = () => {
      const template = document.createElement('template'); template.innerHTML = parts.renderNestingViewportOverlay(context, view);
      return template.content.querySelector('.tube-designer-cutting-scene-actions')?.outerHTML ?? '';
    };
    const ops = f.ops = { appendProjectLog() {}, showNotice() {},
      createSideSketchViewport(options) { const viewport = createThreeViewport(options); f.sideViewport = viewport; f.viewports.push(viewport); return viewport; },
      createPartDrawingViewport(options) { const viewport = createThreeViewport(options); f.drawingViewport = viewport; f.viewports.push(viewport); return viewport; },
      renderProject() {
        const html = standard.renderNestingStandardPartDialog(view) + sketch.renderSectionSketchDialog(context, view)
          + drawing.renderPartDrawingDialog(view) + renderDesignerOperationOverlay(context, view);
        if (sketch.patchSketchDialogDom(context, view, mount, ops)
          || drawingDom.patchPartDrawingDom(view, mount, html)
          || standard.patchNestingStandardPartDom(view, mount, html, context.sceneProxy)) f.patches++;
        else {
          f.renders++;
          mount.innerHTML = '<main id="main-workbench">' + newButtons + entries()
            + '<aside class="cam-context-pane fixture-pane"><div class="fixture-content">左栏</div></aside>'
            + '<aside class="cam-info-pane fixture-pane"><div class="fixture-content">右栏</div></aside></main>' + html;
          standard.rememberNestingStandardPartDom(view, mount, context.sceneProxy);
          sketch.rememberSketchDialogDom(view, mount, context.sceneProxy);
          drawingDom.rememberPartDrawingDom(view, mount);
        }
        sketch.attachSketchAreaInteractions(context, view, mount, ops);
        drawingPreview.attachPartDrawingPreview(context, view, mount, ops);
        drawing.attachPartDrawingEditor(context, view, mount, ops);
      },
    };
    f.selectPart = id => {
      view.tubeDesignerActivePartId = id; view.tubeDesignerActiveNestingPartId = id;
      view.tubeDesignerNestingSelectedPartIds = [id]; view.tubeDesignerNestingSelectionKind = 'part';
      ops.renderProject();
    };
    f.dispatch = async (action, target, ribbon = false) => {
      if (!action) return; f.pending++;
      try { return await (ribbon ? actions.handleDesignerRibbonCommand(context, view, action, ops)
        : actions.handleDesignerAreaAction(context, view, action, target, ops)); }
      catch (error) { f.failures.push({ action, error: error.message }); }
      finally { f.pending--; }
    };
    mount.addEventListener('click', event => {
      const target = event.target.closest('[data-cam-action],[data-fixture-ribbon-command]');
      if (target && !target.disabled) void f.dispatch(target.dataset.camAction ?? target.dataset.fixtureRibbonCommand,
        target, target.hasAttribute('data-fixture-ribbon-command'));
    });
    mount.addEventListener('change', event => void f.dispatch(event.target.dataset.camChangeAction, event.target));
    f.render = ops.renderProject; ops.renderProject();
  }, { profiles, scene });

  const idle = () => page.waitForFunction(() => {
    const f = window.fixture;
    return !f.pending && !f.view.pending && !f.side?.sidePreviewPending && !f.side?.sideSavePending
      && !f.view.tubeDesignerPartDrawing?.loading && !f.draw?.previewPending;
  }, null, { timeout: 180000 });
  const ui = async () => {
    assert.deepEqual(await page.evaluate(() => window.fixture.failures), []);
    assert.equal(await page.locator('.tube-cad-backdrop').count(), 0, 'Existing 2D/3D enter the production drawing pages');
    assert.equal(await page.evaluate(() => window.fixture.view.error || window.fixture.side?.sideSaveError
      || window.fixture.side?.sidePreviewError || window.fixture.draw?.error || ''), '');
  };
  const input = async (locator, value) => { await locator.fill(String(value)); await locator.press('Tab'); await idle(); await ui(); };
  const sketchCommand = name => page.locator(`[data-sketch-command="sketch.${name}"]`);
  const drawCommand = name => page.locator(`[data-drawing-command="${name}"]`);
  const drawAction = name => page.locator(`[data-cam-action="tube-designer-drawing-${name}"]`).first();
  const drawField = name => page.locator(`[data-tube-designer-punch-field="${name}"]`);
  const readySide = async () => {
    await idle(); await ui();
    assert.equal(await page.evaluate(async () => window.fixture.sidePreview.waitForSideSketchPreview(document.querySelector('#app'))), true);
    await page.locator('[data-side-sketch-preview-ready=true]').waitFor();
    assert.ok(await page.evaluate(() => {
      const f = window.fixture, reference = f.side.sidePreview.geometry ?? f.side.sidePreview.baseGeometry;
      return f.sideViewport.geometryObjects.get(reference.url)?.getAttribute('position')?.count;
    }) > 0, 'The actual displayed finished native part has SDK/GPU vertices');
  };
  const sideFinishedFits = async label => {
    const fit = await page.evaluate(() => {
      const viewport = window.fixture.sideViewport, object = viewport.sceneObjects.get('drawing:finished');
      const positions = object?.geometry?.getAttribute('position');
      if (!positions?.count) return null;
      object.updateWorldMatrix(true, false);
      viewport.camera.updateMatrixWorld(true);
      const point = viewport.camera.position.clone(), min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
      for (let index = 0; index < positions.count; index++) {
        point.set(positions.getX(index), positions.getY(index), positions.getZ(index)).applyMatrix4(object.matrixWorld).project(viewport.camera);
        for (const [axis, value] of [point.x, point.y, point.z].entries()) {
          min[axis] = Math.min(min[axis], value); max[axis] = Math.max(max[axis], value);
        }
      }
      const canvas = viewport.renderer.domElement;
      return { vertices: positions.count, ndc: { min, max }, width: canvas.clientWidth, height: canvas.clientHeight };
    });
    assert.ok(fit?.vertices > 0 && fit.width > 0 && fit.height > 0, `${label}: the actual finished mesh and canvas exist`);
    for (let axis = 0; axis < 3; axis++) {
      assert.ok(Number.isFinite(fit.ndc.min[axis]) && Number.isFinite(fit.ndc.max[axis])
        && fit.ndc.min[axis] >= -1 - 1e-6 && fit.ndc.max[axis] <= 1 + 1e-6,
      `${label}: every actual finished-mesh vertex remains in the displayed viewport (axis ${axis}: ${fit.ndc.min[axis]}..${fit.ndc.max[axis]})`);
    }
    return fit;
  };
  const readyDrawing = async () => {
    await page.waitForFunction(() => {
      const f = window.fixture, s = f.draw;
      if (s?.error || s?.previewRenderError) return true;
      return s && !f.pending && !f.view.pending && !s.previewPending && !s.previewRenderPending
        && s.preview?.revision === s.revision && document.querySelector('[data-part-drawing-preview-ready=true]');
    }, null, { timeout: 180000 });
    await ui();
    assert.equal(await page.evaluate(() => window.fixture.draw.previewRenderError || ''), '', 'Native resources render in the original viewport');
    assert.ok(await page.evaluate(() => window.fixture.drawingViewport?.geometryObjects.size) > 0);
  };
  const point = async (u, s) => {
    const screen = await page.evaluate(({ u, s }) => {
      const svg = document.querySelector('[data-tube-sketch-canvas]'), d = svg.dataset;
      const p = svg.createSVGPoint();
      p.x = Number(d.sketchCanvasLeft) + (u - Number(d.sketchXMin)) / (Number(d.sketchXMax) - Number(d.sketchXMin)) * (Number(d.sketchCanvasRight) - Number(d.sketchCanvasLeft));
      p.y = Number(d.sketchCanvasBottom) - (s - Number(d.sketchYMin)) / (Number(d.sketchYMax) - Number(d.sketchYMin)) * (Number(d.sketchCanvasBottom) - Number(d.sketchCanvasTop));
      const screen = p.matrixTransform(svg.getScreenCTM()); return { x: screen.x, y: screen.y };
    }, { u, s });
    await page.mouse.click(screen.x, screen.y); await idle();
  };
  const circle = async (u, s, radius) => {
    await sketchCommand('circle').click(); await point(u, s); await point(u + radius, s);
    const diameter = page.locator('[data-sketch-field=diameter]');
    // Mouse coordinates are subpixel values while the existing page rounds the
    // displayed diameter. Ensure a real change event commits the exact number.
    if (Number(await diameter.inputValue()) === radius * 2) await input(diameter, radius * 2 + 1);
    await input(diameter, radius * 2);
    return page.evaluate(() => window.fixture.draft.entities.at(-1));
  };
  const polygon = async points => {
    await sketchCommand('polyline').click(); for (const [u, s] of [...points, points[0]]) await point(u, s);
    const entity = await page.evaluate(() => window.fixture.draft.entities.at(-1));
    assert.equal(entity.kind, 'polyline');
    assert.equal(entity.points.length, points.length, 'Every mouse vertex is retained');
    assert.equal(entity.closed, true, 'The actual mouse command closes the path'); return entity;
  };
  const choosePart = id => page.evaluate(id => window.fixture.selectPart(id), id);
  const openSide = async id => {
    await choosePart(id); await page.locator('[data-cam-action=tube-designer-part-open-sketch]').click();
    await readySide();
    assert.equal(await page.locator('.tube-section-sketch-dialog').count(), 1);
    assert.equal(await page.locator('.tube-section-sketch-toolbar').count(), 1);
    assert.equal(await page.locator('[data-tube-sketch-canvas]').count(), 1);
    assert.equal(await page.evaluate(() => window.fixture.side.sideCadSession.partEntityId), id);
  };
  const openDrawing = async id => {
    await choosePart(id); await page.locator('[data-cam-action=tube-designer-drawing-open]').click();
    await readyDrawing(); assert.equal(await page.locator('.td-draw-workbench').count(), 1);
    assert.equal(await page.evaluate(() => window.fixture.draw.partId), id);
  };
  const equivalent = async id => {
    const body = await bridge.rpc({ action: 'geometry', payload: { partEntityId: id } });
    const document = await bridge.rpc({ action: 'cad-document-geometry', payload: { partEntityId: id } });
    assert.equal(body.valid, true); assert.equal(document.valid, true);
    assert.ok(document.directedDifferenceVolume <= Math.max(1e-3, body.volume * 1e-8));
    return { body, document };
  };
  const saveOpen = async (id, label) => {
    // The test bridge intentionally accepts a filename only and owns its
    // artifact directory. It cannot read or overwrite an arbitrary project.
    const before = await get(id), file = 'existing-workbench-' + label + '.ictd';
    const saved = await bridge.rpc({ action: 'save', payload: { file } }); assert.equal(saved.saved, true);
    const reopened = await bridge.rpc({ action: 'open', payload: { file } }); assert.equal(reopened.opened, true);
    const after = await get(id); assert.deepEqual(after.geometry, before.geometry);
    assert.equal(after.documentResourceId, before.documentResourceId); assert.equal(after.revision, before.revision);
    await page.evaluate(scene => { window.fixture.view.scene = scene; window.fixture.render(); }, reopened.snapshot);
    return { file: saved.file, documentResourceId: after.documentResourceId, revision: after.revision, ...await equivalent(id) };
  };
  const screenshot = name => page.screenshot({ path: resolve(output, name + '.png') });
  const toolbar = selector => page.locator(selector).evaluateAll(nodes => nodes.map(node => node.dataset.sketchCommand ?? node.dataset.drawingCommand));
  const testInteraction = async (mode, start) => {
    const style = await page.addStyleTag({ content: mode === 'side'
      ? '.tube-section-sketch-body>aside{overflow:auto;max-height:420px}.tube-sketch-preview-panel{padding-bottom:700px}.tube-sketch-property-panel{padding-bottom:700px}.tube-sketch-property-body{height:125px;overflow:auto;padding-bottom:500px}'
      : '.td-draw-tree>[role=tree]::after{content:"";display:block;height:950px}.td-draw-property-scroll>.tube-designer-punch-controls{padding-bottom:600px}.td-draw-property-scroll .td-draw-properties:last-of-type{height:140px;overflow:auto;padding-bottom:500px}' });
    await page.evaluate(() => { window.fixture.holdNextPreview = true; }); await start();
    await page.waitForFunction(() => window.fixture.nativeResponseWaiting, null, { timeout: 180000 });
    if (mode === 'side') await sketchCommand('circle').click(); else await drawCommand('main').click();
    const field = mode === 'side' ? '[data-tube-sketch-numeric-field=x]' : '[data-drawing-field=name]';
    await page.locator(field).fill(mode === 'side' ? '42.321' : '继续输入的三维零件名');
    await page.evaluate(({ mode, field }) => {
      const f = window.fixture, input = document.querySelector(field);
      const selectors = mode === 'side'
        ? ['.cam-context-pane', '.cam-info-pane', '.tube-section-sketch-body>aside:first-of-type', '.tube-section-sketch-body>aside:last-of-type', '.tube-sketch-property-body']
        : ['.cam-context-pane', '.cam-info-pane', '.td-draw-tree>[role=tree]', '.td-draw-property-scroll', '.td-draw-property-scroll .td-draw-properties:last-of-type'];
      const nodes = selectors.map(selector => document.querySelector(selector));
      input.focus({ preventScroll: true }); input.setSelectionRange(1, 4);
      nodes.forEach((node, index) => { node.scrollTop = [185, 265, 145, 175, 70][index]; });
      f.interaction = { input, field, canvas: document.querySelector(mode === 'side' ? '[data-tube-sketch-canvas]' : '[data-part-drawing-viewport] .icax-three-viewport-canvas'),
        gpu: document.querySelector(mode === 'side' ? '[data-side-sketch-viewport] .icax-three-viewport-canvas' : '[data-part-drawing-viewport] .icax-three-viewport-canvas'),
        canvasSelector: mode === 'side' ? '[data-tube-sketch-canvas]' : '[data-part-drawing-viewport] .icax-three-viewport-canvas',
        gpuSelector: mode === 'side' ? '[data-side-sketch-viewport] .icax-three-viewport-canvas' : '[data-part-drawing-viewport] .icax-three-viewport-canvas',
        nodes, selectors, positions: nodes.map(node => node.scrollTop), value: input.value, selection: [input.selectionStart, input.selectionEnd], probes: 0 };
      input.addEventListener('cad-current-listener-probe', () => f.interaction.probes++);
      f.releaseResponse();
    }, { mode, field });
    await idle(); if (mode === 'side') await readySide(); else await readyDrawing();
    const retained = await page.evaluate(() => {
      const old = window.fixture.interaction, input = document.querySelector(old.field); input.dispatchEvent(new Event('cad-current-listener-probe'));
      return { input: input === old.input, focus: document.activeElement === input, value: input.value === old.value,
        selection: JSON.stringify([input.selectionStart, input.selectionEnd]) === JSON.stringify(old.selection),
        canvas: document.querySelector(old.canvasSelector) === old.canvas, gpu: document.querySelector(old.gpuSelector) === old.gpu,
        nodes: old.selectors.every((selector, i) => document.querySelector(selector) === old.nodes[i]),
        scroll: old.nodes.every((node, i) => Math.abs(node.scrollTop - old.positions[i]) < 1),
        listener: old.probes === 1, positions: old.positions };
    });
    for (const key of ['input', 'focus', 'value', 'selection', 'canvas', 'gpu', 'nodes', 'scroll', 'listener']) assert.equal(retained[key], true, `${mode} ${key}`);
    assert.ok(retained.positions.every(position => position > 0), 'All five containers actually scroll: ' + JSON.stringify(retained));
    await style.evaluate(node => node.remove());
    await page.evaluate(() => document.querySelectorAll('*').forEach(node => { if (node.scrollTop) node.scrollTop = 0; }));
    if (mode === 'side') await page.locator('[data-tube-sketch-canvas]').press('Escape');
    return retained;
  };
  const addBranch = async (station, diameter) => {
    await drawCommand('branch').click(); await idle();
    await page.locator('[data-drawing-section=branch][data-cam-change-action=tube-designer-drawing-section-select]').selectOption('system:round');
    await idle(); await input(page.locator('[data-drawing-section=branch][data-drawing-parameter=width]'), diameter);
    await input(drawField('station'), station); await readyDrawing();
    return page.evaluate(() => structuredClone(window.fixture.draw.features.at(-1)));
  };
  const pickStartTopEdge = async () => {
    const selected = await page.evaluate(async () => {
      const f = window.fixture, topology = f.draw.preview.topology;
      const near = (a, b) => Math.abs(a - b) < 1e-6;
      const start = Math.min(...topology.edges.flatMap(edge => edge.points.map(p => p[0])));
      const top = Math.max(...topology.edges.flatMap(edge => edge.points.map(p => p[2])));
      const edge = topology.edges.find(edge => edge.points.every(p => near(p[0], start) && near(p[2], top))
        && Math.max(...edge.points.map(p => p[1])) - Math.min(...edge.points.map(p => p[1])) > 5);
      if (!edge) throw new Error('No exact outer start-top edge in the native topology');
      const byId = new Map(topology.edges.map(edge => [edge.id, edge]));
      const face = topology.faces.find(face => face.edgeIds.includes(edge.id)
        && face.edgeIds.every(id => byId.get(id)?.points.every(p => near(p[0], start))));
      if (!face) throw new Error('No exact cut-wall reference face for the start-top edge');
      // Rounded stock has a complete tangent outer wire. Derive that wire from
      // real endpoint connectivity, then select every edge with real Ctrl picks.
      const wire = [edge], sharesEndpoint = (a, b) => [a.points[0], a.points.at(-1)].some(p =>
        [b.points[0], b.points.at(-1)].some(q => p.every((v, i) => near(v, q[i]))));
      for (let changed = true; changed;) {
        changed = false;
        for (const id of face.edgeIds) {
          const candidate = byId.get(id);
          if (candidate && !wire.includes(candidate) && wire.some(current => sharesEndpoint(current, candidate))) {
            wire.push(candidate); changed = true;
          }
        }
      }
      return { edge, face, wire };
    });
    // The SDK maps a real native topology resource into the actual camera.
    // Only its screen point is read here; the browser mouse invokes production
    // ray picking and onPick. No topology selection state is manufactured.
    await page.locator('[data-cam-change-action=tube-designer-drawing-selection-mode]').selectOption('edge');
    await readyDrawing();
    await page.locator('[data-part-drawing-viewport] [data-cam-view="top-front-left"]').click();
    // The projected SVG face groups overlap in their bounding rectangles.
    // Use the cube's production keyboard activation for the visible left face.
    await page.locator('[data-part-drawing-viewport] [data-cam-view="left"]').press('Enter');
    await page.waitForFunction(() => {
      const state = window.fixture.drawingViewport.getCameraState();
      return Math.abs(Math.abs(state.theta) - Math.PI) < 1e-5 && Math.abs(state.phi - Math.PI / 2) < 1e-5;
    });
    const candidatesFor = id => page.evaluate(async id => {
      const { Vector3 } = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
      const viewport = window.fixture.drawingViewport, object = viewport.sceneObjects.get(`drawing:topology:edge:${id}`);
      if (!object) throw new Error('The selected native edge has no GPU render object');
      const rect = viewport.renderer.domElement.getBoundingClientRect(), points = [];
      object.updateWorldMatrix(true, true); viewport.camera.updateMatrixWorld(true);
      object.traverse(child => {
        const positions = child.geometry?.getAttribute('position'); if (!positions) return;
        for (const fraction of [.5, .3, .7]) {
          const along = (positions.count - 1) * fraction, index = Math.floor(along);
          const p = new Vector3().fromBufferAttribute(positions, index).lerp(
            new Vector3().fromBufferAttribute(positions, Math.min(index + 1, positions.count - 1)), along - index);
          child.localToWorld(p); p.project(viewport.camera);
          points.push({ x: rect.left + (p.x + 1) * rect.width / 2, y: rect.top + (1 - p.y) * rect.height / 2 });
        }
      });
      return points;
    }, id);
    const mousePick = async (candidate, additive) => {
      if (additive) await page.keyboard.down('Control');
      try { await page.mouse.click(candidate.x, candidate.y); }
      finally { if (additive) await page.keyboard.up('Control'); }
      await idle();
    };
    let picked, expected = [];
    for (const edge of selected.wire) {
      let accepted = false;
      for (const candidate of await candidatesFor(edge.id)) {
        await mousePick(candidate, expected.length > 0);
        picked = await page.evaluate(() => window.fixture.draw.topologySelection);
        const actual = (picked?.edgeIds ?? (picked?.kind === 'edge' ? [picked.id] : [])).map(Number);
        if (actual.length === expected.length + 1 && [...expected, Number(edge.id)].every(id => actual.includes(id))) {
          expected.push(Number(edge.id)); accepted = true; break;
        }
        // Undo an unsuccessful additive pick through the same production event.
        if (expected.length) {
          await mousePick(candidate, true);
          const restored = await page.evaluate(() => window.fixture.draw.topologySelection);
          assert.deepEqual((restored?.edgeIds ?? [restored?.id]).map(Number).sort((a, b) => a - b), [...expected].sort((a, b) => a - b));
        }
      }
      assert.equal(accepted, true, 'Real mouse Ctrl ray picking selects native tangent edge ' + edge.id);
      console.log(`Native canvas edge ${edge.id} selected (${expected.length}/${selected.wire.length}).`);
    }
    assert.equal(picked?.kind, 'edge'); assert.equal(expected.length, selected.wire.length);
    await page.locator('[data-cam-change-action=tube-designer-drawing-bevel-reference-face]').selectOption(String(selected.face.id));
    await readyDrawing();
    return selected;
  };

  // The baseline is authored with real controls, not a neutral-document fixture.
  if (!sideBoundaryOnly) {
  await page.locator('[data-fixture-ribbon-command="nesting.draw-2d-part"]').click(); await idle();
  await input(page.getByRole('spinbutton', { name: '长度（mm）', exact: true }), 1000);
  await screenshot('new-2d-rounded-60x40-r3-section-setup');
  await page.getByRole('button', { name: '开始绘制', exact: true }).click(); await readySide();
  const newSideToolbar = await toolbar('[data-sketch-command]');
  const firstCircle = await circle(20, 200, 6);
  const firstPath = await polygon([[105, 410], [125, 410], [125, 450], [105, 450]]);
  await page.locator('[data-cam-action=tube-designer-sketch-preview-side]').click(); await readySide();
  await sketchCommand('commit').click(); await idle(); await ui();
  const sideId = await page.evaluate(() => window.fixture.calls.find(call => call.method === 'TubeDesigner.AddNestingSideSketchPart')?.result.partEntityId);
  assert.ok(sideId); const initialSide = await get(sideId, 'side');
  writeFileSync(resolve(output, 'initial-side-document.json'), JSON.stringify(initialSide, null, 2));
  assert.equal(initialSide.authoringView.mode, 'side');
  assert.ok(initialSide.geometry.SolidNodes.some(node => node.Data.type === 'SWrappedVolumeNode'), '2D creation keeps an exact editable wrapped node');
  assert.ok(Object.keys(initialSide.authoringView.bindings).length > 0);
  const savedCircle = initialSide.authoringView.sketch.entities.find(entity => entity.kind === 'circle');
  const savedPath = initialSide.authoringView.sketch.entities.find(entity => entity.kind === 'path');
  assert.ok(savedCircle && savedPath, 'The current CAD graph projects the actual circle and precise closed path');
  near(savedCircle.radius, firstCircle.radius, 'Created circle radius');
  near(savedCircle.cx, firstCircle.cx, 'Created circle U'); near(savedCircle.cy, firstCircle.cy, 'Created circle S');
  const circleBinding = initialSide.authoringView.bindings.find(binding => binding.entityId === savedCircle.id);
  const pathBinding = initialSide.authoringView.bindings.find(binding => binding.entityId === savedPath.id);
  assert.ok(circleBinding?.nodeId && pathBinding?.nodeId);
  recordCheck({ case: 'Real 2D creation circle and closed mouse path; rounded 60×40 R3 exact base closure',
    partEntityId: sideId, roundedBase: roundedBase(initialSide), ...await equivalent(sideId) });

  await openSide(sideId); assert.deepEqual(await toolbar('[data-sketch-command]'), newSideToolbar);
  assert.equal(await page.evaluate(() => window.fixture.side.sideReference.unfolding.available), true);
  assert.ok(await page.locator(`[data-tube-sketch-entity-id="${savedCircle.id}"]`).count() > 0);
  await sketchCommand('select').click(); await point(savedCircle.cx + savedCircle.radius, savedCircle.cy);
  await input(page.locator('[data-sketch-field=diameter]'), 14);
  await point(firstPath.points[0][0], firstPath.points[0][1]); await sketchCommand('delete').click(); await idle();
  assert.equal(await page.evaluate(id => window.fixture.draft.entities.some(entity => entity.id === id), savedPath.id), false);
  const secondPath = await polygon([[100, 520], [125, 520], [125, 560], [100, 560]]);
  const sideInteraction = await testInteraction('side', () => page.locator('[data-cam-action=tube-designer-sketch-preview-side]').click());
  const sideBeforeApply = await get(sideId); assert.equal(sideBeforeApply.revision, initialSide.revision);
  assert.deepEqual(sideBeforeApply.geometry, initialSide.geometry, 'Current native 2D preview never publishes the modified graph');
  await screenshot('existing-2d-production-side-unfold');
  await sketchCommand('commit').click(); await idle(); await ui();
  const sideEdited = await get(sideId, 'side'); sameDocument(initialSide, sideEdited);
  assert.equal(sideEdited.geometry.SolidNodes.some(node => node.ID === pathBinding.nodeId), false);
  const editedCircleBinding = sideEdited.authoringView.bindings.find(binding => binding.nodeId === circleBinding.nodeId);
  assert.ok(editedCircleBinding, 'Updating the circle preserves its current neutral node identity');
  near(sideEdited.authoringView.sketch.entities.find(entity => entity.id === editedCircleBinding.entityId).radius, 7, 'Circle changed through original property editor');
  const newSavedPath = sideEdited.authoringView.sketch.entities.find(entity => entity.kind === 'path');
  assert.ok(newSavedPath?.closed && newSavedPath.id !== savedPath.id);
  const nativePoints = newSavedPath.segments.filter(segment => segment.kind === 'line').map(segment => [segment.x1, segment.y1]);
  for (const [u, s] of secondPath.points) assert.ok(nativePoints.some(p => Math.hypot(p[0] - u, p[1] - s) < 1e-6), 'New mouse path points reach the authoritative wrapped region');
  recordCheck({ case: 'Production existing 2D uses original page; edit/add/delete, temporary preview and same-document save',
    interaction: sideInteraction, ...await saveOpen(sideId, 'existing-side-edited') });
  await openSide(sideId); await sketchCommand('cancel').click(); await idle();

  // Another view of this same part may add its own construct without rewriting
  // the wrapped side nodes that its editor cannot safely edit.
  await openDrawing(sideId);
  const wrappedIds = sideEdited.geometry.SolidNodes.filter(node => node.Data.type === 'SWrappedVolumeNode').map(node => node.ID);
  for (const id of wrappedIds) assert.equal(await page.locator(`[data-drawing-node="${id}"]`).count(), 1);
  await addBranch(700, 10); await drawAction('apply').click(); await idle(); await ui();
  const mixed = await get(sideId); sameDocument(sideEdited, mixed); preserve(sideEdited, mixed, wrappedIds);
  recordCheck({ case: 'Original 3D page adds a branch to the 2D-authored document and preserves exact wrapped nodes', ...await equivalent(sideId) });

  await page.locator('[data-fixture-ribbon-command="nesting.draw-part"]').click(); await readyDrawing();
  const newDrawToolbar = await toolbar('[data-drawing-command]');
  await input(page.locator('[data-drawing-field=length]'), 1000); await readyDrawing();
  await screenshot('new-3d-rounded-60x40-r3-rich-page');
  await addBranch(330, 10); await addBranch(550, 12);
  await drawAction('apply').click(); await idle(); await ui();
  const drawingId = await page.evaluate(() => window.fixture.calls.findLast(call => call.method === 'TubeDesigner.AddPartDrawing')?.result.partEntityId);
  assert.ok(drawingId); const drawingOriginal = await get(drawingId, 'drawing');
  assert.equal(drawingOriginal.authoringView.mode, 'drawing'); assert.equal(drawingOriginal.authoringView.features.length, 2);
  await openDrawing(drawingId); assert.deepEqual(await toolbar('[data-drawing-command]'), newDrawToolbar);
  const [feature1, feature2] = await page.evaluate(() => window.fixture.draw.features.map(feature => structuredClone(feature)));
  await page.locator(`[data-drawing-node="${feature1.id}"]`).click();
  const drawInteraction = await testInteraction('drawing', async () => {
    await drawField('station').fill(String(feature1.station + 12)); await drawField('station').press('Tab');
  });
  await page.locator(`[data-drawing-node="${feature2.id}"]`).click(); await drawAction('selected-remove').click(); await readyDrawing();
  const feature3 = await addBranch(750, 8);
  await drawCommand('main').click(); await idle();
  const metadata = { name: '原三维页面保存名称数量材料', quantity: 3, material: '铝' };
  for (const [key, value] of Object.entries(metadata)) await input(page.locator(`[data-drawing-field=${key}]`), value);
  await readyDrawing();
  const drawingBeforeApply = await get(drawingId); assert.equal(drawingBeforeApply.revision, drawingOriginal.revision);
  assert.deepEqual(drawingBeforeApply.geometry, drawingOriginal.geometry, 'Current native 3D preview never publishes added, moved or deleted constructs');
  await screenshot('existing-3d-production-rich-workbench');
  await drawAction('apply').click(); await idle(); await ui();
  const drawingEdited = await get(drawingId, 'drawing'); sameDocument(drawingOriginal, drawingEdited);
  for (const [key, value] of Object.entries(metadata)) assert.equal(drawingEdited.authoringView.drawing[key], value, 'Original main controls persist ' + key);
  const editedScenePart = parts(await bridge.invoke('List')).find(part => part.entityId === drawingId);
  for (const [key, value] of Object.entries(metadata)) assert.equal(key === 'material'
    ? editedScenePart.properties['manufacturing.material'] : editedScenePart[key], value, 'Canonical scene metadata agrees with drawing ' + key);
  assert.equal(drawingEdited.authoringView.features.length, 2);
  near(drawingEdited.authoringView.features.find(feature => feature.cadNodeId === feature1.cadNodeId || feature.id === feature1.id).station,
    feature1.station + 12, 'A saved branch moves through the same station control');
  assert.equal(drawingEdited.geometry.SolidNodes.some(node => node.ID === (feature2.cadNodeId ?? feature2.id)), false);
  assert.ok(drawingEdited.authoringView.features.some(feature => feature.id === feature3.id || feature.station === 750));
  recordCheck({ case: 'Production existing rich 3D page edits/adds/deletes current CAD features, native preview, GPU and same identity',
    interaction: drawInteraction, metadata, ...await saveOpen(drawingId, 'existing-drawing-edited') });
  const reopenedMetadata = await get(drawingId, 'drawing');
  for (const [key, value] of Object.entries(metadata)) assert.equal(reopenedMetadata.authoringView.drawing[key], value, 'Fresh reopen retains main metadata ' + key);

  await openSide(drawingId); const saved3DIds = drawingEdited.authoringView.features.map(feature => feature.cadNodeId ?? feature.id);
  await circle(35, 130, 5); await page.locator('[data-cam-action=tube-designer-sketch-preview-side]').click(); await readySide();
  await screenshot('3d-part-existing-2d-production-side-unfold');
  await sketchCommand('commit').click(); await idle(); await ui();
  const crossEdited = await get(drawingId); sameDocument(drawingEdited, crossEdited); preserve(drawingEdited, crossEdited, saved3DIds);
  recordCheck({ case: 'Save/open then actual 2D page adds a circle while preserving unedited 3D constructions',
    ...await saveOpen(drawingId, 'cross-workbench-edited') });
  await openDrawing(drawingId); await screenshot('reopened-cross-workbench-production');
  assert.equal(await page.evaluate(() => window.fixture.draw.cadSession.bevelEditable), true,
    'The current authoring service must expose the existing rich topology/bevel capability');
  const picked = await pickStartTopEdge();
  await page.locator('[data-cad-bevel-field=enabled]').check(); await readyDrawing();
  await input(page.locator('[data-cad-bevel-field=rootFace]'), .5); await readyDrawing();
  const bevelDraft = await page.evaluate(() => structuredClone(window.fixture.draw.drawing.bevels[0]));
  assert.ok(bevelDraft.edgeNames?.every(name => name.startsWith('cad-topology/')));
  assert.ok(bevelDraft.referenceFaceName?.startsWith('cad-topology/'));
  const beforeBevel = await get(drawingId);
  await screenshot('existing-3d-native-mouse-selected-bevel');
  await drawAction('apply').click(); await idle(); await ui();
  const beveled = await get(drawingId, 'drawing'); sameDocument(beforeBevel, beveled);
  assert.equal(beveled.authoringView.drawing.bevels.length, 1);
  const savedBevel = beveled.authoringView.drawing.bevels[0];
  assert.deepEqual(savedBevel.edgeNames, bevelDraft.edgeNames);
  assert.equal(savedBevel.referenceFaceName, bevelDraft.referenceFaceName);
  recordCheck({ case: 'Production canvas ray pick, cut-wall selection and rich-page new bevel preserve persistent topology names',
    picked, ...await saveOpen(drawingId, 'existing-native-bevel') });
  await openDrawing(drawingId); await page.locator(`[data-drawing-node="${savedBevel.id}"]`).click(); await idle();
  await input(page.locator('[data-cad-bevel-field=angleDegrees]'), 30); await readyDrawing();
  await drawAction('apply').click(); await idle(); await ui();
  const bevelEdited = await get(drawingId, 'drawing'); sameDocument(beveled, bevelEdited);
  near(bevelEdited.authoringView.drawing.bevels[0].bevel.angleDegrees, 30, 'Reopened saved bevel uses the original angle control');
  recordCheck({ case: 'Actual save/open and original feature tree edit an existing bevel angle',
    ...await saveOpen(drawingId, 'existing-bevel-angle-edited') });
  await openDrawing(drawingId); await screenshot('existing-3d-reopened-bevel-production');
  const finalPreview = await page.evaluate(() => ({ key: window.fixture.draw.previewResourceKey,
    geometry: window.fixture.draw.preview.geometry }));
  await drawAction('cancel').click(); await idle(); await ui();
  await page.waitForFunction(key => window.fixture.calls.some(call => call.method === 'TubeDesigner.ReleaseTubeCadDocumentPreview'
    && call.payload.previewResourceKey === key && call.result), finalPreview.key, { timeout: 180000 });
  await assert.rejects(bridge.rpc({ action: 'resource', payload: finalPreview.geometry }), /not present|不存在|缺失/);
  recordCheck({ case: 'Closing the production existing 3D page actually releases its temporary native GPU resources', previewResourceKey: finalPreview.key });
  const circleProfile = await bridge.invoke('EvaluateProfilePackage', { profileRef: { scope: 'system', id: 'rect' },
    parameters: { width: 80, depth: 60, wallThickness: 3, cornerRadius: 0, innerRadius: 0 } }, 'product');
  const circleTools = await bridge.invoke('GetPartDrawingTools'), circleTool = circleTools.tools.find(tool => tool.id === 'circle');
  assert.ok(circleTool);
  const circleAdded = await bridge.invoke('AddNestingPunchPart', { drawing: { schemaVersion: 1, length: 400,
    section: { source: 'library', profile: circleProfile.profile } }, features: [{ id: 'current-circle', type: 'circle',
    toolTarget: circleTool.target, toolRef: { scope: 'system', id: circleTool.id },
    toolParameters: { ...circleTool.defaultParameters, diameter: 10 }, station: 140, face: 'top', offset: 0, layoutDatum: 'base' }],
    ends: {}, name: '普通四弧孔用原三维页改直径', quantity: 1 });
  await page.evaluate(scene => { window.fixture.view.scene = scene; window.fixture.render(); }, await bridge.invoke('List'));
  const circleBefore = await get(circleAdded.partEntityId, 'drawing'), circleFeature = circleBefore.authoringView.features[0];
  assert.equal(circleFeature.type, 'circle');
  const circleNodeId = circleFeature.cadNodeId ?? circleFeature.id;
  const beforeCircle = solids(circleBefore).get(circleNodeId).Data.value.Section.Boundaries[0];
  assert.equal(beforeCircle.Curves.length, 4, 'Current ordinary creation produces four exact Circle2 arcs');
  await openDrawing(circleAdded.partEntityId); await page.locator(`[data-drawing-node="${circleFeature.id}"]`).click(); await idle();
  await input(page.locator('[data-tube-designer-punch-parameter=diameter]'), 12); await readyDrawing();
  const circleUnpublished = await get(circleAdded.partEntityId, 'drawing');
  assert.deepEqual(circleUnpublished, circleBefore, 'The real diameter preview does not publish or mutate the authoritative part');
  await screenshot('ordinary-four-arc-hole-original-3d-diameter-12');
  await drawAction('apply').click(); await idle(); await ui();
  const circleAfter = await get(circleAdded.partEntityId, 'drawing'); sameDocument(circleBefore, circleAfter);
  const savedFourArcLoop = solids(circleAfter).get(circleNodeId).Data.value.Section.Boundaries[0];
  savedFourArcLoop.Curves.forEach(curve => near(curve.Segment.Curve.value.Radius, 6, 'Original diameter control changes actual exact radii'));
  assert.deepEqual(savedFourArcLoop.Curves.map(c => c.Segment.Range), beforeCircle.Curves.map(c => c.Segment.Range));
  assert.deepEqual(savedFourArcLoop.Curves.map(c => c.Segment.Curve.value.Placement), beforeCircle.Curves.map(c => c.Segment.Curve.value.Placement));
  const circleEvidence = await saveOpen(circleAdded.partEntityId, 'ordinary-four-arc-diameter');
  near(circleEvidence.body.volume, (80 * 60 - 74 * 54) * 400 - Math.PI * 6 ** 2 * 3, 'Actual 12 mm circle removes the analytical wall material');
  recordCheck({ case: 'Production original 3D circle diameter control changes ordinary four-arc geometry, preserves ranges/placement and persists',
    nodeId: circleNodeId, diameter: 12, ...circleEvidence });
  }

  const beginSide = async (length = 1000) => {
    await page.locator('[data-fixture-ribbon-command="nesting.draw-2d-part"]').click(); await idle();
    await input(page.getByRole('spinbutton', { name: '长度（mm）', exact: true }), length);
    await page.getByRole('button', { name: '开始绘制', exact: true }).click(); await readySide();
  };
  const commitSideCreation = async () => {
    await sketchCommand('commit').click(); await idle(); await ui();
    return page.evaluate(() => structuredClone(window.fixture.calls.findLast(call => call.method === 'TubeDesigner.AddNestingSideSketchPart').result));
  };
  const selectSideCircle = async entity => {
    await sketchCommand('select').click(); await point(entity.cx + entity.radius, entity.cy);
    assert.equal(await page.evaluate(() => window.fixture.draft.selectedIds[0]), entity.id,
      'The actual canvas selects the projected current CAD circle');
  };
  const sideCadApply = async () => {
    await page.locator('[data-cam-action=tube-designer-sketch-cad-apply]').click(); await idle(); await ui();
    assert.equal(await page.evaluate(() => window.fixture.side.command), null);
  };
  const sidePreview = async () => {
    await page.locator('[data-cam-action=tube-designer-sketch-preview-side]').click(); await readySide();
  };
  const exactSidePoint = async (u, s) => {
    await input(page.locator('[data-tube-sketch-numeric-field=x]'), u);
    await input(page.locator('[data-tube-sketch-numeric-field=y]'), s);
    await page.locator('[data-cam-action=tube-designer-sketch-numeric-apply]').click(); await idle(); await ui();
  };
  const exactSideCircle = async (u, s, radius) => {
    await sketchCommand('circle').click(); await exactSidePoint(u, s); await exactSidePoint(u + radius, s);
    const entity = await page.evaluate(() => structuredClone(window.fixture.draft.entities.at(-1)));
    near(entity.cx, u, 'The original precision controls retain circle U');
    near(entity.cy, s, 'The original precision controls retain circle S');
    near(entity.radius, radius, 'The original precision controls retain circle radius'); return entity;
  };
  const exactSidePolygon = async points => {
    await sketchCommand('polyline').click(); for (const [u, s] of [...points, points[0]]) await exactSidePoint(u, s);
    const entity = await page.evaluate(() => structuredClone(window.fixture.draft.entities.at(-1)));
    assert.equal(entity.closed, true); assert.deepEqual(entity.points, points); return entity;
  };

  // 2D bevels are edited on the selected exact UV boundary in this original
  // page. A radius edit after reopen also exercises dynamic topology rebinding.
  if (!seamPreviewOnly) {
  await beginSide(); await exactSideCircle(20, 250, 6);
  const sideBevelAdded = await commitSideCreation(), sideBevelId = sideBevelAdded.partEntityId;
  const sideBevelBefore = await get(sideBevelId, 'side');
  await openSide(sideBevelId);
  let sideBevelCircle = sideBevelBefore.authoringView.sketch.entities.find(entity => entity.kind === 'circle');
  assert.ok(sideBevelCircle); await selectSideCircle(sideBevelCircle);
  await page.locator('[data-cad-bevel-field=enabled]').check(); await idle();
  await input(page.locator('[data-cad-bevel-field=rootFace]'), .5);
  await sidePreview();
  assert.deepEqual((await get(sideBevelId)).geometry, sideBevelBefore.geometry, 'The original 2D bevel preview is temporary');
  await screenshot('existing-2d-original-cutout-bevel');
  await sketchCommand('commit').click(); await idle(); await ui();
  const sideBeveled = await get(sideBevelId, 'side'); sameDocument(sideBevelBefore, sideBeveled);
  sideBevelCircle = sideBeveled.authoringView.sketch.entities.find(entity => entity.kind === 'circle');
  assert.equal(sideBevelCircle.bevel.enabled, true); near(sideBevelCircle.bevel.angleDegrees, 45, '2D saved bevel angle');
  const dynamicModifiers = sideBeveled.geometry.SolidNodes.filter(node => node.Data.type === 'STopologyBevelNode');
  assert.equal(dynamicModifiers.length, 1);
  assert.ok(dynamicModifiers[0].Data.value.Selections.every(selection => selection.WrappedSourceNodeID && selection.UVBoundary.length),
    'Persisted bevel selection names the exact editable UV source and boundary');
  const sideBevelFirstEvidence = await saveOpen(sideBevelId, 'existing-side-bevel');
  await openSide(sideBevelId); await selectSideCircle(sideBevelCircle);
  await input(page.locator('[data-cad-bevel-field=angleDegrees]'), 30);
  await input(page.locator('[data-sketch-field=diameter]'), 14);
  await sidePreview(); await sketchCommand('commit').click(); await idle(); await ui();
  const sideBevelEdited = await get(sideBevelId, 'side'); sameDocument(sideBeveled, sideBevelEdited);
  const sideBevelEditedCircle = sideBevelEdited.authoringView.sketch.entities.find(entity => entity.id === sideBevelCircle.id);
  assert.ok(sideBevelEditedCircle, '2D diameter and bevel edits retain the wrapped boundary identity');
  near(sideBevelEditedCircle.radius, 7, 'Actual 2D diameter property updates the current precise hole');
  near(sideBevelEditedCircle.bevel.angleDegrees, 30, 'Actual 2D bevel property updates after fresh reopen');
  const sideBevelFinalEvidence = await saveOpen(sideBevelId, 'existing-side-bevel-radius-angle');
  assert.ok(sideBevelFinalEvidence.body.volume < sideBevelFirstEvidence.body.volume, 'A larger beveled hole removes actual wall material');
  recordCheck({ case: 'Actual original 2D selected cutout bevel saves/reopens, edits angle and radius, and rebinds current exact boundary',
    sourceEntityId: sideBevelCircle.id, original: sideBevelFirstEvidence, ...sideBevelFinalEvidence });
  await openSide(sideBevelId); await selectSideCircle(sideBevelEditedCircle);
  near(Number(await page.locator('[data-cad-bevel-field=angleDegrees]').inputValue()), 30, 'Reopened 2D angle control shows the saved value');
  near(Number(await page.locator('[data-sketch-field=diameter]').inputValue()), 14, 'Reopened 2D diameter control shows the saved value');
  await sketchCommand('cancel').click(); await idle();

  await beginSide(); await exactSidePolygon([[10, 420], [25, 420], [25, 460], [10, 460]]);
  const partialBevelAdded = await commitSideCreation(), partialBevelId = partialBevelAdded.partEntityId;
  const partialBevelBefore = await get(partialBevelId, 'side');
  const partialPath = partialBevelBefore.authoringView.sketch.entities.find(entity => entity.kind === 'path');
  assert.equal(partialPath.segments.length, 4);
  await openSide(partialBevelId); await sketchCommand('select').click(); await point(25, 440);
  assert.equal(await page.evaluate(() => window.fixture.draft.selectedIds[0]), partialPath.id);
  await page.locator('[data-cad-bevel-field=enabled]').check(); await idle();
  await input(page.locator('[data-cad-bevel-field=rootFace]'), .5);
  await page.locator('[data-cad-bevel-field=scope]').selectOption('selected'); await idle();
  await page.locator('[data-cad-bevel-segment="1"]').check(); await idle();
  await page.locator('[data-cad-bevel-segment="0"]').uncheck(); await idle();
  assert.deepEqual(await page.evaluate(() => window.fixture.draft.entities.find(entity => entity.id === window.fixture.draft.selectedIds[0]).bevel.segmentIndices), [1]);
  await sidePreview(); await sketchCommand('commit').click(); await idle(); await ui();
  const partialBeveled = await get(partialBevelId, 'side'); sameDocument(partialBevelBefore, partialBeveled);
  assert.deepEqual(partialBeveled.authoringView.sketch.entities.find(entity => entity.id === partialPath.id).bevel.segmentIndices, [1],
    'Get projects the exact selected boundary instead of expanding to every path segment');
  const partialSelection = partialBeveled.geometry.SolidNodes.find(node => node.Data.type === 'STopologyBevelNode').Data.value.Selections;
  assert.equal(partialSelection.length, 1); assert.equal(partialSelection[0].UVBoundary.length, 1,
    'The current neutral document stores just the chosen exact path boundary');
  await saveOpen(partialBevelId, 'existing-side-partial-bevel');
  await openSide(partialBevelId); await sketchCommand('select').click(); await point(25, 440);
  assert.equal(await page.locator('[data-cad-bevel-field=scope]').inputValue(), 'selected');
  for (let index = 0; index < 4; index++) assert.equal(await page.locator(`[data-cad-bevel-segment="${index}"]`).isChecked(), index === 1,
    'The original reopened 2D checkbox preserves only boundary segment 2');
  await input(page.locator('[data-cad-bevel-field=angleDegrees]'), 30); await sidePreview();
  await screenshot('existing-2d-original-partial-boundary-bevel');
  await sketchCommand('commit').click(); await idle(); await ui();
  const partialEdited = await get(partialBevelId, 'side'); sameDocument(partialBeveled, partialEdited);
  const partialEditedPath = partialEdited.authoringView.sketch.entities.find(entity => entity.id === partialPath.id);
  near(partialEditedPath.bevel.angleDegrees, 30, 'Original selected-boundary control edits the saved local bevel angle');
  assert.deepEqual(partialEditedPath.bevel.segmentIndices, [1], 'Editing the angle does not expand the local bevel scope');
  const editedPartialSelection = partialEdited.geometry.SolidNodes.find(node => node.Data.type === 'STopologyBevelNode').Data.value.Selections;
  assert.deepEqual(editedPartialSelection.map(selection => selection.UVBoundary), partialSelection.map(selection => selection.UVBoundary),
    'The saved angle edit preserves exactly the single geometric boundary');
  recordCheck({ case: 'Actual original 2D only-one-boundary bevel saves/reopens checked segment, then angle edit preserves local selection',
    sourceEntityId: partialPath.id, segmentIndices: [1], ...await saveOpen(partialBevelId, 'existing-side-partial-bevel-angle') });
  }

  // Nonzero start trimming retains the source stock UV domain while the actual
  // manufactured piece is translated to X=0. No frozen final-body intersection
  // may replace the editable wrapped region.
  await beginSide(); await exactSideCircle(20, 250, 6);
  await sketchCommand('end-cuts').click(); await idle();
  await input(page.locator('[data-tube-sketch-cad-field=start]'), 100);
  await input(page.locator('[data-tube-sketch-cad-field=end]'), 900);
  await sideCadApply(); await sidePreview();
  const trimmedAdded = await commitSideCreation(), trimmedId = trimmedAdded.partEntityId;
  const trimmedBefore = await get(trimmedId, 'side');
  assert.ok(trimmedBefore.geometry.SolidNodes.some(node => node.Data.type === 'SWrappedVolumeNode'));
  near(trimmedBefore.authoringView.reference.length, 1000, 'Trimmed piece preserves the original stock station domain');
  near(trimmedBefore.authoringView.reference.actualPiece.sourceAxialFirst, 100, 'The editor declares the source-to-piece offset');
  near(trimmedBefore.authoringView.reference.actualPiece.sourceAxialLast, 900, 'The editor declares the source-to-piece end');
  near(trimmedBefore.authoringView.preview.baseBounds.min[0], 0, 'Actual trimmed manufacturing geometry begins at X=0');
  near(trimmedBefore.authoringView.preview.baseBounds.max[0], 800, 'Actual trimmed manufacturing geometry has length 800');
  const trimmedCircle = trimmedBefore.authoringView.sketch.entities.find(entity => entity.kind === 'circle');
  assert.ok(trimmedCircle); near(trimmedCircle.cy, 250, 'Trimmed creation retains the source circle station');
  const trimmedOriginalEvidence = await equivalent(trimmedId);
  await openSide(trimmedId); await selectSideCircle(trimmedCircle);
  await input(page.locator('[data-sketch-field=diameter]'), 14); await sidePreview();
  await screenshot('existing-2d-original-nonzero-start-trim');
  await sketchCommand('commit').click(); await idle(); await ui();
  const trimmedAfter = await get(trimmedId, 'side'); sameDocument(trimmedBefore, trimmedAfter);
  near(trimmedAfter.authoringView.sketch.entities.find(entity => entity.id === trimmedCircle.id).radius, 7, 'Trimmed part edits the actual original wrapped circle');
  const trimmedEvidence = await saveOpen(trimmedId, 'nonzero-start-side-edit');
  near(trimmedOriginalEvidence.body.volume - trimmedEvidence.body.volume, Math.PI * (7 ** 2 - 6 ** 2) * 2,
    'The normalized actual trimmed body loses precisely the enlarged circle wall material');
  const repeatReadTimings = [];
  for (let attempt = 0; attempt < 2; attempt++) {
    const started = Date.now();
    await openSide(trimmedId);
    const uiReadyMs = Date.now() - started;
    const request = bridge.requests.findLast(request => request.method === 'GetTubeCadDocument' && request.started >= started);
    assert.ok(request && Number.isFinite(request.elapsed), 'Each actual original-page reopen completes its real native Get');
    await selectSideCircle(trimmedAfter.authoringView.sketch.entities.find(entity => entity.id === trimmedCircle.id));
    near(Number(await page.locator('[data-sketch-field=diameter]').inputValue()), 14, 'Trimmed saved hole reopens through the original page');
    await sketchCommand('cancel').click(); await idle();
    repeatReadTimings.push({ attempt: attempt + 1, uiReadyMs, nativeGetMs: request.elapsed });
  }
  recordCheck({ case: 'Actual original 2D nonzero start/end trim creates editable exact UV nodes, displays normalized manufacturing body, edits and reopens twice',
    sourceStation: trimmedCircle.cy, sourceLength: 1000, manufacturedLength: 800, repeatReadTimings, ...trimmedEvidence });

  // A real full-circumference band separates two manufacturing pieces. Each
  // independent saved document keeps source UV stations and its own component
  // selector; editing either piece must leave the other document untouched.
  if (!seamPreviewOnly) {
  await beginSide(); await exactSideCircle(20, 200, 6); await exactSideCircle(20, 800, 6);
  const splitPerimeter = await page.evaluate(() => window.fixture.side.sideReference.unfolding.surfaces.find(surface => !surface.inner).uPeriod);
  const splitBand = await exactSidePolygon([[-5, 490], [splitPerimeter + 5, 490], [splitPerimeter + 5, 510], [-5, 510]]);
  assert.equal(splitBand.kind, 'polyline'); assert.equal(splitBand.points.length, 4,
    'The original precision controls keep all four 20 mm narrow-band vertices until the exact first point is entered again');
  await sketchCommand('split-parts').click(); await idle();
  await page.locator('[data-tube-sketch-cad-field=splitParts]').check(); await idle(); await sideCadApply();
  await sidePreview(); const splitAdded = await commitSideCreation();
  assert.equal(splitAdded.partEntityIds.length, 2, 'The actual split checkbox creates two independent native parts');
  const splitIds = splitAdded.partEntityIds, splitBefore = await Promise.all(splitIds.map(id => get(id, 'side')));
  const splitEvidence = [];
  for (let index = 0; index < splitIds.length; index++) {
    const id = splitIds[index], before = await get(id, 'side'), otherId = splitIds[1 - index], otherBefore = await get(otherId);
    assert.ok(before.geometry.SolidNodes.some(node => node.Data.type === 'SConnectedComponentNode'), 'Split creation keeps a real current connected-solid selection');
    assert.ok(before.geometry.SolidNodes.some(node => node.Data.type === 'SWrappedVolumeNode'), 'Split creation retains exact editable wrapped curves');
    near(before.authoringView.preview.baseBounds.min[0], 0, 'Each actual split manufacturing body begins at X=0');
    near(before.authoringView.preview.baseBounds.max[0], 490, 'Each actual split manufacturing body has length 490');
    const originalStart = before.authoringView.reference.actualPiece.sourceAxialFirst;
    near(before.authoringView.reference.actualPiece.sourceToPartTranslation[0], -originalStart, 'Source and actual piece coordinates have an explicit rigid translation');
    const station = originalStart < 250 ? 200 : 800;
    const entity = before.authoringView.sketch.entities.find(entity => entity.kind === 'circle' && Math.abs(entity.cy - station) < 1e-6);
    assert.ok(entity, `Split part keeps the actual circle at source station ${station}`);
    const beforeGeometry = await equivalent(id);
    await openSide(id); await selectSideCircle(entity);
    const initialFit = await sideFinishedFits(`Split piece ${index + 1} original-page initial preview`);
    await input(page.locator('[data-sketch-field=diameter]'), 14); await sidePreview();
    const editedFit = await sideFinishedFits(`Split piece ${index + 1} original-page edited preview`);
    await screenshot('existing-2d-original-split-piece-' + (index + 1));
    await sketchCommand('commit').click(); await idle(); await ui();
    const after = await get(id, 'side'); sameDocument(before, after);
    near(after.authoringView.sketch.entities.find(value => value.id === entity.id).radius, 7, 'Actual split-piece property edits its current wrapped circle');
    assert.deepEqual(await get(otherId), otherBefore, 'Editing one independently saved piece leaves the other current CAD document unchanged');
    const evidence = await saveOpen(id, 'split-piece-' + (index + 1));
    near(beforeGeometry.body.volume - evidence.body.volume, Math.PI * (7 ** 2 - 6 ** 2) * 2, 'Only the selected real split piece loses the larger circle wall volume');
    splitEvidence.push({ partEntityId: id, sourceStation: station, originalStart, previewFit: { initial: initialFit, edited: editedFit }, ...evidence });
  }
  recordCheck({ case: 'Actual original 2D ring-band split checkbox creates two editable current documents; both original-page circle edits save/reopen independently',
    partEntityIds: splitIds, originalRevisions: splitBefore.map(document => document.revision), pieces: splitEvidence });
  }

  let delayedSourceId;
  if (caseSelection !== 'late-preview') {
  await beginSide();
  const seamPeriod = await page.evaluate(() => window.fixture.side.sideReference.unfolding.surfaces.find(surface => !surface.inner).uPeriod);
  await sketchCommand('ellipse').click(); await exactSidePoint(seamPeriod - 3, 350); await exactSidePoint(seamPeriod + 9, 368);
  const seamAdded = await commitSideCreation(), seamId = seamAdded.partEntityId;
  const seamBefore = await get(seamId, 'side');
  const seamEntity = seamBefore.authoringView.sketch.entities.find(entity => entity.kind === 'ellipse');
  assert.ok(seamEntity, 'A whole cross-seam ellipse reopens with the original ellipse tools and radius controls');
  const ellipseWhole = (entity, sourceStation, radiusU = 12, radiusS = 18) => {
    assert.equal(entity.kind, 'ellipse', 'The complete exact ellipse retains its original authoring kind');
    assert.equal(entity.closed, true);
    near(entity.cx, seamPeriod - 3, 'The full ellipse keeps its unwrapped source U center');
    near(entity.cy, sourceStation, 'The full ellipse keeps its current source S center');
    const cosine = Math.cos(entity.rotation ?? 0), sine = Math.sin(entity.rotation ?? 0);
    near(entity.radiusX ** 2 * cosine ** 2 + entity.radiusY ** 2 * sine ** 2, radiusU ** 2,
      'The complete precise ellipse keeps its U half-axis');
    near(entity.radiusX ** 2 * sine ** 2 + entity.radiusY ** 2 * cosine ** 2, radiusS ** 2,
      'The complete precise ellipse keeps its S half-axis');
    near((entity.radiusX ** 2 - entity.radiusY ** 2) * sine * cosine, 0,
      'The complete precise ellipse keeps its original orientation');
  };
  ellipseWhole(seamEntity, 350); const seamOriginalEvidence = await equivalent(seamId);
  await openSide(seamId); await sketchCommand('select').click(); await point(seamPeriod + 9, 350);
  assert.equal(await page.evaluate(() => window.fixture.draft.selectedIds[0]), seamEntity.id);
  await input(page.locator('[data-sketch-field=centerY]'), 370); await sidePreview();
  await screenshot('existing-2d-original-complete-cross-seam-ellipse');
  await sketchCommand('commit').click(); await idle(); await ui();
  const seamAfter = await get(seamId, 'side'); sameDocument(seamBefore, seamAfter);
  const movedEllipse = seamAfter.authoringView.sketch.entities.find(entity => entity.id === seamEntity.id);
  assert.ok(movedEllipse); ellipseWhole(movedEllipse, 370);
  const movedEvidence = await saveOpen(seamId, 'complete-cross-seam-ellipse');
  assert.ok(Math.abs(seamOriginalEvidence.body.volume - movedEvidence.body.volume) < 1e-3, 'A pure axial move preserves the actual entire wrapped ellipse removal volume');
  await openSide(seamId); await sketchCommand('select').click(); await point(seamPeriod + 9, 370);
  assert.equal(await page.evaluate(() => window.fixture.draft.selectedIds[0]), seamEntity.id);
  near(Number(await page.locator('[data-sketch-field=radiusX]').inputValue()), movedEllipse.radiusX,
    'The reopened original ellipse page displays the current first radius');
  near(Number(await page.locator('[data-sketch-field=radiusY]').inputValue()), movedEllipse.radiusY,
    'The reopened original ellipse page displays the current second radius');
  const firstRadiusAlongU = Math.abs(Math.cos(movedEllipse.rotation ?? 0)) > 0.5;
  await input(page.locator('[data-sketch-field=radiusX]'), firstRadiusAlongU ? 13 : 19);
  await input(page.locator('[data-sketch-field=radiusY]'), firstRadiusAlongU ? 19 : 13);
  await sidePreview(); await screenshot('existing-2d-original-cross-seam-ellipse-radius-edit');
  await sketchCommand('commit').click(); await idle(); await ui();
  const resizedDocument = await get(seamId, 'side'); sameDocument(seamAfter, resizedDocument);
  const resizedEllipse = resizedDocument.authoringView.sketch.entities.find(entity => entity.id === seamEntity.id);
  assert.ok(resizedEllipse); ellipseWhole(resizedEllipse, 370, 13, 19);
  const seamEvidence = await saveOpen(seamId, 'complete-cross-seam-ellipse-radius-edit');
  assert.ok(movedEvidence.body.volume - seamEvidence.body.volume > 1,
    'Increasing both ellipse half-axes through the original controls removes more actual finished material');
  await openSide(seamId); await sketchCommand('select').click(); await point(seamPeriod + 10, 370);
  assert.equal(await page.evaluate(() => window.fixture.draft.selectedIds[0]), seamEntity.id);
  near(Number(await page.locator('[data-sketch-field=radiusX]').inputValue()), resizedEllipse.radiusX,
    'After disk reopen, the original first ellipse radius control retains the actual edit');
  near(Number(await page.locator('[data-sketch-field=radiusY]').inputValue()), resizedEllipse.radiusY,
    'After disk reopen, the original second ellipse radius control retains the actual edit');
  await sketchCommand('cancel').click(); await idle(); await ui();
  recordCheck({ case: 'Actual original 2D cross-period ellipse retains its complete precise curve and original controls; center and both radius edits save/reopen',
    sourceEntityId: seamEntity.id, period: seamPeriod, sourceStation: 370, radiusU: 13, radiusS: 19, movedEvidence, ...seamEvidence });
  delayedSourceId = seamId;
  } else {
    await beginSide(); await exactSideCircle(20, 600, 5);
    delayedSourceId = (await commitSideCreation()).partEntityId;
  }

  // Delay a completed real native response, close through the production button,
  // replace the scene snapshot using the same proxy, and open another part.
  // The late result must release its own CAD resources and leave the new page.
  await openSide(delayedSourceId);
  await page.evaluate(() => { window.fixture.holdNextPreview = true; });
  await page.locator('[data-cam-action=tube-designer-sketch-preview-side]').click();
  await page.waitForFunction(() => window.fixture.nativeResponseWaiting, null, { timeout: 180000 });
  const oldPreview = await page.evaluate(() => {
    const f = window.fixture, call = f.calls.findLast(call => call.method === 'TubeDesigner.PreviewTubeCadDocument');
    f.oldSceneProxy = f.context.sceneProxy;
    return { key: call.payload.previewResourceKey, geometry: call.result.geometry };
  });
  await sketchCommand('cancel').click();
  await page.waitForFunction(() => !window.fixture.view.tubeDesignerSketchDialogOpen);
  await page.evaluate(scene => { window.fixture.view.scene = scene; window.fixture.render(); }, await bridge.invoke('List'));
  await choosePart(trimmedId); await page.locator('[data-cam-action=tube-designer-part-open-sketch]').click();
  await page.waitForFunction(id => window.fixture.side?.sideCadSession?.partEntityId === id
    && document.querySelector('[data-side-sketch-preview-ready=true]'), trimmedId, { timeout: 180000 });
  await page.evaluate(() => {
    const f = window.fixture;
    f.newPageEvidence = { key: f.side.sidePreviewResourceKey, state: f.side,
      canvas: document.querySelector('[data-tube-sketch-canvas]'), draft: JSON.stringify(f.draft.entities), preview: JSON.stringify(f.side.sidePreview) };
    f.releaseResponse();
  });
  await readySide();
  const stale = await page.evaluate(() => {
    const f = window.fixture, before = f.newPageEvidence;
    return { sameProxy: f.context.sceneProxy === f.oldSceneProxy, samePage: f.side === before.state,
      sameCanvas: document.querySelector('[data-tube-sketch-canvas]') === before.canvas,
      sameKey: f.side.sidePreviewResourceKey === before.key, sameDraft: JSON.stringify(f.draft.entities) === before.draft,
      samePreview: JSON.stringify(f.side.sidePreview) === before.preview, target: f.side.sideCadSession.partEntityId };
  });
  for (const property of ['sameProxy', 'samePage', 'sameCanvas', 'sameKey', 'sameDraft', 'samePreview']) assert.equal(stale[property], true, property);
  assert.equal(stale.target, trimmedId);
  await page.waitForFunction(key => window.fixture.calls.filter(call => call.method === 'TubeDesigner.ReleaseTubeCadDocumentPreview'
    && call.payload.previewResourceKey === key && call.result).length >= 2, oldPreview.key, { timeout: 180000 });
  assert.equal(await page.evaluate(key => window.fixture.calls.some(call => call.method === 'TubeDesigner.ReleaseNestingSideSketchPreview'
    && call.payload.previewResourceKey === key), oldPreview.key), false, 'Both close and late response retain the original CAD resource release contract');
  await assert.rejects(bridge.rpc({ action: 'resource', payload: oldPreview.geometry }), /not present|不存在|缺失/);
  await screenshot('existing-2d-original-late-preview-ownership');
  await sketchCommand('cancel').click(); await idle(); await ui();
  recordCheck({ case: 'Actual original 2D close/new-page/same-proxy scene replacement rejects a delayed real native preview and releases its own resources',
    stale, releasedPreviewResourceKey: oldPreview.key });
  const calls = await page.evaluate(() => window.fixture.calls);
  assert.ok(calls.some(call => call.method === 'TubeDesigner.GetTubeCadDocument' && call.payload.authoringView === 'side'));
  if (!sideBoundaryOnly) assert.ok(calls.some(call => call.method === 'TubeDesigner.GetTubeCadDocument' && call.payload.authoringView === 'drawing'));
  assert.ok(calls.some(call => call.method === 'TubeDesigner.ApplyTubeCadDocument' && call.payload.authoringEdit?.mode === 'side'));
  if (!sideBoundaryOnly) assert.ok(calls.some(call => call.method === 'TubeDesigner.ApplyTubeCadDocument' && call.payload.authoringEdit?.mode === 'drawing'));
  assert.ok(calls.filter(call => call.method === 'TubeDesigner.PreviewTubeCadDocument').length >= 2);
  assert.deepEqual(errors, []); assert.deepEqual(external, []);
} catch (error) {
  failure = error;
  if (page) {
    await page.screenshot({ path: resolve(output, 'failure.png') }).catch(() => {});
    writeFileSync(resolve(output, 'failure-state.json'), JSON.stringify(await page.evaluate(() => ({
      view: window.fixture?.view, calls: window.fixture?.calls, failures: window.fixture?.failures,
      alerts: [...document.querySelectorAll('[role=alert]')].map(node => node.textContent),
    })).catch(() => ({})), null, 2));
  }
} finally {
  const calls = page ? await page.evaluate(() => window.fixture?.calls ?? []).catch(() => []) : [];
  const resources = page ? await page.evaluate(() => window.fixture?.resources ?? []).catch(() => []) : [];
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: !failure, failure: failure?.stack,
    buildPhase, caseSelection, completionClaim: !failure && !sideBoundaryOnly && buildPhase === 'final',
    tubeDesignerSha256: bridge.dllHash, importerSha256: bridge.importerHash, dllDirectory: bridge.dllDirectory,
    loadedModules,
    coverage: { actualProductionEntryEvents: true, actualCreationWorkbenches: true, actualSceneSDO: true,
      actualNativeResources: true, actualSDKGPU: true, actualDiskSaveOpen: true, original2DWorkbench: true, original3DWorkbench: !sideBoundaryOnly,
      installedDesktopHost: false, desktopFilePicker: false }, checks, calls, resources, errors, external,
    requests: bridge.requests, servedProductionAssets: Object.fromEntries(assets) }, null, 2));
  await browser?.close(); await bridge.close();
}
if (failure) throw failure;
console.log(`Existing production 2D/3D workbenches: ${checks.length} native cases passed (${bridge.dllHash}).`);
