import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {activeTool,workspaceState} from './workspaceModel.mjs';
import {pathEditCommand} from './pathEditing.mjs';
import {toolpathLayerVisible} from './toolpathLayers.mjs';
import {renderTransformTools,positionTransformHud} from './transformHud.mjs';
import {placementWorldMatrix} from './setupWorldFrame.mjs';

const modes=['translate','rotate'];
const rowMajor=m=>m.clone().transpose().toArray();
const format=n=>String(Number(n.toFixed(8)));
function selection(view){
  const ids=workspaceState(view).selectedIntentIds||[workspaceState(view).selectedIntentId].filter(Boolean);
  const cuts=ids.map(id=>view.scene?.intentToolpaths?.find(c=>c.id===id));
  if(!cuts.length||cuts.some(c=>!c||c.locked||c.enabled===false||!toolpathLayerVisible(view,c.id)))return null;
  const placement=view.scene.domain.placements.find(p=>p.id===cuts[0].placementId);
  const setup=view.scene.domain.setups.find(s=>s.id===placement?.setupId);
  if(!placement||setup?.locked||setup?.enabled===false||cuts.some(c=>c.placementId!==placement.id))return null;
  return {ids,cuts,placement};
}
const allowed=view=>view.fiveAxisArea==='toolpath'&&modes.some(m=>activeTool(view)==='path-'+m)&&!!selection(view);
export function renderPathTransform(view){
  if(!allowed(view))return '';
  return renderTransformTools({owner:'path',title:'刀路变换',mode:activeTool(view).slice(5),busy:!!(view.pending||view.fivePathEdit?.request),modes:modes.map((m,i)=>[m,['平移','旋转'][i],'five.tool.path-'+m])});
}
function restore(g){
  for(const s of g?.objects||[]){s.object.matrix.copy(s.matrix);s.object.matrixAutoUpdate=s.auto;s.object.updateMatrixWorld(true);}
  for(const s of g?.markers||[]){s.attribute.array.set(s.points);s.attribute.needsUpdate=true;s.object.geometry.computeBoundingSphere();}
  for(const s of g?.labels||[]){s.label.worldPoint.copy(s.point);s.label.anchorPoint.copy(s.anchor);}
  if(g){g.objects=[];g.markers=[];g.labels=[];}
}
export function disposePathTransform(view){
  const g=view.fivePathTransform;if(!g)return;restore(g);
  if(g.gesture)Object.assign(g.draft,g.gesture.saved);
  if(g.viewport.transformManipulator?.options?.owner==='path')g.viewport.clearTransformManipulator();
  view.fivePathTransform=null;
}
// Transform display overlays only. Never preview through a CAD scene object.
// Conjugating through each overlay's parent also covers rotated placements.
function preview(view,g,delta){
  restore(g);const ids=new Set(g.ids);
  const roots=[view.fiveCutPreview?.group,view.fiveCompensationOverlay?.group,view.fiveLeadOverlay?.group,view.fiveToolAxisDisplay?.group];
  const visit=object=>{
    const data=object.userData,id=data.cuttingLineId||data.cutDirectionId||data.compensatedIntentId||data.toolpathLayerId||data.intentId;
    if(ids.has(id)){
      object.updateWorldMatrix(true,false);const parent=object.parent.matrixWorld;
      g.objects.push({object,matrix:object.matrix.clone(),auto:object.matrixAutoUpdate});
      object.matrix.premultiply(parent.clone().invert().multiply(delta).multiply(parent));object.matrixAutoUpdate=false;object.updateMatrixWorld(true);return;
    }
    if(data.pathMarkerIds){
      const attribute=object.geometry.getAttribute('position');object.updateWorldMatrix(true,false);
      const local=object.matrixWorld.clone().invert().multiply(delta).multiply(object.matrixWorld);
      g.markers.push({object,attribute,points:attribute.array.slice()});
      for(let i=0;i<data.pathMarkerIds.length;i++)if(ids.has(data.pathMarkerIds[i])){
        const p=new THREE.Vector3().fromBufferAttribute(attribute,i).applyMatrix4(local);attribute.setXYZ(i,p.x,p.y,p.z);
      }
      attribute.needsUpdate=true;object.geometry.computeBoundingSphere();
    }
    for(const child of object.children)visit(child);
  };
  for(const root of roots)if(root)visit(root);
  for(const label of g.viewport.specificationLabels)if(ids.has(label.element.dataset.fiveIntentId)){
    g.labels.push({label,point:label.worldPoint.clone(),anchor:label.anchorPoint.clone()});label.worldPoint.applyMatrix4(delta);label.anchorPoint.applyMatrix4(delta);
  }
}
function frame(view,placement){
  const source=view.viewport.sceneObjects.get(placement.id);
  if(source){source.updateWorldMatrix(true,false);return source.matrixWorld.clone();}
  return placementWorldMatrix(view,placement);
}
function boundsCenter(cuts){
  const box=new THREE.Box3();for(const c of cuts)for(const p of c.previewPoints||[])box.expandByPoint(new THREE.Vector3(...p));
  return box.isEmpty()?null:box.getCenter(new THREE.Vector3());
}
export function syncPathTransform(context,view,ops){
  const vp=view.viewport,d=view.fivePathEdit,s=vp&&allowed(view)&&selection(view);
  if(!s||!d||!modes.includes(d.mode)||view.pending||d.request){disposePathTransform(view);return;}
  const key=JSON.stringify([vp.id,d.mode,s.ids,view.scene.domain.expected]);
  if(view.fivePathTransform?.key!==key)disposePathTransform(view);
  const g=view.fivePathTransform??={key,viewport:vp,draft:d,ids:s.ids,objects:[],markers:[],labels:[]};
  const base=frame(view,s.placement),inverse=base.clone().invert(),center=boundsCenter(s.cuts);if(!center){disposePathTransform(view);return;}
  const pivot=d.centerEdited?new THREE.Vector3(...['cx','cy','cz'].map(k=>Number(d[k]))):center;
  const delta=g.gesture?.delta||new THREE.Matrix4();
  if(g.gesture)preview(view,g,delta);else restore(g);
  const worldPivot=pivot.clone().applyMatrix4(base),shownPivot=g.gesture?.pivot||worldPivot.clone().applyMatrix4(delta);
  const space=vp.transformSpace||'world',quaternion=new THREE.Quaternion().setFromRotationMatrix(base);
  vp.setTransformManipulator({owner:'path',mode:d.mode,space,planeTranslate:d.mode==='translate',position:shownPivot.toArray(),quaternion:quaternion.toArray(),
    onPosition:p=>positionTransformHud(context,p,'path'),
    onStart:({axis})=>{
      if(view.pending||view.fivePathEdit!==d||context.isCurrentProject?.()===false)return false;
      const saved=Object.fromEntries(['x','y','z','cx','cy','cz','axis','angle','centerEdited'].map(k=>[k,d[k]]));
      const direction=new THREE.Vector3(...({X:[1,0,0],Y:[0,1,0],Z:[0,0,1]})[axis]||[0,0,1]);if(space==='local')direction.applyQuaternion(quaternion);
      g.gesture={saved,pivot:shownPivot.clone(),baseDelta:delta.clone(),direction,axis,delta:delta.clone()};view.error='';
      context.actions?.refreshProjectHistoryControls?.();return true;
    },
    onChange:({translation,angle})=>{
      const gesture=g.gesture;if(!gesture)return;let change;
      if(d.mode==='translate')change=new THREE.Matrix4().makeTranslation(...translation);
      else{
        const p=gesture.pivot;
        change=new THREE.Matrix4().makeTranslation(p.x,p.y,p.z).multiply(new THREE.Matrix4().makeRotationAxis(gesture.direction,angle)).multiply(new THREE.Matrix4().makeTranslation(-p.x,-p.y,-p.z));
      }
      gesture.delta=change.multiply(gesture.baseDelta);g.matrix=inverse.clone().multiply(gesture.delta).multiply(base);
      preview(view,g,gesture.delta);
      if(d.mode==='translate'){
        const offset=new THREE.Vector3().setFromMatrixPosition(g.matrix);['x','y','z'].forEach((k,i)=>d[k]=format(offset.getComponent(i)));
      }else{
        const p=gesture.pivot.clone().applyMatrix4(inverse);['cx','cy','cz'].forEach((k,i)=>d[k]=format(p.getComponent(i)));d.centerEdited=true;
        d.axis=gesture.axis;d.angle=format(angle*180/Math.PI);
      }
      for(const input of context.mount.querySelectorAll('[data-five-path-field]'))input.value=d[input.dataset.fivePathField];
    },
    onEnd:({moved})=>{
      const gesture=g.gesture,matrix=g.matrix;delete g.gesture;delete g.matrix;
      if(!moved||!matrix){if(gesture)Object.assign(d,gesture.saved);ops.renderProject(context,view);return;}
      restore(g);d.request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),intentIds:[...g.ids],matrix:rowMajor(matrix)};
      void pathEditCommand(context,view,'apply',ops);
    },
    onCancel:()=>{if(g.gesture)Object.assign(d,g.gesture.saved);delete g.gesture;delete g.matrix;restore(g);ops.renderProject(context,view);},
  });
}
