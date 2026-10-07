// Real mouse events, production WebGL viewport and its actual scene raycaster.
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, serveBrowserAsset } from './browserPackageRuntime.mjs';

const output = browserReportDirectory('hover-selection-20261006/annotation-source');
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({
  headless: true,
  channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge',
});
const errors = [], checks = [];
let passed = false;
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 760 } });
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://annotation-picking.test/**', route => {
    if (new URL(route.request().url()).pathname === '/') return route.fulfill({
      contentType: 'text/html', body: '<!doctype html><meta charset="utf-8">' +
        '<style>body{margin:0}#viewport{width:900px;height:600px}' +
        '#outside{height:160px;background:#eee}</style><div id="viewport"></div><div id="outside"></div>',
    });
    return serveBrowserAsset(route);
  });
  await page.goto('http://annotation-picking.test/');
  const initial = await page.evaluate(async () => {
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const state = window.annotationPicking = { picks: [], hovers: [], edits: 0, commits: 0, cameras: [] };
    const viewport = state.viewport = createThreeViewport({
      continuousRender: false, showGrid: false,
      onPick(data, hit) {
        state.picks.push({ objectId: data?.objectId ?? null, actualMeshHit: hit?.object?.isMesh === true });
        viewport.setSelectedObjectId(data?.objectId ?? null);
      },
      onHover(data, hit, event, hits, { pointerActive }) {
        state.hovers.push({ objectId: data?.objectId ?? null,
          actualMeshHit: hit?.object?.isMesh === true, hitCount: hits.length, pointerActive });
      },
      onCameraChange(camera, { reason }) { state.cameras.push(reason); },
    });
    viewport.mount(document.querySelector('#viewport'));
    state.camera = { target: { x: 0, y: 0, z: 0 }, radius: 600, theta: -Math.PI / 2, phi: Math.PI / 2 };
    viewport.setCameraState(state.camera);
    const mesh = new THREE.Mesh(new THREE.BoxGeometry(400, 20, 300),
      new THREE.MeshBasicMaterial({ color: 0x779899 }));
    mesh.userData.objectId = 'fixture.component';
    viewport.content.add(mesh);
    viewport.sceneObjects.set(mesh.userData.objectId, mesh);
    state.annotation = {
      id: 'fixture.width', parameter: 'width', start: [-100, -20, 0], end: [100, -20, 0],
      label: '正面宽度 1,200 mm', oldValue: 1200, newValue: 1200,
      cleanLabel: '正面宽度 1,200 mm', changedLabel: '正面宽度 1,200 mm',
    };
    state.rest = () => viewport.setSpecificationAnnotations([state.annotation]);
    state.rest();
    viewport.specificationLabelLayer.addEventListener('click', event => {
      if (!event.target.matches('.icax-three-specification-trigger')) return;
      state.edits += 1;
      viewport.setSpecificationAnnotations([{ ...state.annotation, editing: true,
        editor: { type: 'text', value: 1200 } }]);
      viewport.focusSpecificationAnnotationEditor('width');
    });
    viewport.specificationLabelLayer.addEventListener('change', () => {
      state.commits += 1;
      state.rest();
    });
    const rect = document.querySelector('.icax-three-specification-label').getBoundingClientRect();
    const label = { x: (rect.left + rect.right) / 2, y: (rect.top + rect.bottom) / 2 };
    const bare = { x: label.x + 140, y: label.y + 70 };
    return { label, bare,
      actualWebGL: viewport.renderer.getContext() instanceof WebGL2RenderingContext,
      labelPointerTargetIsCanvas: document.elementFromPoint(label.x, label.y) === viewport.renderer.domElement,
    };
  });
  assert.equal(initial.actualWebGL, true);
  assert.equal(initial.labelPointerTargetIsCanvas, true, 'Resting labels must preserve canvas navigation');
  const state = () => page.evaluate(() => ({ picks: [...window.annotationPicking.picks],
    hovers: [...window.annotationPicking.hovers],
    hovered: window.annotationPicking.hovers.at(-1)?.objectId ?? null,
    selected: window.annotationPicking.viewport.selectedObjectId,
    edits: window.annotationPicking.edits, commits: window.annotationPicking.commits,
    pointerId: window.annotationPicking.viewport.navigation.pointerId,
    cameras: [...window.annotationPicking.cameras],
    camera: window.annotationPicking.viewport.getCameraState(),
  }));
  const labelPoint = () => page.locator('.icax-three-specification-label').evaluate(node => {
    const rect = node.getBoundingClientRect();
    return { x: (rect.left + rect.right) / 2, y: (rect.top + rect.bottom) / 2 };
  });
  const drag = async (from, to, button = 'left') => {
    await page.mouse.move(from.x, from.y);
    await page.mouse.down({ button });
    const before = await state();
    assert.equal(before.hovered, null, `${button} pointer press clears hover`);
    const hoverIndex = before.hovers.length;
    await page.mouse.move(to.x, to.y, { steps: 5 });
    await page.evaluate(() => new Promise(requestAnimationFrame));
    const during = await state();
    assert.equal(during.hovered, null, `${button} drag must not hover a component`);
    assert.equal(during.selected, before.selected, `${button} drag preserves selected component`);
    assert.ok(during.hovers.slice(hoverIndex).every(event => event.objectId === null),
      `${button} drag cannot emit an intermediate component hover`);
    await page.mouse.up({ button });
  };
  const expectPicks = async (count, name) => {
    const result = await state();
    assert.equal(result.picks.length, count, name);
    checks.push({ name, pickCallbacks: count });
    return result;
  };
  const expectHover = async (objectId, name) => {
    await page.waitForFunction(expected =>
      (window.annotationPicking.hovers.at(-1)?.objectId ?? null) === expected, objectId);
    const result = await state();
    checks.push({ name, hovered: result.hovered, selected: result.selected,
      hoverCallbacks: result.hovers.length });
    return result;
  };

  await page.mouse.move(initial.bare.x, initial.bare.y);
  const normalHover = await expectHover('fixture.component', 'Normal scene movement hovers a real component');
  assert.equal(normalHover.hovers.at(-1).actualMeshHit, true);
  assert.ok(normalHover.hovers.at(-1).hitCount > 0);
  assert.equal(normalHover.selected, '', 'Hover alone must not select a component');
  assert.equal(normalHover.picks.length, 0, 'Hover alone must not emit a pick');
  const beforeSameObject = normalHover.hovers.length;
  await page.mouse.move(initial.bare.x + 2, initial.bare.y + 2);
  await page.evaluate(() => new Promise(requestAnimationFrame));
  assert.equal((await state()).hovers.length, beforeSameObject,
    'Moving across the same component does not repeatedly emit hover changes');
  await page.mouse.move(initial.label.x, initial.label.y);
  await expectHover(null, 'Resting annotation rectangle blocks hover on the component underneath');

  // Both positive controls hit the same real mesh that sits under the label.
  await page.mouse.click(initial.bare.x, initial.bare.y);
  const normal = await expectPicks(1, 'Normal scene click selects a real component');
  assert.deepEqual(normal.picks[0], { objectId: 'fixture.component', actualMeshHit: true });
  await page.mouse.click(initial.label.x, initial.label.y);
  await expectPicks(1, 'Resting annotation single click does not select or clear the scene');
  await drag(initial.label, initial.bare);
  await expectPicks(1, 'Annotation press followed by canvas release does not pick');
  await drag(initial.bare, initial.label);
  await expectPicks(1, 'Canvas press followed by annotation release does not pick');
  await drag(initial.label, { x: 930, y: 660 });
  const outside = await expectPicks(1, 'Annotation gesture released outside the viewport does not pick');
  assert.equal(outside.pointerId, null, 'Outside release must clear the gesture origin');
  await page.mouse.click(initial.bare.x, initial.bare.y);
  await expectPicks(2, 'Normal scene picking resumes after an outside release');

  await page.mouse.dblclick(initial.label.x, initial.label.y, { delay: 50 });
  const editing = await expectPicks(2, 'Both clicks before double click editing do not pick');
  assert.equal(editing.edits, 1, 'Double click must still open the annotation editor once');
  const input = page.locator('.icax-three-specification-input');
  assert.equal(await input.evaluate(node => document.activeElement === node), true);
  const inputRect = await input.boundingBox();
  await page.mouse.move(inputRect.x + 10, inputRect.y + inputRect.height / 2);
  await expectHover(null, 'Editing annotation input blocks scene hover');
  await drag({ x: inputRect.x + 10, y: inputRect.y + inputRect.height / 2 },
    { x: inputRect.x + 38, y: inputRect.y + inputRect.height / 2 });
  const selection = await input.evaluate(node => ({ focused: document.activeElement === node,
    selectionStart: node.selectionStart, selectionEnd: node.selectionEnd }));
  assert.equal(selection.focused, true);
  assert.ok(selection.selectionEnd > selection.selectionStart, 'Real mouse drag must select input text');
  await expectPicks(2, 'Mouse selection inside annotation input does not pick');
  await drag({ x: inputRect.x + 12, y: inputRect.y + inputRect.height / 2 }, initial.bare);
  await expectPicks(2, 'Editor press followed by canvas release does not inherit picking permission');
  await page.mouse.click(initial.bare.x, initial.bare.y);
  await expectPicks(3, 'Scene picking resumes after editing input');
  await page.evaluate(() => window.annotationPicking.rest());

  let point = await labelPoint();
  const beforeOrbit = (await state()).camera;
  await drag(point, { x: point.x + 26, y: point.y + 16 }, 'right');
  const afterOrbit = await expectPicks(3, 'Right mouse orbit on resting annotation does not pick');
  assert.notEqual(afterOrbit.camera.theta, beforeOrbit.theta, 'Right mouse orbit must continue through the label');
  assert.ok(afterOrbit.cameras.includes('orbit'));
  point = await labelPoint();
  const beforePan = (await state()).camera;
  await drag(point, { x: point.x + 20, y: point.y + 14 }, 'middle');
  const afterPan = await expectPicks(3, 'Middle mouse pan on resting annotation does not pick');
  assert.notDeepEqual(afterPan.camera.target, beforePan.target, 'Middle mouse pan must continue through the label');
  assert.ok(afterPan.cameras.includes('pan'));
  point = await labelPoint();
  const beforeZoom = (await state()).camera.radius;
  await page.mouse.move(point.x, point.y);
  await page.mouse.wheel(0, 120);
  await page.waitForFunction(radius => window.annotationPicking.viewport.getCameraState().radius !== radius, beforeZoom);
  await expectPicks(3, 'Wheel zoom on resting annotation does not pick');
  await expectHover(null, 'Wheel zoom clears scene hover without changing selection');

  await page.evaluate(() => {
    const fixture = window.annotationPicking;
    fixture.viewport.setCameraState(fixture.camera);
    fixture.viewport.setSpecificationAnnotations([{ ...fixture.annotation, editable: false }]);
  });
  point = await labelPoint();
  await page.mouse.click(point.x, point.y);
  await expectPicks(3, 'Visible read only annotation also blocks scene picking');
  await page.locator('#viewport').screenshot({ path: resolve(output, 'scene.png') });
  await page.evaluate(() => document.querySelector('.icax-three-specification-label').hidden = true);
  await page.mouse.click(point.x, point.y);
  const hidden = await expectPicks(4, 'Hidden annotation no longer blocks its former screen rectangle');
  assert.deepEqual(hidden.picks.at(-1), { objectId: 'fixture.component', actualMeshHit: true },
    'The component under the annotation must be genuinely pickable');
  await page.evaluate(() => window.annotationPicking.viewport.clearSpecificationAnnotations());
  await page.mouse.click(point.x, point.y);
  await expectPicks(5, 'Removing annotations restores picking at the same location');

  // Check navigation over the actual model as well as the annotation rectangle.
  const resetModel = async () => {
    await page.evaluate(() => {
      const fixture = window.annotationPicking;
      fixture.viewport.setCameraState(fixture.camera);
      fixture.viewport.clearSpecificationAnnotations();
    });
    await page.mouse.move(initial.bare.x, initial.bare.y);
    return expectHover('fixture.component', 'Model hover resumes after an ordinary pointer move');
  };
  for (const button of ['left', 'right', 'middle']) {
    await resetModel();
    await drag(initial.bare, { x: initial.bare.x + 24, y: initial.bare.y + 16 }, button);
    const navigation = await expectPicks(5, `${button} model drag does not pick`);
    assert.equal(navigation.selected, 'fixture.component');
    await expectHover(null, `${button} model drag clears hover and preserves selection`);
  }

  await resetModel();
  const beforeModelZoom = (await state()).camera.radius;
  await page.mouse.wheel(0, 80);
  await page.waitForFunction(radius => window.annotationPicking.viewport.getCameraState().radius !== radius,
    beforeModelZoom);
  const modelZoom = await expectHover(null, 'Wheel zoom over a model clears hover');
  assert.equal(modelZoom.selected, 'fixture.component');
  await expectPicks(5, 'Wheel zoom over a model preserves scene picking state');

  await resetModel();
  await page.mouse.move(950, 650);
  const leave = await expectHover(null, 'Leaving the canvas clears hover');
  assert.equal(leave.hovers.at(-1).pointerActive, false);
  assert.equal(leave.selected, 'fixture.component');

  await resetModel();
  await page.evaluate(() => window.annotationPicking.viewport.setPickingEnabled(false));
  const disabled = await expectHover(null, 'Disabling picking clears hover');
  assert.equal(disabled.hovers.at(-1).pointerActive, false);
  await page.mouse.move(initial.bare.x + 6, initial.bare.y + 6);
  await page.evaluate(() => new Promise(requestAnimationFrame));
  assert.equal((await state()).hovered, null, 'Disabled picking cannot resume hover');
  assert.equal((await state()).selected, 'fixture.component');
  await page.evaluate(() => window.annotationPicking.viewport.setPickingEnabled(true));

  // Two synchronous events exercise cancellation of a queued RAF without mocking
  // the raycaster or callback. Ordinary movement and gestures above use real mouse.
  for (const cancel of ['leave', 'disable', 'cancel', 'blur']) {
    await resetModel();
    const cancellation = await page.evaluate(async ({ bare, cancel }) => {
      const fixture = window.annotationPicking;
      const canvas = fixture.viewport.renderer.domElement;
      canvas.dispatchEvent(new PointerEvent('pointermove', {
        clientX: bare.x + 1, clientY: bare.y + 1, pointerId: 1, bubbles: true,
      }));
      const queued = fixture.viewport.hoverFrame != null;
      if (cancel === 'leave') canvas.dispatchEvent(new PointerEvent('pointerleave'));
      if (cancel === 'disable') fixture.viewport.setPickingEnabled(false);
      if (cancel === 'cancel') canvas.dispatchEvent(new PointerEvent('pointercancel', { pointerId: 1 }));
      if (cancel === 'blur') window.dispatchEvent(new Event('blur'));
      const last = fixture.hovers.length;
      await new Promise(requestAnimationFrame);
      await new Promise(requestAnimationFrame);
      return { queued, hovered: fixture.hovers.at(-1)?.objectId ?? null,
        lateEvents: fixture.hovers.slice(last), selected: fixture.viewport.selectedObjectId };
    }, { bare: initial.bare, cancel });
    assert.equal(cancellation.queued, true, `${cancel} must cancel a genuinely pending hover frame`);
    assert.equal(cancellation.hovered, null, `${cancel} clears the current hover`);
    assert.deepEqual(cancellation.lateEvents, [], `${cancel} must not restore hover from a late RAF`);
    assert.equal(cancellation.selected, 'fixture.component');
    checks.push({ name: `Pending hover cancelled by ${cancel} cannot arrive late`, ...cancellation });
    await page.evaluate(() => window.annotationPicking.viewport.setPickingEnabled(true));
  }

  await page.evaluate(() => {
    const fixture = window.annotationPicking;
    fixture.viewport.setCameraState(fixture.camera);
    fixture.viewport.setSpecificationAnnotations([{ ...fixture.annotation, editing: true,
      editor: { type: 'select', value: 1200,
        options: [{ value: 1200, label: '1,200 mm' }, { value: 1500, label: '1,500 mm' }] } }]);
  });
  const choice = page.locator('.icax-three-specification-input');
  assert.equal(await choice.evaluate(node => node.tagName), 'SELECT');
  const choiceRect = await choice.boundingBox();
  await page.mouse.move(choiceRect.x + 12, choiceRect.y + choiceRect.height / 2);
  const choiceHover = await expectHover(null, 'Editing annotation choice blocks scene hover');
  assert.equal(choiceHover.selected, 'fixture.component');
  await page.mouse.click(choiceRect.x + 12, choiceRect.y + choiceRect.height / 2);
  await page.keyboard.press('Escape');
  await expectPicks(5, 'Editing annotation choice does not pick or clear the scene');
  await page.locator('#viewport').screenshot({ path: resolve(output, 'hover-guards.png') });
  assert.deepEqual(errors, []);
  await page.evaluate(() => window.annotationPicking.viewport.dispose());
  passed = true;
  console.log(`PASS ${checks.length} annotation, hover and scene picking checks with actual WebGL and real mouse events`);
} catch (error) {
  errors.push(error.stack || String(error));
  throw error;
} finally {
  await browser.close();
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ status: passed ? 'passed' : 'failed',
    actualWebGL: true, realMouseEvents: true, actualProductionRaycaster: true,
    geometry: 'test mesh with annotation positioned over its surface',
    nativeTransport: 'not needed for pointer interaction regression',
    queuedRafCancellation: 'synchronous DOM events exercising the production handlers and actual scheduler',
    standaloneCefEndToEnd: false, checks, errors }, null, 2));
}
