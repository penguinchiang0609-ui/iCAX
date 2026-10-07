// Batch creation uses the unchanged scene SDO once per distinct part. This
// unit contract deliberately includes copied recipes and partial-success retry.
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { handleNestingPunchPartAction } from '../../apps/tube-designer/webpage/nestingPunchPart.mjs';
import { installPunchCatalogue, normalizePunchFeature, selectPunchTool, getPunchWizardPayload } from '../../apps/tube-designer/webpage/punchWizard.mjs';
import { syncPunchBatch, punchBatchPartPayload, pastePunchBatchParts } from '../../apps/tube-designer/webpage/punchBatch.mjs';

const source = fileURLToPath(new URL('../../', import.meta.url));
const definitions = readdirSync(resolve(source, 'apps/tube-designer/templates/mold'), { withFileTypes: true })
  .filter(item => item.isDirectory() && existsSync(resolve(source, 'apps/tube-designer/templates/mold', item.name, 'tool.json')))
  .map(item => JSON.parse(readFileSync(resolve(source, 'apps/tube-designer/templates/mold', item.name, 'tool.json'), 'utf8')))
  .map(item => ({ ...item, digest: 'batch-contract-fixture', defaultParameters: Object.fromEntries(item.parameters.map(p => [p.key, p.defaultValue])) }));
const descriptor = JSON.parse(readFileSync(resolve(source, 'apps/tube-designer/templates/profile/rect/profile.json'), 'utf8'));
const displayPath = resolve(source, 'apps/tube-designer/templates/profile/rect/display.json');
if (existsSync(displayPath)) descriptor.display = JSON.parse(readFileSync(displayPath, 'utf8'));
const profile = { id: 'rect', name: '方管', profileType: 'parametric-package', descriptor,
  defaultParameters: Object.fromEntries(descriptor.parameters.map(p => [p.key, p.defaultValue])),
  previewProfile: { kind: 'rect', width: 60, depth: 40, wallThickness: 2, contours: [{ kind: 'path', closed: true,
    segments: [{ kind: 'line', start: [-30,-20], end: [30,-20] }, { kind: 'line', start: [30,-20], end: [30,20] },
      { kind: 'line', start: [30,20], end: [-30,20] }, { kind: 'line', start: [-30,20], end: [-30,-20] }] }] } };
const prefix = 'tube-designer-nesting-punch-create-';
function fixture() {
  const view = { pending: false, activeAreaId: 'nesting', scene: { tubeDesigner: { nestingGroups: [] } },
    tubeDesignerSystemProfiles: [profile], tubeDesignerSelectedProfileId: 'system:rect',
    tubeDesignerUserData: { profiles: [] }, tubeDesignerTemplateProfiles: [] };
  const calls = [], parts = [], context = { sceneProxy: { async invoke(method, payload) {
    calls.push({ scope: 'scene', method, payload: structuredClone(payload) });
    if (method.endsWith('GetPunchTools')) return { tools: definitions };
    if (method.endsWith('PreviewPunchWizard')) return { toolsOnly: true, length: payload.length,
      baseGeometry: { url: 'base', version: 1 }, baseMaterial: { url: 'material', version: 1 },
      baseBounds: { min: [0,-30,-20], max: [payload.length,30,20] }, toolCount: payload.features.length };
    if (method.endsWith('AddNestingPunchPart')) {
      if (context.rejectName === payload.name) throw new Error('Controlled native failure for ' + payload.name);
      const partEntityId = 'created-' + (parts.length + 1);
      parts.push({ entityId: partEntityId, ...structuredClone(payload) });
      return { partEntityId, tubeDesigner: { nestingGroups: [{ parts: structuredClone(parts) }] } };
    }
    throw new Error('Unexpected scene method ' + method);
  } }, productProxy: { async invoke(method) {
    calls.push({ scope: 'product', method });
    if (method.endsWith('EvaluateProfilePackage')) return { profile: structuredClone(profile.previewProfile) };
    throw new Error('Creation cannot use product scope: ' + method);
  } }, actions: { async refreshActiveSceneState() {} } };
  const ops = { renderProject() {}, showNotice() {}, appendProjectLog() {} };
  const action = (name, target = {}) => handleNestingPunchPartAction(context, view, prefix + name, target, ops);
  return { view, calls, parts, context, action };
}
const f = fixture();
await f.action('open');
assert.equal(f.view.tubeDesignerPunchBatch.parts.length, 1, 'The modal opens with one editable part');
assert.equal(f.parts.length, 0, 'Opening never creates a manufacturing part');
const first = f.view.tubeDesignerPunchBatch.parts[0];
installPunchCatalogue(first.wizard, { tools: definitions });
const circle = normalizePunchFeature({ recordKind: 'tool', station: 100, layoutDatum: 'base' });
selectPunchTool(first.wizard, circle, 'circle'); circle.toolParameters.diameter = 12;
const slot = normalizePunchFeature({ recordKind: 'tool', station: 350, face: 'right', layoutDatum: 'base' });
selectPunchTool(first.wizard, slot, 'slot'); slot.toolParameters.spanAlong = 38; slot.toolParameters.spanAcross = 10;
first.wizard.features = [circle, slot];
first.wizard.ends.start = { type: 'template', toolRef: { id: 'end-miter' }, toolParameters: { angle: 25 }, trim: 0, rotation: 0, datum: 'long' };
Object.assign(first.draft, { name: '立柱', length: '1000', quantity: '12' });
f.view.tubeDesignerPunchBatch.selectedPartIds = [first.id];
await f.action('batch-copy');
const second = f.view.tubeDesignerPunchBatch.parts[1];
assert.equal(second.wizard.features.length, 2, 'A copied part includes both machining records');
assert.deepEqual(second.wizard.ends, first.wizard.ends, 'Both ends copy with the part');
assert.notEqual(second.wizard.features[0], first.wizard.features[0]);
second.wizard.features[0].station = 180;
assert.equal(first.wizard.features[0].station, 100, 'Copied placements remain independent');
assert.equal(second.wizard.features[0].batchDefinitionId, first.wizard.features[0].batchDefinitionId,
  'Copies refer to the same batch-owned hole definition');
second.wizard.features[0].toolParameters.diameter = 14;
syncPunchBatch(f.view);
assert.equal(first.wizard.features[0].toolParameters.diameter, 14, 'Editing a shared shape updates every referencing part');
assert.equal(first.wizard.features[0].station, 100, 'Shape propagation leaves the original placement unchanged');
assert.equal(second.wizard.features[0].station, 180, 'Shape propagation leaves the copied placement unchanged');
assert.equal(punchBatchPartPayload(second).features[0].batchDefinitionId, undefined, 'Batch-only identities stay out of native recipes');
Object.assign(second.draft, { name: '横梁', length: '1400', quantity: '8' });
second.wizard.baseLength = 1400;
await f.action('batch-add');
const third = f.view.tubeDesignerPunchBatch.parts[2];
installPunchCatalogue(third.wizard, { tools: definitions });
Object.assign(third.draft, { name: '密集孔件', length: '600', quantity: '4' });
third.wizard.baseLength = 600;
const dense = normalizePunchFeature({ recordKind: 'tool', station: 60, layoutDatum: 'base',
  arrayGroups: [{ id: 'array-group-1', type: 'linear', axis: 'X', count: 5, spacing: 100, distributionMode: 'pitch' },
    { id: 'array-group-2', type: 'polar', axis: 'X', count: 12, angleMode: 'full-circle' }], arraySkips: [] });
selectPunchTool(third.wizard, dense, 'circle'); dense.toolParameters.diameter = 6;
third.wizard.features = [dense];
await f.action('batch-select', { dataset: { punchBatchPartId: third.id } });
const transported = getPunchWizardPayload(f.view).features[0];
assert.equal(transported.arrayTransforms.length, 60, 'Two dimensions preserve real array transport');
await f.action('apply');
const created = f.calls.filter(call => call.method.endsWith('AddNestingPunchPart'));
assert.equal(created.length, 3, 'Confirm creates every distinct part row exactly once');
assert.deepEqual(created.map(call => [call.payload.name, call.payload.length, call.payload.quantity]),
  [['立柱',1000,12],['横梁',1400,8],['密集孔件',600,4]]);
assert(created.every(call => call.scope === 'scene'));
assert.equal(created[0].payload.features[0].station, 100);
assert.equal(created[1].payload.features[0].station, 180);
assert.equal(created[0].payload.features[1].toolRef.id, 'slot');
assert.equal(created[2].payload.features[0].arrayTransforms.length, 60);
assert.equal(f.view.tubeDesignerPunchBatch, null);
assert.equal(f.view.tubeDesignerPunchWizard, null);
const pasted = fixture(); await pasted.action('open');
const pastedId = pasted.view.tubeDesignerPunchBatch.parts[0].id;
assert(pastePunchBatchParts(pasted.view, pastedId, 'name', '立柱\t1000\t12\tlong\n横梁\t1400\t8\tcenter\n连接件\t450\t4\tshort\n'));
assert.deepEqual(pasted.view.tubeDesignerPunchBatch.parts.map(part => [part.draft.name,part.draft.length,part.draft.quantity,part.draft.lengthDatum]),
  [['立柱','1000','12','long'],['横梁','1400','8','center'],['连接件','450','4','short']], 'Excel rows preserve their column meanings');
const cancel = fixture(); await cancel.action('open'); await cancel.action('batch-add'); await cancel.action('cancel');
assert.equal(cancel.parts.length, 0, 'Cancel discards the batch without native creation');
assert.equal(cancel.view.tubeDesignerPunchBatch, null);
const partial = fixture(); await partial.action('open');
partial.view.tubeDesignerPunchBatch.parts[0].draft.name = 'first';
await partial.action('batch-add'); partial.view.tubeDesignerPunchBatch.parts[1].draft.name = 'second';
partial.context.rejectName = 'second';
await assert.rejects(partial.action('apply'), /Controlled native failure/);
assert.equal(partial.parts.length, 1);
assert(partial.view.tubeDesignerPunchBatch.parts[0].completedPartId, 'Successful rows retain their receipt');
partial.context.rejectName = ''; await partial.action('apply');
assert.equal(partial.parts.length, 2, 'Retry never generates a duplicate of a successful row');

// Confirm acquires its lock synchronously, waits for the matching background
// section, then builds native requests. A second click cannot enter the same
// save or release its lock, and the source callback cannot deadlock by queuing
// a preview behind the operation that is waiting for that callback.
const contourSave=fixture();await contourSave.action('open');
const contourPart=contourSave.view.tubeDesignerPunchBatch.parts[0];
installPunchCatalogue(contourPart.wizard,{tools:definitions});
const branch=normalizePunchFeature({recordKind:'branch',station:300,layoutDatum:'base'});
selectPunchTool(contourPart.wizard,branch,'branch-profile');
branch.section={source:'library',key:'system:rect',ref:{kind:'system',id:'rect'},name:'方管',
  parameters:structuredClone(profile.defaultParameters),profile:structuredClone(profile.previewProfile)};
contourPart.wizard.features=[branch];syncPunchBatch(contourSave.view);
const invoke=contourSave.context.sceneProxy.invoke;
let releaseContour,sourceParameters;
contourSave.context.sceneProxy.invoke=(method,payload,options)=>{
  if(method==='TubeDesigner.GenerateProfilePreview') {
    sourceParameters=structuredClone(payload.parameters);
    return new Promise(resolve=>{releaseContour=resolve;});
  }
  return invoke(method,payload,options);
};
const changing=contourSave.action('profile-parameter',{value:'68',dataset:{tubeDesignerPunchIndex:'0',tubeDesignerPunchProfileParameter:'width'}});
await Promise.resolve();
assert.equal(contourSave.view.pending,false);
assert.equal(contourPart.wizard.features[0].section.profile,null);
const saving=contourSave.action('apply');
const saveOperation=contourSave.view.tubeDesignerOperation;
assert.equal(contourSave.view.pending,true,'Confirm immediately reserves its foreground operation');
assert(saveOperation);
await contourSave.action('apply');
assert.equal(contourSave.view.tubeDesignerOperation,saveOperation,'a repeated Confirm cannot unlock the active save');
assert.equal(contourSave.parts.length,0,'new parameters with old contours cannot be submitted');
releaseContour({profile:{...structuredClone(profile.previewProfile),width:68,parameters:sourceParameters}});
let deadline;
try {
  await Promise.race([Promise.all([changing,saving]),new Promise((_,reject)=>{deadline=setTimeout(()=>reject(new Error('Section/save queue deadlock')),1500);})]);
} finally {clearTimeout(deadline);}
assert.equal(contourSave.parts.length,1);
assert.equal(contourSave.parts[0].features[0].section.parameters.width,68);
assert.equal(contourSave.parts[0].features[0].section.profile.width,68,'Confirm transports the newly generated contour');
assert.equal(contourSave.view.pending,false);
assert.equal(contourSave.view.tubeDesignerOperation,null);
console.log('PASS batch contract: three parts, independent copies, slot and dense 2D recipes, scene-only creation, cancel, partial retry and background contour save gate.');
