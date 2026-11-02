import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {activeTool} from './workspaceModel.mjs';
import {focusShortcutAllowed,focusWorldBounds} from './viewFocus.mjs';

export function fixturePartFocusShortcut(event,view,mount){
  return focusShortcutAllowed(event,view,mount)&&view.fiveAxisArea==='workpiece'&&activeTool(view)==='fixture'
    &&!view.fiveFixtureDesignOpen&&!view.fiveFixturePlate?.open&&!view.fiveFixtureBreakdown&&!!view.fiveFixtureSelectedPart;
}
export function focusFixturePart(view,partId=view.fiveFixtureSelectedPart){
  const part=view.scene?.domain?.fixtures?.find(p=>p.id===partId),source=view.viewport?.sceneObjects?.get(partId);
  if(!part||part.enabled===false||part.visible===false||!source)return false;
  source.updateWorldMatrix(true,true);
  return focusWorldBounds(view,new THREE.Box3().setFromObject(source));
}
