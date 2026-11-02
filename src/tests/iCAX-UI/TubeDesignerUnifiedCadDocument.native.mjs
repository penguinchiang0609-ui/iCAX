// Production scene SDOs and registered CAD services. The stdio bridge only
// replaces host transport; every document, BRep and project file is native.
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import {
  startSideSketchNativeBridge, createPerforatedRectangularTube, repositoryRoot,
} from './fixtures/sideSketchNativeBridge.mjs';

const output = resolve(repositoryRoot, 'output/tests/unified-cad-document');
mkdirSync(output, { recursive: true });
const bridge = await startSideSketchNativeBridge(output);
const results = [];
const clone = value => structuredClone(value);
const get = (id, extra = {}, scope = 'scene') => bridge.invoke('GetTubeCadDocument', { partEntityId: id, ...extra }, scope);
const edit = (read, extra) => bridge.invoke('ApplyTubeCadDocument', {
  partEntityId: read.partEntityId, resourceVersion: read.resourceVersion, revision: read.revision, ...extra,
});
const node = (read, id) => read.geometry.SolidNodes.find(item => item.ID === id);
const circleOf = geometry => geometry.value.Boundaries[0].Curves[0].Segment.Curve.value;
const findCircularCut = read => read.geometry.SolidNodes.find(item => item.ID !== read.geometry.BaseNodeID
  && item.Data.type === 'SExtrudedRegionNode' && item.Data.value.Section.Boundaries.length === 1
  && item.Data.value.Section.Boundaries[0].Curves.length > 0
  && item.Data.value.Section.Boundaries[0].Curves.every(curve => curve.Segment.Curve.type === 'Circle2'));

async function verifyHostResize(read, label, expectedDepth) {
  const cut = findCircularCut(read);
  assert.ok(cut, label + ': authored circle is editable geometry');
  assert.equal(cut.Relations.Extent.HostNodeID, read.geometry.BaseNodeID);
  assert.equal(cut.Relations.Extent.RecomputeAgainstHost, true);
  assert.equal(cut.Relations.Placement.Rule, 1, label + ': placement follows the host frame');
  assert.equal(cut.Relations.Extent.Rule, expectedDepth === undefined ? 2 : 3);
  const geometry = clone(read.geometry);
  const stock = geometry.SolidNodes.find(item => item.ID === geometry.BaseNodeID).Data.value;
  // The outer rectangular section grows from 60 to 80 mm in height while
  // the 54 mm inner boundary stays fixed, making this wall 13 mm thick.
  const outer = stock.Section.Boundaries[0];
  for (const curve of outer.Curves) {
    assert.equal(curve.Segment.Curve.type, 'Segment2');
    curve.Segment.Curve.value.Start.Y *= 4 / 3;
    curve.Segment.Curve.value.End.Y *= 4 / 3;
  }
  await edit(read, { geometry });
  const resized = await get(read.partEntityId);
  const resizedCut = node(resized, cut.ID);
  assert.ok(resizedCut, label + ': host edit preserves the feature identity');
  assert.deepEqual(resizedCut.Data.value.Section, cut.Data.value.Section, label + ': hole contour does not change');
  const rebuilt = await verifyGeometry(read.partEntityId, label);
  const removalDepth = expectedDepth ?? 13;
  const radius = cut.Data.value.Section.Boundaries[0].Curves[0].Segment.Curve.value.Radius;
  const expectedVolume = (80 * 80 - 74 * 54) * 400 - Math.PI * radius ** 2 * removalDepth;
  assert.ok(Math.abs(rebuilt.published.volume - expectedVolume) < 1e-3,
    label + ': actual cut follows the changed wall span/depth');
  if (expectedDepth === undefined) {
    assert.ok(resizedCut.Data.value.Last - resizedCut.Data.value.First > cut.Data.value.Last - cut.Data.value.First + 9,
      label + ': explicit extrusion interval expands with the wall');
  } else assert.equal(resizedCut.Relations.Extent.Depth.Value, expectedDepth);
  results.push({ case: label, id: read.partEntityId, cutId: cut.ID, expectedVolume, removalDepth, ...rebuilt });
}

async function verifyRemovedInterfaces(read, methods, expectedGeometry, label) {
  const removedInterfaces = [];
  for (const method of methods) {
    let rejection;
    await assert.rejects(bridge.invoke(method, { partEntityId: read.partEntityId,
      resourceId: read.resourceId, resourceVersion: read.resourceVersion }), error => {
      rejection = error.message;
      return error.message === 'Missing real SDO: ' + method;
    }, label + ': ' + method + ' is absent from the production method registry');
    assert.deepEqual(await get(read.partEntityId), read,
      label + ': absent ' + method + ' preserves the complete CAD document and resource versions');
    assert.deepEqual(await verifyGeometry(read.partEntityId, label + ': absent ' + method), expectedGeometry,
      label + ': absent ' + method + ' preserves published and independently evaluated geometry');
    removedInterfaces.push({ method, registered: false, rejection });
  }
  return removedInterfaces;
}

async function verifyAuthoredDiameter(read, label) {
  const cut = findCircularCut(read), loop = cut?.Data.value.Section.Boundaries[0];
  assert.ok(loop, label + ': exact authored circular loop');
  assert.equal(loop.Curves.length, 4, label + ': use the real four-arc generated circle');
  const ranges = loop.Curves.map(item => clone(item.Segment.Range));
  const placements = loop.Curves.map(item => clone(item.Segment.Curve.value.Placement));
  const newRadius = loop.Curves[0].Segment.Curve.value.Radius + 1;
  const geometry = clone(read.geometry);
  const { setTubeCadSectionDimension } = await import('../../apps/tube-designer/webpage/tubeCadModel.mjs');
  setTubeCadSectionDimension(geometry, cut.ID, loop.ID, 'radius', newRadius);
  await edit(read, { geometry });
  const edited = await get(read.partEntityId);
  const editedLoop = node(edited, cut.ID).Data.value.Section.Boundaries[0];
  assert.ok(editedLoop.Curves.every(item => Math.abs(item.Segment.Curve.value.Radius - newRadius) < 1e-9));
  assert.deepEqual(editedLoop.Curves.map(item => item.Segment.Range), ranges, label + ': all exact arc ranges remain unchanged');
  assert.deepEqual(editedLoop.Curves.map(item => item.Segment.Curve.value.Placement), placements,
    label + ': existing circle centers/frames are unchanged');
  assert.equal(edited.documentResourceId, read.documentResourceId);
  const rebuilt = await verifyGeometry(read.partEntityId, label);
  const expectedVolume = (80 * 60 - 74 * 54) * 400 - Math.PI * newRadius ** 2 * 3;
  assert.ok(Math.abs(rebuilt.published.volume - expectedVolume) < 1e-3,
    label + ': new diameter has the analytical removed material volume');
  assert.ok(edited.resourceVersion > read.resourceVersion, label + ': neutral save publishes a new current version');
  const removedInterfaces = await verifyRemovedInterfaces(edited,
    ['ApplyPartDrawing', 'ApplyPunchWizard'], rebuilt, label);
  results.push({ case: label, id: read.partEntityId, cutId: cut.ID, diameter: newRadius * 2, exactRanges: ranges,
    expectedVolume, removedInterfaces, ...rebuilt });
  return edited;
}

async function verifyGeometry(id, label) {
  const published = await bridge.rpc({ action: 'geometry', payload: { partEntityId: id } });
  const document = await bridge.rpc({ action: 'cad-document-geometry', payload: { partEntityId: id } });
  assert.equal(published.valid, true, label + ': published BRep is valid');
  assert.equal(document.valid, true, label + ': document independently evaluates a valid BRep');
  const tolerance = Math.max(1e-3, published.volume * 1e-8);
  assert.ok(Math.abs(published.volume - document.volume) <= tolerance, label + ': evaluated/published volume');
  assert.ok(document.directedDifferenceVolume <= tolerance,
    label + ': independently evaluated document and published geometry have no material difference');
  return { published, document };
}

async function verifyCreation(method, payload, label) {
  const created = await bridge.invoke(method, payload);
  const id = created.partEntityId;
  assert.ok(id, label + ': persisted part identity');
  const read = await get(id);
  writeFileSync(resolve(output, method + '-document.json'), JSON.stringify(read, null, 2));
  assert.equal(read.partEntityId, id);
  assert.equal(read.revision, '1');
  assert.ok(read.documentResourceId && Number(read.documentResourceVersion) > 0);
  assert.ok(read.geometry.SolidNodes.length > 0);
  assert.ok(node(read, read.geometry.BaseNodeID));
  const again = await get(id);
  assert.deepEqual(again, read, label + ': document opening does not mutate resources or graph');
  const geometry = await verifyGeometry(id, label);
  results.push({ case: label, id, method, revision: read.revision, nodes: read.geometry.SolidNodes.length, ...geometry });
  return read;
}

async function verifyAssemblyPlanDocument(profileRef, parameters) {
  const stock = { id: 'neutral-plan-stock', profileRef, parameters: clone(parameters), length: 400,
    matrix: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1] };
  const source = { stocks: [stock], instances: [] };
  const plan = await bridge.invoke('ResolveAssemblyProcessPlan', source, 'product');
  const request = { ...source, planId: 'neutral-current-plan', requestId: 'create-neutral-current-plan',
    expectedPlanDigest: plan.planDigest };
  const created = await bridge.invoke('CommitAssemblyProcessPlan', request);
  const id = created.entityIds[stock.id];
  assert.ok(id, 'Current assembly plan creates a persisted stock');
  const createdDocument = await get(id);
  assert.ok(createdDocument.documentResourceId, 'Current assembly plan creation directly owns a CAD document');
  const initialGeometry = await verifyGeometry(id, 'assembly plan creation');

  const updatedSource = { stocks: [{ ...stock, length: 420 }], instances: [] };
  const updatedPlan = await bridge.invoke('ResolveAssemblyProcessPlan', updatedSource, 'product');
  const updatedRequest = { ...updatedSource, planId: request.planId, requestId: 'update-neutral-current-plan',
    expectedPlanDigest: updatedPlan.planDigest, expectedStoredPlanDigest: plan.planDigest };
  const updated = await bridge.invoke('CommitAssemblyProcessPlan', updatedRequest);
  assert.equal(updated.entityIds[stock.id], id, 'Current assembly plan update preserves the stock entity');
  const updatedDocument = await get(id);
  const updatedGeometry = await verifyGeometry(id, 'assembly plan update');
  assert.ok(Math.abs(updatedGeometry.published.volume - (80 * 60 - 74 * 54) * 420) < 1e-3,
    'Updated plan publishes the new raw stock length from its authoritative CAD graph');
  const plans = await bridge.invoke('GetAssemblyProcessPlans');
  const file = 'assembly-plan-current-cad.ictd';
  await bridge.rpc({ action: 'save', payload: { file } });
  await bridge.rpc({ action: 'open', payload: { file } });
  assert.deepEqual(await get(id), updatedDocument, 'Plan CAD document survives a fresh project reopen');
  assert.deepEqual(await bridge.invoke('GetAssemblyProcessPlans'), plans,
    'Current plan metadata survives without a legacy punch component');

  const { addTubeCadHole } = await import('../../apps/tube-designer/webpage/tubeCadModel.mjs');
  const geometry = clone(updatedDocument.geometry);
  const holeId = addTubeCadHole(geometry, { diameter: 8, station: 140, offset: 15, face: 'top' });
  await edit(updatedDocument, { geometry });
  const edited = await get(id);
  assert.ok(node(edited, holeId), 'Current CAD editing continues directly on the plan stock');
  const editedGeometry = await verifyGeometry(id, 'assembly stock independently edited through CAD');
  assert.ok(Math.abs(editedGeometry.published.volume
    - ((80 * 60 - 74 * 54) * 420 - Math.PI * 4 ** 2 * 6)) < 1e-3);
  const editedPlans = await bridge.invoke('GetAssemblyProcessPlans');
  let rejection;
  await assert.rejects(bridge.invoke('CommitAssemblyProcessPlan', { ...updatedRequest,
    requestId: 'do-not-overwrite-neutral-cad', expectedStoredPlanDigest: updatedPlan.planDigest }), error => {
    rejection = error.message;
    return /单独修改.*不能直接覆盖/.test(error.message);
  }, 'Current plan compare-and-swap refuses to overwrite an independently edited CAD stock');
  assert.deepEqual(await get(id), edited, 'Rejected plan commit preserves the complete CAD document');
  assert.deepEqual(await verifyGeometry(id, 'rejected current plan commit'), editedGeometry);
  assert.deepEqual(await bridge.invoke('GetAssemblyProcessPlans'), editedPlans);
  return { id, holeId, initialGeometry, updatedGeometry, editedGeometry,
    projectFile: file, updatedEntityPreserved: true, reopenedDocumentPreserved: true, rejection };
}

async function verifyProductStagingDocument() {
  const generated = await bridge.invoke('GeneratePreview', {
    templateId: 'single-face-security-window', faceType: 'single', receiptOnly: true,
  });
  const productId = generated.tubeDesigner.product.entityId;
  assert.ok(productId);
  const disassembled = await bridge.invoke('DisassembleSelected', { productEntityIds: [productId] });
  const source = disassembled.tubeDesigner.parts.find(part => part.properties?.['manufacturing.partKind'] === 'tube');
  assert.ok(source, 'Real product disassembly exposes a manufactured tube source');
  const staged = await bridge.invoke('StageNestingParts', { partEntityIds: [source.entityId] });
  assert.equal(staged.partEntityIds.length, 1);
  const id = staged.partEntityIds[0];
  assert.notEqual(id, source.entityId, 'Staging creates an independent nesting stock');
  const document = await get(id);
  assert.equal(document.revision, '1');
  assert.ok(document.documentResourceId && document.geometry.SolidNodes.length);
  const geometry = await verifyGeometry(id, 'real product disassembly -> staged neutral CAD stock');
  const file = 'product-staging-current-cad.ictd';
  await bridge.rpc({ action: 'save', payload: { file } });
  await bridge.rpc({ action: 'open', payload: { file } });
  assert.deepEqual(await get(id), document, 'Staged product stock retains its CAD graph after native save/open');
  assert.deepEqual(await verifyGeometry(id, 'reopened staged product stock'), geometry);
  return { templateId: 'single-face-security-window', productId, sourcePartEntityId: source.entityId,
    id, documentResourceId: document.documentResourceId, projectFile: file, reopenedDocumentPreserved: true, ...geometry };
}

let failure;
try {
  const descriptor = JSON.parse(readFileSync(resolve(repositoryRoot,
    'src/apps/tube-designer/templates/profile/rect/profile.json'), 'utf8'));
  const parameters = { ...Object.fromEntries(descriptor.parameters.map(item => [item.key, item.defaultValue])),
    width: 80, depth: 60, wallThickness: 3, cornerRadius: 0, innerRadius: 0 };
  const profileRef = { scope: 'system', id: 'rect' };
  const evaluated = await bridge.invoke('EvaluateProfilePackage', { profileRef, parameters }, 'product');
  const drawing = { schemaVersion: 1, length: 400, section: { source: 'library', profile: evaluated.profile } };
  const tools = await bridge.invoke('GetPartDrawingTools');
  const circleTool = tools.tools.find(item => item.id === 'circle');
  assert.ok(circleTool, 'Native circle tool is available');
  const feature = { id: 'initial-hole', type: 'circle', toolTarget: circleTool.target,
    toolRef: { scope: 'system', id: 'circle' },
    toolParameters: { ...circleTool.defaultParameters, diameter: 10 }, station: 140, face: 'top', offset: 0,
    layoutDatum: 'base' };

  await verifyCreation('AddNestingStandardPart', { profileRef, parameters, length: 400, name: '标准件共享 CAD', quantity: 2 },
    'standard creation owns an evaluable neutral document');
  const side = await createPerforatedRectangularTube(bridge);
  const sideRead = await get(side.part.entityId);
  assert.equal(sideRead.revision, '1');
  assert.ok(sideRead.geometry.SolidNodes.some(item => item.ID !== sideRead.geometry.BaseNodeID));
  const sideGeometry = await verifyGeometry(side.part.entityId, '2D creation');
  const removedInterfaces = await verifyRemovedInterfaces(sideRead, ['SavePartSketch'], sideGeometry, '2D creation');
  results.push({ case: '2D side-sketch creation owns the same neutral document', id: side.part.entityId,
    nodes: sideRead.geometry.SolidNodes.length, removedInterfaces, ...sideGeometry });
  const creationInput = { drawing, features: [feature], ends: {} };
  let drawingRead = await verifyCreation('AddPartDrawing', { ...creationInput, name: '三维绘制共享 CAD', quantity: 1 },
    '3D drawing creation owns an evaluable neutral document');
  let punchRead = await verifyCreation('AddNestingPunchPart', { ...creationInput, name: '冲孔共享 CAD', quantity: 1 },
    'punch creation owns an evaluable neutral document');
  drawingRead = await verifyAuthoredDiameter(drawingRead, 'authored 3D four-arc circle diameter edit preserves exact ranges');
  punchRead = await verifyAuthoredDiameter(punchRead, 'authored punch four-arc circle diameter edit preserves exact ranges');
  await verifyHostResize(drawingRead, 'authored 3D hole follows the resized host wall');
  await verifyHostResize(punchRead, 'authored punch hole follows the resized host wall');
  const blindRead = await verifyCreation('AddNestingPunchPart', { drawing,
    features: [{ ...feature, blindHole: true, cutDepth: 1.5 }], ends: {}, name: '盲孔深度保持', quantity: 1 },
  'authored blind punch owns host/depth relationships');
  await verifyHostResize(blindRead, 'authored blind punch keeps its 1.5 mm depth after host resize', 1.5);
  results.find(item => item.case === 'authored punch four-arc circle diameter edit preserves exact ranges')
    .assemblyPlan = await verifyAssemblyPlanDocument(profileRef, parameters);
  results[0].productStaging = await verifyProductStagingDocument();

  const imported = await bridge.invoke('ImportNestingPart', { sourcePath: resolve(repositoryRoot,
    'samples/tube-geometry/05_double_cavity_blind_hole.step') });
  const id = imported.partEntityId;
  let read = await get(id);
  const originalDocumentResourceId = read.documentResourceId;
  const originalHole = findCircularCut(read);
  assert.ok(originalHole, 'Imported blind circular feature is individually editable');
  const holeId = originalHole.ID;
  const initial = await verifyGeometry(id, 'imported STEP');
  const removedRecoveryInterfaces = await verifyRemovedInterfaces(read,
    ['GetPartDrawing', 'RecoverImportedPartDrawing'], initial, 'imported STEP');
  await assert.rejects(get(id, {}, 'product'), /场景|context/i,
    'A product channel cannot impersonate native scene editing');

  // Draft evaluation owns runtime resources only. It must not mutate the
  // document, manufacturing BRep or saved project.
  const previewBefore = await get(id);
  const previewGeometry = clone(previewBefore.geometry);
  previewGeometry.SolidNodes.find(item => item.ID === holeId).Data.value.Section
    .Boundaries[0].Curves[0].Segment.Curve.value.Radius += 0.5;
  const previewPayload = { partEntityId: id, resourceVersion: previewBefore.resourceVersion,
    revision: previewBefore.revision, geometry: previewGeometry, previewResourceKey: 'unified-cad-preview' };
  const preview = await bridge.invoke('PreviewTubeCadDocument', previewPayload);
  assert.equal(preview.revision, previewBefore.revision);
  assert.deepEqual(await get(id), previewBefore, 'Native preview does not change the authoritative document');
  assert.deepEqual(await bridge.rpc({ action: 'geometry', payload: { partEntityId: id } }), initial.published,
    'Native preview does not publish manufacturing geometry');
  const previewMesh = await bridge.rpc({ action: 'resource', payload: preview.geometry });
  assert.ok(previewMesh.bytes > 0);
  assert.equal(previewMesh.runtimeOnly, true);
  assert.equal(previewMesh.previewOwner, previewPayload.previewResourceKey);
  const previewReferences = [preview.geometry, preview.baseGeometry, ...(preview.toolPreviews ?? []).map(tool => tool.geometry)]
    .filter((reference, index, references) => reference?.url && references.findIndex(item => item?.url === reference.url) === index);
  for (const reference of previewReferences) {
    const resource = await bridge.rpc({ action: 'resource', payload: reference });
    assert.equal(resource.runtimeOnly, true); assert.equal(resource.previewOwner, previewPayload.previewResourceKey);
    assert.ok(resource.bytes > 0);
  }
  await assert.rejects(bridge.invoke('PreviewTubeCadDocument', { ...previewPayload, revision: '0' }));
  await assert.rejects(bridge.invoke('PreviewTubeCadDocument', previewPayload, 'product'));
  await assert.rejects(bridge.invoke('ReleaseTubeCadDocumentPreview', {
    previewResourceKey: previewPayload.previewResourceKey }, 'product'));
  const released = await bridge.invoke('ReleaseTubeCadDocumentPreview', { previewResourceKey: previewPayload.previewResourceKey });
  assert.equal(released.released, true); assert.equal(Number(released.resourceCount), previewReferences.length * 2,
    'Release cleans every published preview mesh and its owned BRep source');
  for (const reference of previewReferences) await assert.rejects(bridge.rpc({ action: 'resource', payload: reference }));
  const transient = await bridge.invoke('PreviewTubeCadDocument', { ...previewPayload, previewResourceKey: 'unified-cad-preview-unsaved' });
  const transientFile = 'unified-cad-preview-runtime-only.ictd';
  await bridge.rpc({ action: 'save', payload: { file: transientFile } });
  await bridge.rpc({ action: 'open', payload: { file: transientFile } });
  assert.deepEqual(await get(id), previewBefore);
  await assert.rejects(bridge.rpc({ action: 'resource', payload: transient.geometry }),
    'Unsaved preview mesh is absent in a fresh repository/resource library');
  assert.equal(Number((await bridge.invoke('ReleaseTubeCadDocumentPreview', {
    previewResourceKey: 'unified-cad-preview-unsaved' })).resourceCount), 0);
  results.push({ case: 'real draft preview keeps document/part unchanged, validates scope/revision, releases ownership and is excluded from disk persistence',
    geometry: preview.geometry, releasedResources: Number(released.resourceCount), previewMeshBytes: previewMesh.bytes });

  // The exact 2D feature view edits the same 3D circular extrusion.
  const projected = await get(id, { nodeId: holeId });
  const sketch = clone(projected.sketch);
  const originalRadius = circleOf(sketch.geometry).Radius;
  circleOf(sketch.geometry).Radius = originalRadius + 1;
  let saved = await edit(read, { sketch });
  assert.equal(saved.revision, String(Number(read.revision) + 1));
  await assert.rejects(edit(read, { sketch }), /改变|重新|版本|revision/i, 'Stale edits are rejected');
  read = await get(id);
  assert.equal(read.documentResourceId, originalDocumentResourceId, '2D save preserves authoritative resource identity');
  assert.equal(node(read, holeId).Data.value.Section.Boundaries[0].Curves[0].Segment.Curve.value.Radius,
    originalRadius + 1, '2D save updates the same spatial feature');
  const after2D = await verifyGeometry(id, '2D edit');
  assert.ok(after2D.published.volume < initial.published.volume - 1, 'Larger imported blind hole removes more material');

  // The 3D frame edit must reappear in the next 2D construction frame.
  const geometry = clone(read.geometry);
  const moved = geometry.SolidNodes.find(item => item.ID === holeId);
  const movedX = moved.Data.value.Frame.Location.X + 12;
  moved.Data.value.Frame.Location.X = movedX;
  saved = await edit(read, { geometry });
  const after3D = await get(id, { nodeId: holeId });
  assert.equal(after3D.sketch.constructionFrame.Location.X, movedX);
  assert.equal(circleOf(after3D.sketch.geometry).Radius, originalRadius + 1);
  const after3DGeometry = await verifyGeometry(id, '3D edit');
  assert.ok(Math.abs(after3DGeometry.published.volume - after2D.published.volume) < 1e-3,
    'Moving an interior blind hole preserves the independently measured removal volume');

  // Exercise the production UI model adapter for adding a host-constrained
  // punch cut, then commit through the same authoritative native API.
  const { addTubeCadHole } = await import('../../apps/tube-designer/webpage/tubeCadModel.mjs');
  read = await get(id);
  const punched = clone(read.geometry);
  const newHoleId = addTubeCadHole(punched, { diameter: 8, station: 60, offset: 15, face: 'top' });
  assert.ok(newHoleId && newHoleId !== holeId);
  saved = await edit(read, { geometry: punched });
  read = await get(id, { nodeId: holeId });
  assert.equal(read.documentResourceId, originalDocumentResourceId);
  assert.equal(read.sketch.constructionFrame.Location.X, movedX, 'Punch edits retain prior 3D placement');
  assert.equal(circleOf(read.sketch.geometry).Radius, originalRadius + 1, 'Punch edits retain the prior 2D diameter');
  const newHole = node(read, newHoleId);
  assert.ok(newHole, 'Punch commits retain the new feature identity');
  assert.equal(newHole.Relations.Extent.HostNodeID, read.geometry.BaseNodeID);
  assert.equal(newHole.Relations.Extent.Rule, 1, 'ThroughAllHostMaterial is saved as geometry semantics');
  const afterPunch = await verifyGeometry(id, 'punch after 2D/3D');
  assert.ok(afterPunch.published.volume < after3DGeometry.published.volume - 1,
    'New neutral punch feature removes actual material');

  // Failed native edits leave document bytes, revision, published BRep and
  // resource versions unchanged.
  const beforeRejected = await get(id);
  const invalid = clone(beforeRejected.geometry);
  const root = invalid.SolidNodes.find(item => item.ID === invalid.RootNodeID);
  assert.equal(root.Data.type, 'SBooleanNode');
  root.Data.value.Children.push(root.ID);
  await assert.rejects(edit(beforeRejected, { geometry: invalid }));
  assert.deepEqual(await get(id), beforeRejected, 'Rejected cyclic graph leaves the scene unchanged');

  // Open creates a fresh repository and resource library in the bridge.
  const file = 'unified-cad-import-2d-3d-punch.ictd';
  const savedFile = await bridge.rpc({ action: 'save', payload: { file, name: '同一 CAD 文档跨编辑器验收' } });
  const beforeOpen = await get(id);
  const opened = await bridge.rpc({ action: 'open', payload: { file } });
  assert.equal(opened.opened, true);
  const reopened = await get(id);
  assert.deepEqual(reopened, beforeOpen, 'Disk roundtrip preserves complete graph, feature identities and versions');
  const persistedPart = opened.snapshot.tubeDesigner.nestingGroups.flatMap(group => group.parts).find(item => item.entityId === id);
  assert.ok(persistedPart, 'Actual persisted imported part is in the reopened scene');
  const reopenedGeometry = await verifyGeometry(id, 'disk reopen');
  assert.ok(Math.abs(reopenedGeometry.published.volume - afterPunch.published.volume) < 1e-3);

  const continued = clone(reopened.geometry);
  continued.SolidNodes.find(item => item.ID === holeId).Data.value.Frame.Location.X += 3;
  saved = await edit(reopened, { geometry: continued });
  assert.equal(saved.revision, String(Number(reopened.revision) + 1));
  const continuedView = await get(id, { nodeId: holeId });
  assert.equal(continuedView.sketch.constructionFrame.Location.X, movedX + 3);
  await verifyGeometry(id, 'edit after reopen');
  await bridge.rpc({ action: 'save', payload: { file } });
  results.push({ case: 'STEP -> 2D -> 3D -> punch -> reject invalid/stale -> disk reopen -> continue', id,
    holeId, newHoleId, projectFile: savedFile.file, initialRadius: originalRadius,
    editedRadius: originalRadius + 1, finalRevision: saved.revision, removedRecoveryInterfaces,
    volumes: [initial, after2D, after3DGeometry, afterPunch, reopenedGeometry].map(item => item.published.volume) });
  console.log('Unified CAD native acceptance passed: four creation entries, shared 2D/3D/punch geometry, scene scoping, atomic rejects, disk save/reopen/continue.');
} catch (error) {
  failure = error;
  throw error;
} finally {
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: !failure, error: failure?.stack,
    tubeDesignerDllSha256: bridge.dllHash, importerDllSha256: bridge.importerHash,
    nativeTransport: 'production scene SDO + registered neutral CAD service',
    hostFilePickerExercised: false, standaloneDesktopExercised: false, browserUIExercised: false,
    results, requests: bridge.requests }, null, 2));
  await bridge.close();
}
