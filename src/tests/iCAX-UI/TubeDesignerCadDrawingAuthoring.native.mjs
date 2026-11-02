// Current original drawing-workbench commands against real registered SDOs.
// This isolates backend authoring checks from the earlier 2D browser setup.
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
import { repositoryRoot, startSideSketchNativeBridge } from './fixtures/sideSketchNativeBridge.mjs';
import { createDrawingCadState, captureDrawingCadBaseline, installDrawingCatalogue,
  getDrawingCadPayload, normalizeDrawingFeature } from '../../apps/tube-designer/webpage/partDrawingModel.mjs';

const output = resolve(repositoryRoot, 'output/tests/cad-drawing-authoring-native');
mkdirSync(output, { recursive: true });
const bridge = await startSideSketchNativeBridge(output);
const results = [];
const get = (id, authoringView = 'drawing') => bridge.invoke('GetTubeCadDocument', { partEntityId: id, authoringView });
const clone = value => structuredClone(value);
const defaults = id => Object.fromEntries(JSON.parse(readFileSync(resolve(repositoryRoot,
  `src/apps/tube-designer/templates/profile/${id}/profile.json`), 'utf8')).parameters.map(p => [p.key, p.defaultValue]));
const tools = await bridge.invoke('GetPartDrawingTools');
const circleTool = tools.tools.find(tool => tool.id === 'circle');
assert.ok(circleTool);
const circle = (id, station) => ({ id, type: 'circle', toolTarget: circleTool.target,
  toolRef: { scope: 'system', id: circleTool.id }, toolParameters: { ...circleTool.defaultParameters, diameter: 10 },
  station, face: 'top', offset: 0, layoutDatum: 'base' });
let serial = 0;
async function create(features = [circle('initial-circle', 140)], profileId = 'rect') {
  const parameters = profileId === 'rect'
    ? { ...defaults(profileId), width: 80, depth: 60, wallThickness: 3, cornerRadius: 0, innerRadius: 0 }
    : { ...defaults(profileId), width: 60, wallThickness: 3 };
  const evaluated = await bridge.invoke('EvaluateProfilePackage', { profileRef: { scope: 'system', id: profileId }, parameters }, 'product');
  const added = await bridge.invoke('AddPartDrawing', { drawing: { schemaVersion: 1, length: 400,
    section: { source: 'library', profile: evaluated.profile } }, features, ends: {}, quantity: 1, name: '当前三维工作台-' + ++serial });
  return get(added.partEntityId);
}
const stateOf = read => {
  const state = createDrawingCadState({ entityId: read.partEntityId }, read);
  installDrawingCatalogue(state, tools); captureDrawingCadBaseline(state); return state;
};
async function equivalent(id) {
  const body = await bridge.rpc({ action: 'geometry', payload: { partEntityId: id } });
  const document = await bridge.rpc({ action: 'cad-document-geometry', payload: { partEntityId: id } });
  assert.equal(body.valid, true); assert.equal(document.valid, true);
  assert.ok(document.directedDifferenceVolume <= Math.max(1e-3, body.volume * 1e-8));
  return { body, document };
}
async function preview(read, state, label) {
  const payload = getDrawingCadPayload(state), key = 'current-drawing-native-' + label;
  const result = await bridge.invoke('PreviewTubeCadDocument', { ...payload, previewResourceKey: key,
    individualToolPreviews: true, includeTopology: true });
  assert.ok(result.geometry?.url && result.baseGeometry?.url && result.topology?.edges?.length);
  assert.deepEqual(await get(read.partEntityId), read, 'Preview preserves the complete authoritative document and metadata');
  const released = await bridge.invoke('ReleaseTubeCadDocumentPreview', { previewResourceKey: key });
  assert.ok(released.resourceCount > 0);
  await assert.rejects(bridge.rpc({ action: 'resource', payload: result.geometry }), /not present|不存在|缺失/);
  return result;
}
async function apply(read, state) {
  const response = await bridge.invoke('ApplyTubeCadDocument', getDrawingCadPayload(state));
  assert.ok(response.snapshot?.tubeDesigner, 'Current saved response has the canonical scene snapshot');
  const saved = await get(read.partEntityId);
  assert.equal(saved.documentResourceId, read.documentResourceId);
  assert.ok(Number(saved.revision) > Number(read.revision));
  return saved;
}
async function reopen(read, label) {
  const file = 'drawing-authoring-' + label + '.ictd';
  await bridge.rpc({ action: 'save', payload: { file } });
  await bridge.rpc({ action: 'open', payload: { file } });
  assert.deepEqual(await get(read.partEntityId), read, 'Fresh project reopen preserves current graph and its workspace projection');
  return { file, ...await equivalent(read.partEntityId) };
}
async function check(label, action) {
  try { results.push({ case: label, passed: true, ...await action() }); }
  catch (error) { results.push({ case: label, passed: false, error: error.stack }); }
}
try {
  await check('Current 3D projection and local feature add/move/remove preserve unedited exact geometry', async () => {
    const read = await create([circle('first-circle', 140), circle('second-circle', 220)]), state = stateOf(read);
    assert.equal(state.features.length, 2);
    const moved = state.features[0], removed = state.features[1], movedId = moved.cadNodeId ?? moved.id;
    const originalNode = read.geometry.SolidNodes.find(n => n.ID === movedId);
    moved.station += 25;
    state.features.splice(1, 1);
    state.features.push(normalizeDrawingFeature({ ...clone(moved), id: 'new-current-local-feature', cadNodeId: undefined, station: 300 }));
    writeFileSync(resolve(output, 'local-diff-before.json'), JSON.stringify({ read, payload: getDrawingCadPayload(state) }, null, 2));
    await preview(read, state, 'local-feature-diff');
    const saved = await apply(read, state);
    writeFileSync(resolve(output, 'local-diff-after.json'), JSON.stringify(saved, null, 2));
    assert.equal(saved.authoringView.features.length, 2);
    const savedMoved = saved.authoringView.features.find(f => f.cadNodeId === movedId || f.id === movedId);
    assert.ok(savedMoved); assert.ok(Math.abs(savedMoved.station - moved.station) < 1e-6);
    assert.equal(saved.geometry.SolidNodes.some(n => n.ID === (removed.cadNodeId ?? removed.id)), false);
    assert.deepEqual(saved.geometry.SolidNodes.find(n => n.ID === movedId).Data.value.Section, originalNode.Data.value.Section);
    assert.ok(saved.authoringView.features.some(f => Math.abs(f.station - 300) < 1e-6));
    return { id: saved.partEntityId, movedId, ...await reopen(saved, 'local-feature-diff') };
  });
  await check('Ordinary circle keeps the original 3D diameter tool and four exact arc ranges', async () => {
    const read = await create(), state = stateOf(read), feature = state.features[0];
    assert.equal(feature.type, 'circle', 'An exact safely aligned circle must use the original circle diameter control');
    assert.ok(Math.abs(feature.toolParameters.diameter - 10) < 1e-9);
    const id = feature.cadNodeId ?? feature.id;
    const node = read.geometry.SolidNodes.find(n => n.ID === id), loop = node.Data.value.Section.Boundaries[0];
    assert.equal(loop.Curves.length, 4);
    feature.toolParameters.diameter = 12;
    await preview(read, state, 'circle-diameter');
    const saved = await apply(read, state), savedLoop = saved.geometry.SolidNodes.find(n => n.ID === id).Data.value.Section.Boundaries[0];
    assert.ok(savedLoop.Curves.every(c => Math.abs(c.Segment.Curve.value.Radius - 6) < 1e-9));
    assert.deepEqual(savedLoop.Curves.map(c => c.Segment.Range), loop.Curves.map(c => c.Segment.Range));
    assert.deepEqual(savedLoop.Curves.map(c => c.Segment.Curve.value.Placement), loop.Curves.map(c => c.Segment.Curve.value.Placement));
    assert.ok(Math.abs(saved.authoringView.features.find(f => (f.cadNodeId ?? f.id) === id).toolParameters.diameter - 12) < 1e-9);
    const evidence = await reopen(saved, 'circle-diameter'), expectedVolume = (80 * 60 - 74 * 54) * 400 - Math.PI * 6 ** 2 * 3;
    assert.ok(Math.abs(evidence.body.volume - expectedVolume) < 1e-3, 'The actual 12 mm hole removes the analytical wall material');
    return { id: saved.partEntityId, cutId: id, expectedVolume, ...evidence };
  });
  await check('Original main name, quantity and material controls save through the current CAD transaction', async () => {
    const read = await create(), state = stateOf(read);
    state.drawing.name = '当前图权威零件名称'; state.drawing.quantity = 3; state.drawing.material = '铝';
    await preview(read, state, 'metadata');
    const saved = await apply(read, state);
    assert.equal(saved.authoringView.drawing.name, state.drawing.name);
    assert.equal(saved.authoringView.drawing.quantity, 3);
    assert.equal(saved.authoringView.drawing.material, '铝');
    const beforeGeometry = clone(read.geometry), afterGeometry = clone(saved.geometry);
    delete beforeGeometry.Version; delete afterGeometry.Version;
    assert.deepEqual(afterGeometry, beforeGeometry, 'Metadata never changes geometric nodes, curves, bindings or bodies');
    return { id: saved.partEntityId, ...await reopen(saved, 'metadata') };
  });
  await check('Original 3D end tool supports local add, update and removal', async () => {
    const read = await create([]), state = stateOf(read), tool = tools.tools.find(t => t.id === 'end-miter');
    assert.ok(tool);
    state.ends.start = { type: 'end-miter', toolRef: { scope: 'system', id: tool.id },
      toolParameters: { ...tool.defaultParameters, angle: 10 }, angle: 10, datum: 'center', trim: 10, rotation: 0 };
    await preview(read, state, 'end-add');
    let saved = await apply(read, state), end = saved.authoringView.ends.start;
    assert.equal(end.type, 'end-miter'); assert.ok(end.cadNodeId ?? end.id);
    const firstId = end.cadNodeId ?? end.id;
    let next = stateOf(saved); next.ends.start.trim = 15;
    saved = await apply(saved, next); end = saved.authoringView.ends.start;
    assert.equal(end.cadNodeId ?? end.id, firstId); assert.ok(Math.abs(end.trim - 15) < 1e-6);
    next = stateOf(saved); next.ends.end = { ...clone(next.ends.start), id: undefined, cadNodeId: undefined, trim: 20 };
    saved = await apply(saved, next);
    const lastId = saved.authoringView.ends.end.cadNodeId ?? saved.authoringView.ends.end.id;
    next = stateOf(saved); next.drawing.length += 50;
    saved = await apply(saved, next);
    assert.equal(saved.authoringView.ends.end.cadNodeId ?? saved.authoringView.ends.end.id, lastId);
    assert.ok(Math.abs(saved.authoringView.ends.end.trim - 20) < 1e-6, 'Main length preserves the current end-relative trim');
    next = stateOf(saved); next.ends.start = { type: 'keep' };
    saved = await apply(saved, next); assert.equal(saved.authoringView.ends.start.type, 'keep');
    assert.equal(saved.geometry.SolidNodes.some(n => n.ID === firstId), false);
    return { id: saved.partEntityId, endNodeId: firstId, ...await reopen(saved, 'end-add-update-remove') };
  });
  await check('Current round stock opens the original 2D page without losing its exact base profile', async () => {
    const read = await create([], 'round'), side = await get(read.partEntityId, 'side');
    assert.equal(side.authoringView.mode, 'side');
    assert.ok(side.authoringView.reference.profile.contours.length === 2);
    assert.ok(side.authoringView.reference.unfolding.surfaces.length);
    return { id: read.partEntityId, ...await equivalent(read.partEntityId) };
  });
} finally {
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: results.every(r => r.passed), checkCount: results.length,
    passedCount: results.filter(r => r.passed).length, buildPhase: process.env.ICAX_CAD_BUILD_PHASE || 'current-authoring',
    completeProductAcceptanceClaim: false, dllSha256: bridge.dllHash, cadDllSha256: bridge.importerHash,
    frontendModelSha256: createHash('sha256').update(readFileSync(resolve(repositoryRoot,
      'src/apps/tube-designer/webpage/partDrawingModel.mjs'))).digest('hex'),
    filePickerExercised: false, standaloneDesktopHostExercised: false, results, requests: bridge.requests }, null, 2));
  await bridge.close();
}
for (const result of results) console.log((result.passed ? 'PASS ' : 'FAIL ') + result.case + (result.passed ? '' : '\n' + result.error));
if (results.some(r => !r.passed)) process.exitCode = 1;
