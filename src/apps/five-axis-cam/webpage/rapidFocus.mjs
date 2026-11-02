import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {rapidPreview} from './rapidPlanning.mjs';
import {focusShortcutAllowed,focusWorldBounds} from './viewFocus.mjs';

export function rapidFocusShortcut(event,view,mount){
  return focusShortcutAllowed(event,view,mount)&&view.fiveAxisArea==='toolpath'&&view.fiveCamPage==='rapid'&&!!(view.fiveRapid?.id||view.fiveRapidSelectedId);
}

export function focusRapid(view,id=view.fiveRapid?.id||view.fiveRapidSelectedId){
  const setup=view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId),geometry=rapidPreview(view,setup?.generatedGeometry);
  const transition=geometry?.transitions?.find(t=>`${t.from}/${t.to}`===id);if(!transition)return false;
  const operation=geometry.operations?.find(o=>o.intentId===transition.from),placement=view.scene.domain.placements?.find(p=>p.id===operation?.placementId);
  if(transition.frame!=='Setup'&&!placement)return false;
  const matrix=transition.frame==='Setup'?new THREE.Matrix4():new THREE.Matrix4().set(...placement.transform),box=new THREE.Box3();
  for(const point of transition.points||[])if(point.length===3&&point.every(Number.isFinite))box.expandByPoint(new THREE.Vector3(...point).applyMatrix4(matrix));
  return focusWorldBounds(view,box);
}
