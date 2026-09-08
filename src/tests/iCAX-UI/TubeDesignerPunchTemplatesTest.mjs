import assert from "node:assert/strict";
import {readFileSync,readdirSync} from "node:fs";
import {createPunchWizardState,installPunchCatalogue,updatePunchWizardField,addPunchWizardFeature,
  editPunchWizardFeature,getPunchWizardPayload,renderPunchWizardDialog,validatePunchWizard,missingPunchTools,isPunchToolReadOnly} from "../../apps/tube-designer/webpage/punchWizard.mjs";
import {previewPunch,beginPunchOperation,finishPunchOperation} from "../../apps/tube-designer/webpage/punchEditor.mjs";
import {handlePartsAreaAction} from "../../apps/tube-designer/webpage/partsArea.mjs";
const toolsRoot=new URL("../../apps/tube-designer/templates/_shared/punch-tools/",import.meta.url);
const tools=readdirSync(toolsRoot,{withFileTypes:true}).filter(item=>item.isDirectory()).map(item=>JSON.parse(readFileSync(new URL(item.name+"/tool.json",toolsRoot))))
  .map(t=>({...t,digest:"test",defaultParameters:Object.fromEntries(t.parameters.map(p=>[p.key,p.defaultValue]))}));
const part={entityId:"part",length:500,manufacturingGeometryResourceId:"old-id",manufacturingGeometryResourceVersion:1,
  independentNesting:true,profile:{kind:"rect",width:40,depth:20},properties:{"manufacturing.partKind":"tube"}};
const state=createPunchWizardState(part),view={tubeDesignerPunchWizard:state,pending:false,scene:{tubeDesigner:{nestingGroups:[{parts:[part]}]}}};
installPunchCatalogue(state,{tools,errors:[]});
const change=(field,value,more={})=>updatePunchWizardField(view,{value,dataset:{tubeDesignerPunchField:field,...more}});
assert.equal(change("tool","circle"),true);
assert.equal(change("depthMode","reverse"),true);
assert.equal(state.draft.reverse,true);assert.equal(state.draft.through,false);assert.equal(state.draft.opposite,false);
assert.equal(change("tool","v-notch"),true);
const sheet=renderPunchWizardDialog(part,view,{tableMode:true,showEnds:true,branchProfiles:[]});
assert.match(sheet,/V 槽/);assert.match(sheet,/左端面/);assert.match(sheet,/右端面/);
assert.equal(change("tool","diamond-12"),true);
assert.deepEqual(state.draft.toolParameters,{});
assert.match(renderPunchWizardDialog(part,view),/定式刀具：形状尺寸固定/);
assert.ok(addPunchWizardFeature(view,part));
assert.equal(getPunchWizardPayload(view).features[0].toolRef.id,"diamond-12");
editPunchWizardFeature(view,"edit",0);
change("station","120");
assert.match(validatePunchWizard(view,part),/保存当前/);
assert.ok(addPunchWizardFeature(view,part));
assert.equal(state.features.length,1);assert.equal(state.features[0].station,120);
editPunchWizardFeature(view,"undo");
assert.equal(state.features[0].station,250);
editPunchWizardFeature(view,"redo");
assert.equal(state.features[0].station,120);
editPunchWizardFeature(view,"toggle",0);assert.equal(state.features[0].enabled,false);
editPunchWizardFeature(view,"copy",0);assert.equal(state.features.length,1);assert.equal(state.editingId,"");
change("tool","end-convex",{tubeDesignerPunchEnd:"end"});
change("parameter",60,{tubeDesignerPunchEnd:"end",tubeDesignerPunchParameter:"diameter"});
assert.equal(getPunchWizardPayload(view).ends.end.toolParameters.diameter,60);
assert.match(renderPunchWizardDialog(part,view),/圆柱凸头/);
state.draft.arrayCount=1.5;assert.equal(addPunchWizardFeature(view,part),false);
assert.equal(state.draft.arrayCount,1.5); // Invalid values are not silently clamped.
state.draft.arrayCount=1;
const original=getPunchWizardPayload(view);
let release, invocation;
const context={sceneProxy:{invoke(method,payload,options){
  invocation={method,payload,options};return new Promise(resolve=>release=resolve);
}}};
const ops={renderProject(){}};
const pending=previewPunch(context,view,part,ops);
await Promise.resolve();
assert.equal(view.pending,true);assert.equal(view.tubeDesignerOperation.kind,"punch");
const revision=state.revision;
assert.equal(change("station","999"),false);
await handlePartsAreaAction(context,view,"tube-designer-punch-remove",{dataset:{tubeDesignerPunchIndex:0}},ops);
assert.deepEqual(getPunchWizardPayload(view),original);
release({toolsOnly:true,baseGeometry:{url:"base",version:1},toolGeometry:{url:"preview",version:1},bounds:{width:500}});
await pending;
assert.equal(view.pending,false);assert.equal(view.tubeDesignerOperation,null);
assert.equal(state.preview.revision,revision);
assert.equal(invocation.payload.resourceId,"old-id");
assert.equal(invocation.method,"TubeDesigner.PreviewPunchWizard");
part.manufacturingGeometryResourceId="new-id"; // UI still sends the originally captured identity.
assert.equal(getPunchWizardPayload(view).resourceId,"old-id");
const options=beginPunchOperation({},view,"test");
options.onReport({message:"真实进度",completed:3,total:8});
assert.equal(view.tubeDesignerOperation.phaseLabel,"3 / 8");
finishPunchOperation(view);
// No API call on invalid metadata; zero-feature/end-only edits remain supported.
state.features=[];state.editingId="";
assert.equal(validatePunchWizard(view,part),"");
const relocated={...part,thumbnailGeometryResourceId:"saved-mesh",thumbnailGeometryResourceVersion:1,properties:{
  ...part.properties,"tubeDesigner.punchWizard":{baseLength:500,features:[{
    type:"diamond-12",enabled:true,station:200,arrayCount:1,rowCount:1,
    toolRef:{id:"diamond-12",version:"1.0.0",digest:"original"},toolParameters:{},toolLabel:"菱形孔",toolKind:"fixed",
    toolSnapshot:{descriptor:{parameters:["must not travel"]},geometry:{mode:"profile"}}
  }],ends:{start:{type:"keep"},end:{type:"keep"}}}}};
const relocatedView={tubeDesignerPunchWizard:createPunchWizardState(relocated),pending:false};
installPunchCatalogue(relocatedView.tubeDesignerPunchWizard,{tools:[]});
assert.equal(relocatedView.tubeDesignerPunchWizard.preview.geometry.url,"saved-mesh");
assert.equal(missingPunchTools(relocatedView.tubeDesignerPunchWizard).length,1);
assert.match(validatePunchWizard(relocatedView,relocated),/本机缺少/);
assert.match(renderPunchWizardDialog(relocated,relocatedView),/仍可查看、排样和导出/);
assert.equal(JSON.stringify(getPunchWizardPayload(relocatedView)).includes("toolSnapshot"),false);
assert.equal(editPunchWizardFeature(relocatedView,"toggle",0),false);
let relocatedState=relocatedView.tubeDesignerPunchWizard, frozenFeature=relocatedState.features[0];
frozenFeature.frozenTool={schema:"icax.frozen-punch-tool",schemaVersion:1,ref:structuredClone(frozenFeature.toolRef),
  parameters:{},instance:structuredClone(frozenFeature),geometry:{mode:"profile",contours:[{kind:"circle",radius:6}]}};
frozenFeature.frozenCut={url:"embedded-cutter",version:1,coordinateSpace:"part"};
// A frozen record arrives from a saved drawing, not by mutating a locked UI node.
relocatedView.tubeDesignerPunchWizard=createPunchWizardState({...relocated,properties:{...relocated.properties,
  "tubeDesigner.punchWizard":{...relocated.properties["tubeDesigner.punchWizard"],features:[structuredClone(frozenFeature)]}}});
relocatedState=relocatedView.tubeDesignerPunchWizard;frozenFeature=relocatedState.features[0];
installPunchCatalogue(relocatedState,{tools:[]});
assert.equal(missingPunchTools(relocatedState).length,0);
assert.equal(validatePunchWizard(relocatedView,relocated),"");
assert.match(renderPunchWizardDialog(relocated,relocatedView),/退化定式 · 只读/);
assert.ok(getPunchWizardPayload(relocatedView).features[0].frozenTool);
for(const action of ["edit","copy","toggle"]){
  assert.equal(editPunchWizardFeature(relocatedView,action,0),false);
}
assert.match(renderPunchWizardDialog(relocated,relocatedView),/节点只读，仅可删除/);
installPunchCatalogue(relocatedView.tubeDesignerPunchWizard,{tools});
assert.equal(isPunchToolReadOnly(relocatedState,frozenFeature),true); // Wrong digest does not substitute a local version.
relocatedState.draft=structuredClone(frozenFeature);relocatedState.editingId=frozenFeature.id;
assert.equal(updatePunchWizardField(relocatedView,{value:99,dataset:{tubeDesignerPunchField:"station"}}),false);
assert.equal(updatePunchWizardField(relocatedView,{value:"circle",dataset:{tubeDesignerPunchField:"tool"}}),false);
assert.equal(addPunchWizardFeature(relocatedView,relocated),false);
relocatedState.editingId="";
assert.equal(editPunchWizardFeature(relocatedView,"remove",0),true);
assert.equal(relocatedState.features.length,0);
assert.equal(validatePunchWizard(relocatedView,relocated),"");
editPunchWizardFeature(relocatedView,"undo");
assert.equal(relocatedState.features.length,1);assert.ok(relocatedState.features[0].frozenCut);
// Matching original installation restores normal editability.
installPunchCatalogue(relocatedState,{tools:tools.map(t=>t.id==="diamond-12"?{...t,digest:"original"}:t)});
assert.equal(isPunchToolReadOnly(relocatedState,relocatedState.features[0]),false);
assert.equal(editPunchWizardFeature(relocatedView,"edit",0),true);
relocatedState.editingId="";
relocatedState.ends.start={...structuredClone(frozenFeature),type:"template",toolRef:{id:"missing-end",version:"1"}};
assert.equal(updatePunchWizardField(relocatedView,{value:99,dataset:{tubeDesignerPunchField:"trim",tubeDesignerPunchEnd:"start"}}),false);
assert.equal(editPunchWizardFeature(relocatedView,"remove-end","start"),true);
assert.equal(relocatedState.ends.start.type,"keep");
console.log("Frozen cutter persistence, delete-only degradation, matching-version editing, history, preview and busy guards passed.");
