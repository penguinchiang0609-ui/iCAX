// Real entry lifecycle hooks and WebGL renderers; no modelling transport is used.
import assert from 'node:assert/strict';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve, sep } from 'node:path';

const sourceRoot = resolve(fileURLToPath(new URL('../../', import.meta.url)));
const repository = fileURLToPath(new URL('../../../', import.meta.url));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE
  || 'file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome' });
try {
  const page = await browser.newPage({ viewport: { width: 1200, height: 750 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://entry-sketch-render.test/**', route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><body></body>' });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ''));
    if (!pathname.startsWith('/src/') || !path.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    let code = readFileSync(path, 'utf8');
    // Expose the actual entry descriptor for this test, without a production API.
    if (pathname.endsWith('/tube-designer/webpage/entry.mjs'))
      code += '\nexport { withDesignerContext as createEntryTestContext };\n';
    return route.fulfill({ contentType: 'text/javascript', body: code });
  });
  await page.goto('http://entry-sketch-render.test/');
  await page.evaluate(async () => {
    const { createEntryTestContext } = await import('/src/apps/tube-designer/webpage/entry.mjs');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const { attachSideSketchPreview, disposeSideSketchPreview } = await import('/src/apps/tube-designer/webpage/sideSketchPreview.mjs');
    const { encodeNestingGeometry, encodePreviewMaterial } = await import('/src/apps/tube-designer/webpage/nestingPreview.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    document.head.insertAdjacentHTML('beforeend', '<style>body{margin:0}#app{display:grid;grid-template-columns:1fr 1fr;gap:8px}#main,[data-side-sketch-viewport],#replacement{position:relative;height:420px;min-width:0}</style>');
    document.body.innerHTML = '<main id="app"><div id="main"></div><div data-side-sketch-viewport></div><div id="replacement"></div></main>';
    const mount = document.querySelector('#app');
    const box = new THREE.BoxGeometry(180, 40, 30);
    const mesh = encodeNestingGeometry({ positions: [...box.attributes.position.array], indices: [...box.index.array] });
    box.dispose();
    const detailed = new THREE.TorusKnotGeometry(70, 9, 512, 24);
    const mainMesh = encodeNestingGeometry({ positions: [...detailed.attributes.position.array], indices: [...detailed.index.array] });
    detailed.dispose();
    const material = encodePreviewMaterial(0x4499ccff);
    const counters = { resourceReads: 0, releases: 0 };
    const resources = { async get(url) { counters.resourceReads++;
      return new Response(url === 'fixture:mesh' ? mesh : url === 'fixture:main-mesh' ? mainMesh : material); } };
    const main = createThreeViewport({ showGrid: false, continuousRender: true });
    main.mount(document.querySelector('#main'));
    await main.applyViewSnapshot({ revision: 'main', rows: [{ entityId: 'main:box', data: {
      geometry: { url: 'fixture:main-mesh', version: 1 }, material: { url: 'fixture:material', version: 1 }, meshEdges: true,
    } }] }, resources);
    main.fitViewToViewport();
    const view = { activeAreaId: 'nesting', scene: { tubeDesigner: {} }, viewport: main, tubeDesignerSketchDialogOpen: false,
      tubeDesignerSketch: { mode: 'side', targetPartId: 'fixture', sidePreviewPayload: {}, sidePreviewResourceKey: 'fixture-preview',
        sidePreview: { baseGeometry: { url: 'fixture:mesh', version: 1 }, baseMaterial: { url: 'fixture:material', version: 1 },
          baseBounds: { min: [-90, -20, -15], max: [90, 20, 15] } } } };
    const context = { mount, project: { projectId: 'entry-sketch-render' }, activeRibbonTabId: 'nesting', sceneProxy: {
      resources, invoke() { counters.releases++; return Promise.resolve({}); },
    } };
    const descriptor = createEntryTestContext(context);
    const frames = async (count = 5) => { for (let i = 0; i < count; i++) await new Promise(resolve => requestAnimationFrame(resolve)); };
    const before = () => descriptor.beforeProjectRender(descriptor, view, mount);
    const f = window.fixture = { view, main, context, descriptor, before, frames, counters, createThreeViewport, disposeSideSketchPreview };
    f.open = async () => {
      view.tubeDesignerSketchDialogOpen = true;
      before();
      await attachSideSketchPreview(descriptor, view, mount, { createSideSketchViewport(options) { return f.side = createThreeViewport(options); } });
      await frames();
    };
    f.attachCached = () => attachSideSketchPreview(descriptor, view, mount);
    await frames();
  });
  const initial = await page.evaluate(async () => {
    const f = window.fixture, start = f.main.renderSequence;
    await f.frames();
    return { start, end: f.main.renderSequence, triangles: f.main.getDebugState().renderInfo.triangles };
  });
  assert.ok(initial.end > initial.start, 'The ordinary main viewport initially renders continuously.');
  assert.ok(initial.triangles > 10000, 'The lifecycle test renders a detailed background solid.');
  await page.evaluate(() => window.fixture.open());
  const paused = await page.evaluate(async () => {
    const f = window.fixture, start = f.main.renderSequence, sideStart = f.side.renderSequence;
    const reads = f.counters.resourceReads;
    for (let i = 0; i < 6; i++) { f.before(); await f.attachCached(); }
    await f.frames();
    return { start, end: f.main.renderSequence, sideStart, sideEnd: f.side.renderSequence,
      reads, afterReads: f.counters.resourceReads, debug: f.main.getDebugState(), sideDebug: f.side.getDebugState() };
  });
  assert.equal(paused.end, paused.start, 'The main viewport stays idle behind the sketch modal across entry patch hooks.');
  assert.equal(paused.debug.continuousRendering, false);
  assert.equal(paused.debug.animationFrameActive, false);
  assert.equal(paused.sideDebug.continuousRendering, false);
  assert.equal(paused.sideEnd, paused.sideStart, 'Unchanged side preview attachment does not render or resize again.');
  assert.equal(paused.afterReads, paused.reads, 'Unchanged side preview attachment reuses its resources.');
  const sideBefore = await page.evaluate(() => window.fixture.side.getCameraState());
  const canvas = await page.locator('[data-side-sketch-viewport] .icax-three-viewport-canvas').boundingBox();
  await page.mouse.move(canvas.x + canvas.width * .45, canvas.y + canvas.height * .45);
  await page.mouse.down({ button: 'right' });
  await page.mouse.move(canvas.x + canvas.width * .63, canvas.y + canvas.height * .52, { steps: 6 });
  await page.mouse.up({ button: 'right' });
  const orbited = await page.evaluate(async () => {
    const f = window.fixture, sideSequence = f.side.renderSequence, mainAtRelease = f.main.renderSequence;
    await f.frames();
    return { sideSequence, afterIdle: f.side.renderSequence, mainAtRelease, mainSequence: f.main.renderSequence,
      mainContinuous: f.main.continuousRendering, camera: f.side.getCameraState() };
  });
  assert.notEqual(orbited.camera.theta, sideBefore.theta, 'The side preview still responds to an actual right-button orbit.');
  assert.ok(orbited.sideSequence > paused.sideEnd);
  assert.equal(orbited.afterIdle, orbited.sideSequence, 'Side preview returns to idle after orbiting.');
  assert.equal(orbited.mainSequence, orbited.mainAtRelease, 'Side navigation does not resume the covered main viewport.');
  assert.equal(orbited.mainContinuous, false);
  const restored = await page.evaluate(async () => {
    const f = window.fixture;
    f.view.tubeDesignerSketchDialogOpen = false;
    f.before();
    f.disposeSideSketchPreview(f.context.mount);
    const start = f.main.renderSequence;
    await f.frames();
    return { start, end: f.main.renderSequence, continuous: f.main.continuousRendering };
  });
  assert.equal(restored.continuous, true);
  assert.ok(restored.end > restored.start, 'Closing through the entry patch hook restores the main render loop.');
  const alreadyPaused = await page.evaluate(async () => {
    const f = window.fixture;
    f.main.setContinuousRendering(false);
    f.view.tubeDesignerSketchDialogOpen = true; f.before();
    f.before();
    f.view.tubeDesignerSketchDialogOpen = false; f.before();
    const start = f.main.renderSequence;
    await f.frames();
    return { start, end: f.main.renderSequence, continuous: f.main.continuousRendering };
  });
  assert.equal(alreadyPaused.continuous, false, 'A renderer paused by another workflow stays paused.');
  assert.equal(alreadyPaused.end, alreadyPaused.start);
  const replacement = await page.evaluate(async () => {
    const f = window.fixture;
    f.main.setContinuousRendering(true);
    f.view.tubeDesignerSketchDialogOpen = true; f.before();
    f.replacement = f.createThreeViewport({ continuousRender: true, showGrid: false });
    f.replacement.mount(document.querySelector('#replacement'));
    f.view.viewport = f.replacement; f.before();
    await f.frames();
    const oldStart = f.main.renderSequence, pausedStart = f.replacement.renderSequence;
    await f.frames();
    const pausedEnd = f.replacement.renderSequence;
    f.view.tubeDesignerSketchDialogOpen = false; f.before();
    const restoredStart = f.replacement.renderSequence;
    await f.frames();
    return { oldStart, oldEnd: f.main.renderSequence, oldContinuous: f.main.continuousRendering,
      pausedStart, pausedEnd, restoredStart, restoredEnd: f.replacement.renderSequence };
  });
  assert.equal(replacement.pausedEnd, replacement.pausedStart);
  assert.equal(replacement.oldEnd, replacement.oldStart, 'Replacing the viewport never resumes the abandoned renderer.');
  assert.equal(replacement.oldContinuous, false);
  assert.ok(replacement.restoredEnd > replacement.restoredStart, 'Closing restores only the currently owned replacement renderer.');
  const firstCreated = await page.evaluate(async () => {
    const f = window.fixture;
    f.replacement.dispose();
    const view = { activeAreaId: 'nesting', scene: { tubeDesigner: {} }, tubeDesignerSketchDialogOpen: true };
    f.descriptor.beforeProjectRender(f.descriptor, view, f.context.mount);
    view.viewport = f.createThreeViewport({ continuousRender: true, showGrid: false });
    view.viewport.mount(document.querySelector('#replacement'));
    f.descriptor.configureViewport(f.descriptor, view, 'nesting');
    await f.frames();
    const start = view.viewport.renderSequence;
    await f.frames();
    const end = view.viewport.renderSequence;
    view.tubeDesignerSketchDialogOpen = false;
    f.descriptor.beforeProjectRender(f.descriptor, view, f.context.mount);
    const restoredStart = view.viewport.renderSequence;
    await f.frames();
    const restoredEnd = view.viewport.renderSequence;
    f.descriptor.configureViewport(f.descriptor, view, 'sketch');
    const sketchAreaContinuous = view.viewport.continuousRendering;
    view.viewport.dispose(); f.main.dispose();
    return { start, end, restoredStart, restoredEnd, sketchAreaContinuous };
  });
  assert.equal(firstCreated.end, firstCreated.start, 'A viewport first created while the modal is open starts idle.');
  assert.ok(firstCreated.restoredEnd > firstCreated.restoredStart);
  assert.equal(firstCreated.sketchAreaContinuous, false, 'The ordinary sketch-area render policy remains unchanged.');
  assert.deepEqual(errors, []);
  const result = { initial, paused, orbited, restored, alreadyPaused, replacement, firstCreated };
  const output = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repository, 'output/tests/side-sketch-performance/entry-rendering'));
  mkdirSync(output, { recursive: true });
  writeFileSync(resolve(output, 'entry-sketch-rendering.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify({ passed: true, backgroundTriangles: initial.triangles,
    coveredMainFrames: paused.end - paused.start, idleSideFrames: orbited.afterIdle - orbited.sideSequence,
    restoredMainFrames: restored.end - restored.start, replacementRestoredFrames: replacement.restoredEnd - replacement.restoredStart }));
} finally {
  await browser.close();
}
