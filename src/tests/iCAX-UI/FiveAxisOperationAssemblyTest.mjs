import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {operationAssemblyMatrix,operationPlacementMatrix,placementMovesAssembly} from '../../apps/five-axis-cam/webpage/operationAssembly.mjs';
import {setupWorldFrame,setupReferenceWorldFrame,applySetupDisplayFrame,placementWorldMatrix} from '../../apps/five-axis-cam/webpage/setupWorldFrame.mjs';
import {nearestCutLine} from '../../apps/five-axis-cam/webpage/cutSelection.mjs';
import {focusCuts} from '../../apps/five-axis-cam/webpage/cutFocus.mjs';
import {updateGeneratedPreview} from '../../apps/five-axis-cam/webpage/toolpathGeneration.mjs';
import {openPlacement,syncPlacementPreview,disposePlacementPreview,placementMatrix} from '../../apps/five-axis-cam/webpage/workpiecePlacement.mjs';
import {workpieceViewSnapshot} from '../../apps/five-axis-cam/webpage/workpieceViewSnapshot.mjs';
import {applyPlacementReceipt} from '../../apps/five-axis-cam/webpage/placementReceipt.mjs';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';

const row=matrix=>matrix.clone().transpose().toArray(),matrix=values=>new THREE.Matrix4().set(...values);
const near=(actual,expected)=>actual.elements.forEach((v,i)=>assert.ok(Math.abs(v-expected.elements[i])<1e-9,`matrix[${i}] ${v} != ${expected.elements[i]}`));
function fixture(type='TableTable',{tooling=true}={}){
 const assembly=new THREE.Matrix4().makeTranslation(50,-30,20).multiply(new THREE.Matrix4().makeRotationZ(.7)),local=new THREE.Matrix4().makeTranslation(7,11,13),part=new THREE.Matrix4().makeTranslation(-9,15,-4);
 const setup={id:'s',enabled:true,machineType:{machineType:type},assemblyTransform:row(assembly),fixtureDesign:{assembly:{validity:'Current'}}},placement={id:'p',setupId:'s',workpieceId:'w',transform:row(local)};
 const previews=new Map(),geometry=new THREE.BoxGeometry(2,4,6),domain={expected:{epoch:'e',mutationSequence:'1'},activeSetupId:'s',activePlacementId:'p',setups:[setup],placements:[placement],workpieces:[{id:'w',enabled:true}],fixtureUnits:tooling?[{id:'u',setupId:'s',design:{assembly:{validity:'Current'}}}]:[],fixtures:tooling?[{id:'f',setupId:'s',transform:row(part)}]:[],intentToolpaths:[]};
 const view={fiveAxisArea:'operation',scene:{activePlacementId:'p',model:{isLoaded:true},domain},viewport:{renderer:{domElement:{closest:()=>null}},sceneObjects:new Map([['p',{geometry}]]),transformPreviews:previews,setTransformPreview(id,values){previews.set(id,[...values]);},clearTransformPreview(id){previews.delete(id);}}};
 selectTool(view,'translate');return {view,domain,setup,placement,assembly,local,part,previews};
}
test('only table-side operations with tooling use common assembly placement',()=>{
 for(const type of ['HeadHead','HeadTable','TableTable'])for(const tooling of [false,true]){
  const f=fixture(type,{tooling});assert.equal(placementMovesAssembly(f.view,'s'),type!=='HeadHead'&&tooling);
  near(operationPlacementMatrix(f.view,f.placement),f.assembly.clone().multiply(f.local));near(setupWorldFrame(f.view,'s'),f.assembly);near(setupReferenceWorldFrame(f.view,'s'),new THREE.Matrix4());
 }
});
test('table-side preview moves the workpiece and fixture together and cancel clears both without touching local geometry',()=>{
 const f=fixture(),before=structuredClone(f.domain);assert.equal(openPlacement(f.view,'translate'),true);const draft=f.view.fivePlacement;
 assert.equal(draft.motionScope,'Assembly');near(matrix(draft.base),f.assembly.clone().multiply(f.local));draft.axis='x';draft.value='17';syncPlacementPreview(f.view);
 const desired=placementMatrix(draft),nextAssembly=desired.clone().multiply(f.local.clone().invert());
 near(new THREE.Matrix4().fromArray(f.previews.get('p')),desired);near(new THREE.Matrix4().fromArray(f.previews.get('f')),nextAssembly.clone().multiply(f.part));
 assert.deepEqual(f.domain,before);disposePlacementPreview(f.view);assert.equal(f.previews.size,0);
});
test('head-side preview changes only the workpiece even when tooling and a saved assembly frame exist',()=>{
 const f=fixture('HeadHead');openPlacement(f.view,'translate');f.view.fivePlacement.axis='y';f.view.fivePlacement.value='-8';syncPlacementPreview(f.view);
 assert.equal(f.view.fivePlacement.motionScope,'Workpiece');assert.equal(f.previews.has('p'),true);assert.equal(f.previews.has('f'),false);disposePlacementPreview(f.view);
});
test('setup view projection multiplies assembly exactly once for workpiece and tooling and leaves source CAD separate',()=>{
 const f=fixture(),source={id:'source',workpieceId:'w',setupId:'',transform:row(new THREE.Matrix4())};f.domain.placements.push(source);
 const snapshot={rows:[{entityId:'p',data:{setupPlacementTransform:row(f.local)}},{entityId:'f',data:{setupFixtureTransform:row(f.part)}},{entityId:'source',data:{setupPlacementTransform:source.transform}}]};
 const result=workpieceViewSnapshot({},f.view,'operation',snapshot);
 near(matrix(result.rows[0].data.localToWorldMatrix),f.assembly.clone().multiply(f.local));near(matrix(result.rows[1].data.localToWorldMatrix),f.assembly.clone().multiply(f.part));near(matrix(result.rows[2].data.localToWorldMatrix),new THREE.Matrix4());
});
test('a physical machine parent and the common assembly frame are each composed once',()=>{
 const f=fixture();f.view.fiveAxisArea='simulation';const support=new THREE.Matrix4().makeTranslation(100,200,300),registration=new THREE.Matrix4().makeRotationX(.2);
 f.setup.machineSetup={supportId:'table',placement:row(registration)};f.view.viewport.sceneObjects.set('table',{matrix:support});
 near(setupReferenceWorldFrame(f.view,'s'),support.clone().multiply(registration));near(setupWorldFrame(f.view,'s'),support.clone().multiply(registration).multiply(f.assembly));
});
test('assembly placement receipts keep local placement and support validity while relative placement invalidates support',()=>{
 for(const scope of ['Assembly','Workpiece']){
  const f=fixture(),next=new THREE.Matrix4().makeRotationY(.3),before=[...f.placement.transform];
  const request={setupId:'s',placementId:'p',expected:structuredClone(f.domain.expected)},result={motionScope:scope,statePatch:{expected:{epoch:'e',mutationSequence:'2'},inputRevision:'2',placement:{id:'p',transform:before},setup:{id:'s',assemblyTransform:row(next)}}};
  assert.equal(applyPlacementReceipt(f.view,request,result),true);assert.deepEqual(f.placement.transform,before);near(operationAssemblyMatrix(f.view,'s'),next);
  assert.equal(f.setup.fixtureDesign.assembly.validity,scope==='Assembly'?'Current':'Stale');assert.equal(f.domain.fixtureUnits[0].design.assembly.validity,scope==='Assembly'?'Current':'Stale');
 }
});
test('display parents apply assembly once while leaving local pose values available to native playback',()=>{
 const f=fixture(),group=new THREE.Group();group.position.set(3,4,5);group.quaternion.setFromAxisAngle(new THREE.Vector3(1,0,0),.2);group.updateMatrix();const local=group.matrix.clone();
 applySetupDisplayFrame(f.view,group);near(group.matrix,f.assembly.clone().multiply(local));applySetupDisplayFrame(f.view,group);near(group.matrix,f.assembly.clone().multiply(local));assert.deepEqual(group.position.toArray(),[3,4,5]);
});
test('cut picking and focus follow the assembly but nearest-point requests retain authoritative workpiece coordinates',()=>{
 const f=fixture(),world=placementWorldMatrix(f.view,f.placement),center=new THREE.Vector3(5,0,0).applyMatrix4(world),camera=new THREE.OrthographicCamera(-50,50,50,-50,.1,1000);
 camera.position.copy(center).add(new THREE.Vector3(0,0,100));camera.lookAt(center);camera.updateMatrixWorld();f.view.fiveAxisArea='toolpath';selectTool(f.view,'select');
 Object.assign(f.view.viewport,{camera,perspectiveFov:45,getCameraState:()=>({radius:100}),setCameraState(value){this.cameraState=value;},renderer:{domElement:{getBoundingClientRect:()=>({x:0,y:0,left:0,top:0,width:1000,height:1000})}}});
 f.view.scene.intentToolpaths=[{id:'cut',placementId:'p',enabled:true,previewPoints:[[0,0,0],[10,0,0]]}];
 const picked=nearestCutLine(f.view,{clientX:500,clientY:500});assert.equal(picked.intentId,'cut');picked.nearPoint.forEach((n,i)=>assert.ok(Math.abs(n-[5,0,0][i])<1e-8));
 assert.equal(focusCuts(f.view,['cut']),true);const target=f.view.viewport.cameraState.target;near(new THREE.Matrix4().makeTranslation(target.x,target.y,target.z),new THREE.Matrix4().makeTranslation(...center.toArray()));
});
test('generated cutting and Setup rapid display use their respective local frames and neither alters CAD nor saved paths',()=>{
 const f=fixture(),points=[[0,0,0],[10,0,0]],directions=[[0,0,1],[0,0,1]];f.view.fiveAxisArea='toolpath';selectTool(f.view,'generate');
 f.view.scene.intentToolpaths=[{id:'a',placementId:'p',enabled:true},{id:'b',placementId:'p',enabled:true}];
 f.setup.generatedGeometry={status:'Current',revision:'1',operations:['a','b'].map((id,index)=>({intentId:id,placementId:'p',order:index+1,parts:[{role:'Cut',points,directions}],start:points[0]})),transitions:[{from:'a',to:'b',points:[[7,11,13],[17,11,13]],directions,geometryStatus:'Generated',frame:'Setup',referencePoint:'ToolTip',validation:'Unchecked'}]};
 Object.assign(f.view.viewport,{id:'viewport',scene:new THREE.Scene(),specificationLabels:[],setSpecificationAnnotations(rows){this.specificationLabels=rows.map(()=>({element:{dataset:{},style:{},classList:{add(){}},removeAttribute(){}}}));}});
 const before=structuredClone(f.domain);updateGeneratedPreview(f.view);const group=f.view.fiveGeneratedPreview.group,cut=group.children.find(o=>o.userData.generatedIntentId==='a'),rapid=group.children.find(o=>o.userData.generatedRapid);
 const point=object=>new THREE.Vector3().fromBufferAttribute(object.geometry.getAttribute('position'),0);
 assert.ok(point(cut).distanceTo(new THREE.Vector3(...points[0]).applyMatrix4(f.assembly.clone().multiply(f.local)))<1e-5);
 assert.ok(point(rapid).distanceTo(new THREE.Vector3(7,11,13).applyMatrix4(f.assembly))<1e-5);assert.deepEqual(f.domain,before);
});
