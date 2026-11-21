import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {placementWorldMatrix} from './setupWorldFrame.mjs';
import { sceneColors } from './scenePalette.mjs';
import { workspaceState } from './workspaceModel.mjs';
import { addCutDirectionMarkers } from './cutDirectionMarker.mjs';
import {toolpathLayerColor,syncToolpathLayerMaterials} from './toolpathLayers.mjs';
import {pathPreview} from './pathPreviewTask.mjs';

const cuts=view=>(view.scene?.intentToolpaths||[]).filter(c=>c.enabled!==false);
const empty=Object.freeze([]);
// Selection and tool switches do not change geometry. Process/curve/pose edits,
// undo, reload and workpiece changes do; never retain an obsolete offset.
const scope=view=>JSON.stringify([view.scene?.domain?.expected?.epoch,cuts(view).map(c=>[c.id,c.processRef,c.curveRef,c.featureRef,c.poseRef,c.sourceState,c.reversed,c.kerfWidthMm]),
  view.scene?.model?.geometryRef]);
export function currentCompensation(view){
  return view.fiveCompensation?.scope===scope(view)?view.fiveCompensation.items:empty;
}
export function disposeCompensationOverlay(view){
  const group=view.fiveCompensationOverlay?.group;
  if(group){group.removeFromParent();group.traverse(n=>{n.geometry?.dispose();n.material?.dispose();});}
  view.fiveCompensationOverlay=null;
}
export function syncCompensation(context,view,ops){
  if(view.fiveAxisArea==='simulation'){disposeCompensationOverlay(view);return;}
  const key=scope(view);
  if(!view.pending&&view.fiveCompensation?.scope!==key){
    const state={scope:key,items:[],errors:{},loading:true};view.fiveCompensation=state;
    const candidates=cuts(view).filter(c=>!c.poseDeferred&&c.sourceState!=='Pending'),expected=structuredClone(view.scene?.domain?.expected);
    const valid=()=>!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveCompensation===state&&scope(view)===key;
    void (async()=>{
      try{
        const items=[];
        const widths=new Map(candidates.filter(c=>c.kerfWidthMm!=null).map(c=>[c.id,Number(c.kerfWidthMm)]));
        // Older runtimes do not project widths. Read policies first; a zero
        // kerf must never launch a geometric compensation preview.
        const unknown=candidates.filter(c=>!widths.has(c.id)).map(c=>c.id);
        for(let i=0;i<unknown.length;i+=256){
          if(!valid())return;
          const policies=await context.sceneProxy.invoke('CamProcess.Read',{expected,intentIds:unknown.slice(i,i+256)});
          for(const item of policies.items)widths.set(item.intentId,Number(item.assignment.kerf.widthMm));
        }
        const ids=candidates.filter(c=>widths.get(c.id)>0).map(c=>c.id);
        for(let i=0;i<ids.length;i+=256){
          if(!valid())return;
          const result=await pathPreview(context.sceneProxy,'Compensation',{expected,intentIds:ids.slice(i,i+256)},{valid});
          for(const item of result.items){if(item.error){state.errors[item.intentId]=item.error;ops.appendProjectLog(context,'error',`刀路 ${item.intentId} 刀补预览失败：${item.error}`);}if(item.widthMm>0&&item.parts?.some(p=>p.points?.length>1))items.push(item);}
        }
        if(valid())state.items=items;
      }catch(error){if(valid()){
        // A rejected command snapshot does not mean the geometric inputs
        // changed. Retry against the current sequence without expiring paths.
        if(/CAM_PREVIEW_CANCELLED|STALE|REVISION|Superseded/.test(error.message)&&JSON.stringify(expected)!==JSON.stringify(view.scene?.domain?.expected))state.scope=null;
        else if(error.message!=='CAM_PREVIEW_CANCELLED'){state.error=error.message;ops.appendProjectLog(context,'error',`刀补预览失败：${error.message}`);}
      }}
      finally{if(valid()){state.loading=false;ops.renderProject(context,view);}}
    })();
  }
  const items=currentCompensation(view),v=view.viewport,placements=view.scene?.domain?.placements||[];
  const selected=workspaceState(view).selectedIntentIds||[workspaceState(view).selectedIntentId];
  const overlayKey=JSON.stringify([key,view.fiveAxisArea,items,selected,placements.map(p=>[p.id,placementWorldMatrix(view,p).elements])]);
  if(view.fiveCompensationOverlay?.key===overlayKey&&view.fiveCompensationOverlay?.viewport===v){syncToolpathLayerMaterials(view,view.fiveCompensationOverlay.group);return;}
  disposeCompensationOverlay(view);if(!v||!items.length)return;
  const root=new THREE.Group();root.name='five-compensated-paths';
  for(const item of items){
    const placement=placements.find(p=>p.id===item.placementId);if(!placement)continue;
    const group=new THREE.Group(),source=v.sceneObjects?.get(placement.id);if(source){source.updateWorldMatrix(true,false);group.matrix.copy(source.matrixWorld);}else group.matrix.copy(placementWorldMatrix(view,placement));group.matrixAutoUpdate=false;root.add(group);
    for(const part of item.parts||[]){
      if(!part.points?.length)continue;
      const color=toolpathLayerColor(view,item.intentId);
      const geometry=new THREE.BufferGeometry().setFromPoints(part.points.map(p=>new THREE.Vector3(...p)));
      const line=new THREE.Line(geometry,new THREE.LineBasicMaterial({color,depthTest:false,depthWrite:false}));
      line.renderOrder=42;line.raycast=()=>{};line.userData={compensatedIntentId:item.intentId,role:part.role};group.add(line);
      if(part.role==='Cut'&&view.fiveAxisArea==='toolpath')addCutDirectionMarkers(group,part.points,item.intentId,color);
    }
  }
  v.scene.add(root);view.fiveCompensationOverlay={key:overlayKey,viewport:v,group:root};
  syncToolpathLayerMaterials(view,root);
}
