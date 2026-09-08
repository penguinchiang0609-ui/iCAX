import assert from "node:assert/strict";
import { createPunchWizardState, normalizePunchFeature, openPunchParameters, closePunchParameters,
  updatePunchWizardField, validatePunchWizard, editPunchWizardFeature, getPunchWizardPayload,
  renderPunchWizardDialog, addPunchWizardFeature, selectPunchTool } from "../../apps/tube-designer/webpage/punchWizard.mjs";
import { buildPunchReviewSummary,renderPunchReview } from "../../apps/tube-designer/webpage/punchReview.mjs";
const part={entityId:"review-fixture",name:"布局复查",length:1000,profile:{kind:"rect",width:40,depth:20}};
const view={pending:false,tubeDesignerPunchWizard:createPunchWizardState(part)},s=view.tubeDesignerPunchWizard;
s.catalogueStatus="ready";
s.features=[normalizePunchFeature({type:"circle",diameter:10,station:100,arrayCount:5,arrayPitch:150,layoutDatum:"base"})];
const original=structuredClone(s.features[0]);
assert.equal(openPunchParameters(view,"0"),true);
assert.match(validatePunchWizard(view,part),/关闭当前参数编辑/);
assert.equal(addPunchWizardFeature(view,part),false);
updatePunchWizardField(view,{value:"18",dataset:{tubeDesignerPunchField:"diameter",tubeDesignerPunchIndex:"0"}});
assert.equal(s.features[0].diameter,18);
assert.equal(s.history.length,0,"preview edits must not create separate undo entries");
assert.equal(closePunchParameters(view,false,part),true);
assert.deepEqual(s.features[0],original,"cancel restores the complete source and layout");
assert.equal(s.history.length,0);
openPunchParameters(view,"0");
updatePunchWizardField(view,{value:"12",dataset:{tubeDesignerPunchField:"diameter",tubeDesignerPunchIndex:"0"}});
const revisionBeforeCommit=s.revision;
assert.equal(closePunchParameters(view,true,part),true);
assert.equal(s.revision,revisionBeforeCommit,"Confirm only commits the transaction; it must not invalidate an already computed geometry preview");
assert.equal(s.history.length,1);
assert.equal(s.features[0].diameter,12);
editPunchWizardFeature(view,"undo");
assert.equal(s.features[0].diameter,10);
editPunchWizardFeature(view,"redo");
assert.equal(s.features[0].diameter,12);
openPunchParameters(view,"0");
updatePunchWizardField(view,{value:"-1",dataset:{tubeDesignerPunchField:"diameter",tubeDesignerPunchIndex:"0"}});
assert.equal(closePunchParameters(view,true,part),false);
assert.match(s.parameterEditor.error,/尺寸/);
closePunchParameters(view,false,part);

s.features=[normalizePunchFeature({type:"circle",diameter:10,distributionMode:"fill",headMargin:100,tailMargin:100,arrayPitch:180,arrayCount:1,skipInstancesText:"3",layoutDatum:"base"}),
  normalizePunchFeature({type:"circle",diameter:10,station:500,depthMode:"through",through:true}),
  normalizePunchFeature({type:"circle",diameter:10,enabled:false,distributionMode:"sequence",spacingSequence:""})];
const payload=getPunchWizardPayload(view);
assert.equal(payload.features[0].arrayCount,5);
assert.deepEqual(payload.features[0].arrayOffsets,[0,180,360,540,720]);
assert.deepEqual(payload.features[0].skippedInstances,["0:2"]);
assert.equal(payload.features[0].spacingSequence,"");
assert.equal(payload.features[2].arrayOffsets,undefined,"invalid disabled rows carry no invalid derived offsets");
assert.equal(payload.features[0].layoutSummary,undefined);
const saved={...part,properties:{"tubeDesigner.punchWizard":{features:payload.features,baseLength:1000}}};
const reloaded=createPunchWizardState(saved);
reloaded.baseLength=1200;
const second=getPunchWizardPayload({tubeDesignerPunchWizard:reloaded});
assert.equal(second.features[0].arrayCount,6);
assert.equal(second.features[0].arrayPitch,180);
assert.deepEqual(second.features[0].skippedInstances,["0:2"]);
const summary=buildPunchReviewSummary(s,part);
assert.equal(summary.positions,5);assert.equal(summary.cutters,5);assert.equal(summary.estimatedOpenings,6);
assert.equal(summary.groups.length,1,"same hole specifications aggregate across records");
assert.equal(summary.skipped,1);assert.equal(summary.disabled,1);
assert.equal(summary.finishedLength,null);
s.preview={revision:s.revision,length:990,includesDraft:false};
assert.equal(buildPunchReviewSummary(s,part).finishedLength,990);
s.features.push(normalizePunchFeature({toolTarget:"part",section:{name:"任意DXF",profile:{contours:[{kind:"circle",radius:3}]}},station:200}));
assert.equal(buildPunchReviewSummary(s,part).estimatedOpenings,null,"a branch cutter does not imply a known physical opening count");
assert.match(renderPunchReview(s,part,n=>"fixture-"+n),/孔刀实例/);
assert.doesNotMatch(renderPunchReview(s,part,n=>"fixture-"+n),/侧壁开孔预估/);
s.features.push(normalizePunchFeature({distributionMode:"sequence",spacingSequence:"bad"}));
assert.equal(buildPunchReviewSummary(s,part).issues.length,1);
assert.match(renderPunchReview(s,part,n=>"fixture-"+n),/summary-select/);

s.features=[original];s.error="";
let html=renderPunchWizardDialog(part,view,{tableMode:true});
assert.doesNotMatch(html,/零件复查/);assert.match(html,/data-punch-edit-name/);
assert.match(html,/class="tube-designer-punch-sidebar"/);
assert.match(html,/class="tube-designer-punch-rightbar"/);
assert.doesNotMatch(html,/class="tube-designer-punch-footer"/);
assert.doesNotMatch(html,/data-tube-designer-punch-field="diameter"/);
openPunchParameters(view,"0");
html=renderPunchWizardDialog(part,view,{tableMode:true});
assert.match(html,/data-punch-parameter-dialog/);assert.match(html,/data-tube-designer-punch-field="diameter"/);
assert.doesNotMatch(html,/<section inert /,"A parameter transaction must not make the entire scene inert");
assert.match(html,/class="tube-designer-punch-parameter-dialog" role="dialog" aria-modal="false"/);
assert.match(html,/class="tube-designer-punch-records has-ends" inert/);
assert.match(html,/data-punch-parameter-drag/);
s.parameterEditor.windowPosition={left:32,top:48};
assert.match(renderPunchWizardDialog(part,view,{tableMode:true}),/style="position:fixed;left:32px;top:48px;margin:0"/,"Rerender retains the manually positioned parameter window");
closePunchParameters(view,false,part);
s.tools=[{id:"branch-profile",target:"part",requiresSection:true,defaultParameters:{},version:"1",digest:"fixture"}];
const branch=normalizePunchFeature({face:"round",rowDistributionMode:"full-circle",rowCount:4,section:{source:"library"}});
selectPunchTool(s,branch,"branch-profile");
assert.equal(branch.face,"round");assert.equal(branch.rowDistributionMode,"full-circle");

const lockedRecipe={id:"legacy-frozen",type:"custom-external",toolRef:{id:"custom-external",version:"1",digest:"missing"},
  station:980,arrayCount:2,arrayPitch:50,enabled:true,toolParameters:{width:10}};
lockedRecipe.frozenTool={schema:"icax.frozen-punch-tool",schemaVersion:1,ref:structuredClone(lockedRecipe.toolRef),
  geometry:{kind:"fixture"},instance:structuredClone(lockedRecipe)};
lockedRecipe.frozenCut={url:"memory://frozen",version:1};
const frozenView={tubeDesignerPunchWizard:createPunchWizardState({...part,properties:{"tubeDesigner.punchWizard":{
  features:[lockedRecipe,{id:"editable",type:"circle",diameter:10,station:200}],baseLength:1000}}})};
frozenView.tubeDesignerPunchWizard.catalogueStatus="ready";
assert.notDeepEqual(frozenView.tubeDesignerPunchWizard.features[0],lockedRecipe,"UI normalization still supplies rendering defaults");
assert.deepEqual(getPunchWizardPayload(frozenView).features[0],lockedRecipe,"Immutable saved recipes must not gain UI defaults or recalculated layout arrays");
assert.equal(validatePunchWizard(frozenView,part),"","Read-only frozen geometry is not invalidated by new center-position validation");
updatePunchWizardField(frozenView,{value:"240",dataset:{tubeDesignerPunchField:"station",tubeDesignerPunchIndex:"1"}});
assert.deepEqual(getPunchWizardPayload(frozenView).features[0],lockedRecipe);
assert.equal(openPunchParameters(frozenView,"0"),false);
editPunchWizardFeature(frozenView,"remove","0");
assert.equal(getPunchWizardPayload(frozenView).features.length,1);
editPunchWizardFeature(frozenView,"undo");
assert.deepEqual(getPunchWizardPayload(frozenView).features[0],lockedRecipe,"Undo restores the exact frozen recipe too");
console.log("Punch parameter transactions, rule transport/reopen, review counts and compact table checks passed.");
