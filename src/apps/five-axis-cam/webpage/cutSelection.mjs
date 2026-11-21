import {camPathKind} from './workspaceModel.mjs';
import { projectWorkpiece } from './domainActions.mjs';
import { orderedCuts, cutTreeBusy } from './cutTree.mjs';
import { buildCutTree } from './cutTreeModel.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {placementWorldMatrix} from './setupWorldFrame.mjs';
import { workspaceState, activeTool } from './workspaceModel.mjs';
import { polylineMatchesRectangle } from '../../../iCAX-UI/SDK/Viewport/rectangleSelection.mjs';
import { loadCutDraft } from './cutEditing.mjs';
import { loadProcessDraft } from './processEditing.mjs';
import { applyLeadToCut } from './cutLeads.mjs';
import {toolpathLayerVisible} from './toolpathLayers.mjs';
import {revealSceneCut} from './sceneListReveal.mjs';

export function selectCut(view,id,event={}) {
  const state=workspaceState(view),items=orderedCuts(view).filter(c=>!c.locked),ids=items.map(c=>c.id);
  if(!ids.includes(id))return;
  let selected=new Set((state.selectedIntentIds||[state.selectedIntentId]).filter(value=>ids.includes(value)));
  if(event.shiftKey&&ids.includes(state.cutSelectionAnchor)){
    const ends=[ids.indexOf(state.cutSelectionAnchor),ids.indexOf(id)].sort((a,b)=>a-b);
    const range=ids.slice(ends[0],ends[1]+1),remove=range.every(value=>selected.has(value));
    for(const value of range){if(remove)selected.delete(value);else selected.add(value);}
  }else if(event.ctrlKey||event.metaKey){if(selected.has(id))selected.delete(id);else selected.add(id);state.cutSelectionAnchor=id;}
  else {selected=new Set([id]);state.cutSelectionAnchor=id;}
  state.selectedIntentIds=[...selected];state.selectedIntentId=selected.has(id)?id:state.selectedIntentIds.at(-1)||'';
  view.fivePreferredCutSelection=[...state.selectedIntentIds];
  const selectedCut=items.find(c=>c.id===state.selectedIntentId);
  if(selectedCut){view.fiveActivePlacementId=selectedCut.placementId;projectWorkpiece(view.scene,selectedCut.placementId);}
  const tree=buildCutTree(view);
  state.selectedGroupIds=(event.ctrlKey||event.metaKey||event.shiftKey?state.selectedGroupIds||[]:[]).filter(g=>tree.nodes.get(`g:${g}`)?.leafIds.every(id=>selected.has(id)));
  state.selectedGroupId=state.selectedGroupIds.length===1?state.selectedGroupIds[0]:'';
}
export function nearestCutLine(view,event) {
  const v=view.viewport,box=v.renderer.domElement.getBoundingClientRect();let best=8,chosen=null;
  for(const cut of view.scene.intentToolpaths||[]){
    if(cut.locked||cut.enabled===false||!toolpathLayerVisible(view,cut.id))continue;
    const placement=view.scene.domain?.placements.find(p=>p.id===cut.placementId);if(!placement)continue;
    const matrix=placementWorldMatrix(view,placement);
    const points=(cut.previewPoints||[]).map(p=>{
      const q=new THREE.Vector3(...p).applyMatrix4(matrix).project(v.camera);
      return [box.x+(q.x+1)*box.width/2,box.y+(1-q.y)*box.height/2,q.z];
    });
    for(let i=1;i<points.length;++i){
      const a=points[i-1],b=points[i],dx=b[0]-a[0],dy=b[1]-a[1];
      if(a[2]<-1||a[2]>1||b[2]<-1||b[2]>1)continue;
      const t=Math.max(0,Math.min(1,((event.clientX-a[0])*dx+(event.clientY-a[1])*dy)/(dx*dx+dy*dy||1)));
      const distance=Math.hypot(event.clientX-a[0]-t*dx,event.clientY-a[1]-t*dy);
      if(distance<best){
        const from=new THREE.Vector3(...cut.previewPoints[i-1]),to=new THREE.Vector3(...cut.previewPoints[i]);
        const depth=p=>-p.clone().applyMatrix4(matrix).applyMatrix4(v.camera.matrixWorldInverse).z;
        const perspectiveT=v.camera.isPerspectiveCamera?t*depth(from)/((1-t)*depth(to)+t*depth(from)):t;
        const near=from.lerp(to,perspectiveT),world=near.clone().applyMatrix4(matrix),ndc=world.clone().project(v.camera);
        const offset=ndc.clone();offset.x+=16/box.width;
        const maximumDistanceMm=Math.min(100,Math.max(.15,offset.unproject(v.camera).distanceTo(world)));
        best=distance;chosen={intentId:cut.id,nearPoint:near.toArray(),maximumDistanceMm};
      }
    }
  }
  return chosen;
}
export function handleCutLinePick(context,view,_data,_hit,event,_hits,ops) {
  if(view.fiveAxisArea!=='toolpath'||camPathKind(view)==='rapid'||view.fiveToolAxisRequest||view.fiveReverseRequest||view.fiveClear?.request||view.fiveAxisExtractionPick||view.fiveStart?.request||view.fiveHole?.request||view.fiveLeads?.request||view.fiveLeads?.saving||view.pending||!event)return false;
  if(view.fiveAxisPointEdit?.dirty){view.fiveAxisPointEdit.error='请先应用或取消当前关键点修改。';ops.renderProject(context,view);return true;}
  const chosen=nearestCutLine(view,event);
  if(!chosen){
    if(event.ctrlKey||event.metaKey||event.shiftKey)return true;
    const state=workspaceState(view);
    state.selectedIntentIds=[];state.selectedIntentId='';state.cutSelectionAnchor='';state.selectedGroupId='';state.selectedGroupIds=[];
    ops.renderProject(context,view);
    // Let the shared viewport clear its object selection on a blank hit too.
    return false;
  }
  selectCut(view,chosen.intentId,event);ops.renderProject(context,view);revealSceneCut(context,view,ops);
  if(activeTool(view)==='leads'&&!view.fiveAxisLeadsOpen){void applyLeadToCut(context,view,chosen.intentId,ops);return true;}
  void (async()=>{await loadProcessDraft(context,view,ops);await loadCutDraft(context,view,ops);})();return true;
}

export function cutRectangleSelectionKey(context,view) {
  if(view.fivePoseSelection?.active||view.disposed||context.isCurrentProject?.()===false||view.fiveAxisArea!=='toolpath'||camPathKind(view)==='rapid'
    ||cutTreeBusy(view)||view.fiveTreeRequest||view.fiveAxisStartOpen||view.fiveAxisSplitOpen
    ||['rapid','simulation','leads'].includes(activeTool(view))||!view.scene?.intentToolpaths?.length)return null;
  return JSON.stringify([view.scene.domain?.expected,activeTool(view),view.fiveCutPreview?.group.uuid,view.fiveGeneratedPreview?.group.uuid]);
}

export function handleCutRectangleSelect(context,view,rectangle,event,ops) {
  if(cutRectangleSelectionKey(context,view)==null)return;
  const viewport=view.viewport, groups=[view.fiveCutPreview?.group,view.fiveGeneratedPreview?.group];
  const state=workspaceState(view), cuts=orderedCuts(view).filter(c=>!c.locked&&toolpathLayerVisible(view,c.id)), known=new Set(cuts.map(c=>c.id)), matches=new Map();
  viewport.scene.updateMatrixWorld(true);viewport.camera.updateMatrixWorld(true);
  const bounds=viewport.renderer.domElement.getBoundingClientRect();
  for(const group of groups)if(group?.parent&&group.visible)for(const line of group.children){
    const id=line.userData.cuttingLineId||line.userData.generatedIntentId;
    if(!line.isLine||!line.visible||!known.has(id))continue;
    const positions=line.geometry.getAttribute('position'), points=[];
    for(let i=0;i<positions.count;++i)points.push([positions.getX(i),positions.getY(i),positions.getZ(i)]);
    const hit=polylineMatchesRectangle(points,line.matrixWorld,viewport.camera,bounds,rectangle);
    matches.set(id,rectangle.mode==='contain'?(matches.get(id)??true)&&hit:(matches.get(id)??false)||hit);
  }
  const selected=new Set(event.ctrlKey||event.metaKey||event.shiftKey?(state.selectedIntentIds||[state.selectedIntentId]):[]);
  for(const [id,hit] of matches)if(hit)selected.add(id);
  state.selectedIntentIds=cuts.filter(c=>selected.has(c.id)).map(c=>c.id);
  state.selectedIntentId=state.selectedIntentIds.includes(state.selectedIntentId)?state.selectedIntentId:state.selectedIntentIds.at(-1)||'';
  state.cutSelectionAnchor=state.selectedIntentId;state.selectedGroupId='';state.selectedGroupIds=[];
  ops.renderProject(context,view);
  revealSceneCut(context,view,ops);
  void (async()=>{await loadProcessDraft(context,view,ops);await loadCutDraft(context,view,ops);})();
}
