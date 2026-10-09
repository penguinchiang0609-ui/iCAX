// Real native perforated BRep -> native GPU resource -> production nesting UI and WebGL.
// The stdio host and browser shell are controlled; installed CEF and file pickers are outside scope.
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve, sep } from 'node:path';
import { startSideSketchNativeBridge, createPerforatedRectangularTube, createCrossPeriodRoundedTube, repositoryRoot } from './fixtures/sideSketchNativeBridge.mjs';

const artifacts = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repositoryRoot, 'output/tests/nesting-hole-visibility-native'));
mkdirSync(artifacts, { recursive: true });
const sourceRoot = resolve(repositoryRoot, 'src');
const bridge = await startSideSketchNativeBridge(artifacts);
let browser, page;
try {
  const native = await createPerforatedRectangularTube(bridge);
  assert.equal(native.geometry.valid, true);
  const blankVolume = (80 * 60 - 74 * 54) * 400;
  const removedVolume = (Math.PI * 12 ** 2 + 20 * 50) * 3;
  assert.ok(Math.abs(native.geometry.volume - (blankVolume - removedVolume)) < 0.1,
    'Both complete native hole interiors must have been removed from the near wall');
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs');
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome' });
  page = await browser.newPage({ viewport: { width: 1500, height: 940 } });
  page.setDefaultTimeout(30000);
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.exposeFunction('nativeRpc', bridge.rpc);
  await page.route('http://nesting-holes.test/**', route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><body></body>' });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ''));
    if (!pathname.startsWith('/src/') || !path.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    return route.fulfill({ contentType: 'text/javascript', body: readFileSync(path, 'utf8') });
  });
  await page.goto('http://nesting-holes.test/');
  await page.addStyleTag({ content: readFileSync(resolve(sourceRoot, 'apps/_shared/workbench/styles/laser3dcam.css'), 'utf8') });
  await page.evaluate(async native => {
    const area = await import('/src/apps/tube-designer/webpage/partsArea.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const { tubeDesignerCss } = await import('/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
    document.head.insertAdjacentHTML('beforeend', `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font:14px Arial,"Microsoft Yahei",sans-serif}#workspace{display:grid;grid-template-columns:260px minmax(0,1fr) 280px;height:100vh}#stage{position:relative;min-width:0;min-height:0}#scene,#overlay{position:absolute;inset:0}#overlay{pointer-events:none}aside{overflow:auto;background:#f5f8f8}</style>`);
    document.body.classList.add('tube-designer-workspace');
    document.body.innerHTML = '<div id="workspace"><aside id="left"></aside><main id="stage"><div id="scene"></div><div id="overlay"></div></main><aside id="right"></aside></div>';
    const view = { activeAreaId: 'nesting', tubeDesignerNestingSelectionKind: 'part', tubeDesignerActivePartId: native.part.entityId, scene: native.scene };
    const f = window.fixture = { area, THREE, native, view, reads: [], snapshots: [] };
    const resources = { async get(url, options) {
      const version = Number(options?.headers?.get('ICAX-Resource-Version') || 0);
      const response = await window.nativeRpc({ action: 'resource', payload: { url, version } });
      f.reads.push({ url, version, bytes: response.bytes });
      return new Response(Uint8Array.from(atob(response.base64), c => c.charCodeAt(0)));
    } };
    f.context = { mount: document.body, sceneProxy: { resources, async invoke(method, payload) {
      throw Error('Nesting inspection must not request measurement or reconstructed hole outlines: ' + method);
    } } };
    const viewport = view.viewport = createThreeViewport({ continuousRender: false, projectionMode: 'orthographic',
      showGrid: false, backgroundColor: 0x13252d });
    viewport.mount(document.querySelector('#scene'));
    const apply = viewport.applyViewSnapshot.bind(viewport);
    viewport.applyViewSnapshot = (snapshot, resources) => { f.snapshots.push(structuredClone(snapshot)); return apply(snapshot, resources); };
    f.apply = apply; f.resources = resources;
    f.render = () => {
      document.querySelector('#left').innerHTML = area.renderNestingLeftPane(f.context, view);
      document.querySelector('#right').innerHTML = area.renderNestingRightPane(f.context, view);
      document.querySelector('#overlay').innerHTML = area.renderNestingViewportOverlay(f.context, view);
    };
    f.render();
    f.capture = () => {
      viewport.renderer.render(viewport.scene, viewport.camera);
      const canvas = viewport.renderer.domElement, gl = viewport.renderer.getContext();
      const bytes = new Uint8Array(canvas.width * canvas.height * 4);
      gl.readPixels(0, 0, canvas.width, canvas.height, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
      return { width: canvas.width, height: canvas.height, bytes };
    };
  }, native);
  await page.waitForFunction(() => Boolean(window.fixture.view.tubeDesignerPartViewportKey)
    && !window.fixture.view.tubeDesignerPartProgress);
  const loaded = await page.evaluate(() => {
    const f = window.fixture, v = f.view.viewport, mesh = v.sceneObjects.get(f.native.part.entityId);
    mesh.geometry.computeBoundingBox();
    f.canvas = v.renderer.domElement; f.mesh = mesh;
    f.productionSnapshot = structuredClone(f.snapshots.findLast(item => item.rows.some(row => row.entityId === f.native.part.entityId)));
    return { snapshot: f.productionSnapshot, bounds: { min: mesh.geometry.boundingBox.min.toArray(), max: mesh.geometry.boundingBox.max.toArray() },
      debug: v.getDebugState(), meshEdges: mesh.children.filter(child => child.isLineSegments).length,
      webgl: v.renderer.getContext() instanceof WebGL2RenderingContext, reads: f.reads };
  });
  writeFileSync(resolve(artifacts, 'loaded-fixture.json'), JSON.stringify({ dllHash: bridge.dllHash,
    native: { partEntityId: native.part.entityId, geometry: native.geometry, sketch: native.sketch, profile: native.profile }, loaded }, null, 2));
  assert.equal(loaded.webgl, true);
  assert.equal(loaded.snapshot.rows.find(row => row.entityId === native.part.entityId).data.meshEdges, true,
    'The production independent-part rendering path must enable true mesh feature edges');
  assert.ok(loaded.meshEdges > 0);
  // Snapshot comparison uses the same actual native mesh and camera, changing only
  // the public edge presentation flag. It never draws outlines from the sketch.
  const renderMode = async (enabled, direction) => page.evaluate(async ({ enabled, direction }) => {
    const f = window.fixture, snapshot = structuredClone(f.productionSnapshot), v = f.view.viewport;
    snapshot.revision += enabled ? ':edges' : ':plain';
    for (const row of snapshot.rows) row.data.meshEdges = enabled;
    await f.apply(snapshot, f.resources);
    if (direction) v.setViewDirection(direction);
    v.fitViewToViewport(1.16);
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    const mesh = v.sceneObjects.get(f.native.part.entityId), edges = mesh.children.filter(child => child.isLineSegments);
    f[enabled ? 'afterPixels' : 'beforePixels'] = f.capture();
    return { edgeCount: edges.length, edges: edges.map(edge => ({ depthTest: edge.material.depthTest,
      depthWrite: edge.material.depthWrite, dashed: Boolean(edge.material.isLineDashedMaterial), color: edge.material.color?.getHexString(),
      points: edge.geometry.getAttribute('position').count })),
      sameCanvas: f.canvas === v.renderer.domElement, sameMesh: f.mesh === mesh };
  }, { enabled, direction });
  const direction = [-1, -2, 1];
  const before = await renderMode(false, direction);
  await page.locator('#stage').screenshot({ path: resolve(artifacts, '01-oblique-before.png') });
  const after = await renderMode(true, direction);
  await page.locator('#stage').screenshot({ path: resolve(artifacts, '02-oblique-after.png') });
  assert.equal(before.edgeCount, 0);
  assert.ok(after.edgeCount > 0);
  assert.equal(after.sameCanvas, true); assert.equal(after.sameMesh, true);
  for (const edge of after.edges) { assert.equal(edge.depthTest, true); assert.equal(edge.depthWrite, false); assert.equal(edge.dashed, false); }
  const contrast = await page.evaluate(() => {
    const { beforePixels: before, afterPixels: after } = window.fixture;
    const result = { changedPixels: 0, darkerPixels: 0, brightestDelta: 0 };
    for (let i = 0; i < before.bytes.length; i += 4) {
      const a = before.bytes[i] + before.bytes[i + 1] + before.bytes[i + 2];
      const b = after.bytes[i] + after.bytes[i + 1] + after.bytes[i + 2];
      if (Math.abs(a - b) > 24) result.changedPixels++;
      if (a - b > 24) result.darkerPixels++;
      result.brightestDelta = Math.max(result.brightestDelta, a - b);
    }
    return result;
  });
  assert.ok(contrast.darkerPixels > 150, 'Native mesh feature edges must create visible dark boundary contrast');
  const inspectHoles = await page.evaluate(() => {
    const f = window.fixture, { THREE } = f, mesh = f.view.viewport.sceneObjects.get(f.native.part.entityId);
    const shoot = (x, z) => {
      const ray = new THREE.Raycaster(new THREE.Vector3(x, -100, z), new THREE.Vector3(0, 1, 0));
      return ray.intersectObject(mesh, true).map(hit => ({ y: hit.point.y, mesh: Boolean(hit.object.isMesh) }));
    };
    const sourceVertices = new Set(), position = mesh.geometry.getAttribute('position');
    const key = (x, y, z) => `${x},${y},${z}`;
    for (let i = 0; i < position.count; i++) sourceVertices.add(key(position.getX(i), position.getY(i), position.getZ(i)));
    let featurePoints = 0, foreignPoints = 0;
    for (const edge of mesh.children.filter(child => child.isLineSegments)) {
      const points = edge.geometry.getAttribute('position'); featurePoints += points.count;
      for (let i = 0; i < points.count; i++)
        if (!sourceVertices.has(key(points.getX(i), points.getY(i), points.getZ(i)))) foreignPoints++;
    }
    return { circle: shoot(125, 0), circleOutside: shoot(125, 18), rectangle: shoot(270, 0), rectangleOutside: shoot(235, 0),
      featurePoints, foreignPoints };
  });
  assert.ok(inspectHoles.circle[0].y > 30 && inspectHoles.rectangle[0].y > 30,
    'A ray through each actual hole must reach the opposite inner wall');
  assert.ok(inspectHoles.circleOutside[0].y < -39 && inspectHoles.rectangleOutside[0].y < -39,
    'Surrounding material remains on the near wall');
  assert.equal(inspectHoles.foreignPoints, 0, 'Every displayed feature endpoint must belong to the actual native mesh');
  assert.ok(Object.values(inspectHoles).filter(Array.isArray).flat().every(hit => hit.mesh),
    'Feature boundaries must never steal model picking');
  await renderMode(false, [0, -1, 0]);
  await page.locator('#stage').screenshot({ path: resolve(artifacts, '03-front-before.png') });
  await renderMode(true, [0, -1, 0]);
  await page.locator('#stage').screenshot({ path: resolve(artifacts, '04-front-after.png') });
  const holeContrast = async () => page.evaluate(() => {
    const f = window.fixture, v = f.view.viewport, before = f.beforePixels, after = f.afterPixels;
    const samples = [{ name: 'circle', x1: 110, x2: 140, z1: -15, z2: 15 },
      { name: 'rectangle', x1: 242, x2: 298, z1: -13, z2: 13 }];
    return samples.map(box => {
      const projected = [[box.x1, box.z1], [box.x2, box.z2]].map(([x, z]) => {
        const p = new f.THREE.Vector3(x, 0, z).project(v.camera);
        return [Math.round((p.x + 1) / 2 * before.width), Math.round((p.y + 1) / 2 * before.height)];
      });
      const x1 = Math.max(0, Math.min(projected[0][0], projected[1][0])), x2 = Math.min(before.width - 1, Math.max(projected[0][0], projected[1][0]));
      const y1 = Math.max(0, Math.min(projected[0][1], projected[1][1])), y2 = Math.min(before.height - 1, Math.max(projected[0][1], projected[1][1]));
      let darkerPixels = 0, sumDelta = 0;
      for (let y = y1; y <= y2; y++) for (let x = x1; x <= x2; x++) {
        const i = (y * before.width + x) * 4;
        const delta = before.bytes[i] + before.bytes[i + 1] + before.bytes[i + 2]
          - after.bytes[i] - after.bytes[i + 1] - after.bytes[i + 2];
        if (delta > 24) { darkerPixels++; sumDelta += delta; }
      }
      return { name: box.name, darkerPixels, sumDelta };
    });
  });
  const frontContrast = await holeContrast();
  for (const item of frontContrast) assert.ok(item.darkerPixels > 40, `${item.name} native rim must gain local dark contrast`);
  await renderMode(false, [0, 1, 0]);
  await renderMode(true, [0, 1, 0]);
  await page.locator('#stage').screenshot({ path: resolve(artifacts, '05-back-hidden-holes.png') });
  const hiddenContrast = await holeContrast();
  for (const item of hiddenContrast) assert.equal(item.darkerPixels, 0, `${item.name} far rim must be occluded by the intact near wall`);
  await renderMode(true, direction);
  const lifecycle = await page.evaluate(async () => {
    const f = window.fixture, v = f.view.viewport, mesh = v.sceneObjects.get(f.native.part.entityId);
    const line = mesh.children.find(child => child.isLineSegments), originalGeometry = line.geometry;
    const counters = { reads: f.reads.length, snapshots: f.snapshots.length };
    f.render(); await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    v.setSelectedObjectId(f.native.part.entityId);
    const selectedLine = v.sceneObjects.get(f.native.part.entityId).children.find(child => child.isLineSegments);
    return { sameCanvas: f.canvas === v.renderer.domElement, sameEdgeGeometry: selectedLine.geometry === originalGeometry,
      color: selectedLine.material.color.getHexString(), readsBefore: counters.reads, readsAfter: f.reads.length,
      visible: v.getDebugState().visibleObjectCount, dimensions: v.getDebugState().dimensionAnnotationCount };
  });
  assert.equal(lifecycle.sameCanvas, true); assert.equal(lifecycle.sameEdgeGeometry, true);
  assert.equal(lifecycle.readsAfter, lifecycle.readsBefore);
  assert.notEqual(lifecycle.color, 'ffd400', 'Selection keeps the true feature boundaries dark');
  assert.equal(lifecycle.visible, 1); assert.equal(lifecycle.dimensions, 0);
  const rounded = await createCrossPeriodRoundedTube(bridge);
  assert.equal(rounded.geometry.valid, true);
  await page.evaluate(native => {
    const f = window.fixture;
    f.native = native; f.view.scene = native.scene; f.view.tubeDesignerActivePartId = native.part.entityId;
    f.render();
  }, rounded);
  await page.waitForFunction(() => !window.fixture.view.tubeDesignerPartProgress
    && window.fixture.view.viewport.sceneObjects.has(window.fixture.native.part.entityId));
  const roundedEdges = await page.evaluate(() => {
    const f = window.fixture, v = f.view.viewport, mesh = v.sceneObjects.get(f.native.part.entityId);
    f.productionSnapshot = structuredClone(f.snapshots.findLast(item => item.rows.some(row => row.entityId === f.native.part.entityId)));
    v.setViewDirection([-1,-2,1]); v.fitViewToViewport(1.16); v.renderer.render(v.scene, v.camera);
    const line = mesh.children.find(child => child.isLineSegments), p = line.geometry.getAttribute('position');
    let longEdges = 0;
    for(let i=0;i<p.count;i+=2) {
      const a=new f.THREE.Vector3().fromBufferAttribute(p,i),b=new f.THREE.Vector3().fromBufferAttribute(p,i+1);
      if(a.distanceTo(b)>250)longEdges++;
    }
    return { surfaceNormals: mesh.geometry.userData.surfaceNormals, longEdges, points: p.count, canvasRetained: f.canvas === v.renderer.domElement };
  });
  assert.equal(roundedEdges.surfaceNormals, true); assert.equal(roundedEdges.longEdges, 0);
  assert.ok(roundedEdges.points > 200); assert.equal(roundedEdges.canvasRetained, true);
  await page.locator('#stage').screenshot({ path: resolve(artifacts, '06-cross-period-ellipse-without-false-lines.png') });
  const disposal = await page.evaluate(async () => {
    const f = window.fixture, v = f.view.viewport, mesh = v.sceneObjects.get(f.native.part.entityId);
    const line = mesh.children.find(child => child.isLineSegments), originalGeometry = line.geometry;
    const counts = { lineMaterial: 0, lineGeometry: 0, sourceGeometry: 0 };
    line.material.addEventListener('dispose', () => counts.lineMaterial++);
    originalGeometry.addEventListener('dispose', () => counts.lineGeometry++);
    mesh.geometry.addEventListener('dispose', () => counts.sourceGeometry++);
    const reads = f.reads.length;
    await f.apply({ revision: 'hole-visibility:clear', rows: [] }, f.resources);
    const cleared = { objects: v.sceneObjects.size, ...counts };
    await f.apply(f.productionSnapshot, f.resources);
    const restored = v.sceneObjects.get(f.native.part.entityId).children.find(child => child.isLineSegments);
    const reused = restored.geometry === originalGeometry, readsAfter = f.reads.length;
    v.dispose();
    return { cleared, final: counts, reused, reads, readsAfter };
  });
  assert.equal(disposal.cleared.objects, 0);
  assert.equal(disposal.cleared.lineMaterial, 1);
  assert.equal(disposal.cleared.lineGeometry, 0, 'Geometry cache survives an entity-only clear');
  assert.equal(disposal.reused, true); assert.equal(disposal.reads, disposal.readsAfter);
  assert.equal(disposal.final.sourceGeometry, 1); assert.equal(disposal.final.lineGeometry, 1);
  assert.deepEqual(errors, []);
  const report = { passed: true, dllHash: bridge.dllHash, importerHash: bridge.importerHash, nativeGeometry: native.geometry, contrast, frontContrast, hiddenContrast,
    inspectHoles, lifecycle, roundedEdges, disposal, edgeMaterials: after.edges,
    scope: 'Production native SDO and GPU resources with production nesting modules and actual Chrome WebGL; controlled stdio host and browser shell; installed CEF excluded' };
  writeFileSync(resolve(artifacts, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ passed: true, artifacts, contrast, frontContrast, hiddenContrast, lifecycle, disposal }));
} catch (error) {
  if (page) await page.screenshot({ path: resolve(artifacts, 'failure.png') }).catch(() => {});
  writeFileSync(resolve(artifacts, 'failure.json'), JSON.stringify({ error: error.stack, dllHash: bridge.dllHash }, null, 2));
  throw error;
} finally {
  if (browser) await browser.close();
  await bridge.close();
}
