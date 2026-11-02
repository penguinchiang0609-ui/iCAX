import assert from "node:assert/strict";
import { createPunchWizardState, normalizePunchFeature, getPunchWizardPayload, resolvePunchDistribution } from "../../apps/tube-designer/webpage/punchWizard.mjs";
import { handleNestingPunchPartAction } from "../../apps/tube-designer/webpage/nestingPunchPart.mjs";

const part={entityId:"array-transport",length:1000,independentNesting:true,profile:{kind:"rect",width:40,depth:20},properties:{"manufacturing.partKind":"tube"}};
const tool={id:"circle",version:"1.0",parameters:[{key:"diameter",valueType:"number",defaultValue:10}],defaultParameters:{diameter:10}};
const feature=()=>normalizePunchFeature({id:"grouped",recordKind:"tool",type:"circle",toolRef:{id:"circle",version:"1.0"},diameter:10,station:100,reference:"center",layoutDatum:"base",
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
s.catalogueStatus="ready";s.tools=[tool];s.features=[feature()];s.draft={...feature(),id:"draft-grouped"};
const original=structuredClone(s.features[0]);
cleanTransport(getPunchWizardPayload(view).features[0]);
assert.deepEqual(s.features[0],original,"Serialization must not mutate the editable recipe.");
const saved={...part,properties:{...part.properties,"tubeDesigner.punchWizard":{baseLength:1000,features:getPunchWizardPayload(view).features}}};
const reloaded=createPunchWizardState(part,saved.properties["tubeDesigner.punchWizard"]);reloaded.baseLength=1200;
cleanTransport(getPunchWizardPayload({tubeDesignerPunchWizard:reloaded}).features[0]);
const invalidDisabled={...feature(),enabled:false,arrayGroups:[{id:"unfinished",type:"linear",axis:"X",count:0}]};
s.features.push(invalidDisabled);
const disabledPayload=getPunchWizardPayload(view).features[1];
assert.equal(disabledPayload.arrayTransforms,undefined);assert.equal(disabledPayload.arrayGroups[0].count,0);
assert.equal(disabledPayload.arrayGroupsError,undefined);s.features.pop();

console.log("Punch creation array transport: transforms, suppressions, validation and direct draft serialization passed.");
