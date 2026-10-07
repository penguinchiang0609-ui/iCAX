// Shipped AppShell and TubeDesigner UI, with an explicit native protocol fixture.
// The desktop file picker, native archive import and CEF are tested separately.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const sourceTemplate = JSON.parse(readFileSync(browserAssetPath('apps/tube-designer/templates/assembly/mechanical-fastener/assembly.json'), 'utf8'));
const templates = Array.from({ length: 18 }, (_, index) => ({ ...structuredClone(sourceTemplate),
  id: index ? `browser-fixture-fastener-${index}` : sourceTemplate.id,
  displayName: index ? `孔系紧固 ${index}` : sourceTemplate.displayName, libraryScope: 'builtin' }));
const imported = { ...structuredClone(sourceTemplate), id: 'user-browser-mechanical', displayName: '孔系扩展工艺', libraryScope: 'user' };
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('assembly-template-import-20261006');
const browser = await chromium.launch({ headless: true, channel: 'chrome' });
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 960 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://assembly-import.test/**', route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"></head><body><div id="app"></div></body></html>' });
    if (pathname === '/src/iCAX-UI/SDK/runtime.mjs') return route.fulfill({ contentType: 'text/javascript', body: 'export async function connectApplication(){return globalThis.__assemblyImport.appProxy;}' });
    if (!pathname.startsWith('/src/')) return route.abort();
    const file = browserAssetPath(pathname.slice('/src/'.length));
    return route.fulfill({ contentType: pathname.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(file, 'utf8') });
  });
  await page.goto('http://assembly-import.test/');
  await page.evaluate(async ({ templates, imported }) => {
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const projectId = 'assembly-import-project', sceneId = 'assembly-import-scene';
    const requests = [];
    const designer = { templates: [], instances: [], product: null, activeProductId: '', members: [], joints: [], parts: [], manufacturingGroups: [] };
    const state = { sceneId, undoRedo: { revision: 1, canUndo: false, canRedo: false }, tubeDesigner: designer };
    const view = getProjectView(projectId);
    Object.assign(view, { scene: structuredClone(state), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true });
    const snapshot = { viewId: 'assembly-import-view', revision: '1', rows: [] };
    const box = new THREE.BoxGeometry(600, 30, 20);
    const resources = { async get(url) { return { url, version: 1, type: 'geometry', data: {
      kind: 'mesh', positions: [...box.attributes.position.array], indices: [...box.index.array] } }; } };
    const sceneProxy = { state, pdo: { enabled: false }, resources,
      views: { async start() { return { snapshot, async poll() { return snapshot; }, async stop() {} }; } },
      async getState() { return structuredClone(state); }, async invoke(method, payload, options) {
        requests.push({ scope: 'scene', method, payload, options });
        if (method === 'TubeDesigner.List') return { tubeDesigner: structuredClone(designer) };
        if (method === 'TubeDesigner.GetPunchTools') return { tools: [] };
        if (method === 'TubeDesigner.GetAssemblyTemplates') return { assemblies: structuredClone(templates), errors: [] };
        if (method === 'TubeDesigner.ResolveAssemblyTemplatePreview' && payload.finishedOnly) return {
          schema: 'icax.finished-product-preview', schemaVersion: 1, layoutShape: payload.finishedProduct.shapeId,
          finishedProduct: structuredClone(payload.finishedProduct), designParts: Object.entries(payload.finishedProduct.spans).map(([id, span], index) => ({
            id, role: id, label: id, request: { length: span.length, profileRef: span.profileRef, parameters: span.parameters },
            matrix: [1, 0, 0, 0, 0, 1, 0, index * 50, 0, 0, 1, 0, 0, 0, 0, 1] })) };
        if (method === 'TubeDesigner.PreviewPunchWizard') return { baseGeometry: { url: `memory://assembly-import/${payload.previewResourceKey}`, version: 1 } };
        if (method === 'TubeDesigner.CheckAssemblyTemplateApplicability') return {
          schema: 'icax.assembly-applicability', schemaVersion: 1, templateId: payload.templateId, applicable: true };
        throw new Error('Unexpected scene request in assembly import test: ' + method);
      } };
    const projectState = { projectId, projectName: '装配导入浏览器验证', mainScene: state };
    const projectProxy = { projectId, state: projectState, getMainScene() { return sceneProxy; } };
    const productState = { productId: 'icax.tube-designer', productName: 'TubeDesigner', isStarted: true,
      frontendEntry: '/src/apps/tube-designer/webpage/entry.mjs', catalogs: [{ mainProject: projectState }], projectFile: { fileExtensions: ['ictd'] } };
    const settings = { enabled: false, inputDirectory: '', tempDirectory: '', outputDirectory: '' };
    const fixture = { pickerMode: 'success', pendingImport: null, requests, view, imported, sceneProxy };
    const productProxy = { productId: productState.productId, state: productState, async getState() { return productState; },
      getProject() { return projectProxy; }, async openProjectCatalog() { return { projectProxy, sceneProxy, catalog: { mainProject: projectState } }; },
      async invoke(method, payload, options) {
        requests.push({ scope: 'product', method, payload, options });
        if (method === 'TubeDesigner.GetBatchExcelAutomationSettings') return { settings: structuredClone(settings) };
        if (method === 'TubeDesigner.ScanBatchExcelAutomation') return { settings: structuredClone(settings), files: [], errors: [] };
        if (method === 'TubeDesigner.ListUserData') return { customers: [], parameterPresets: [], profiles: [], punchTools: [], productTemplates: [], systemProfiles: [], templateProfiles: [] };
        if (method === 'TubeDesigner.ImportAssemblyTemplatePackage') return new Promise((resolve, reject) => {
          fixture.pendingImport = { resolve, reject };
        });
        throw new Error('Unexpected product request in assembly import test: ' + method);
      } };
    const appProxy = { bridge: { async openFileDialog(options) {
        requests.push({ scope: 'bridge', method: 'openFileDialog', payload: options });
        if (fixture.pickerMode === 'cancel') return '';
        return fixture.pickerMode === 'error' ? 'C:/test/invalid.itat' : 'C:/test/new-assembly.itat';
      } }, products: new Map([[productState.productId, productProxy]]), async getState() { return { products: [productState] }; },
      getProduct() { return productProxy; }, async startProduct() { return productProxy; } };
    Object.assign(fixture, { appProxy, productProxy });
    globalThis.__assemblyImport = fixture;
    await import('/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs');
  }, { templates, imported });
  await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().startupPhase === 'ready');
  const tabs = await page.locator('[data-action="select-ribbon-tab"]').allTextContents();
  assert(!tabs.some(title => title.trim() === '加工'), '加工页签应隐藏');
  assert.equal(await page.locator('[data-command-id="designer.send-to-machining"]').count(), 0);
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="resources"]').click();
  await page.locator('[data-command-id="resources.assemblies"]').click();
  await page.waitForFunction(() => __assemblyImport.view.activeAreaId === 'assemblies' && __assemblyImport.view.tubeDesignerAssemblyLibrary?.catalogueStatus === 'ready' && !__assemblyImport.view.pending);
  await page.locator(".project-progress-backdrop").waitFor({ state: "detached" });
  await page.waitForFunction(() => !__assemblyImport.view.progress && !__assemblyImport.view.tubeDesignerLoadProgress && !__assemblyImport.view.pending && !__assemblyImport.view.activeAreaAction);
  const importButton = page.locator('[data-action="ribbon-command"][data-command-id="assemblies.import-package"]');
  await importButton.waitFor({ state: 'visible' });
  assert.equal(await importButton.isEnabled(), true);
  assert.equal((await importButton.innerText()).trim(), '导入');
  await page.locator('.tube-connection-library-search input').fill('孔系');
  await page.locator('.tube-connection-library-search input').press('Tab');
  const scrollingFixtureStyle = await page.addStyleTag({ content: '.cam-context-pane,.cam-info-pane{display:block!important;overflow:auto!important;height:440px!important;max-height:440px!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:400px}.tube-connection-library-list{max-height:180px!important;overflow:auto!important}.tube-connection-library-editor-body{max-height:220px!important;overflow:auto!important}.tube-connection-library-editor-body::after{content:"";display:block;height:500px}' });
  await page.locator('[data-tube-assembly-scene-viewport] .icax-three-viewport-canvas').waitFor({ state: 'attached' });
  await page.waitForFunction(() => !__assemblyImport.view.tubeDesignerAssemblyLibrary.previewRequest);
  await page.evaluate(() => {
    const diameter = document.querySelector('[data-tube-assembly-parameter="nominalDiameter"]');
    diameter.value = '12'; diameter.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await page.waitForFunction(() => !__assemblyImport.view.activeAreaAction && !__assemblyImport.view.tubeDesignerAssemblyLibrary.previewRequest);
  await page.evaluate(() => {
    __assemblyImport.beforeImport = { selectedId: __assemblyImport.view.tubeDesignerAssemblyLibrary.selectedId,
      search: __assemblyImport.view.tubeDesignerAssemblyLibrary.search,
      drafts: JSON.stringify(__assemblyImport.view.tubeDesignerAssemblyLibrary.parameterDrafts) };
    __assemblyImport.beforeBusySearch = document.querySelector(".tube-connection-library-search input");
  });
  await importButton.click();
  await page.waitForFunction(() => !!__assemblyImport.pendingImport);
  await page.evaluate(() => {
    const search = document.querySelector('.tube-connection-library-search input');
    const canvas = document.querySelector('[data-tube-assembly-scene-viewport] .icax-three-viewport-canvas');
    const left = document.querySelector('.cam-context-pane'), right = document.querySelector('.cam-info-pane');
    const list = document.querySelector('.tube-connection-library-list'), editor = document.querySelector('.tube-connection-library-editor-body');
    let inputEvents = 0, canvasEvents = 0;
    search.addEventListener('test-listener', () => inputEvents++);
    canvas.addEventListener('test-listener', () => canvasEvents++);
    __assemblyImport.nodes = { search, canvas, left, right, list, editor };
    __assemblyImport.listenerCounts = () => ({ inputEvents, canvasEvents });
  });
  await page.evaluate(() => { __assemblyImport.busyStartSearchWasReplaced = __assemblyImport.beforeBusySearch !== __assemblyImport.nodes.search; });
  await page.evaluate(() => {
    const { search, left, right, list, editor } = __assemblyImport.nodes;
    search.focus({ preventScroll: true }); search.setSelectionRange(1, 2, 'backward');
    // Continue operating after the request starts: restoration must use these positions.
    left.scrollTop = 90; right.scrollTop = 100; list.scrollTop = 130; editor.scrollTop = 170;
    __assemblyImport.latestScroll = [left.scrollTop, right.scrollTop, list.scrollTop, editor.scrollTop];
    __assemblyImport.pendingImport.resolve({ template: structuredClone(__assemblyImport.imported), libraryScope: 'user' });
    __assemblyImport.pendingImport = null;
  });
  const importedCard = page.locator(`[data-tube-assembly-id="${imported.id}"]`);
  await importedCard.waitFor({ state: 'attached' });
  const stable = await page.evaluate(() => {
    const nodes = __assemblyImport.nodes, state = __assemblyImport.view.tubeDesignerAssemblyLibrary;
    nodes.search.dispatchEvent(new Event('test-listener')); nodes.canvas.dispatchEvent(new Event('test-listener'));
    return { sameSearch: nodes.search === document.querySelector('.tube-connection-library-search input'),
      sameCanvas: nodes.canvas === document.querySelector('[data-tube-assembly-scene-viewport] .icax-three-viewport-canvas'),
      sameLeft: nodes.left === document.querySelector('.cam-context-pane'), sameRight: nodes.right === document.querySelector('.cam-info-pane'),
      sameList: nodes.list === document.querySelector('.tube-connection-library-list'), sameEditor: nodes.editor === document.querySelector('.tube-connection-library-editor-body'),
      focused: document.activeElement === nodes.search, selection: [nodes.search.selectionStart, nodes.search.selectionEnd, nodes.search.selectionDirection],
      latestScroll: __assemblyImport.latestScroll, actualScroll: [nodes.left.scrollTop, nodes.right.scrollTop, nodes.list.scrollTop, nodes.editor.scrollTop],
      busyStartSearchWasReplaced: __assemblyImport.busyStartSearchWasReplaced, preservationBoundary: "after native request starts through its asynchronous response",
      beforeImport: __assemblyImport.beforeImport, afterImport: { selectedId: state.selectedId, search: state.search, drafts: JSON.stringify(state.parameterDrafts) },
      listeners: __assemblyImport.listenerCounts() };
  });
  writeFileSync(resolve(output, 'interaction-diagnostic.json'), JSON.stringify(stable, null, 2));
  assert.equal(stable.sameSearch && stable.sameCanvas && stable.sameLeft && stable.sameRight && stable.sameList && stable.sameEditor, true);
  assert.equal(stable.focused, true);
  assert.deepEqual(stable.selection, [1, 2, 'backward']);
  assert.deepEqual(stable.actualScroll, stable.latestScroll);
  assert(stable.latestScroll.every(value => value > 0), '四个容器都应实际滚动');
  assert.deepEqual(stable.afterImport, stable.beforeImport);
  assert.equal(JSON.parse(stable.afterImport.drafts)['mechanical-fastener'].nominalDiameter, 12);
  assert.deepEqual(stable.listeners, { inputEvents: 1, canvasEvents: 1 });
  assert.match(await importedCard.innerText(), /我的/);
  await page.screenshot({ path: resolve(output, 'import-added-with-editing-preserved.png') });
  await importedCard.click();
  await page.waitForFunction(id => __assemblyImport.view.tubeDesignerAssemblyLibrary.selectedId === id && !__assemblyImport.view.tubeDesignerAssemblyLibrary.selectionRequest, imported.id);
  assert.match(await page.locator('.tube-connection-library-editor-heading').innerText(), /孔系扩展工艺/);
  await page.waitForFunction(() => !__assemblyImport.view.tubeDesignerAssemblyLibrary.previewRequest);
  const resetStyles = await page.evaluate(() => {
    const reset = document.querySelector('[data-cam-action="tube-designer-finished-reset"]');
    const other = document.querySelector('[data-cam-action="tube-designer-assembly-reset"]');
    const properties = ['backgroundColor', 'color', 'borderColor', 'borderRadius', 'fontSize', 'padding'];
    const read = node => Object.fromEntries(properties.map(key => [key, getComputedStyle(node)[key]]));
    return reset && other ? { reset: read(reset), other: read(other) } : null;
  });
  assert(resetStyles, '需存在重置成品与重置工艺两个按钮');
  assert.deepEqual(resetStyles.reset, resetStyles.other, '两个重置按钮应采用同一中性样式');
  await page.evaluate(() => {
    const search = document.querySelector('.tube-connection-library-search input');
    const diameter = document.querySelector('[data-tube-assembly-parameter="nominalDiameter"]');
    __assemblyImport.conditionalNodes = { search, diameter, canvas: __assemblyImport.nodes.canvas };
    search.focus({ preventScroll: true }); search.setSelectionRange(0, 2);
    const fastener = document.querySelector('[data-tube-assembly-parameter="fastenerType"]');
    fastener.value = 'adjustableBolt'; fastener.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await page.locator('[data-tube-assembly-parameter="adjustment"]').waitFor({ state: 'attached' });
  await page.waitForFunction(() => !__assemblyImport.view.activeAreaAction);
  const conditionalAdded = await page.evaluate(() => {
    const nodes = __assemblyImport.conditionalNodes;
    return { search: nodes.search === document.querySelector('.tube-connection-library-search input'),
      diameter: nodes.diameter === document.querySelector('[data-tube-assembly-parameter="nominalDiameter"]'),
      canvas: nodes.canvas === document.querySelector('[data-tube-assembly-scene-viewport] .icax-three-viewport-canvas'),
      focus: document.activeElement === nodes.search, selection: [nodes.search.selectionStart, nodes.search.selectionEnd] };
  });
  assert.equal(conditionalAdded.search && conditionalAdded.diameter && conditionalAdded.canvas && conditionalAdded.focus, true);
  assert.deepEqual(conditionalAdded.selection, [0, 2]);
  await page.evaluate(() => {
    const fastener = document.querySelector('[data-tube-assembly-parameter="fastenerType"]');
    fastener.value = 'bolt'; fastener.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await page.locator('[data-tube-assembly-parameter="adjustment"]').waitFor({ state: 'detached' });
  await page.waitForFunction(() => !__assemblyImport.view.activeAreaAction);
  const conditionalRemoved = await page.evaluate(() => {
    const nodes = __assemblyImport.conditionalNodes;
    return nodes.diameter === document.querySelector('[data-tube-assembly-parameter="nominalDiameter"]')
      && nodes.search === document.activeElement && nodes.canvas === document.querySelector('[data-tube-assembly-scene-viewport] .icax-three-viewport-canvas');
  });
  assert.equal(conditionalRemoved, true);
  await page.evaluate(() => { __assemblyImport.pickerMode = 'cancel'; });
  const beforeCancel = await page.evaluate(() => ({ count: __assemblyImport.view.tubeDesignerAssemblyTemplates.length,
    selected: __assemblyImport.view.tubeDesignerAssemblyLibrary.selectedId,
    imports: __assemblyImport.requests.filter(row => row.method === 'TubeDesigner.ImportAssemblyTemplatePackage').length }));
  await importButton.click();
  await page.waitForFunction(() => __assemblyImport.requests.filter(row => row.method === 'openFileDialog').length === 2);
  const afterCancel = await page.evaluate(() => ({ count: __assemblyImport.view.tubeDesignerAssemblyTemplates.length,
    selected: __assemblyImport.view.tubeDesignerAssemblyLibrary.selectedId,
    imports: __assemblyImport.requests.filter(row => row.method === 'TubeDesigner.ImportAssemblyTemplatePackage').length }));
  assert.deepEqual(afterCancel, beforeCancel);
  await page.evaluate(() => { __assemblyImport.pickerMode = 'error'; });
  await importButton.click();
  await page.waitForFunction(() => !!__assemblyImport.pendingImport);
  await page.evaluate(() => { __assemblyImport.pendingImport.reject(new Error('装配工艺包无效：缺少 assembly.json')); __assemblyImport.pendingImport = null; });
  await page.waitForFunction(() => document.querySelector('.log-list')?.textContent.includes('缺少 assembly.json'));
  const afterError = await page.evaluate(() => ({ count: __assemblyImport.view.tubeDesignerAssemblyTemplates.length,
    selected: __assemblyImport.view.tubeDesignerAssemblyLibrary.selectedId, requests: __assemblyImport.requests }));
  assert.equal(afterError.count, beforeCancel.count);
  assert.equal(afterError.selected, imported.id);
  const imports = afterError.requests.filter(call => call.method === 'TubeDesigner.ImportAssemblyTemplatePackage');
  assert.equal(imports.length, 2);
  assert(imports.every(call => call.scope === 'product'));
  assert(imports.every(call => call.options?.timeoutMs === 120000));
  assert.deepEqual(imports[0].payload, { sourcePath: 'C:/test/new-assembly.itat' });
  const pickers = afterError.requests.filter(call => call.method === 'openFileDialog');
  assert.equal(pickers.length, 3);
  for (const picker of pickers) {
    assert.equal(picker.payload.title, '导入装配工艺（.itat）');
    assert.deepEqual(picker.payload.filters, [{ name: 'iCAX 装配工艺包', extensions: ['itat'] }]);
  }
  assert.deepEqual(errors, []);
  await scrollingFixtureStyle.evaluate(node => node.remove());
  await page.evaluate(() => { document.querySelector('.tube-connection-library-list').scrollTop = 100000; document.querySelector('.tube-connection-library-editor-body').scrollTop = 0; });
  await page.screenshot({ path: resolve(output, 'imported-selected.png') });
  await page.evaluate(async () => (await import('/src/apps/tube-designer/webpage/batchExcelAutomation.mjs')).stopBatchExcelAutomation());
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true,
    resourceRibbonMouseNavigation: true, importRibbonMouseClick: true, hiddenMachiningTab: true,
    importedCardMarkedMine: true, importedCardCanBeSelected: true, currentEditingPreserved: stable,
    cancellationDoesNotInvokeImport: true, errorDoesNotChangeCatalogueOrSelection: true, resetButtonStyles: resetStyles,
    conditionalFields: { addedWithoutReplacingControls: conditionalAdded, removedWithoutReplacingControls: conditionalRemoved },
    requests: afterError.requests, pageErrors: errors, nativeTransport: 'controlled native protocol fixture',
    filePicker: 'controlled desktop picker fixture', standaloneCefEndToEnd: false }, null, 2));
  console.log('PASS AppShell assembly import: mouse navigation, direct product import, mine card selection, cancel/error, stable editing and four latest scroll positions.');
} finally { await browser.close(); }
