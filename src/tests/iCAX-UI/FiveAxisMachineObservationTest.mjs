import assert from 'node:assert/strict';
import {test} from 'node:test';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {
  disposeMachineObservation, machineObservationField, renderMachineObservation,
  setMachineObservationTable, syncMachineObservation,
} from '../../apps/five-axis-cam/webpage/machineObservation.mjs';

const originalRequest = globalThis.requestAnimationFrame, originalCancel = globalThis.cancelAnimationFrame;
const originalStorage = Object.getOwnPropertyDescriptor(globalThis, 'localStorage');
function fixture(t, {mode = 'machine-instances', machineId = 'machine', storage = new Map(), orthographic = false, declaredTable = true, splitTable = false} = {}) {
  const frames = new Map(); let frameId = 0, time = 0, current = true;
  const request = callback => {frames.set(++frameId, callback); return frameId;}, cancel = id => frames.delete(id);
  globalThis.requestAnimationFrame = request; globalThis.cancelAnimationFrame = cancel;
  Object.defineProperty(globalThis, 'localStorage', {configurable: true, value: {
    getItem: key => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, String(value)), removeItem: key => storage.delete(key),
  }});
  const scene = new THREE.Scene(), objects = new Map(), originals = new Map(), meshes = new Set();
  const camera = orthographic ? new THREE.OrthographicCamera(-16, 16, 12, -12, .1, 100)
    : new THREE.PerspectiveCamera(45, 4 / 3, .1, 100);
  camera.position.set(0, 0, 30); camera.lookAt(0, 0, 0); camera.updateMatrixWorld(true);
  const machine = {entityId: machineId, tools: [], elements: [{entityId: machineId, kind: 'machine'}]};
  const domain = {activeSetupId: 'setup', setups: [{id: 'setup', machineSetup: {}}], placements: []};
  const canvas = {clientWidth: 800, clientHeight: 600, width: 800, height: 600,
    getBoundingClientRect: () => ({left: 0, top: 0, width: 800, height: 600, right: 800, bottom: 600})};
  const view = {
    fiveAxisArea: 'simulation', fiveAxisUI: {tools: {simulation: mode}, selectionSnapshot: domain},
    fiveMachine: {machineId}, scene: {machines: [machine], domain},
    viewport: {scene, camera, sceneObjects: objects, renderer: {domElement: canvas}, getCameraState: () => ({target: {x: 0, y: 0, z: 0}})},
  };
  const context = {isCurrentProject: () => current};
  function mesh(size = [3, 3, 1], position = [0, 0, 8]) {
    const material = new THREE.MeshPhongMaterial({color: 0x668899, side: THREE.DoubleSide});
    const object = new THREE.Mesh(new THREE.BoxGeometry(...size), material); object.position.fromArray(position);
    meshes.add(object); originals.set(object, material); return object;
  }
  function part(id, {parent = machineId, size, position, group, ...properties} = {}) {
    const object = group || mesh(size, position); object.userData.objectId = id;
    scene.add(object); objects.set(id, object); machine.elements.push({entityId: id, kind: 'part', parentEntityId: parent, ...properties});
    scene.updateMatrixWorld(true); return object;
  }
  let table;
  if (splitTable) {
    const group = new THREE.Group(); group.add(mesh([6, 10, .5], [-5, 0, 0]), mesh([6, 10, .5], [5, 0, 0]));
    table = part('table', {group, workSupport: declaredTable});
  } else table = part('table', {size: [12, 10, .5], position: [0, 0, 0], workSupport: declaredTable});
  const body = part('body');
  function tick() {
    const next = frames.entries().next().value; assert.ok(next, 'observation schedules an animation callback');
    frames.delete(next[0]); scene.updateMatrixWorld(true); next[1](time += 100);
  }
  const sync = () => syncMachineObservation(context, view);
  t.after(() => {
    globalThis.requestAnimationFrame = request; globalThis.cancelAnimationFrame = cancel;
    disposeMachineObservation(view);
    for (const object of meshes) {object.geometry.dispose(); originals.get(object).dispose();}
    if (originalRequest === undefined) delete globalThis.requestAnimationFrame; else globalThis.requestAnimationFrame = originalRequest;
    if (originalCancel === undefined) delete globalThis.cancelAnimationFrame; else globalThis.cancelAnimationFrame = originalCancel;
    if (originalStorage) Object.defineProperty(globalThis, 'localStorage', originalStorage); else delete globalThis.localStorage;
  });
  return {view, machine, domain, objects, originals, frames, storage, body, table, part, mesh, tick, sync,
    setMode(value) {view.fiveAxisUI.tools.simulation = value;}, setCurrent(value) {current = value;}};
}
function assertFaded(f, object) {
  assert.equal(f.view.fiveMachineObservation.faded.has(object), true, `${object.userData.objectId || object.name} is an occluder`);
  assert.notEqual(object.material, f.originals.get(object)); assert.equal(object.material.transparent, true);
  assert.equal(object.material.depthWrite, false); assert.ok(object.material.opacity > 0 && object.material.opacity < 1);
}
function assertOpaque(f, object) {
  object.traverse(node => {if (!node.isMesh) return;
    assert.equal(f.view.fiveMachineObservation.faded.has(node), false, `${node.userData.objectId || object.userData.objectId} remains clear`);
    assert.equal(node.material, f.originals.get(node)); assert.equal(node.material.opacity, 1);
  });
}

test('perspective and orthographic views fade only foreground bodies in front of actual worktable surfaces', t => {
  for (const orthographic of [false, true]) {
    const f = fixture(t, {orthographic}), behind = f.part('behind-table', {position: [0, 0, -6]});
    const outside = f.part('outside-table', {position: [20, 0, 8]}), lateral = f.part('lateral-body', {position: [-20, 0, 0]});
    const geometry = f.body.geometry, positions = geometry.attributes.position.array.slice(), indices = geometry.index.array.slice();
    f.sync(); f.tick(); assertFaded(f, f.body);
    for (const object of [f.table, behind, outside, lateral]) assertOpaque(f, object);
    assert.deepEqual(geometry.attributes.position.array, positions); assert.deepEqual(geometry.index.array, indices);
    assert.deepEqual(f.machine.tools, []); assert.deepEqual(f.domain.setups[0].machineSetup, {});
    disposeMachineObservation(f.view);
  }
});

test('worktable silhouette gaps do not fade unrelated bodies inside its projected bounding rectangle', t => {
  for (const orthographic of [false, true]) {
    const f = fixture(t, {orthographic, splitTable: true}); f.body.position.set(-3.5, 0, 8);
    const gap = f.part('inside-table-gap', {size: [1, 4, 1], position: [0, 0, 8]});
    const behind = f.part('behind-real-panel', {position: [5, 0, -6]});
    f.sync(); f.tick(); assertFaded(f, f.body);
    for (const object of [f.table, gap, behind]) assertOpaque(f, object);
    disposeMachineObservation(f.view);
  }
});

test('camera movement updates actual table occluders while selection and tool choices never retarget the table', t => {
  const f = fixture(t), behind = f.part('opposite-body', {position: [0, 0, -8]});
  const outside = f.part('outside-body', {position: [20, 0, 8]});
  f.sync(); f.tick(); assertFaded(f, f.body); assertOpaque(f, behind);
  const originalFaded = f.body.material;
  for (const selected of ['body', 'outside-body', f.machine.entityId, '']) {
    f.view.fiveMachineSelectedPartId = selected; f.view.fiveMachine.toolId = 'outside-body';
    f.view.viewport.getCameraState = () => ({target: {x: 200, y: -100, z: 0}});
    f.sync(); f.tick(); assertFaded(f, f.body); assertOpaque(f, outside);
    assert.equal(f.body.material, originalFaded);
  }
  f.view.viewport.camera.position.set(0, 0, -30); f.view.viewport.camera.lookAt(0, 0, 0); f.sync(); f.tick();
  assertFaded(f, behind); assertOpaque(f, f.body); assertOpaque(f, outside); assertOpaque(f, f.table);
});

test('worktable descendants and helper subtrees stay opaque while a tool blocking the table also fades', t => {
  const f = fixture(t), tool = f.part('physical-tool', {size: [2, 2, 1], position: [0, 0, 15]});
  const tip = f.part('tool-tip', {parent: 'physical-tool', size: [2, 2, 1], position: [-2, 0, 15]});
  const tableChild = f.part('table-slat', {parent: 'table', size: [1, 1, .5], position: [2, 0, .5]});
  f.machine.tools.push({entityId: 'physical-tool'});
  const helper = new THREE.Group(), handle = f.mesh([2, 2, 1], [0, 0, 2]);
  helper.userData.independentInteractionVisual = true; helper.add(handle); f.body.add(helper);
  f.sync(); f.tick(); assertFaded(f, f.body);
  for (const object of [f.table, tableChild, handle]) assertOpaque(f, object);
  assertFaded(f, tool); assertFaded(f, tip);
});

test('missing table roles leave the body opaque and dirty or other-machine support drafts cannot create a display target', t => {
  const f = fixture(t, {declaredTable: false}), console = f.part('console', {position: [20, 0, 0]});
  f.view.fiveMachine.supportId = 'console'; f.view.fiveMachine.dirty = true;
  f.domain.setups[0].machineSetup = {machineId: 'different-machine', supportId: 'table'};
  const before = structuredClone(f.domain); f.sync(); f.tick();
  for (const object of [f.body, f.table, console]) assertOpaque(f, object);
  assert.deepEqual(f.domain, before, 'display observation never writes machining configuration');
  f.domain.setups[0].machineSetup = {machineId: f.machine.entityId, supportId: 'table'};
  f.sync(); f.tick(); assertFaded(f, f.body); assertOpaque(f, f.table); assertOpaque(f, console);
});

test('a validated display-only table pin persists per machine and never modifies machining state', t => {
  const f = fixture(t, {declaredTable: false}), before = structuredClone({domain: f.domain, machine: f.machine});
  f.sync(); f.tick(); assertOpaque(f, f.body);
  setMachineObservationTable(f.view, 'table'); f.sync(); f.tick(); assertFaded(f, f.body); assertOpaque(f, f.table);
  assert.equal(f.storage.size, 1); assert.ok([...f.storage.values()].includes('table'));
  for (const invalid of ['missing', 'other-machine-part', f.machine.entityId]) {
    const saved = [...f.storage]; assert.throws(() => setMachineObservationTable(f.view, invalid)); assert.deepEqual([...f.storage], saved);
  }
  assert.deepEqual({domain: f.domain, machine: f.machine}, before);
  disposeMachineObservation(f.view); f.view.fiveMachineObservation = undefined; f.sync(); f.tick();
  assertFaded(f, f.body); assertOpaque(f, f.table);
  const other = fixture(t, {machineId: 'other-machine', storage: f.storage, declaredTable: false});
  other.sync(); other.tick(); assertOpaque(other, other.body); disposeMachineObservation(other.view);
  setMachineObservationTable(f.view, ''); f.sync(); f.tick(); assertOpaque(f, f.body); assert.equal(f.storage.size, 0);
});

test('selected occluders keep highlights and translucency without leaking faded opacity into original materials', t => {
  const f = fixture(t); f.sync(); f.tick();
  const original = f.originals.get(f.body), faded = f.body.material, baseColor = original.color.clone(), highlight = new THREE.Color(0xffa340);
  f.body.userData.selectionMaterialState = [{color: faded.color.clone(), emissive: faded.emissive.clone(),
    emissiveIntensity: faded.emissiveIntensity, opacity: faded.opacity, transparent: faded.transparent, depthWrite: faded.depthWrite}];
  f.view.fiveMachineSelectedPartId = 'body'; faded.color.copy(highlight); faded.opacity = 1; faded.transparent = false; faded.depthWrite = true;
  f.sync(); f.tick(); assert.equal(f.body.material, faded); assertFaded(f, f.body); assert.ok(faded.color.equals(highlight));
  const saved = f.body.userData.selectionMaterialState[0];
  for (const key of ['opacity', 'transparent', 'depthWrite']) faded[key] = saved[key];
  assertFaded(f, f.body);
  machineObservationField(f.view, 'observeAutomatic', false); f.sync(); f.tick(); assertOpaque(f, f.body);
  original.color.copy(saved.color); original.emissive.copy(saved.emissive); original.emissiveIntensity = saved.emissiveIntensity;
  for (const key of ['opacity', 'transparent', 'depthWrite']) original[key] = saved[key];
  f.body.userData.selectionMaterialState = null;
  assert.equal(original.opacity, 1); assert.equal(original.transparent, false); assert.equal(original.depthWrite, true); assert.ok(original.color.equals(baseColor));
});

test('disabling observation and leaving the project dispose temporary materials and restore originals', t => {
  for (const [kind, stop] of [['toggle', f => machineObservationField(f.view, 'observeAutomatic', false)], ['area', f => {f.view.fiveAxisArea = 'toolpath';}], ['project', f => f.setCurrent(false)]]) {
    const f = fixture(t); f.sync(); f.tick(); const original = f.originals.get(f.body), faded = f.body.material;
    let temporaryDisposals = 0, originalDisposals = 0;
    faded.addEventListener('dispose', () => temporaryDisposals++); original.addEventListener('dispose', () => originalDisposals++);
    stop(f); f.tick(); assertOpaque(f, f.body); assert.equal(original.transparent, false); assert.equal(original.depthWrite, true);
    assert.equal(temporaryDisposals, 1); assert.equal(originalDisposals, 0);
    assert.equal(f.frames.size, kind === 'toggle' ? 1 : 0);
    disposeMachineObservation(f.view);
  }
});

test('instance controls offer one translucency toggle while executed trails remain in the job page', t => {
  const f = fixture(t), instance = renderMachineObservation(f.view);
  assert.match(instance, /data-five-machine-field="observeAutomatic"/);
  for (const key of ['observeTrail', 'observeRapid', 'observeCut']) assert.doesNotMatch(instance, new RegExp(`data-five-machine-field="${key}"`));
  const root = new THREE.Group(), geometry = new THREE.BufferGeometry(), material = new THREE.LineBasicMaterial();
  root.add(new THREE.LineSegments(geometry, material)); f.view.viewport.scene.add(root);
  let geometryDisposals = 0; geometry.addEventListener('dispose', () => geometryDisposals++);
  f.view.fiveMachineObservation.trail = {root}; f.view.fiveMachine.report = {}; f.sync();
  assert.equal(f.view.fiveMachineObservation.trail, null); assert.equal(root.parent, null); assert.equal(geometryDisposals, 1);
  f.setMode('machine-simulation'); const job = renderMachineObservation(f.view);
  for (const key of ['observeAutomatic', 'observeTrail', 'observeRapid', 'observeCut']) assert.match(job, new RegExp(`data-five-machine-field="${key}"`));
});
