import assert from "node:assert/strict";
import { createPunchWizardState, normalizePunchFeature, getPunchWizardPayload, resolvePunchDistribution } from "../../apps/tube-designer/webpage/punchWizard.mjs";
import { handlePartsAreaAction } from "../../apps/tube-designer/webpage/partsArea.mjs";
import { handleNestingPunchPartAction } from "../../apps/tube-designer/webpage/nestingPunchPart.mjs";

const part={entityId:"array-transport",length:1000,independentNesting:true,profile:{kind:"rect",width:40,depth:20},properties:{"manufacturing.partKind":"tube"}};
const feature=()=>normalizePunchFeature({id:"grouped",recordKind:"tool",type:"circle",diameter:10,station:100,reference:"center",layoutDatum:"base",
  arrayGroups:[{id:"x",type:"linear",axis:"X",count:2,spacing:50},{id:"y",type:"linear",axis:"Y",count:2,spacing:20}],arraySkips:[{x:1,y:1}]});
function cleanTransport(row) {
  for(const key of ["layoutSummary","layoutError","arrayGroupsSummary","arrayGroupsError","arrayInstances","arraySkipText"])
    assert.equal(Object.hasOwn(row,key),false,`${key} is a UI derived field, not native recipe data.`);
  assert.equal(row.arrayCandidateCount,4);assert.equal(row.arrayTransforms.length,3);
  assert.deepEqual(row.arraySkips,[{x:1,y:1}]);assert.equal(row.arrayGroups.length,2);
  assert.equal(row.arrayCount,1);assert.equal(row.rowCount,1);
  assert.deepEqual(row.arrayTransforms.map(m=>[m[3],m[7]]),[[0,0],[50,0],[0,20]]);
  assert.equal(row.station,100);assert.equal(row.reference,"center");
}
const s=createPunchWizardState(part),view={pending:false,tubeDesignerPunchWizard:s,scene:{tubeDesigner:{nestingGroups:[{parts:[part]}]}}};
s.catalogueStatus="ready";s.features=[feature()];s.draft={...feature(),id:"draft-grouped"};
const original=structuredClone(s.features[0]);
cleanTransport(getPunchWizardPayload(view).features[0]);
assert.deepEqual(s.features[0],original,"Serialization must not mutate the editable recipe.");
const saved={...part,properties:{...part.properties,"tubeDesigner.punchWizard":{baseLength:1000,features:getPunchWizardPayload(view).features}}};
const reloaded=createPunchWizardState(saved);reloaded.baseLength=1200;
cleanTransport(getPunchWizardPayload({tubeDesignerPunchWizard:reloaded}).features[0]);
const invalidDisabled={...feature(),enabled:false,arrayGroups:[{id:"unfinished",type:"linear",axis:"X",count:0}]};
s.features.push(invalidDisabled);
const disabledPayload=getPunchWizardPayload(view).features[1];
assert.equal(disabledPayload.arrayTransforms,undefined);assert.equal(disabledPayload.arrayGroups[0].count,0);
assert.equal(disabledPayload.arrayGroupsError,undefined);s.features.pop();

const requests=[],renders=[];
const context={sceneProxy:{async invoke(method,payload,options){requests.push({method,payload,options});
  return {toolsOnly:true,baseGeometry:{url:"memory://base",version:requests.length},toolGeometry:{url:"memory://tools",version:requests.length},toolCount:3,length:1000};}}};
const ops={renderProject(_context,current){renders.push({pending:current.pending,mode:current.tubeDesignerPunchWizard?.parameterEditor?.mode});}};
await handlePartsAreaAction(context,view,"tube-designer-punch-preview",{},ops);
assert.equal(requests.at(-1).method,"TubeDesigner.PreviewPunchWizard");
assert.equal(requests.at(-1).payload.features.length,2);
requests.at(-1).payload.features.forEach(cleanTransport);
assert.equal(requests.at(-1).payload.toolsOnly,true);
assert.ok(renders.some(r=>r.pending));assert.equal(view.pending,false);

// The two real action dispatchers route group actions into the same isolated
// punch transaction and recompute transforms, without changing the pose.
await handlePartsAreaAction(context,view,"tube-designer-punch-parameters-open",{dataset:{tubeDesignerPunchIndex:"0",tubeDesignerPunchEditorMode:"arrays"}},ops);
assert.equal(s.parameterEditor.mode,"arrays");
await handlePartsAreaAction(context,view,"tube-designer-punch-array-group-add",{dataset:{tubeDesignerPunchIndex:"0",tubeDesignerPunchArrayType:"linear",tubeDesignerPunchArrayAxis:"Z"}},ops);
assert.equal(requests.at(-1).payload.features.length,1);assert.equal(requests.at(-1).payload.features[0].arrayCandidateCount,8);
assert.equal(requests.at(-1).payload.features[0].arrayTransforms.length,6);
assert.equal(requests.at(-1).payload.features[0].station,100);assert.equal(s.history.length,0);
await handlePartsAreaAction(context,view,"tube-designer-punch-parameters-cancel",{},ops);
assert.deepEqual(s.features[0],original);assert.equal(s.history.length,0);

const profile={id:"round",name:"圆管",defaultParameters:{},previewProfile:{kind:"round",width:40,depth:40,diameter:40,contours:[{kind:"circle",radius:20},{kind:"circle",radius:18}]}};
const createView={pending:false,tubeDesignerSystemProfiles:[profile],tubeDesignerNestingSelectedPartIds:[]};
await handleNestingPunchPartAction(context,createView,"tube-designer-nesting-punch-create-open",{},ops);
const create=createView.tubeDesignerPunchWizard;
create.catalogueStatus="ready";create.features=[feature()];
await handleNestingPunchPartAction(context,createView,"tube-designer-nesting-punch-create-parameters-open",{dataset:{tubeDesignerPunchIndex:"0",tubeDesignerPunchEditorMode:"arrays"}},ops);
assert.equal(create.parameterEditor.mode,"arrays");
await handleNestingPunchPartAction(context,createView,"tube-designer-nesting-punch-create-array-group-field",{value:"30",dataset:{tubeDesignerPunchIndex:"0",tubeDesignerPunchArrayGroup:"y",tubeDesignerPunchArrayField:"spacing"}},ops);
assert.equal(requests.at(-1).payload.length,1000);assert.equal(requests.at(-1).payload.profileRef.id,"round");
assert.deepEqual(requests.at(-1).payload.features[0].arrayTransforms.map(m=>[m[3],m[7]]),[[0,0],[50,0],[0,30]]);
await handleNestingPunchPartAction(context,createView,"tube-designer-nesting-punch-create-parameters-apply",{},ops);
assert.equal(create.parameterEditor,null);assert.equal(create.history.length,1);
await handleNestingPunchPartAction(context,createView,"tube-designer-nesting-punch-create-undo",{},ops);
assert.equal(create.features[0].arrayGroups[1].spacing,20);
assert.equal(resolvePunchDistribution(create.features[0],create.baseLength).arrayTransforms.length,3);
const metadataRevision=create.revision,metadataPreview=create.preview,metadataRequests=requests.length,metadataHistory=create.history.length;
for(const [name,value] of [["name","元数据独立"],["material","Q235B"],["quantity","2"]]) {
  await handleNestingPunchPartAction(context,createView,"tube-designer-nesting-punch-create-draft-change",{value,dataset:{tubeDesignerNestingPunchField:name}},ops);
  assert.equal(create.revision,metadataRevision,"Metadata must not invalidate the geometry revision.");
  assert.equal(create.preview,metadataPreview,"Metadata edits keep the identical completed preview object.");
  assert.equal(requests.length,metadataRequests,"Metadata edits must not issue native geometry requests.");
  assert.equal(createView.tubeDesignerNestingPunchPartDraft[name],value);
}
assert.equal(create.history.length,metadataHistory+3,"All metadata edits remain independently undoable.");
console.log("Punch array transport passed: saved and draft payload hygiene, reload, both production dispatchers, transaction cancel/undo, and controlled preview.");
