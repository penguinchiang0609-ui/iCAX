import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {activeTool} from './workspaceModel.mjs';
import {selectedFixture,fixtureDraft,fixtureCommand,fixtureTransform} from './fixtureActions.mjs';
import {placementSnapOptions} from './placementSnapping.mjs';
import {renderTransformTools,renderTransformValue,positionTransformHud} from './transformHud.mjs';
import {setupWorldFrame} from './setupWorldFrame.mjs';

function editable(view){
 const f=selectedFixture(view),setup=view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
 return view.fiveAxisArea==='workpiece'&&activeTool(view)==='fixture'&&f&&f.kind!=='Generated'&&!f.locked&&f.enabled!==false&&!view.fiveFixtureLibrary?.open&&!view.fixtureDesignPicking&&!view.fiveFixtureDesignOpen&&!setup?.locked&&setup?.enabled!==false;
}
function matrixValues(matrix){const e=new THREE.Euler().setFromRotationMatrix(matrix,'ZYX'),p=new THREE.Vector3().setFromMatrixPosition(matrix);return [...p.toArray(),e.x*180/Math.PI,e.y*180/Math.PI,e.z*180/Math.PI].map(n=>String(Number(n.toFixed(8))));}
export function renderFixturePlacement(view){
 if(!editable(view))return '';const d=fixtureDraft(view),mode=view.fiveFixtureMoveMode||'translate',busy=!!(view.pending||view.fixtureRequest),values=view.fiveFixtureGizmo?.matrix?matrixValues(view.fiveFixtureGizmo.matrix):['x','y','z','rx','ry','rz'].map(k=>d[k]);
 return renderTransformTools({owner:'fixture',title:'夹具变换',mode,busy,modes:[['translate','平移','five.fixture.move-mode:translate'],['rotate','旋转','five.fixture.move-mode:rotate']]})+renderTransformValue({owner:'fixture',mode,title:mode==='translate'?'装夹坐标 · 位置':'装夹坐标 · 空间角',fields:['X','Y','Z'].map((label,i)=>({label,name:(mode==='translate'?'':'r')+label.toLowerCase(),value:values[i+(mode==='rotate'?3:0)],aria:`浮动夹具${mode==='translate'?'位置':'空间角'} ${label}`})),attribute:'data-five-fixture-field',busy,error:view.error});
}

export function disposeFixturePlacement(view){
 const g=view?.fiveFixtureGizmo;if(!g)return;
 for(const id of g.partIds)g.viewport.clearTransformPreview(id);g.viewport.clearTransformManipulator();view.fiveFixtureGizmo=null;
}
export function syncFixturePlacement(context,view,ops){
 const vp=view.viewport,f=selectedFixture(view),setup=view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
 const allowed=vp&&editable(view)&&!view.pending&&!view.fixtureRequest;
 if(!allowed){disposeFixturePlacement(view);return;}
 if(view.fiveFixtureGizmo?.id!==f.id)disposeFixturePlacement(view);
 const g=view.fiveFixtureGizmo??={id:f.id,partIds:f.partIds,viewport:vp};
 const clear=()=>{for(const id of f.partIds)vp.clearTransformPreview(id);};
 let parent;try{parent=setupWorldFrame(view,setup.id);}catch(error){view.error=error.message;disposeFixturePlacement(view);return;}
 const d=fixtureDraft(view);let draftMatrix,valid=true;try{draftMatrix=new THREE.Matrix4().set(...fixtureTransform(d));}catch{valid=false;}
 const base=g.gesture?.domainBase||draftMatrix||new THREE.Matrix4().set(...f.transform),objects=f.partIds.map(id=>vp.sceneObjects.get(id)).filter(Boolean);if(!objects.length)return;
 if(!g.gesture){if(d.dirty&&valid){const delta=parent.clone().multiply(base).multiply(new THREE.Matrix4().set(...f.transform).invert());for(const id of f.partIds){const part=view.scene.domain.fixtures.find(p=>p.id===id);if(part)vp.setTransformPreview(id,delta.clone().multiply(new THREE.Matrix4().set(...part.transform)).elements);}}else clear();}
 const bounds=new THREE.Box3();for(const object of objects){object.geometry.computeBoundingBox();bounds.union(object.geometry.boundingBox.clone().applyMatrix4(object.matrixWorld));}const pivot=bounds.getCenter(new THREE.Vector3());
 const mode=view.fiveFixtureMoveMode||'translate';
 const worldBase=parent.clone().multiply(base);
 vp.setTransformManipulator({mode,space:view.fivePlacementSpace||'world',screenRotate:true,planeTranslate:mode==='translate',position:pivot.toArray(),quaternion:new THREE.Quaternion().setFromRotationMatrix(worldBase).toArray(),enabled:valid,
  onPosition:p=>positionTransformHud(context,p,'fixture'),
  snap:placementSnapOptions(vp,new THREE.Vector3().setFromMatrixPosition(worldBase).toArray()),
  onStart:({axis,direction})=>{const vector=axis==='E'||mode!=='rotate'?new THREE.Vector3(...direction):new THREE.Vector3(...({X:[1,0,0],Y:[0,1,0],Z:[0,0,1]})[axis]);if(axis!=='E'&&(view.fivePlacementSpace||'world')==='local')vector.applyQuaternion(new THREE.Quaternion().setFromRotationMatrix(worldBase));g.gesture={base:worldBase.clone(),domainBase:base.clone(),pivot:pivot.clone(),axis:vector.toArray()};context.actions?.refreshProjectHistoryControls?.();},
  onChange:({translation,angle})=>{
   if(!g.gesture)return;let matrix;
   if(mode==='translate')matrix=new THREE.Matrix4().makeTranslation(...translation).multiply(g.gesture.base);
   else{const p=g.gesture.pivot;matrix=new THREE.Matrix4().makeTranslation(p.x,p.y,p.z).multiply(new THREE.Matrix4().makeRotationAxis(new THREE.Vector3(...g.gesture.axis).normalize(),angle)).multiply(new THREE.Matrix4().makeTranslation(-p.x,-p.y,-p.z)).multiply(g.gesture.base);}
   g.matrix=parent.clone().invert().multiply(matrix);const delta=matrix.clone().multiply(new THREE.Matrix4().set(...f.transform).invert());for(const id of f.partIds){const part=view.scene.domain.fixtures.find(p=>p.id===id);vp.setTransformPreview(id,delta.clone().multiply(new THREE.Matrix4().set(...part.transform)).elements);}
   const values=matrixValues(g.matrix);for(const input of context.mount.querySelectorAll('.five-placement-hud[data-transform-owner="fixture"] input')){const i=['x','y','z','rx','ry','rz'].indexOf(input.dataset.fiveFixtureField);if(i>=0&&input!==context.mount.ownerDocument.activeElement)input.value=values[i];}
  },
  onEnd:({moved})=>{
   const matrix=g.matrix;delete g.gesture;delete g.matrix;
   if(!moved||!matrix){clear();context.actions?.refreshProjectHistoryControls?.();return;}
   const d=fixtureDraft(view),e=new THREE.Euler().setFromRotationMatrix(matrix,'ZYX'),position=new THREE.Vector3().setFromMatrixPosition(matrix);
   for(const [i,k] of ['x','y','z'].entries())d[k]=String(position.toArray()[i]);
   for(const [i,k] of ['rx','ry','rz'].entries())d[k]=String([e.x,e.y,e.z][i]*180/Math.PI);
   d.dirty=true;++d.revision;void fixtureCommand(context,view,'apply',ops).finally(()=>clear());
  },
  onCancel:()=>{delete g.gesture;delete g.matrix;clear();ops.renderProject(context,view);}
 });
}
