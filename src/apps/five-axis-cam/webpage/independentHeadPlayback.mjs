import {loadCuttingHeadPreview} from './cuttingHeadPreview.mjs';

function dispose(group){group?.removeFromParent();group?.traverse(o=>{o.geometry?.dispose();for(const m of Array.isArray(o.material)?o.material:[o.material])m?.dispose();});}
export function clearIndependentHead(view){const s=view.fiveMachine;if(!s)return;s.headToken=(s.headToken||0)+1;dispose(s.headGroup);s.headGroup=null;s.headKey=null;}
export function syncIndependentHead(context,view,s,head,onReady){
 const key=JSON.stringify([head?.instance?.id,head?.previewRef,head?.assembly?.nodes.map(n=>n.previewRef)]);
 if(s.headKey===key)return;s.headKey=key;const token=s.headToken=(s.headToken||0)+1;dispose(s.headGroup);s.headGroup=null;
 const current=()=>!view.disposed&&context.isCurrentProject?.()!==false&&token===s.headToken;
 void loadCuttingHeadPreview(context.sceneProxy.resources,head,current).then(result=>{
  if(!current()){dispose(result?.group);return;}if(!result)return;
  s.headGroup=result.group;s.headGroup.name='cam-independent-head';s.headGroup.traverse(o=>{o.raycast=()=>{};});view.viewport.scene.add(s.headGroup);onReady();
 }).catch(error=>{if(current()){s.error=error.message;s.headKey=null;onReady();}});
}
export function paintIndependentHead(s,snapshot,world){
 const group=s.headGroup;if(!group)return;
 group.visible=s.camShowHead!==false;
 const indices=snapshot.headNodes||[];
 if(group.userData.assembly)group.children.forEach((part,i)=>{if(world[indices[i]]){part.matrix.copy(world[indices[i]]);part.matrixWorldNeedsUpdate=true;}});
 else if(world[indices[0]]){group.matrixAutoUpdate=false;group.matrix.copy(world[indices[0]]);group.matrixWorldNeedsUpdate=true;}
 group.updateMatrixWorld(true);
}
