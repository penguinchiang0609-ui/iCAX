// Actual shipped AppShell, nesting page, ribbon and metadata editor.
// Native CAD recognition/listing/preview resources are explicit controlled fixtures.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';
import { nativeSectionIdentity } from './fixtures/nestingSectionIdentity.mjs';

const profile = { sectionIdentity: nativeSectionIdentity('direct-import-rect-40-20-t1.5'),
  id: 'imported-rect-40-20-1.5', kind: 'rect', displayName: '矩形管',
  specification: '40 × 20 × 壁厚 1.5 mm', width: 40, depth: 20, wallThickness: 1.5, hollow: true };

async function verifyBlankFit(page, scope) {
  const before = await page.evaluate(async scope => {
    const f = __directImport;
    const viewport = scope === 'picker' ? f.view.tubeDesignerNestingFilePicker.viewport : f.view.viewport;
    if (!viewport.blankDoubleClickFitEnabled || !viewport.getVisibleBounds()) throw Error(`Blank fit not configured: ${scope}`);
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const canvas = viewport.renderer.domElement, pose = viewport.getCameraState();
    viewport.setCameraState({ ...pose, radius: pose.radius * 0.2,
      target: { ...pose.target, x: pose.target.x + 1200 } });
    const selected = [...viewport.selectedObjectIds];
    const previousHandler = viewport.options.onCameraChange, reasons = [];
    viewport.options.onCameraChange = (state, detail) => { reasons.push(detail.reason); previousHandler?.(state, detail); };
    f.blankFit = { viewport, canvas, previousHandler, reasons, selected, pose:viewport.getCameraState() };
    const rect = canvas.getBoundingClientRect(), raycaster = new THREE.Raycaster();
    for (let y = 100; y < rect.height - 110; y += 23) for (let x = 70; x < rect.width - 100; x += 23) {
      const point = { x:rect.left + x, y:rect.top + y };
      if (document.elementFromPoint(point.x, point.y) !== canvas) continue;
      raycaster.setFromCamera(new THREE.Vector2(x / rect.width * 2 - 1, 1 - y / rect.height * 2), viewport.camera);
      if (!raycaster.intersectObjects([...viewport.sceneObjects.values()].filter(object => object.visible), false).length)
        return { ...point, pose:f.blankFit.pose, selected, scope };
    }
    throw Error(`No actual canvas blank point: ${scope}`);
  }, scope);
  await page.mouse.dblclick(before.x, before.y, { button:'left', delay:50 });
  await page.waitForFunction(() => __directImport.blankFit.reasons.includes('fit-viewport'));
  const result = await page.evaluate(async () => {
    const f = __directImport.blankFit, viewport = f.viewport;
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const bounds = viewport.getVisibleBounds(), points = [];
    for (const x of bounds.min[0] === bounds.max[0] ? [bounds.min[0]] : [bounds.min[0],bounds.max[0]])
      for (const y of [bounds.min[1],bounds.max[1]]) for (const z of [bounds.min[2],bounds.max[2]])
        points.push(new THREE.Vector3(x,y,z).project(viewport.camera));
    viewport.options.onCameraChange = f.previousHandler;
    return { enabled:viewport.blankDoubleClickFitEnabled, camera:viewport.getCameraState(),
      fitCount:f.reasons.filter(reason => reason === 'fit-viewport').length, selected:[...viewport.selectedObjectIds],
      framing:Math.max(...points.flatMap(point => [Math.abs(point.x),Math.abs(point.y)])),
      depthVisible:points.every(point => point.z >= -1 && point.z <= 1),
      sameCanvas:f.canvas === viewport.renderer.domElement && f.canvas.isConnected };
  });
  assert.equal(result.fitCount, 1);
  assert.equal(result.camera.theta, before.pose.theta);
  assert.equal(result.camera.phi, before.pose.phi);
  assert.equal(result.camera.projectionMode, before.pose.projectionMode);
  assert.deepEqual(result.selected, before.selected);
  assert.equal(result.depthVisible && result.sameCanvas, true);
  assert(result.framing < 0.9, `${scope}: model must fit with visible margin`);
  return { scope, ...result };
}
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('nesting-direct-import-20261006');
const browser = await chromium.launch({ headless: true, channel: 'chrome' });
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 960 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://nesting-direct-import.test/**', route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"></head><body><div id="app"></div></body></html>' });
    if (pathname === '/src/iCAX-UI/SDK/runtime.mjs') return route.fulfill({ contentType: 'text/javascript', body: 'export async function connectApplication(){return globalThis.__directImport.appProxy;}' });
    if (!pathname.startsWith('/src/')) return route.abort();
    return route.fulfill({ contentType: pathname.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(browserAssetPath(pathname.slice('/src/'.length)), 'utf8') });
  });
  await page.goto('http://nesting-direct-import.test/');
  await page.evaluate(async profile => {
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const { encodeNestingGeometry } = await import('/src/apps/tube-designer/webpage/nestingPreview.mjs');
    const projectId = 'nesting-direct-import-project', sceneId = 'nesting-direct-import-scene';
    const requests = [];
    const makePart = (id, name, quantity = 1) => ({ entityId: id, generationRunId: 'import-batch', index: 1,
      partNumber: name, name, role: '导入零件', quantity, profile: structuredClone(profile), partKind: 'tube', length: 508,
      thumbnailGeometryResourceId:'geometry://nesting/' + id, thumbnailGeometryResourceVersion:1,
      manufacturingGeometryResourceVersion:1,
      properties: { 'tubeDesigner.profile': structuredClone(profile), 'manufacturing.partKind': 'tube',
        'manufacturing.materialCategory': 'tube', 'manufacturing.sourcing': 'made', 'manufacturing.process': 'straight-cut',
        'manufacturing.requiresBending': false, 'manufacturing.imported': true, 'manufacturing.import.sourceFileName': name + '.step',
        'manufacturing.geometryMeasurement': { openingCount: 4 }, 'manufacturing.material': '' } });
    const designer = { templates: [], instances: [], product: null, activeProductId: '', members: [], joints: [], parts: [], manufacturingGroups: [],
      nestingGroups: [{ productEntityId: 'import-batch', generationRunId: 'import-batch', name: '导入零件',
        parts: Array.from({ length: 40 }, (_, i) => makePart('existing-part-' + i, '已有加工零件 ' + i)) }], nestingSettings: {} };
    const state = { sceneId, undoRedo: { revision: 1, canUndo: false, canRedo: false }, tubeDesigner: designer };
    const view = getProjectView(projectId);
    Object.assign(view, { scene: structuredClone(state), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true,
      tubeDesignerNestingSelectedPartIds: ['existing-part-0'], tubeDesignerActiveNestingPartId: 'existing-part-0',
      tubeDesignerActivePartId: 'existing-part-0', tubeDesignerNestingSelectionKind: 'part' });
    const fixture = { requests, view, designer, pendingDirectory: null, directories: [], previews: [], pendingImport: null, confirmationSeen: false };
    const snapshot = { viewId: 'nesting-direct-import-view', revision: '1', rows: [] };
    const sceneProxy = { state, pdo: { enabled: false }, resources: { async get(url, options) {
      requests.push({ scope: 'scene-resource', method: 'get', payload: { url, options } });
      const shape = new THREE.Shape(); shape.moveTo(-20, -10); shape.lineTo(20, -10); shape.lineTo(20, 10); shape.lineTo(-20, 10); shape.closePath();
      const hole = new THREE.Path(); hole.moveTo(-18.5, -8.5); hole.lineTo(-18.5, 8.5); hole.lineTo(18.5, 8.5); hole.lineTo(18.5, -8.5); hole.closePath(); shape.holes.push(hole);
      const mesh = new THREE.ExtrudeGeometry(shape, { depth: url.startsWith('geometry://nesting/') ? 508 : 280, bevelEnabled: false });
      const bytes = encodeNestingGeometry({ positions: [...mesh.attributes.position.array], indices: Array.from({ length: mesh.attributes.position.count }, (_, index) => index) });
      mesh.dispose();
      const response = new Response(bytes, { headers: { 'Content-Type': 'application/vnd.icax.flatbuffer' } });
      if (fixture.holdNextResource) {
        fixture.holdNextResource = false;
        return new Promise(resolve => { fixture.pendingResource = () => { resolve(response); fixture.pendingResource = null; }; });
      }
      return response;
    } },
      views: { async start() { return { snapshot, async poll() { return snapshot; }, async stop() {} }; } },
      async getState() { return structuredClone(state); }, async invoke(method, payload, options) {
        requests.push({ scope: 'scene', method, payload, options });
        if (method === 'TubeDesigner.List') return { tubeDesigner: structuredClone(designer) };
        if (method === 'TubeDesigner.GetPunchTools') return { tools: [] };
        if (method === 'TubeDesigner.MeasurePartGeometry') return { available:true, source:'final-brep', partKind:'tube',
          length:508, reference:{ length:508 }, section:{ width:40, depth:20, wallThickness:1.5 }, features:[] };
        if (method === 'TubeDesigner.ListNestingPartFiles') return new Promise((resolve, reject) => {
          const request = { payload, resolve, reject }; fixture.pendingDirectory = request; fixture.directories.push(request);
        });
        if (method === 'TubeDesigner.PreviewNestingPartFile') return new Promise((resolve, reject) => { fixture.previews.push({ payload, resolve, reject }); });
        if (method === 'TubeDesigner.ReleaseNestingPartFilePreview') return { released: true };
        if (method === 'TubeDesigner.ImportNestingPart') return new Promise((resolve, reject) => { fixture.pendingImport = { resolve, reject, payload }; });
        if (method === 'TubeDesigner.UpdateManufacturingPart') {
          const part = designer.nestingGroups.flatMap(group => group.parts).find(part => part.entityId === payload.partEntityId);
          Object.assign(part, { name: payload.name, quantity: payload.quantity, nestingPriority: payload.nestingPriority });
          part.properties['manufacturing.material'] = payload.material;
          return { tubeDesigner: structuredClone(designer) };
        }
        throw Error('Unexpected scene method: ' + method);
      } };
    fixture.succeedImport = () => {
      const { payload, resolve } = fixture.pendingImport;
      const part = makePart('new-imported-part', payload.name, payload.quantity);
      part.properties['manufacturing.import.sourceFileName'] = payload.sourcePath.split(/[\\/]/).at(-1);
      part.properties['manufacturing.material'] = payload.material;
      designer.nestingGroups[0].parts.push(part);
      designer.nestingTask = { revision: 'imported-r1', parts: [{ partEntityId: part.entityId, generationRunId: part.generationRunId }] };
      resolve({ partEntityId: part.entityId, profile: structuredClone(profile), sourceFileName: part.properties['manufacturing.import.sourceFileName'],
        recognition: { available: true, length: part.length, section: structuredClone(profile), features: [] }, tubeDesigner: structuredClone(designer) });
      fixture.pendingImport = null;
    };
    fixture.resolveDirectory = (directory = 'D:/CAD') => {
      fixture.pendingDirectory.resolve({ directory, parentDirectory: directory === 'D:/' ? '' : 'D:/', entries: [
        { name: '加工件', path: 'D:/CAD/加工件', directory: true },
        { name: '旧加工件.STEP', path: 'D:/CAD/旧加工件.STEP', directory: false },
        { name: '真实加工件.IGES', path: 'D:/CAD/真实加工件.IGES', directory: false },
        { name: '不支持截面.STEP', path: 'D:/CAD/不支持截面.STEP', directory: false },
        { name: '说明.xlsx', path: 'D:/CAD/说明.xlsx', directory: false },
        ...Array.from({ length: 35 }, (_, i) => ({ name: '测试管' + i + '.stp', path: 'D:/CAD/测试管' + i + '.stp', directory: false }))]
        .map(entry => entry.directory ? entry : { ...entry, sizeBytes: 1000, lastModifiedNs: '1001' }) });
      fixture.pendingDirectory = null;
    };
    fixture.resolvePreview = index => fixture.previews[index].resolve({ sourceFileName: fixture.previews[index].payload.sourcePath.split('/').at(-1),
      sourcePath: fixture.previews[index].payload.sourcePath, sizeBytes: 1000, lastModifiedNs: '1001',
      wireframe: { polylines: [[-200,-20,-10,200,-20,-10,200,20,-10,-200,20,-10,-200,-20,-10], [-200,-20,10,200,-20,10,200,20,10,-200,20,10,-200,-20,10]], edgeCount: 2, pointCount: 10 } });
    const projectState = { projectId, projectName: '下料直接导入浏览器验证', mainScene: state };
    const projectProxy = { projectId, state: projectState, getMainScene() { return sceneProxy; } };
    const otherState = { ...structuredClone(state), sceneId: 'other-scene' };
    const otherSceneProxy = { ...sceneProxy, state: otherState, async getState() { return structuredClone(otherState); }, async invoke(method, payload, options) {
      requests.push({ scope: 'scene-other', method, payload, options });
      if (method === 'TubeDesigner.List') return { tubeDesigner: structuredClone(designer) };
      if (method === 'TubeDesigner.GetPunchTools') return { tools: [] };
      if (method === 'TubeDesigner.MeasurePartGeometry') return { available:true, source:'final-brep', partKind:'tube',
        length:508, reference:{ length:508 }, section:{ width:40, depth:20, wallThickness:1.5 }, features:[] };
      throw Error('Old picker must never import into the new project: ' + method);
    } };
    const otherProjectState = { projectId: 'other-project', projectName: '另一个项目', mainScene: otherState };
    const otherProjectProxy = { projectId: 'other-project', state: otherProjectState, getMainScene() { return otherSceneProxy; } };
    Object.assign(getProjectView('other-project'), { scene: structuredClone(otherState), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true });
    const productState = { productId: 'icax.tube-designer', productName: 'TubeDesigner', isStarted: true,
      frontendEntry: '/src/apps/tube-designer/webpage/entry.mjs', catalogs: [{ mainProject: projectState }], projectFile: { fileExtensions: ['ictd'] } };
    const settings = { enabled: false, inputDirectory: '', tempDirectory: '', outputDirectory: '' };
    const productProxy = { productId: productState.productId, state: productState, async getState() { return productState; },
      getProject(id) { return id === 'other-project' ? otherProjectProxy : projectProxy; }, async openProjectCatalog(path) {
        if (path === 'D:/other.ictd') return { projectProxy: otherProjectProxy, sceneProxy: otherSceneProxy, catalog: { mainProject: otherProjectState } };
        return { projectProxy, sceneProxy, catalog: { mainProject: projectState } };
      },
      async invoke(method, payload, options) {
        requests.push({ scope: 'product', method, payload, options });
        if (method === 'TubeDesigner.GetBatchExcelAutomationSettings') return { settings: structuredClone(settings) };
        if (method === 'TubeDesigner.ScanBatchExcelAutomation') return { settings: structuredClone(settings), files: [], errors: [] };
        if (method === 'TubeDesigner.ListUserData') return { customers: [], parameterPresets: [], profiles: [], punchTools: [], productTemplates: [], systemProfiles: [], templateProfiles: [] };
        throw Error('Scene methods are forbidden on the product proxy: ' + method);
      } };
    const appProxy = { bridge: { async openDirectoryDialog(options) {
      requests.push({ scope: 'bridge', method: 'openDirectoryDialog', payload: options }); return 'D:/CAD';
    } }, products: new Map([[productState.productId, productProxy]]), async getState() { return { products: [productState] }; },
      async openProjectFile(path) {
        assertFixturePath(path);
        return { productProxy, projectProxy: otherProjectProxy, sceneProxy: otherSceneProxy, catalog: { mainProject: otherProjectState } };
      },
      getProduct() { return productProxy; }, async startProduct() { return productProxy; } };
    function assertFixturePath(path) { if (path !== 'D:/other.ictd') throw Error('Unexpected project fixture path'); }
    Object.assign(fixture, { appProxy, sceneProxy, productProxy });
    globalThis.__directImport = fixture;
    new MutationObserver(() => { if (document.querySelector('.tube-nesting-part-import-dialog, #nesting-part-import-title, [data-tube-designer-nesting-import-form]')) fixture.confirmationSeen = true; })
      .observe(document.body, { childList: true, subtree: true });
    await import('/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs');
  }, profile);
  await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().startupPhase === 'ready');
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();
  await page.waitForFunction(() => __directImport.view.activeAreaId === 'nesting' && !__directImport.view.pending);
  await page.locator('.project-progress-backdrop').waitFor({ state: 'detached' });
  const importButton = page.locator('[data-command-id="nesting.import-part"]');
  await importButton.waitFor({ state: 'visible' });
  await importButton.click();
  await page.waitForFunction(() => !!__directImport.pendingDirectory);
  await page.evaluate(() => __icaxAppShell.executeRibbonCommand('nesting.import-part'));
  assert.equal(await page.evaluate(() => __directImport.directories.length), 1, '选择文件期间重入不能再次打开选择器');
  await page.evaluate(() => __directImport.resolveDirectory());
  const dialog = page.locator('.tube-nesting-file-picker');
  await dialog.locator('[data-nesting-file-path="D:/CAD/旧加工件.STEP"]').waitFor();
  assert.equal(await dialog.locator('[data-nesting-file-path$="xlsx"]').count(), 0);
  await page.evaluate(() => {
    __directImport.mainNodes = { canvas: document.querySelector('.cam-project-workspace .icax-three-viewport-canvas') || document.querySelector('.icax-three-viewport-canvas'),
      input: document.querySelector('[data-cam-change-action="tube-designer-parts-search"]'), left: document.querySelector('.cam-context-pane'), right: document.querySelector('.cam-info-pane') };
  });
  await dialog.locator('[data-nesting-file-path="D:/CAD/旧加工件.STEP"]').click();
  await page.waitForFunction(() => __directImport.previews.length === 1);
  await dialog.locator('[data-nesting-file-path="D:/CAD/真实加工件.IGES"]').click();
  assert.equal(await page.evaluate(() => __directImport.previews.length), 1, '同key原生预览必须串行，避免旧请求回写资源');
  await page.evaluate(() => __directImport.resolvePreview(0));
  await page.waitForFunction(() => __directImport.previews.length === 2);
  assert.equal(await dialog.locator('.icax-three-viewport-canvas').count(), 0, '过期预览不可进入独立视口');
  await page.evaluate(() => {
    const input = document.querySelector('[data-nesting-file-directory]'); input.focus(); input.setSelectionRange(1, 4, 'backward');
    const list = document.querySelector('.tube-nesting-file-list'); list.scrollTop = 100;
    __directImport.pickerInteraction = { input, list, scroll: list.scrollTop };
    __directImport.resolvePreview(1);
  });
  await page.waitForFunction(() => __directImport.view.tubeDesignerNestingFilePicker?.previewStatus === 'ready');
  const previewInteraction = await page.evaluate(() => {
    const picker = __directImport.view.tubeDesignerNestingFilePicker, n = __directImport.pickerInteraction;
    return { sameDirectoryInput: n.input === document.querySelector('[data-nesting-file-directory]'), sameList: n.list === document.querySelector('.tube-nesting-file-list'),
      focus: document.activeElement === n.input, selection: [n.input.selectionStart, n.input.selectionEnd, n.input.selectionDirection], scroll: n.list.scrollTop,
      expectedScroll: n.scroll, mainCanvas: __directImport.mainNodes.canvas === document.querySelector('.icax-three-viewport-canvas'),
      mainInput: __directImport.mainNodes.input === document.querySelector('[data-cam-change-action="tube-designer-parts-search"]'),
      mainLeft: __directImport.mainNodes.left === document.querySelector('.cam-context-pane'), mainRight: __directImport.mainNodes.right === document.querySelector('.cam-info-pane'),
      selectedPath: picker.selectedPath, edgeCount: picker.viewport.getDebugState().edgeCount,
      viewBox: picker.viewport.getDebugState().viewBox, yaw: picker.viewport.getDebugState().yaw };
  });
  assert.equal(previewInteraction.sameDirectoryInput && previewInteraction.sameList && previewInteraction.focus && previewInteraction.mainCanvas && previewInteraction.mainInput && previewInteraction.mainLeft && previewInteraction.mainRight, true);
  assert.deepEqual(previewInteraction.selection, [1, 4, 'backward']);
  assert.equal(previewInteraction.scroll, previewInteraction.expectedScroll);
  assert.equal(previewInteraction.edgeCount, 2);
  assert.equal(await dialog.locator('canvas').count(), 0, 'SVG preview never creates a WebGL canvas');
  await dialog.locator('[data-nesting-file-path="D:/CAD/旧加工件.STEP"]').click();
  await page.waitForFunction(() => __directImport.view.tubeDesignerNestingFilePicker.previewStatus === 'ready');
  await dialog.locator('[data-nesting-file-path="D:/CAD/真实加工件.IGES"]').click();
  await page.waitForFunction(() => __directImport.view.tubeDesignerNestingFilePicker.previewStatus === 'ready');
  assert.equal(await page.evaluate(() => __directImport.previews.length), 2, 'cached wireframes never reread STEP');
  const previewSvg = dialog.locator('[data-nesting-wireframe]');
  const previewBox = await previewSvg.boundingBox();
  await page.mouse.move(previewBox.x + 20, previewBox.y + 20);
  await page.mouse.down({ button: 'right' }); await page.mouse.move(previewBox.x + 90, previewBox.y + 55, { steps: 6 }); await page.mouse.up({ button: 'right' });
  await page.mouse.wheel(0, 200);
  const navigated = await page.evaluate(() => {
    __directImport.firstPreviewSvg = document.querySelector('[data-nesting-wireframe]');
    return __directImport.view.tubeDesignerNestingFilePicker.viewport.getDebugState();
  });
  assert.notDeepEqual(navigated.viewBox, previewInteraction.viewBox, 'SVG zoom changes viewBox');
  assert.notEqual(navigated.yaw, previewInteraction.yaw, 'SVG right drag rotates the wireframe');
  await dialog.locator('[data-nesting-file-action="fit"]').click();
  const fittedViewBox = await previewSvg.getAttribute('viewBox');
  await page.mouse.move(previewBox.x + 20, previewBox.y + 20); await page.mouse.wheel(0, -300);
  await page.waitForFunction(before => document.querySelector('[data-nesting-wireframe]').getAttribute('viewBox') !== before, fittedViewBox);
  await previewSvg.dblclick({ position: { x: 20, y: 20 } });
  assert.equal(await previewSvg.getAttribute('viewBox'), fittedViewBox, 'blank left double click restores fit');
  const pickerBlankFit = { representation: 'svg-wireframe', fittedViewBox, blankDoubleClick: true };
  await dialog.locator('[data-nesting-file-path="D:/CAD/真实加工件.IGES"]').scrollIntoViewIfNeeded();
  await page.screenshot({ path: resolve(output, 'file-picker-3d-preview.png') });
  await dialog.locator('[data-nesting-file-action="open"]').click();
  assert.equal(await page.evaluate(() => !__directImport.firstPreviewSvg.isConnected), true, 'closed preview SVG is detached');
  await page.waitForFunction(() => !!__directImport.pendingImport);
  const pending = await page.evaluate(() => ({ pending: __directImport.view.pending,
    calls: __directImport.requests.filter(row => row.method === 'TubeDesigner.ImportNestingPart'), confirmationSeen: __directImport.confirmationSeen }));
  assert.equal(pending.pending, true);
  assert.equal(pending.calls.length, 1);
  assert.equal(pending.calls[0].scope, 'scene');
  assert.equal(pending.calls[0].options.timeoutMs, 180000);
  assert.deepEqual(pending.calls[0].payload, { sourcePath: 'D:/CAD/真实加工件.IGES', name: '真实加工件', material: '', quantity: 1 });
  assert.equal(pending.confirmationSeen, false, '选择文件后必须直接识别，不能曾经插入确认弹窗');
  await page.evaluate(() => __icaxAppShell.executeRibbonCommand('nesting.import-part'));
  assert.equal(await page.evaluate(() => __directImport.requests.filter(row => row.method === 'TubeDesigner.ImportNestingPart').length), 1, '识别导入期间重入不能发起第二次导入');
  assert.equal(await page.evaluate(() => __directImport.directories.length), 1);

  const scrollStyle = await page.addStyleTag({ content: '.cam-context-pane,.cam-info-pane{display:block!important;overflow:auto!important;height:440px!important;max-height:440px!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:500px}.tube-designer-cutting-group-list{max-height:180px!important;overflow:auto!important}.tube-designer-cutting-inspector-scroll{max-height:220px!important;overflow:auto!important}.tube-designer-cutting-inspector-scroll::after{content:"";display:block;height:500px}' });
  await page.evaluate(() => {
    const nodes = { input: document.querySelector('[data-cam-change-action="tube-designer-parts-search"]'),
      canvas: document.querySelector('.icax-three-viewport-canvas'), left: document.querySelector('.cam-context-pane'), right: document.querySelector('.cam-info-pane'),
      list: document.querySelector('.tube-designer-cutting-group-list'), editor: document.querySelector('.tube-designer-cutting-inspector-scroll') };
    let inputEvents = 0, canvasEvents = 0;
    nodes.input.addEventListener('test-listener', () => inputEvents++); nodes.canvas.addEventListener('test-listener', () => canvasEvents++);
    nodes.input.value = '继续输入'; nodes.input.focus({ preventScroll: true }); nodes.input.setSelectionRange(1, 3, 'backward');
    nodes.left.scrollTop = 90; nodes.right.scrollTop = 100; nodes.list.scrollTop = 130; nodes.editor.scrollTop = 170;
    __directImport.nodes = nodes; __directImport.listenerCounts = () => ({ inputEvents, canvasEvents });
    __directImport.latestScroll = [nodes.left.scrollTop, nodes.right.scrollTop, nodes.list.scrollTop, nodes.editor.scrollTop];
    __directImport.succeedImport();
  });
  await page.waitForFunction(() => !__directImport.view.pending && __directImport.view.tubeDesignerActiveNestingPartId === 'new-imported-part');
  await page.locator('[data-tube-designer-part-field="name"]').waitFor({ state: 'visible' });
  const interaction = await page.evaluate(() => {
    const n = __directImport.nodes;
    const actual = { input: document.querySelector('[data-cam-change-action="tube-designer-parts-search"]'),
      canvas: document.querySelector('.icax-three-viewport-canvas'), left: document.querySelector('.cam-context-pane'), right: document.querySelector('.cam-info-pane'),
      list: document.querySelector('.tube-designer-cutting-group-list'), editor: document.querySelector('.tube-designer-cutting-inspector-scroll') };
    actual.input.dispatchEvent(new Event('test-listener')); actual.canvas.dispatchEvent(new Event('test-listener'));
    return { sameInput: n.input === actual.input, sameCanvas: n.canvas === actual.canvas, sameLeft: n.left === actual.left, sameRight: n.right === actual.right,
      sameList: n.list === actual.list, editorChangedWithNewSelectedPart: n.editor !== actual.editor,
      focused: document.activeElement === actual.input, text: actual.input.value,
      selection: [actual.input.selectionStart, actual.input.selectionEnd, actual.input.selectionDirection],
      actualScroll: [actual.left.scrollTop, actual.right.scrollTop, actual.list.scrollTop, actual.editor.scrollTop],
      latestScroll: __directImport.latestScroll, listeners: __directImport.listenerCounts(),
      restorationPath: 'public interaction capture immediately before necessary nesting DOM replacement' };
  });
  writeFileSync(resolve(output, 'interaction-diagnostic.json'), JSON.stringify(interaction, null, 2));
  assert.equal(interaction.sameCanvas && interaction.focused, true, '换入新零件后必须保留画布和当前搜索焦点');
  assert.equal(interaction.text, '继续输入');
  assert.deepEqual(interaction.selection, [1, 3, 'backward']);
  assert.deepEqual(interaction.actualScroll, interaction.latestScroll);
  assert(interaction.latestScroll.every(value => value > 0));
  assert.deepEqual(interaction.listeners, { inputEvents: interaction.sameInput ? 1 : 0, canvasEvents: 1 },
    '画布自定义监听器保留；被替换的输入节点不能计作监听器保留');
  assert.deepEqual(await page.evaluate(() => __directImport.view.tubeDesignerNestingSelectedPartIds), ['new-imported-part']);
  assert.equal(await page.locator('[data-tube-designer-part-field="name"]').inputValue(), '真实加工件');
  assert.equal(await page.locator('[data-tube-designer-part-field="material"]').inputValue(), '');
  assert.equal(await page.locator('[data-tube-designer-part-field="quantity"]').inputValue(), '1');
  await scrollStyle.evaluate(node => node.remove());
  await page.evaluate(() => { document.querySelector('.cam-info-pane').scrollTop = 0; document.querySelector('.tube-designer-cutting-inspector-scroll').scrollTop = 0; });
  await page.screenshot({ path: resolve(output, 'direct-import-selected.png') });
  await page.waitForFunction(() => __directImport.view.viewport.getVisibleBounds()
    && !__directImport.view.tubeDesignerPartProgress);
  assert.equal(await page.locator('[data-tube-designer-part-dimension-tree]').count(), 0);
  assert.equal(await page.locator('[data-tube-designer-nesting-measurement]').count(), 0);
  assert.equal(await page.locator('[data-tube-inspection-element-row]').count(), 0);
  assert.equal(await page.evaluate(() => __directImport.requests.filter(row => row.method === 'TubeDesigner.MeasurePartGeometry').length), 0);
  assert.equal(await page.evaluate(() => __directImport.view.viewport.getDebugState().dimensionAnnotationCount), 0);
  const nestingBlankFit = await verifyBlankFit(page, 'nesting');

  assert.equal(await page.locator('.tube-designer-cutting-scene-actions button').count(), 0, 'imported CAD has no drawing recovery or punch entry');
  const editEntries = [];
  for (const kind of ['part-drawing', 'punch', 'straight-cut']) {
    await page.evaluate(async kind => {
      const part = __directImport.designer.nestingGroups[0].parts.find(part => part.entityId === 'new-imported-part');
      part.properties['manufacturing.process'] = kind;
      part.properties['manufacturing.punchPart'] = kind === 'punch';
      if (kind === 'part-drawing') part.properties['tubeDesigner.partDrawing'] = { drawing: { schemaVersion: 1, length: 508 } };
      else delete part.properties['tubeDesigner.partDrawing'];
      __directImport.view.scene.tubeDesigner = structuredClone(__directImport.designer);
      await __icaxWorkbench.executeAreaAction('tube-designer-parts-select-part', { dataset: { tubeDesignerPartId: part.entityId } });
    }, kind);
    await page.locator('.project-progress-backdrop').waitFor({ state: 'detached' });
    const actions = await page.locator('.tube-designer-cutting-scene-actions button').evaluateAll(buttons => buttons.map(button => ({ action: button.dataset.camAction, label: button.textContent })));
    assert.deepEqual(actions, kind === 'part-drawing' ? [{ action: 'tube-designer-drawing-open', label: '三维编辑' }]
      : kind === 'punch' ? [{ action: 'tube-designer-punch-open', label: '冲孔向导' }] : []);
    editEntries.push({ kind, actions });
  }

  await page.locator('[data-tube-designer-part-field="name"]').fill('修改后的加工件');
  await page.locator('[data-tube-designer-part-field="material"]').fill('Q235B');
  await page.locator('[data-tube-designer-part-field="quantity"]').fill('4');
  await page.locator('[data-cam-action="tube-designer-part-edit-save"]').click();
  await page.waitForFunction(() => !__directImport.view.pending && __directImport.requests.some(row => row.method === 'TubeDesigner.UpdateManufacturingPart'));
  assert.equal(await page.locator('[data-tube-designer-part-field="name"]').inputValue(), '修改后的加工件');
  assert.equal(await page.locator('[data-tube-designer-part-field="material"]').inputValue(), 'Q235B');
  assert.equal(await page.locator('[data-tube-designer-part-field="quantity"]').inputValue(), '4');

  const callsBeforeCancel = await page.evaluate(() => __directImport.requests.filter(row => row.method === 'TubeDesigner.ImportNestingPart').length);
  await importButton.click();
  await page.waitForFunction(() => !!__directImport.pendingDirectory);
  await page.evaluate(() => __directImport.resolveDirectory());
  await dialog.locator('[data-nesting-file-action="up"]').click();
  await page.waitForFunction(() => !!__directImport.pendingDirectory);
  assert.equal(await dialog.locator('.tube-nesting-file-row').count(), 0, '读取新目录时不能继续选择旧行');
  assert.equal(await dialog.locator('[data-nesting-file-action="open"]').isDisabled(), true);
  await page.evaluate(() => {
    const input = document.querySelector('[data-nesting-file-directory]'); input.value = 'D:/继续输入'; input.focus(); input.setSelectionRange(3, 5, 'backward');
    __directImport.resolveDirectory('D:/');
  });
  await page.waitForFunction(() => document.querySelector('.tube-nesting-file-row'));
  assert.equal(await dialog.locator('[data-nesting-file-directory]').inputValue(), 'D:/继续输入', '目录响应不能覆盖请求之后输入的路径');
  assert.deepEqual(await dialog.locator('[data-nesting-file-directory]').evaluate(input => [input.selectionStart, input.selectionEnd, input.selectionDirection]), [3, 5, 'backward']);
  await dialog.locator('[data-nesting-file-directory]').press('Enter');
  await page.waitForFunction(() => !!__directImport.pendingDirectory);
  await page.evaluate(() => { __directImport.pendingDirectory.reject(Error('目录不存在')); __directImport.pendingDirectory = null; });
  await dialog.locator('[data-nesting-file-directory-status]').filter({ hasText: '目录不存在' }).waitFor();
  await dialog.locator('[data-nesting-file-action="browse"]').click();
  await page.waitForFunction(() => !!__directImport.pendingDirectory);
  await page.evaluate(() => __directImport.resolveDirectory());
  await dialog.locator('[data-nesting-file-path="D:/CAD/测试管0.stp"]').click();
  await page.waitForFunction(() => __directImport.previews.length === 3);
  const releasesBeforeInFlightCancel = await page.evaluate(() => __directImport.requests.filter(row => row.method === 'TubeDesigner.ReleaseNestingPartFilePreview').length);
  await dialog.locator('footer [data-nesting-file-action="cancel"]').click();
  assert.equal(await page.evaluate(() => __directImport.requests.filter(row => row.method === 'TubeDesigner.ReleaseNestingPartFilePreview').length), releasesBeforeInFlightCancel, '取消在途预览不能提前释放后被旧请求重建资源');
  await page.evaluate(() => __directImport.resolvePreview(2));
  await page.waitForFunction(count => __directImport.requests.filter(row => row.method === 'TubeDesigner.ReleaseNestingPartFilePreview').length === count + 1, releasesBeforeInFlightCancel);
  assert.equal(await dialog.count(), 0, '取消后迟来预览不可重新创建视口或窗口');
  assert.equal(await page.evaluate(() => __directImport.requests.filter(row => row.method === 'TubeDesigner.ImportNestingPart').length), callsBeforeCancel);
  assert.equal(await page.evaluate(() => __directImport.view.tubeDesignerActiveNestingPartId), 'new-imported-part');

  await importButton.click();
  await page.waitForFunction(() => !!__directImport.pendingDirectory);
  await page.evaluate(() => __directImport.resolveDirectory());
  await dialog.locator('[data-nesting-file-path="D:/CAD/不支持截面.STEP"]').click();
  await page.waitForFunction(() => __directImport.previews.length === 4);
  await page.evaluate(() => __directImport.previews[3].reject(Error('预览无法读取')));
  await dialog.locator('[data-nesting-file-preview-status]').filter({ hasText: '预览无法读取' }).waitFor();
  await page.waitForFunction(() => __directImport.requests.filter(row => row.method === 'TubeDesigner.ReleaseNestingPartFilePreview').length === 3);
  await dialog.locator('[data-nesting-file-path="D:/CAD/不支持截面.STEP"]').dblclick();
  await page.waitForFunction(() => !!__directImport.pendingImport);
  assert.equal(await page.evaluate(() => __directImport.previews.length), 4, '双击打开直接导入，不等防抖也不先发送多余preview');
  await page.evaluate(() => { __directImport.pendingImport.reject(Error('无法导入：当前截面不支持')); __directImport.pendingImport = null; });
  await page.waitForFunction(() => !__directImport.view.pending && document.querySelector('.log-list')?.textContent.includes('当前截面不支持'));
  await importButton.click();
  await page.waitForFunction(() => !!__directImport.pendingDirectory);
  await page.evaluate(() => __directImport.resolveDirectory());
  await dialog.locator('[data-nesting-file-path="D:/CAD/测试管1.stp"]').click();
  await page.waitForFunction(() => __directImport.previews.length === 5);
  await page.evaluate(async () => {
    __directImport.connectedMount = document.querySelector('#app');
    await __icaxAppShell.openProject('D:/other.ictd', 'icax.tube-designer');
  });
  await page.waitForFunction(() => __icaxAppShell.getState().activeProjectId === 'other-project');
  await page.locator('.project-progress-backdrop').waitFor({ state: 'detached' });
  assert.equal(await dialog.count(), 0, '切换项目须关闭旧context持有的选择窗');
  await page.evaluate(() => __icaxAppShell.selectRibbonTab('nesting'));
  await page.locator('.project-progress-backdrop').waitFor({ state: 'detached' });
  await page.locator('[data-cam-change-action="tube-designer-parts-search"]').waitFor();
  await page.evaluate(() => {
    const input = document.querySelector('[data-cam-change-action="tube-designer-parts-search"]'); input.focus(); input.value = '新项目输入'; input.setSelectionRange(1, 3, 'backward');
    __directImport.otherFocus = input; __directImport.resolvePreview(4);
  });
  await page.waitForFunction(() => __directImport.requests.filter(row => row.method === 'TubeDesigner.ReleaseNestingPartFilePreview').length === 5);
  const projectSwitch = await page.evaluate(() => ({ connectedMountRetained: __directImport.connectedMount === document.querySelector('#app') && __directImport.connectedMount.isConnected,
    latestFocus: document.activeElement === __directImport.otherFocus, value: __directImport.otherFocus.value,
    selection: [__directImport.otherFocus.selectionStart, __directImport.otherFocus.selectionEnd, __directImport.otherFocus.selectionDirection],
    oldPickerClosed: !__directImport.view.tubeDesignerNestingFilePicker,
    imports: __directImport.requests.filter(row => row.method === 'TubeDesigner.ImportNestingPart').map(row => row.scope) }));
  assert.equal(projectSwitch.connectedMountRetained && projectSwitch.latestFocus && projectSwitch.oldPickerClosed, true);
  assert.deepEqual(projectSwitch.selection, [1, 3, 'backward']);
  assert.equal(projectSwitch.value, '新项目输入');
  assert.deepEqual(projectSwitch.imports, ['scene', 'scene']);
  const final = await page.evaluate(() => ({ selected: __directImport.view.tubeDesignerActiveNestingPartId,
    count: __directImport.view.scene.tubeDesigner.nestingGroups.flatMap(group => group.parts).length,
    confirmationSeen: __directImport.confirmationSeen, requests: __directImport.requests }));
  assert.equal(final.selected, 'new-imported-part');
  assert.equal(final.count, 41);
  assert.equal(final.confirmationSeen, false);
  const imports = final.requests.filter(row => row.method === 'TubeDesigner.ImportNestingPart');
  assert.equal(imports.length, 2);
  assert(imports.every(row => row.scope === 'scene'));
  assert.equal(final.requests.filter(row => row.method === 'TubeDesigner.ReleaseNestingPartFilePreview').length, 5);
  assert.equal(final.requests.filter(row => row.method === 'openFileDialog').length, 0, '没有测试专用旧宿主文件选择fallback');
  assert.deepEqual(errors, []);
  await page.evaluate(async () => (await import('/src/apps/tube-designer/webpage/batchExcelAutomation.mjs')).stopBatchExcelAutomation());
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, actualAppShellRibbonClick: true, directSceneImportAfterPicker: true,
    noConfirmationDialogAtAnyStage: true, defaultMetadata: pending.calls[0].payload, pickerAndImportReentryProtected: true,
    cancelWithoutSceneImport: true, failureKeepsSelectedPartAndClearsPending: true, importedPartSelected: true,
    subsequentNameMaterialQuantityEditable: true, interaction, previewInteraction, pickerBlankFit, nestingBlankFit, editEntries,
    isolatedPreview: true, serializedStalePreviewIgnored: true,
    previewRotationAndZoom: true, cachedSvgPreview: true, previewFailureStillAllowsOpen: true, doubleClickDirectImport: true, inFlightCancelReleasedAfterCompletion: true,
    directoryNavigationFailureAndLatestInputPreserved: true, projectSwitch, allPickerResourcesReleased: true, requests: final.requests, pageErrors: errors,
    nativeTransport: 'controlled native protocol with CAD wireframe fixture and main-scene mesh', filePicker: 'actual production in-app file browser', nativeCadRecognition: false,
    standaloneCefEndToEnd: false }, null, 2));
  console.log('PASS direct nesting import: picker to exactly one scene request, no confirmation, defaults, reentry/cancel/error, selected part and metadata editor.');
} finally { await browser.close(); }
