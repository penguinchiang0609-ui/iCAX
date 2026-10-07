// Actual AppShell resource navigation and production DOM/Three viewport.
// Native scene state and preview responses are explicit controlled fixtures, not native business acceptance.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, readdirSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetRoot, browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const assemblyRoot = resolve(browserAssetRoot, 'apps/tube-designer/templates/assembly');
const templates = readdirSync(assemblyRoot).filter(name => existsSync(resolve(assemblyRoot, name, 'assembly.json')))
  .map(name => ({ ...JSON.parse(readFileSync(browserAssetPath('apps/tube-designer/templates/assembly/' + name + '/assembly.json'), 'utf8')), libraryScope: 'builtin' }));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('assembly-resource-entry-20261006');
const browser = await chromium.launch({ headless: true, channel: 'chrome' });
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 960 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://assembly-resource-entry.test/**', route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"></head><body><div id="app"></div></body></html>' });
    if (pathname === '/src/iCAX-UI/SDK/runtime.mjs') return route.fulfill({ contentType: 'text/javascript', body: 'export async function connectApplication(){return globalThis.__assemblyResource.appProxy;}' });
    if (!pathname.startsWith('/src/')) return route.abort();
    const file = browserAssetPath(pathname.slice('/src/'.length));
    return route.fulfill({ contentType: pathname.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(file, 'utf8') });
  });
  await page.goto('http://assembly-resource-entry.test/');
  await page.evaluate(async ({ templates, imported }) => {
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const projectId = 'assembly-resource-entry-project', sceneId = 'assembly-resource-entry-scene';
    const requests = [];
    const productId = 'persisted-security-window', generationId = 'persisted-security-window-run';
    const designer = { templates: [], instances: [{ entityId: productId, name: '当前防盗窗', templateId: 'single-face-security-window', productionQuantity: 2 }],
      product: { entityId: productId, name: '当前防盗窗', templateId: 'single-face-security-window', activeGenerationRunId: generationId,
        parameters: {}, productionQuantity: 2 }, activeProductId: productId, generationRun: { entityId: generationId },
      members: [{ entityId: 'persisted-member-a' }, { entityId: 'persisted-member-b' }], joints: [], parts: [], manufacturingGroups: [] };
    const state = { sceneId, undoRedo: { revision: 1, canUndo: false, canRedo: false }, tubeDesigner: designer };
    const view = getProjectView(projectId);
    Object.assign(view, { scene: structuredClone(state), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true, tubeDesignerAssemblyTemplates: structuredClone(templates),
      tubeDesignerAssemblyLibrary: { selectedId: 'wrap-a-over-b', workMode: 'product', workModeUserSelected: true } });
    const snapshot = { viewId: 'assembly-resource-entry-view', revision: '1', rows: [] };
    const box = new THREE.BoxGeometry(600, 30, 20);
    const resources = { async get(url) { return { url, version: 1, type: 'geometry', data: {
      kind: 'mesh', positions: [...box.attributes.position.array], indices: [...box.index.array] } }; } };
    const sceneProxy = { state, pdo: { enabled: false }, resources,
      views: { async start() { return { snapshot, async poll() { return snapshot; }, async stop() {} }; } },
      async getState() { return structuredClone(state); }, async invoke(method, payload, options) {
        requests.push({ scope: 'scene', method, payload, options });
        if (method === 'TubeDesigner.List') return { tubeDesigner: structuredClone(designer) };
        if (method === 'TubeDesigner.GetPunchTools') return { tools: [] };
        if (method === 'TubeDesigner.GetAssemblyTemplates') {
          if (fixture.holdCatalogue) return new Promise(resolve => { fixture.completeCatalogue = () => resolve({ assemblies: structuredClone(templates), errors: [] }); });
          return { assemblies: structuredClone(templates), errors: [] };
        }
        if (method === 'TubeDesigner.ResolveAssemblyTemplatePreview' && payload.finishedOnly) return {
          schema: 'icax.finished-product-preview', schemaVersion: 1, layoutShape: payload.finishedProduct.shapeId,
          finishedProduct: structuredClone(payload.finishedProduct), designParts: Object.entries(payload.finishedProduct.spans).map(([id, span], index) => ({
            id, role: id, label: id, request: { length: span.length, profileRef: span.profileRef, parameters: span.parameters },
            matrix: [1, 0, 0, 0, 0, 1, 0, index * 50, 0, 0, 1, 0, 0, 0, 0, 1] })) };
        if (method === 'TubeDesigner.PreviewPunchWizard') return { baseGeometry: { url: `memory://assembly-resource-entry/${payload.previewResourceKey}`, version: 1 } };
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
    globalThis.__assemblyResource = fixture;
    await import('/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs');
  }, { templates, imported: templates[0] });
  await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().startupPhase === 'ready');
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="resources"]').click();
  await page.locator('[data-command-id="resources.assemblies"]').click();
  await page.waitForFunction(() => __assemblyResource.view.activeAreaId === 'assemblies'
    && __assemblyResource.view.tubeDesignerAssemblyLibrary?.catalogueStatus === 'ready' && !__assemblyResource.view.pending);
  await page.locator(".project-progress-backdrop").waitFor({ state: "detached" });
  await page.waitForFunction(() => !__assemblyResource.view.progress && !__assemblyResource.view.tubeDesignerLoadProgress && !__assemblyResource.view.pending && !__assemblyResource.view.activeAreaAction);
  await page.locator('[data-tube-assembly-scene-viewport] .icax-three-viewport-canvas').waitFor({ state: 'attached' });
  await page.waitForFunction(() => !__assemblyResource.view.tubeDesignerAssemblyLibrary.previewRequest
    && !__assemblyResource.view.tubeDesignerAssemblyLibrary.selectionRequest);
  const exampleIds = templates.filter(item => item.catalogueHidden !== true && item.exampleInput?.shapeId).map(item => item.id);
  const cards = await page.locator('.tube-connection-library-card').count();
  assert.equal(cards, exampleIds.length, '已有产品也应显示完整的独立工艺目录');
  assert(cards >= 17, '应保留原有的 17 项工艺目录');
  const initial = await page.evaluate(() => ({
    activeProductId: __assemblyResource.view.scene.tubeDesigner.activeProductId,
    generationRunId: __assemblyResource.view.scene.tubeDesigner.generationRun.entityId,
    selectedId: __assemblyResource.view.tubeDesignerAssemblyLibrary.selectedId,
    header: document.querySelector('.tube-profile-library-heading')?.textContent,
    editor: document.querySelector('.tube-connection-library-editor-heading')?.textContent,
    finishedEditor: !!document.querySelector('[data-finished-product-editor]'),
    previewLabel: document.querySelector('[data-tube-assembly-preview-pane]')?.getAttribute('aria-label'),
    snapshotRows: __assemblyResource.view.tubeDesignerAssemblyLibrary.preview?.finishedShape?.rows?.map(item => item.entityId),
  }));
  assert.equal(initial.activeProductId, 'persisted-security-window');
  assert.equal(initial.generationRunId, 'persisted-security-window-run');
  assert.equal(initial.selectedId, 'wrap-a-over-b');
  assert.match(initial.header, /17 个模板/);
  assert.match(initial.editor, /长短包接/);
  assert.equal(initial.finishedEditor, true);
  assert.doesNotMatch(initial.previewLabel, /当前防盗窗|产品生成模型/);
  assert.equal(await page.locator('[data-cam-action="tube-designer-assembly-work-mode"], [data-cam-action="tube-designer-assembly-select-connection"], [data-cam-change-action^="tube-designer-binding-"], [data-cam-action^="tube-designer-binding-"]').count(), 0);
  assert.equal(await page.locator('[data-command-id="designer.open-assembly-process"]').count(), 0);
  assert.doesNotMatch(await page.locator('.tube-connection-library-panel, .tube-connection-library-editor').allTextContents().then(items => items.join('\n')),
    /产品节点|工艺示例|选择连接节点|当前产品连接|已提交模型/);
  await page.screenshot({ path: resolve(output, 'generated-product-still-independent-assembly.png') });

  // A native catalogue response while the user continues editing must patch
  // the current DOM and latest interaction state, including nested scrolling.
  const scrollingStyle = await page.addStyleTag({ content: '.cam-context-pane,.cam-info-pane{display:block!important;overflow:auto!important;height:440px!important;max-height:440px!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:400px}.tube-connection-library-list{max-height:180px!important;overflow:auto!important}.tube-connection-library-editor-body{max-height:220px!important;overflow:auto!important}.tube-connection-library-editor-body::after{content:"";display:block;height:500px}' });
  await page.evaluate(() => { __assemblyResource.holdCatalogue = true; __assemblyResource.beforeBusySearch = document.querySelector(".tube-connection-library-search input"); });
  await page.locator('[data-cam-action="tube-designer-assembly-retry"]').click();
  await page.waitForFunction(() => !!__assemblyResource.completeCatalogue);
  await page.evaluate(() => {
    const nodes = { input: document.querySelector('.tube-connection-library-search input'),
      canvas: document.querySelector('[data-tube-assembly-scene-viewport] .icax-three-viewport-canvas'),
      left: document.querySelector('.cam-context-pane'), right: document.querySelector('.cam-info-pane'),
      list: document.querySelector('.tube-connection-library-list'), editor: document.querySelector('.tube-connection-library-editor-body') };
    let inputEvents = 0, canvasEvents = 0;
    nodes.input.addEventListener('test-listener', () => inputEvents++);
    nodes.canvas.addEventListener('test-listener', () => canvasEvents++);
    __assemblyResource.busyStartSearchWasReplaced = __assemblyResource.beforeBusySearch !== nodes.input;
    __assemblyResource.nodes = nodes;
    __assemblyResource.listeners = () => ({ inputEvents, canvasEvents });
  });
  await page.evaluate(() => {
    const { input, left, right, list, editor } = __assemblyResource.nodes;
    input.value = '未提交的搜索'; input.focus({ preventScroll: true }); input.setSelectionRange(1, 4, 'backward');
    left.scrollTop = 90; right.scrollTop = 100; list.scrollTop = 130; editor.scrollTop = 170;
    __assemblyResource.latestScroll = [left.scrollTop, right.scrollTop, list.scrollTop, editor.scrollTop];
    __assemblyResource.completeCatalogue(); __assemblyResource.completeCatalogue = null; __assemblyResource.holdCatalogue = false;
  });
  await page.waitForFunction(() => __assemblyResource.view.tubeDesignerAssemblyLibrary.catalogueStatus === 'ready'
    && !__assemblyResource.view.activeAreaAction && !__assemblyResource.view.tubeDesignerAssemblyLibrary.previewRequest);
  const interaction = await page.evaluate(() => {
    const nodes = __assemblyResource.nodes;
    nodes.input.dispatchEvent(new Event('test-listener')); nodes.canvas.dispatchEvent(new Event('test-listener'));
    return { sameInput: nodes.input === document.querySelector('.tube-connection-library-search input'),
      sameCanvas: nodes.canvas === document.querySelector('[data-tube-assembly-scene-viewport] .icax-three-viewport-canvas'),
      sameLeft: nodes.left === document.querySelector('.cam-context-pane'), sameRight: nodes.right === document.querySelector('.cam-info-pane'),
      sameList: nodes.list === document.querySelector('.tube-connection-library-list'), sameEditor: nodes.editor === document.querySelector('.tube-connection-library-editor-body'),
      busyStartSearchWasReplaced: __assemblyResource.busyStartSearchWasReplaced, preservationBoundary: "after native request starts through its asynchronous response",
      focus: document.activeElement === nodes.input, selection: [nodes.input.selectionStart, nodes.input.selectionEnd, nodes.input.selectionDirection],
      text: nodes.input.value, actualScroll: [nodes.left.scrollTop, nodes.right.scrollTop, nodes.list.scrollTop, nodes.editor.scrollTop],
      latestScroll: __assemblyResource.latestScroll, listeners: __assemblyResource.listeners() };
  });
  writeFileSync(resolve(output, 'interaction-diagnostic.json'), JSON.stringify(interaction, null, 2));
  assert.equal(interaction.sameInput && interaction.sameCanvas && interaction.sameLeft && interaction.sameRight && interaction.sameList && interaction.sameEditor && interaction.focus, true);
  assert.equal(interaction.text, '未提交的搜索');
  assert.deepEqual(interaction.selection, [1, 4, 'backward']);
  assert.deepEqual(interaction.actualScroll, interaction.latestScroll);
  assert(interaction.latestScroll.every(value => value > 0), '四个容器都应实际滚动');
  assert.deepEqual(interaction.listeners, { inputEvents: 1, canvasEvents: 1 });
  await scrollingStyle.evaluate(node => node.remove());
  const requests = await page.evaluate(() => __assemblyResource.requests);
  assert(!requests.some(item => /GetProductAssemblyConnections|GetProductAssemblyBindings|PreviewProductAssemblyBinding/.test(item.method)), '资源工艺页不应读取或预览当前产品节点');
  assert(!requests.some(item => item.method === 'TubeDesigner.List' && item.payload?.includeManufacturingGeometry), '不应恢复当前产品几何来替换独立示例');
  assert.deepEqual(errors, []);
  await page.evaluate(async () => (await import('/src/apps/tube-designer/webpage/batchExcelAutomation.mjs')).stopBatchExcelAutomation());
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, realAppShellResourceMouseEntry: true,
    generatedProductFixture: true, ignoredPreviouslySelectedProductMode: true, cards, independentExample: initial,
    removedProductNodeControls: true, noProductNodeReadsOrGeometryHydration: true, interaction, requests, pageErrors: errors,
    nativeTransport: 'controlled native protocol fixture', standaloneCefEndToEnd: false }, null, 2));
  console.log('PASS assembly resource entry: generated product retains all 17 independent processes, example editor and canvas; product node mode removed; asynchronous editing preserved.');
} finally { await browser.close(); }
