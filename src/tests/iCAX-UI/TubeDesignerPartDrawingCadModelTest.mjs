import assert from "node:assert/strict";
import { createDrawingCadState, captureDrawingCadBaseline, installDrawingCatalogue, getDrawingCadPayload,
  checkpointDrawing, editDrawingFeature, normalizeDrawingFeature, updateSelectedDrawingBevel } from "../../apps/tube-designer/webpage/partDrawingModel.mjs";

const section={source:"cad",name:"精确轮廓",profile:{contours:[{kind:"path",segments:[
  {kind:"arc",center:[0,0],radius:6,startParameter:0,endParameter:Math.PI/2},
  {kind:"bspline",degree:3,controlPoints:[[0,6],[1,7],[5,7],[6,0]],weights:[1,2,3,1],knots:[0,1],multiplicities:[4,4]},
]}]}};
const tool={id:"branch-profile",version:"1",digest:"current",target:"part",requiresSection:true,
  parameters:[],defaultParameters:{},defaultOperationParameters:{angle:90,azimuth:0,roll:0,offsetY:0,offsetZ:0,direction:"through",length:120}};
const features=[100,300].map((station,index)=>({id:`cut-${index}`,cadNodeId:`cut-${index}`,type:tool.id,toolTarget:"part",recordKind:"branch",
  section,station,angle:73,azimuth:16,roll:8,offsetY:2,offsetZ:4,direction:"symmetric",length:88}));
const document={revision:"19",resourceVersion:7,documentResourceId:"CAD",documentResourceVersion:3,
  authoringView:{mode:"drawing",drawing:{length:600,name:"当前零件",quantity:2,material:"钢",section},features,
    ends:{start:{type:"keep"},end:{type:"keep"}},readOnlyNodes:[{nodeId:"wrapped-cut",label:"二维切除"}],bindings:{main:"base"}}};
const pristine=structuredClone(document),part={entityId:"part"};
const state=createDrawingCadState(part,document);
installDrawingCatalogue(state,{tools:[tool]});captureDrawingCadBaseline(state);
assert.equal(state.features[0].angle,73,"Current graph projection keeps its exact authored pose when installing current tools");
assert.equal(state.features[0].length,88);
assert.deepEqual(getDrawingCadPayload(state),{partEntityId:"part",resourceVersion:7,revision:"19",authoringEdit:{mode:"drawing",features:[],ends:[]}});

checkpointDrawing(state);state.features[0].station=125;
let payload=getDrawingCadPayload(state);
assert.equal(payload.authoringEdit.features.length,1);
assert.equal(payload.authoringEdit.features[0].operation,"update");
assert.equal(payload.authoringEdit.features[0].nodeId,"cut-0");
assert.deepEqual(payload.authoringEdit.features[0].feature.section,section,"Placement edits retain precise arc ranges and rational curve data");
assert.equal(payload.authoringEdit.main,undefined);
assert.deepEqual(state.cadSession.readOnlyNodes,[{nodeId:"wrapped-cut",label:"二维切除"}]);
editDrawingFeature(state,"undo");assert.deepEqual(getDrawingCadPayload(state).authoringEdit.features,[]);
editDrawingFeature(state,"redo");assert.equal(getDrawingCadPayload(state).authoringEdit.features[0].feature.station,125);

editDrawingFeature(state,"remove",1);
state.features.push(normalizeDrawingFeature({...state.features[0],id:"new-cut",cadNodeId:undefined,station:400}));
payload=getDrawingCadPayload(state);
assert.deepEqual(payload.authoringEdit.features.map(item=>[item.operation,item.nodeId]),[["update","cut-0"],["add",undefined],["remove","cut-1"]]);
assert.deepEqual(document,pristine,"Draft edits never mutate the fetched document or its bindings");

const readonly=createDrawingCadState(part,{...document,authoringView:{...document.authoringView,drawing:{...document.authoringView.drawing,readOnly:true,section:{}}}});
readonly.drawing.name="修改名称";
assert.equal(getDrawingCadPayload(readonly).authoringEdit.main.name,"修改名称","Business metadata can change without replacing an unprojectable exact base");
readonly.drawing.length=650;
assert.throws(()=>getDrawingCadPayload(readonly),/精确实体/);

const bevelState=createDrawingCadState(part,{...document,authoringView:{...document.authoringView,capabilities:{bevels:true}}});
bevelState.drawing.bevels=[{id:"new-bevel",edgeIds:[7],edgeNames:["edge-name"],referenceFaceId:3,referenceFaceName:"face-name",
  topologyRevision:"current-source",bevel:{enabled:true,side:"outer",angleDegrees:45,rootFace:0.2}}];
assert.deepEqual(getDrawingCadPayload(bevelState).authoringEdit.bevels,bevelState.drawing.bevels);
bevelState.topologySelection={kind:"edge",id:7,edgeIds:[7],topologyRevision:"current-source"};
bevelState.preview={topology:{revision:"current-source",edges:[{id:7,persistentName:"edge-name"}],
  faces:[{id:3,persistentName:"face-name"},{id:5,persistentName:"new-face-name"}]}};
updateSelectedDrawingBevel(bevelState,"referenceFaceId",{value:"5"});
assert.equal(getDrawingCadPayload(bevelState).authoringEdit.bevels[0].referenceFaceName,"new-face-name");
assert.deepEqual(getDrawingCadPayload(bevelState).authoringEdit.bevels[0].edgeNames,["edge-name"],"Changing a reference face preserves the precise edge binding");
bevelState.drawing.bevels[0].bevel.angleDegrees=90;
assert.throws(()=>getDrawingCadPayload(bevelState),/坡口角/);

state.features[0].section.pendingParameters={diameter:12};
assert.throws(()=>getDrawingCadPayload(state),/截面参数正在更新/);
console.log("Current CAD 3D authoring: CAS, unchanged-node preservation, exact sections, local add/update/remove, undo/redo and readonly base verified.");
