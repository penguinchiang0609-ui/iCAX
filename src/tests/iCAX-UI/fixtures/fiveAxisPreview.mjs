// UI test transport only. This file is never referenced by the product manifest.
import { getProjectView } from '../../../apps/_shared/workbench/state/projectViewStore.mjs';
const projectId = 'five-axis-ui-preview', sceneId = 'five-axis-ui-scene';
const requests = [];
let populated = false, releaseRead = null, holdRead = false;
const workpiece = { entityId: 'sample-panel', name: '曲面薄板 · UI 示例', sourcePath: 'UI-preview.step', geometryRevision: '1' };
const scene = { sceneId, machines: [], intentToolpaths: [], model: { isLoaded: false }, selection: {}, undoRedo: { revision: '1', canUndo: false, canRedo: false } };
const view = getProjectView(projectId);
view.scene = structuredClone(scene);

// Build a render-resource fixture consumed by the real SDK binary decoder.
function panelResource() {
  const positions = [], normals = [], indices = [], nx = 128, ny = 72;
  const height = (x, y) => 42 + 28 * Math.cos(x / 200 * Math.PI) + 45 * Math.exp(-(((y - 25) / 47) ** 2));
  for (let y = 0; y <= ny; y++) for (let x = 0; x <= nx; x++) {
    const px = (x / nx - .5) * 430, py = (y / ny - .5) * 230;
    positions.push(px, py, height(px, py));
    const dx = (height(px + .1, py) - height(px - .1, py)) / .2;
    const dy = (height(px, py + .1) - height(px, py - .1)) / .2;
    const length = Math.hypot(dx, dy, 1); normals.push(-dx / length, -dy / length, 1 / length);
  }
  for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) {
    const px = ((x + .5) / nx - .5) * 430, py = ((y + .5) / ny - .5) * 230;
    if (Math.hypot(px - 108, py + 28) < 25 || Math.hypot(px + 108, py + 28) < 25) continue;
    const a = y * (nx + 1) + x; indices.push(a, a + 1, a + nx + 2, a, a + nx + 2, a + nx + 1);
  }
  const p = 80, n = p + 4 + positions.length * 4, i = n + 4 + normals.length * 4;
  const bytes = new Uint8Array(i + 4 + indices.length * 4), data = new DataView(bytes.buffer);
  data.setUint32(0, 32, true); bytes.set(new TextEncoder().encode('ICRG'), 4);
  data.setUint16(8, 24, true); data.setUint16(10, 48, true);
  [4, 8, 16, 24, 28, 32, 36, 0, 0, 40].forEach((offset, field) => data.setUint16(12 + field * 2, offset, true));
  data.setInt32(32, 24, true); data.setUint32(36, 1, true); data.setUint32(40, 1, true); data.setBigUint64(48, 1n, true);
  for (const [field, start, values, integer] of [[64, p, positions, false], [68, n, normals, false], [72, i, indices, true]]) {
    data.setUint32(field, start - field, true); data.setUint32(start, values.length, true);
    values.forEach((value, offset) => integer ? data.setUint32(start + 4 + offset * 4, value, true) : data.setFloat32(start + 4 + offset * 4, value, true));
  }
  return bytes;
}
const snapshot = () => ({ viewId: 'five-axis-preview-view', revision: populated ? '2' : '1', rows: populated ? [{ entityId: workpiece.entityId,
  data: { geometry: { url: 'resource://preview/panel', version: '1' }, geometryKind: 1, layerMask: 1, visible: true, selectable: true } }] : [] });
const sceneProxy = {
  sceneId, state: scene, pdo: { enabled: false }, resources: { async get() { return new Response(panelResource(), { status: 200 }); } },
  views: { async start() { return { get snapshot() { return snapshot(); }, async poll() { return snapshot(); },
    async waitForSnapshot(predicate) { const value = snapshot(); if (!predicate(value)) throw Error('Fixture View expectation mismatch'); return value; }, async stop() {} }; } },
  async getState() { return structuredClone(scene); },
  async invoke(method, payload) {
    requests.push({ scope: 'scene', method, payload });
    if(method==='MachineDefinition.List')return {definitions:[]};
    if(method==='Machine.List')return {machines:[]};
    if(method==='CamProcess.PreviewCompensation')return {items:[]};
    if(method==='CamPlan.Generate')throw Error('CAM_GENERATE_NO_CUTS: UI fixture has no native curves');
    if (method === 'CamScene.ReadState' && holdRead) { holdRead = false; await new Promise(resolve => { releaseRead = resolve; }); releaseRead = null; }
    if (method === 'CamScene.ReadState') return { expected: { epoch: 'preview', mutationSequence: '1' }, jobId: 'preview-job', activeSetupId: 'preview-setup', setups:[{id:'preview-setup',rapidSettings:{strategy:'Quick',followHeightMm:1,liftHeightMm:10,collisionThresholdMm:.1,marginMm:.2,envelopeRadiusMm:0,envelopeLengthMm:0,linearSpeedMmS:100,linearAccelerationMmS2:1000,angularSpeedRadS:1,angularAccelerationRadS2:2}}], placements: scene.model.isLoaded ? [{id:scene.model.entityId,workpieceId:scene.model.entityId,setupId:'preview-setup',transform:[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}] : [], intentToolpaths: structuredClone(scene.intentToolpaths), machines: structuredClone(scene.machines), workpieces: scene.model.isLoaded ? [{ ...scene.model, id: scene.model.entityId, geometryRef: { url: 'resource://preview/brep', version: scene.model.geometryRevision || '1' }, previewRef: { url: 'resource://preview/panel', version: '1' } }] : [] };
    if(method==='CamProcess.Read')return {items:payload.intentIds.map(intentId=>({intentId,assignment:{overcutLengthMm:0,gapLengthMm:0,leadIn:{kind:'None',placement:'ScrapSide',lengthMm:0,angleRad:0},leadOut:{kind:'None',placement:'ScrapSide',lengthMm:0,angleRad:0}}}))};
    if(method==='CamProcess.PreviewLeads'&&Object.values(payload.patch||{}).every(l=>l.kind==='None'))return {items:payload.intentIds.map(intentId=>({intentId,leads:[]}))};
    if (method === 'CamWorkpiece.Inspect') return { cadInspection: { status: 'Ready', message: 'UI protocol fixture only; no native geometric check performed' } };
    if (method === 'Selection.PickTopology') return { selection: payload };
    throw Error(`Unexpected scene command: ${method}`);
  },
};
const projectState = { projectId, projectName: '五轴 CAM · UI 预览示例', mainScene: scene };
const projectProxy = { projectId, state: projectState, getMainScene() { return sceneProxy; } };
const productState = { productId: 'icax.five-axis-cam', productName: '三维五轴 CAM', isStarted: true,
  frontendEntry: '/src/apps/five-axis-cam/webpage/entry.mjs', catalogs: [{ mainProject: projectState }], projectFile: { fileExtensions: ['.i5cam'] } };
const productProxy = { productId: productState.productId, state: productState, async getState() { return productState; },
  getProject() { return projectProxy; }, async openProjectCatalog() { return { projectProxy, sceneProxy, catalog: { mainProject: projectState } }; },
  async invoke(method) { throw Error(`Scene command rejected on product channel: ${method}`); },
};
const bridge = {
  async openFileDialog() { throw Error('这是 UI 预览示例；文件导入需要原生宿主。'); },
  async saveFileDialog() { throw Error('这是 UI 预览示例；工程保存需要原生宿主。'); },
};
const appProxy = { bridge, products: new Map([[productState.productId, productProxy]]), async getState() { return { products: [productState] }; },
  getProduct() { return productProxy; }, async startProduct() { return productProxy; } };
globalThis.__fiveFixture = { appProxy, view, scene, requests,
  hold() { holdRead = true; }, release() { releaseRead?.(); }, get held() { return !!releaseRead; },
  async populate(count = 6) {
    populated = true; scene.model = { ...workpiece, isLoaded: true };
    scene.intentToolpaths = Array.from({ length: count }, (_, i) => ({ id:`cut-${i + 1}`,entityId: `cut-${i + 1}`,setupId:'preview-setup',placementId:workpiece.entityId,number:String(i+1),cutType:'Contour',canBeStandardHole:false,geometryKind: i > 0 && i < 3 ? 'Circle' : 'GeneralContour', name: i === 0 ? '外轮廓修边' : i < 3 ? `标准孔 ${i}` : `局部轮廓 ${i - 2}` }));
    view.scene = structuredClone(scene);
    await __icaxAppShell.executeRibbonCommand('five.refresh');
    view.viewport?.fitView();
  },
};
await import('../../../iCAX-UI/SDK/AppShell/app/bootstrap.mjs');
if (new URLSearchParams(location.search).get('demo') === 'panel') {
  const until = performance.now() + 15000;
  while (globalThis.__icaxAppShell?.getState().startupPhase !== 'ready' && performance.now() < until) await new Promise(requestAnimationFrame);
  if (globalThis.__icaxAppShell?.getState().startupPhase === 'ready') {
    await __fiveFixture.populate();
    await __icaxAppShell.selectRibbonTab('toolpath');
  }
}

