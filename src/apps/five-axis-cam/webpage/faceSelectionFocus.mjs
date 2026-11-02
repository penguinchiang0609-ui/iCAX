import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {activeTool,currentDraft} from './workspaceModel.mjs';
import {focusShortcutAllowed,focusWorldBounds} from './viewFocus.mjs';

export function faceFocusShortcut(event,view,mount){
  if(!focusShortcutAllowed(event,view,mount)||event.target.closest?.('.five-cut-tree'))return null;
  if(view.fiveAxisArea==='toolpath'&&view.fiveAxisExtractionOpen&&['extract','manual-extract'].includes(activeTool(view))){
    const state=currentDraft(view,'extract').extraction;return state?.selectedFace!=null?`five.extraction.focus-face.${state.selectedFace}`:null;
  }
  return view.fiveAxisArea==='workpiece'&&activeTool(view)==='fixture'&&view.fiveFixtureDesignOpen&&view.fixtureDesignSelectedFace!=null?`five.fixture.design.focus-face:${view.fixtureDesignSelectedFace}`:null;
}

export function focusSelectedFace(view,source,ranges){
  const vp=view.viewport,g=source?.geometry,p=g?.attributes.position,index=g?.index;
  if(!vp||!p||!ranges?.length)return false;
  source.updateWorldMatrix(true,false);
  const box=new THREE.Box3(),point=new THREE.Vector3();
  for(const range of ranges)for(let t=+range.first;t<+range.first+ +range.count;t++){
    for(let j=0;j<3;j++)box.expandByPoint(point.fromBufferAttribute(p,index?index.getX(t*3+j):t*3+j).applyMatrix4(source.matrixWorld));
  }
  return focusWorldBounds(view,box);
}

export function attachFaceListFocus(mount,run){
  const doubleClick=event=>{const button=event.target.closest?.('[data-five-face-focus]');if(!button||button.disabled)return;event.preventDefault();run(button.dataset.fiveFaceFocus);};
  mount.addEventListener('dblclick',doubleClick);return ()=>mount.removeEventListener('dblclick',doubleClick);
}
