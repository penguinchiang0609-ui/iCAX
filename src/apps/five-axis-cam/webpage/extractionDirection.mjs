import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {activeTool} from './workspaceModel.mjs';
import {selectedPlacement} from './registrationActions.mjs';
import {extractionDraft,extractionObservationDirection} from './extractionActions.mjs';

export function disposeExtractionDirection(view){
  const arrow=view.fiveExtractionDirection?.arrow;arrow?.removeFromParent();
  arrow?.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});view.fiveExtractionDirection=null;
}
export function syncExtractionDirection(view){
  const vp=view.viewport,menu=vp?.renderer?.domElement?.closest('.five-workspace')?.querySelector('.five-extraction-direction-menu[open]');
  if(!menu||!view.fiveAxisExtractionOpen||view.fiveAxisArea!=='toolpath'||!['extract','manual-extract'].includes(activeTool(view))||extractionDraft(view).extraction.mode!=='Faces'){disposeExtractionDirection(view);return;}
  const source=vp.sceneObjects.get(selectedPlacement(view)?.id);let direction;
  try{direction=extractionObservationDirection(view);}catch{disposeExtractionDirection(view);return;}
  if(!source?.geometry){disposeExtractionDirection(view);return;}
  source.updateWorldMatrix(true,false);source.geometry.computeBoundingBox();
  const key=JSON.stringify([source.geometry.uuid,source.matrixWorld.elements,direction]);
  if(view.fiveExtractionDirection?.key===key&&view.fiveExtractionDirection.viewport===vp)return;
  disposeExtractionDirection(view);
  const box=source.geometry.boundingBox.clone().applyMatrix4(source.matrixWorld),extent=Math.max(box.getSize(new THREE.Vector3()).length(),1);
  const axis=new THREE.Vector3(...direction).transformDirection(source.matrixWorld),origin=box.getCenter(new THREE.Vector3()).addScaledVector(axis,-extent*.3),length=extent*.24;
  const arrow=new THREE.ArrowHelper(axis,origin,length,0xffc65c,length*.16,length*.08);arrow.name='cut-extraction-observation-direction';
  arrow.traverse(o=>{if(o.geometry)o.geometry=o.geometry.clone();if(o.material){o.material.depthTest=false;o.material.depthWrite=false;}o.renderOrder=80;o.raycast=()=>{};});
  vp.scene.add(arrow);view.fiveExtractionDirection={key,arrow,viewport:vp};
}
