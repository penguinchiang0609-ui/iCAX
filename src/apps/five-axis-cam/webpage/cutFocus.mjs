import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {workspaceState} from './workspaceModel.mjs';
import {buildCutTree} from './cutTreeModel.mjs';
import {focusShortcutAllowed,focusWorldBounds} from './viewFocus.mjs';

export function cutFocusShortcut(event,view,mount){
  return focusShortcutAllowed(event,view,mount)&&view.fiveAxisArea==='toolpath'&&view.fiveCamPage!=='rapid'&&(workspaceState(view).selectedIntentIds?.length||workspaceState(view).selectedIntentId);
}

export function focusCuts(view,ids){
  const selected=new Set(ids),box=new THREE.Box3();
  for(const cut of view.scene?.intentToolpaths||[]){
    if(!selected.has(cut.id))continue;
    const placement=view.scene.domain?.placements?.find(p=>p.id===cut.placementId);if(!placement)continue;
    const matrix=new THREE.Matrix4().set(...placement.transform);
    for(const point of cut.previewPoints||[])if(point.length===3&&point.every(Number.isFinite))box.expandByPoint(new THREE.Vector3(...point).applyMatrix4(matrix));
  }
  return focusWorldBounds(view,box);
}

export function focusSelectedCuts(view){
  const ui=workspaceState(view);return focusCuts(view,ui.selectedIntentIds?.length?ui.selectedIntentIds:[ui.selectedIntentId]);
}

export function focusCutNode(view,key){
  const node=buildCutTree(view).nodes.get(key);return node?focusCuts(view,node.leafIds):false;
}
