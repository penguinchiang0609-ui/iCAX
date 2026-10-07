// Production CSG panes/actions and shared viewport; controlled native protocol, not a CAD acceptance test.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('tube-designer-component-creation-memory-20261006');
const checks = [], errors = [];
const check = (actual, expected, name) => { assert.deepEqual(actual, expected, name); checks.push(name); };
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
let page;
try {
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } }); page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://component-memory.test/**', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><body></body>' });
    if (!path.startsWith('/src/')) return route.abort();
    return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(browserAssetPath(path.slice(5)), 'utf8') });
  });
  await page.goto('http://component-memory.test/');
  await page.evaluate(async () => {
    localStorage.clear();
    const component = await import('/src/apps/tube-designer/webpage/componentLibrary.mjs');
    const memory = await import('/src/apps/tube-designer/webpage/windowStateMemory.mjs');
    const { patchParameterContent } = await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const { ensureTubeDesignerStyles } = await import('/src/apps/tube-designer/webpage/styles/ensureStyles.mjs');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const { encodeNestingGeometry } = await import('/src/apps/tube-designer/webpage/nestingPreview.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    document.body.className = 'tube-designer-workspace'; ensureTubeDesignerStyles();
    const style = document.createElement('style'); style.textContent = 'body{margin:0}.cam-workbench{display:grid;grid-template-columns:340px 1fr 390px;height:100vh}.cam-context-pane,.cam-info-pane{height:100vh;overflow:auto}#center{position:relative;min-width:0}#preview{width:100%;height:100%}#overlay{position:absolute;inset:0;pointer-events:none}#overlay button,#overlay [data-component-csg-manipulation]{pointer-events:auto}'; document.head.append(style);
    document.body.innerHTML = '<div id="mount"><div class="cam-workbench"><aside class="cam-context-pane"></aside><main id="center"><div id="preview"></div><div id="overlay"></div></main><aside class="cam-info-pane"></aside></div></div>';
    const mount = document.querySelector('#mount'), left = mount.querySelector('.cam-context-pane'), right = mount.querySelector('.cam-info-pane'), overlay = document.querySelector('#overlay');
    const f = window.componentFixture = { component, memory, mount, left, right, calls: [], errors: [], version: 0, models: [] };
    f.view = { activeAreaId: 'components', pending: false, scene: { tubeDesigner: { templates: [], instances: [], members: [] } } };
    const state = component.componentLibraryState(f.view); state.loadState = 'loaded'; state.scope = 'user';
    const rectangle = (width, depth) => ({ name: '当前矩形', width, depth, contours: [{ kind: 'polygon', points: [[-width / 2, -depth / 2], [width / 2, -depth / 2], [width / 2, depth / 2], [-width / 2, depth / 2]] }] });
    f.view.tubeDesignerSystemProfiles = [{ id: 'rect', name: '当前矩形', profileForm: 'parametric',
      descriptor: { parameters: [{ key: 'width', displayName: '宽度', valueType: 'number', min: 1, max: 100, defaultValue: 40 }, { key: 'depth', displayName: '深度', valueType: 'number', min: 1, defaultValue: 20 }] },
      defaultParameters: { width: 40, depth: 20 }, previewProfile: rectangle(40, 20) }];
    f.context = { mount, project: { projectId: 'component-memory-project' },
      productProxy: { async invoke(method, payload) {
        f.calls.push({ scope: 'product', method, payload: structuredClone(payload) });
        if (method === 'TubeDesigner.EvaluateProfilePackage') return { profile: { ...rectangle(payload.parameters.width, payload.parameters.depth), parameters: payload.parameters } };
        if (method === 'TubeDesigner.CreateComponentCSGModel') {
          const model = { ...payload, id: 'saved-component', scope: 'user', modelType: 'csg', revision: 1 }; f.models.push(model); return { model };
        }
        if (method === 'TubeDesigner.UpdateComponentCSGModel') return { model: { ...payload, id: payload.id, scope: 'user', modelType: 'csg', revision: 2 } };
        throw Error('Unexpected product method: ' + method);
      } }, sceneProxy: { resources: { async get() { return new Response(f.mesh); } }, async invoke(method, payload) {
        f.calls.push({ scope: 'scene', method, payload: structuredClone(payload) });
        if (method !== 'TubeDesigner.GenerateComponentCSGPreview') throw Error('Unexpected scene method: ' + method);
        const first = payload.csgDefinition.features[0], width = first.profile?.snapshot?.width ?? first.parameters.width ?? 40;
        const box = new THREE.BoxGeometry(width, 30, first.parameters.height);
        f.mesh = encodeNestingGeometry({ positions: [...box.attributes.position.array], indices: [...box.index.array] }); box.dispose();
        return { valid: true, geometryResourceId: 'resource://component-memory/result', geometryResourceVersion: ++f.version, bounds: { width, depth: 30, height: first.parameters.height } };
      } } };
    f.viewport = createThreeViewport({ showGrid: false }); f.viewport.mount(document.querySelector('#preview')); f.view.viewport = f.viewport;
    f.render = () => {
      patchParameterContent(mount, left, component.renderComponentLibraryLeftPane(f.context, f.view));
      patchParameterContent(mount, right, component.renderComponentLibraryRightPane(f.context, f.view));
      overlay.innerHTML = component.renderComponentLibraryViewportOverlay(f.context, f.view);
      f.memoryController = memory.bindTubeDesignerWindowMemory(f.context, f.view, f.ops);
      f.previewController = component.ensureComponentCSGPreview(f.context, f.view, mount, f.ops);
    };
    f.ops = { renderProject() { f.render(); }, showNotice() {}, appendProjectLog() {} };
    f.run = (action, target) => {
      const promise = component.handleComponentLibraryAction(f.context, f.view, action, target ?? {}, f.ops);
      f.view.activeAreaAction = { action, promise };
      void promise.catch(error => f.errors.push(error.message)).finally(() => { if (f.view.activeAreaAction?.promise === promise) f.view.activeAreaAction = null; });
      return promise;
    };
    mount.onclick = event => { const target = event.target.closest('[data-cam-action]'); if (target && !target.disabled) void f.run(target.dataset.camAction, target); };
    mount.onchange = event => { const target = event.target.closest('[data-cam-change-action]'); if (target && !target.disabled) void f.run(target.dataset.camChangeAction, target); };
    f.open = () => f.run('tube-designer-component-draw'); f.render();
  });
  const settle = async () => {
    await page.waitForFunction(() => !componentFixture.view.pending && !componentFixture.view.activeAreaAction);
    await page.evaluate(async () => { for (const pane of [componentFixture.left, componentFixture.right]) await componentFixture.memoryController.restoreWindow(pane); });
    await page.waitForFunction(() => !componentFixture.view.activeAreaAction && (!componentFixture.view.tubeDesignerComponentLibrary.csgDraft || componentFixture.view.tubeDesignerComponentLibrary.csgDraft.previewStatus === 'ready'));
  };
  const change = async (selector, value) => { await page.locator(selector).fill(String(value)); await page.locator(selector).dispatchEvent('change'); await settle(); };
  const field = key => `[data-csg-feature-field="${key}"]`, metadata = key => `[data-csg-model-field="${key}"]`;
  const node = id => `[data-csg-feature-id="${id}"][data-csg-node-kind="primitive"]`;
  const cancel = async () => { await page.locator('[data-cam-action="tube-designer-component-cancel-csg"]').click(); await settle(); };
  const open = async () => { await page.evaluate(() => componentFixture.open()); await settle(); };
  await open();
  await change(metadata('name'), '验收新建零件'); await change(metadata('category'), '连接支架');
  await change(metadata('description'), '最后修改的说明');
  await page.locator('[data-cam-change-action="tube-designer-component-csg-profile-select"]').selectOption('system:rect'); await settle();
  await change('[data-csg-profile-parameter="width"]', 55); await change(field('parameters.height'), 72); await change(field('position.x'), 8);
  await page.locator('[data-csg-primitive="box"]').click(); await settle();
  await change(field('parameters.width'), 18); await change(field('position.y'), -12); await change(field('rotation.z'), 30);
  const ids = await page.evaluate(() => componentFixture.view.tubeDesignerComponentLibrary.csgDraft.features.map(item => item.id));
  await cancel();
  const before = await page.evaluate(() => componentFixture.calls.length); await open();
  check(await page.evaluate(() => {
    const draft = componentFixture.view.tubeDesignerComponentLibrary.csgDraft;
    return { name: draft.name, category: draft.category, description: draft.description, ids: draft.features.map(item => item.id),
      profileKey: draft.features[0].profile.key, profileWidth: draft.features[0].profile.parameters.width,
      height: draft.features[0].parameters.height, x: draft.features[0].transform.position.x,
      boxWidth: draft.features[1].parameters.width, y: draft.features[1].transform.position.y, zRotation: draft.features[1].transform.rotation.z };
  }), { name: '验收新建零件', category: '连接支架', description: '最后修改的说明', ids, profileKey: 'system:rect', profileWidth: 55, height: 72, x: 8, boxWidth: 18, y: -12, zRotation: 30 }, 'actual new CSG controls restore stable nodes and last inputs after cancel');
  check(await page.locator(field('parameters.width')).inputValue(), '18', 'selected node DOM agrees with restored model');
  check(await page.evaluate(start => componentFixture.calls.slice(start).map(item => [item.scope, item.method]), before),
    [['product', 'TubeDesigner.EvaluateProfilePackage'], ['scene', 'TubeDesigner.GenerateComponentCSGPreview']], 'remembered profile re-evaluates current resource before actual scene preview');
  check(await page.evaluate(() => componentFixture.calls.filter(item => /CreateComponentCSGModel|UpdateComponentCSGModel/.test(item.method)).length), 0, 'reopening only previews and never saves an entity');
  await page.locator(node(ids[0])).click(); await settle();
  check(await page.locator('[data-csg-profile-parameter="width"]').inputValue(), '55', 'first node profile inputs stay independent of second node dimensions');
  check(await page.locator(field('position.x')).inputValue(), '8', 'first node location stays independent of second node');
  await page.locator(node(ids[1])).click(); await settle();
  check(await page.locator(field('rotation.z')).inputValue(), '30', 'switching nodes restores the correct node rotation');
  await page.locator('[data-cam-action="tube-designer-component-save-csg"]').click(); await settle();
  check(await page.evaluate(() => {
    const payload = componentFixture.calls.find(item => item.method === 'TubeDesigner.CreateComponentCSGModel').payload;
    return { name: payload.name, width: payload.csgDefinition.features[0].profile.parameters.width, boxWidth: payload.csgDefinition.features[1].parameters.width };
  }), { name: '验收新建零件', width: 55, boxWidth: 18 }, 'actual save submits the remembered inputs shown in controls');
  await page.locator('[data-cam-action="tube-designer-component-edit-csg"]').click(); await settle();
  await change(metadata('name'), '已有实体编辑'); await change(field('parameters.height'), 99); await cancel(); await open();
  check(await page.locator(metadata('name')).inputValue(), '验收新建零件', 'editing an existing entity never replaces new creation defaults');
  await page.locator(node(ids[0])).click(); await settle();
  check(await page.locator(field('parameters.height')).inputValue(), '72', 'entity edit dimensions never overwrite new draft dimensions');
  check(await page.evaluate(() => {
    const bounds = componentFixture.viewport.getVisibleBounds(); return { width: bounds.max[0] - bounds.min[0], entityIds: componentFixture.viewport.getAppliedViewState().entityIds };
  }), { width: 55, entityIds: ['component-csg-live-result'] }, 'shared viewport preview uses restored current profile dimensions');
  check(await page.evaluate(() => [...Array(localStorage.length)].map((_, i) => localStorage.getItem(localStorage.key(i))).every(value => !/"snapshot"|"contours"|geometryResourceId/.test(value))), true, 'window memory contains no generated geometry or resource snapshots');
  check(await page.evaluate(() => componentFixture.errors), [], 'production handlers finish without errors'); check(errors, [], 'browser has no uncaught errors');
  await page.screenshot({ path: resolve(output, 'csg-remembered-inputs.png'), fullPage: true });
  await cancel(); await page.evaluate(() => componentFixture.viewport.dispose());
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ status: 'passed', checks, productionCSGRenderer: true,
    productionCSGHandlers: true, sharedViewport: true, nativeTransport: 'controlled protocol', nativeCadGeneration: false, standaloneCefEndToEnd: false }, null, 2));
  console.log(`TubeDesigner CSG creation memory: passed (${checks.length} browser checks).`);
} catch (error) {
  if (page) await page.screenshot({ path: resolve(output, 'failure.png'), fullPage: true }).catch(() => {});
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ status: 'failed', checks, errors, error: error.stack }, null, 2)); throw error;
} finally { await browser.close(); }
