// Native section edges -> production unfolding references -> actual SVG and WebGL.
// The host is a controlled stdio bridge; installed CEF and file dialogs are outside scope.
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve, sep } from 'node:path';
import { startSideSketchNativeBridge, repositoryRoot } from './fixtures/sideSketchNativeBridge.mjs';

const sourceRoot = resolve(repositoryRoot, 'src');
const artifacts = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repositoryRoot, 'output/tests/side-sketch-junctions-native'));
mkdirSync(artifacts, { recursive: true });
const bridge = await startSideSketchNativeBridge(artifacts);
let browser, page;
const junctions = report => report.unfolding.surfaces.find(surface => !surface.inner).wires
  .filter(wire => wire.role === 'profile-junction');
const close = (actual, expected, message) => assert.ok(Math.abs(actual - expected) < 1e-6, `${message}: ${actual} vs ${expected}`);
try {
  const cases = [];
  for (const [id, overrides, expected] of [
    ['round', { width: 40, wallThickness: 2 }, []],
    ['oval', { width: 60, depth: 40, wallThickness: 2 }, []],
    ['rect', { width: 60, depth: 40, wallThickness: 2, cornerRadius: 5, innerRadius: 3 },
      [5 * Math.PI / 2, 50 + 5 * Math.PI / 2, 50 + 5 * Math.PI, 80 + 5 * Math.PI,
        80 + 15 * Math.PI / 2, 130 + 15 * Math.PI / 2, 130 + 10 * Math.PI]],
  ]) {
    const descriptor = JSON.parse(readFileSync(resolve(sourceRoot, `apps/tube-designer/templates/profile/${id}/profile.json`), 'utf8'));
    const parameters = { ...Object.fromEntries(descriptor.parameters.map(item => [item.key, item.defaultValue])), ...overrides };
    const payload = { profileRef: { scope: 'system', id }, parameters, length: 200, previewResourceKey: `junction-${id}` };
    const report = await bridge.invoke('PreviewNestingSideSketchPart', payload);
    cases.push({ id, payload, report, expected });
  }
  const rectangle = (width, height) => {
    const points = [[-width / 2, -height / 2], [width / 2, -height / 2], [width / 2, height / 2], [-width / 2, height / 2]];
    return { kind: 'path', closed: true, segments: points.map((start, index) => ({ kind: 'line', start, end: points[(index + 1) % 4] })) };
  };
  const payload = { profile: { schema: 'icax.imported-tube-profile', schemaVersion: 1, kind: 'fixed-section',
    profileForm: 'fixed', name: '直角方管', width: 40, depth: 20, contours: [rectangle(40, 20), rectangle(36, 16)] },
    length: 200, quantity: 1, previewResourceKey: 'junction-sharp' };
  cases.push({ id: 'sharp', payload, report: await bridge.invoke('PreviewNestingSideSketchPart', payload), expected: [40, 60, 100] });
  writeFileSync(resolve(artifacts, 'native-references.json'), JSON.stringify({ dllHash: bridge.dllHash,
    cases: cases.map(item => ({ id: item.id, expected: item.expected, unfolding: item.report.unfolding })) }, null, 2));
  for (const item of cases) {
    const wires = junctions(item.report).sort((a, b) => a.points[0][1] - b.points[0][1]);
    assert.equal(wires.length, item.expected.length, `${item.id}: only real native profile joins become guides`);
    wires.forEach((wire, index) => {
      assert.equal(wire.points.length, 2);
      close(wire.points[0][0], 0, `${item.id}: axial start`);
      close(wire.points[1][0], 200, `${item.id}: axial end`);
      close(wire.points[0][1], item.expected[index], `${item.id}: exact native arc length`);
      close(wire.points[1][1], item.expected[index], `${item.id}: constant circumferential position`);
    });
  }
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs');
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome' });
  page = await browser.newPage({ viewport: { width: 1680, height: 1040 } });
  page.setDefaultTimeout(45000);
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.exposeFunction('nativeRpc', bridge.rpc);
  await page.route('http://junctions-native.test/**', route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><body><div id="app" class="tube-designer-workspace"></div></body>' });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ''));
    if (!pathname.startsWith('/src/') || !path.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    return route.fulfill({ contentType: 'text/javascript', body: readFileSync(path, 'utf8') });
  });
  await page.goto('http://junctions-native.test/');
  await page.addStyleTag({ content: readFileSync(resolve(sourceRoot, 'apps/_shared/workbench/styles/laser3dcam.css'), 'utf8') });
  await page.evaluate(async () => {
    const sketch = await import('/src/apps/tube-designer/webpage/sketchArea.mjs');
    const parts = await import('/src/apps/tube-designer/webpage/partsArea.mjs');
    const preview = await import('/src/apps/tube-designer/webpage/sideSketchPreview.mjs');
    const { tubeDesignerCss } = await import('/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    document.head.insertAdjacentHTML('beforeend', `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font:14px "Segoe UI",sans-serif;background:#13252d}</style>`);
    const mount = document.querySelector('#app');
    const view = { activeAreaId: 'nesting', scene: { tubeDesigner: { nestingGroups: [] } } };
    const f = window.fixture = { sketch, parts, preview, mount, view, calls: [], pending: 0 };
    Object.defineProperty(f, 'state', { get() { return view.tubeDesignerSketch; } });
    Object.defineProperty(f, 'draft', { get() { return f.state.sideByPart[f.state.targetPartId]; } });
    const invoke = async (method, payload) => {
      const call = { method, payload: structuredClone(payload) }; f.calls.push(call);
      const result = await window.nativeRpc({ action: 'invoke', scope: 'scene', method: method.replace(/^TubeDesigner\./, ''), payload });
      call.result = result; return result;
    };
    f.context = { mount, sceneProxy: { invoke, resources: { async get(url, options) {
      const response = await window.nativeRpc({ action: 'resource', payload: { url, version: Number(options?.headers?.get('ICAX-Resource-Version') || 0) } });
      return new Response(Uint8Array.from(atob(response.base64), c => c.charCodeAt(0)));
    } } } };
    f.ops = { createSideSketchViewport(options) { return f.viewport = createThreeViewport(options); }, showNotice() {}, renderProject() {
      if (!sketch.patchSketchDialogDom(f.context, view, mount, f.ops)) {
        const list = parts.listNestingParts(view.scene.tubeDesigner ?? {});
        mount.innerHTML = '<main>' + list.map(part => `<button data-cam-action="tube-designer-part-open-sketch" data-tube-designer-part-id="${part.entityId}">二维编辑</button>`).join('') + '</main>' + sketch.renderSectionSketchDialog(f.context, view);
        sketch.rememberSketchDialogDom(view, mount, f.context.sceneProxy);
      }
      sketch.attachSketchAreaInteractions(f.context, view, mount, f.ops);
    } };
    f.load = item => {
      sketch.beginNewPartSideSketch(view, item.payload, item.report);
      view.tubeDesignerSketchDialogOpen = true;
      f.ops.renderProject();
    };
    mount.addEventListener('click', event => {
      const target = event.target.closest('[data-cam-action]'); if (!target || target.disabled) return;
      f.pending++;
      const action = target.dataset.camAction;
      const operation = action.startsWith('tube-designer-sketch-')
        ? sketch.handleSketchAreaAction(f.context, view, action, target, f.ops)
        : parts.handlePartsAreaAction(f.context, view, action, target, f.ops);
      Promise.resolve(operation).catch(error => { f.error = error.message; }).finally(() => { f.pending--; });
    });
  });
  const ready = async () => {
    await page.waitForFunction(() => !window.fixture.pending && !window.fixture.view.pending);
    assert.equal(await page.evaluate(() => window.fixture.error || ''), '');
    assert.equal(await page.evaluate(async () => await window.fixture.preview.waitForSideSketchPreview(window.fixture.mount)), true);
  };
  const inspect = () => page.evaluate(() => {
    const f = window.fixture, svg = document.querySelector('[data-tube-sketch-canvas]');
    return { viewport: structuredClone(f.state.sideViewport), period: f.state.sideReference.unfolding.perimeter,
      entities: structuredClone(f.draft.entities), svgSame: !f.svg || f.svg === svg,
      canvasSame: !f.canvas || f.canvas === document.querySelector('.icax-three-viewport-canvas'),
      paths: [...svg.querySelectorAll('[data-tube-sketch-profile-junction]')].map(path => ({
        u: Number(path.dataset.tubeSketchProfileU), d: path.getAttribute('d'),
        dashed: getComputedStyle(path).strokeDasharray, pointer: getComputedStyle(path).pointerEvents,
        entity: Boolean(path.closest('[data-sketch-entity]')), role: path.dataset.tubeSketchWireRole,
      })) };
  });
  const validateVisible = (actual, expected) => {
    const span = actual.period / actual.viewport.zoom, min = actual.viewport.offsetU, max = min + span;
    const values = [];
    for (let lap = Math.floor(min / actual.period) - 1; lap <= Math.ceil(max / actual.period); lap++)
      for (const value of expected) { const u = value + lap * actual.period; if (u >= min && u <= max) values.push(u); }
    const paths = actual.paths.toSorted((a, b) => a.u - b.u); values.sort((a, b) => a - b);
    assert.equal(paths.length, values.length, 'Every visible native junction repeats at the circumference period');
    paths.forEach((path, index) => {
      close(path.u, values[index], 'Periodic native U');
      assert.notEqual(path.dashed, 'none'); assert.equal(path.pointer, 'none'); assert.equal(path.entity, false);
      const coordinates = [...path.d.matchAll(/[-+]?\d*\.?\d+(?:e[-+]?\d+)?/gi)].map(match => Number(match[0]));
      assert.equal(coordinates.length, 4);
      close(coordinates[0], coordinates[2], 'The native junction must be vertical');
      close(coordinates[1], 510, 'Guide terminates at S0'); close(coordinates[3], 90, 'Guide terminates at blank length');
      assert.ok(coordinates[0] >= 70 - 1e-6 && coordinates[0] <= 950 + 1e-6, 'Periodic guides must clip to the visible horizontal interval');
      close(coordinates[0], 70 + (path.u - min) / span * 880, 'Guide follows the actual pan and zoom mapping');
    });
  };
  const results = [];
  for (const item of cases) {
    await page.evaluate(item => window.fixture.load(item), item); await ready();
    const initial = await inspect(); validateVisible(initial, item.expected);
    await page.screenshot({ path: resolve(artifacts, `${item.id}-native-junctions.png`) });
    results.push({ id: item.id, nativeU: junctions(item.report).map(wire => wire.points[0][1]), initial });
    if (item.id !== 'sharp') {
      await page.locator('[data-sketch-command="sketch.cancel"]').click();
      await page.waitForFunction(() => !window.fixture.pending && !window.fixture.view.pending);
    }
  }
  await page.evaluate(() => {
    const f = window.fixture; f.svg = document.querySelector('[data-tube-sketch-canvas]');
    f.canvas = document.querySelector('.icax-three-viewport-canvas');
  });
  const canvas = page.locator('[data-tube-sketch-canvas]'), box = await canvas.boundingBox();
  await page.mouse.move(box.x + box.width * 0.55, box.y + box.height * 0.5);
  await page.mouse.wheel(0, 850);
  await page.waitForFunction(() => window.fixture.state.sideViewport.zoom < 1);
  await page.mouse.move(box.x + box.width * 0.55, box.y + box.height * 0.5);
  await page.mouse.down({ button: 'middle' });
  await page.mouse.move(box.x + box.width * 0.25, box.y + box.height * 0.5, { steps: 5 });
  await page.mouse.up({ button: 'middle' });
  const moved = await inspect(); validateVisible(moved, [40, 60, 100]);
  assert.equal(moved.svgSame, true); assert.equal(moved.canvasSame, true); assert.deepEqual(moved.entities, []);
  assert.ok(moved.paths.length > 3 && moved.viewport.offsetU !== 0);
  const guidePoint = await page.locator('[data-tube-sketch-profile-junction]').first().evaluate(path => {
    const box = path.getBoundingClientRect(); return { x: box.x, y: box.y + box.height / 2 };
  });
  await page.mouse.click(guidePoint.x, guidePoint.y);
  assert.deepEqual((await inspect()).entities, [], 'Reference clicks cannot create or select drawing entities');
  await page.screenshot({ path: resolve(artifacts, 'sharp-periodic-pan-zoom.png') });
  await page.evaluate(() => {
    const f = window.fixture;
    f.draft.entities = [{ id: 'corner-hole', kind: 'circle', cx: 40, cy: 75, radius: 4, closed: true }];
    f.ops.renderProject();
  });
  await page.locator('[data-sketch-command="sketch.commit"]').click();
  await page.waitForFunction(() => !window.fixture.pending && !window.fixture.view.tubeDesignerSketchDialogOpen);
  const created = await page.evaluate(() => window.fixture.calls.findLast(call => call.method === 'TubeDesigner.AddNestingSideSketchPart'));
  assert.ok(created?.result?.partEntityId);
  assert.equal(created.payload.sketch.entities.length, 1, 'Native guides never become saved sketch entities');
  assert.equal(created.payload.sketch.entities[0].id, 'corner-hole');
  await bridge.rpc({ action: 'save', payload: { file: 'side-sketch-junctions-native.ictd' } });
  const reopened = await bridge.rpc({ action: 'open', payload: { file: 'side-sketch-junctions-native.ictd' } });
  await page.evaluate(scene => { const f = window.fixture; f.view.scene = scene; f.ops.renderProject(); }, reopened.snapshot);
  await page.locator(`[data-cam-action="tube-designer-part-open-sketch"][data-tube-designer-part-id="${created.result.partEntityId}"]`).click(); await ready();
  const restored = await inspect(); validateVisible(restored, [40, 60, 100]);
  assert.equal(restored.entities.length, 1); assert.equal(restored.entities[0].id, 'corner-hole');
  const restoredNative = await page.evaluate(() => window.fixture.state.sideReference);
  assert.deepEqual(junctions(restoredNative).map(wire => wire.points), junctions(cases.at(-1).report).map(wire => wire.points),
    'A hole crossing U40 must not split or truncate the immutable blank reference');
  await page.screenshot({ path: resolve(artifacts, 'sharp-hole-frozen-blank-reopen.png') });
  assert.deepEqual(errors, []);
  writeFileSync(resolve(artifacts, 'report.json'), JSON.stringify({ passed: true, dllHash: bridge.dllHash, cases: results, moved, restored,
    persistedEntities: created.payload.sketch.entities, coverage: 'Real native section topology/SDO/GPU resources; production SVG/Three and mouse wheel/middle-button interactions; .ictd reopen from frozen blank; controlled stdio host and browser shell, no installed CEF' }, null, 2));
  console.log(JSON.stringify({ passed: true, artifacts, cases: results.map(item => ({ id: item.id, nativeU: item.nativeU })), periodicPaths: moved.paths.length }));
} catch (error) {
  if (page) await page.screenshot({ path: resolve(artifacts, 'failure.png') }).catch(() => {});
  writeFileSync(resolve(artifacts, 'failure.json'), JSON.stringify({ error: error.stack, dllHash: bridge.dllHash }, null, 2));
  throw error;
} finally {
  if (browser) await browser.close();
  await bridge.close();
}
