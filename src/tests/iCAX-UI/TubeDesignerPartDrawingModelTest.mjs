import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import {createDrawingState,normalizeDrawingFeature,installDrawingCatalogue,selectDrawingTool,
  updateDrawingField,addDrawingFeature,editDrawingFeature,getDrawingPayload,validateDrawing,
  isDrawingToolReadOnly,hasFrozenDrawingTool} from "../../apps/tube-designer/webpage/partDrawingModel.mjs";

const tool={id:"branch-profile",version:"1",digest:"exact",target:"part",requiresSection:true,
  defaultParameters:{},parameters:[]};
const section={source:"library",profile:{contours:[{kind:"circle",radius:20}]},parameters:{diameter:40}};
const state=createDrawingState({entityId:"drawing",length:500});installDrawingCatalogue(state,{tools:[tool]});
state.draft=normalizeDrawingFeature({station:200,section,customRecipeField:{preserve:true}});selectDrawingTool(state,state.draft,tool.id);
assert.equal(addDrawingFeature(state),true);
state.features[0].arrayCount=3;state.features[0].arrayPitch=-60;state.features[0].rowCount=2;state.features[0].rowPitch=-10;
let payload=getDrawingPayload(state);
assert.deepEqual(payload.features[0].arrayOffsets,[0,-60,-120]);assert.deepEqual(payload.features[0].rowOffsets,[0,-10]);
assert.deepEqual(payload.features[0].customRecipeField,{preserve:true});
state.features[0].reference="end";payload=getDrawingPayload(state);assert.deepEqual(payload.features[0].arrayOffsets,[0,60,120]);
const id=state.features[0].id;editDrawingFeature(state,"edit",0);
updateDrawingField(state,{value:"75",dataset:{tubeDesignerPunchField:"angle"}});
assert.equal(state.draft.angle,75);assert.equal(state.features[0].angle,90);
assert.equal(addDrawingFeature(state),true);assert.equal(state.features[0].id,id);
assert.equal(state.features[0].angle,75);
editDrawingFeature(state,"undo");assert.equal(state.features[0].angle,90);
editDrawingFeature(state,"redo");assert.equal(state.features[0].angle,75);
state.editingId="";
assert.equal(validateDrawing(state),"");
state.previewRenderError="上一次网格加载失败";
assert.equal(validateDrawing(state),"","Rendering failures cannot invalidate an editable geometric recipe");
editDrawingFeature(state,"edit",0);
updateDrawingField(state,{value:"76",dataset:{tubeDesignerPunchField:"angle"}});
assert.equal(addDrawingFeature(state),true);
assert.equal(validateDrawing(state),"","Parameter edits recover by requesting fresh resources, without a stale display-error gate");
state.features[0].arrayCount=Infinity;assert.match(validateDrawing(state),/整数/);
state.features[0].enabled=false;assert.equal(validateDrawing(state),"");
assert.equal(getDrawingPayload(state).features[0].arrayOffsets,undefined,"Disabled invalid counts never allocate an unbounded transport array");

const ref={id:"missing",version:"v1",digest:"saved"};
const original={id:"frozen",type:"custom",toolRef:ref,customRecipeField:{unknown:[1,2]},frozenCut:{url:"saved-solid",version:3},
  frozenTool:{schema:"icax.frozen-punch-tool",schemaVersion:1,ref,geometry:{type:"solid"},instance:{type:"custom"}}};
const saved={entityId:"frozen-part",length:800,properties:{"tubeDesigner.partDrawing":{
  drawing:{length:800},features:[original],ends:{start:{type:"keep"}},
}}};
const read=createDrawingState(saved);installDrawingCatalogue(read,{tools:[]});
assert.equal(isDrawingToolReadOnly(read,read.features[0]),true);assert.equal(hasFrozenDrawingTool(read.features[0]),true);
assert.equal(validateDrawing(read),"");assert.deepEqual(getDrawingPayload(read).features[0],original);
assert.equal(read.ends.end.type,"keep");
assert.equal(editDrawingFeature(read,"copy",0),false);assert.match(read.error,/仅可删除/);
assert.equal(editDrawingFeature(read,"remove",0),true);assert.equal(read.features.length,0);
assert.deepEqual(saved.properties["tubeDesigner.partDrawing"].features[0],original,"Model operations never mutate the source part");
for(const path of ["partDrawingModel.mjs","partDrawing.mjs"]){
  const source=readFileSync(new URL("../../apps/tube-designer/webpage/"+path,import.meta.url),"utf8");
  assert.doesNotMatch(source,/from\s+["']\.\/punch(?:Wizard|Layout|Array|Editor)|recipeView/);
}
console.log("Independent drawing model: recipes, regular arrays, undo/redo, invalid input bounds and frozen data preservation passed.");
