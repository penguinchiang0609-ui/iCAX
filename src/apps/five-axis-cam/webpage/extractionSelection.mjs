import { extractionDraft, extractionChanged } from './extractionActions.mjs';
import { selectedPlacement } from './registrationActions.mjs';
import { activeTool } from './workspaceModel.mjs';
import { meshFacesMatchingRectangle } from '../../../iCAX-UI/SDK/Viewport/rectangleSelection.mjs';
import {captureFaceOccluders,visibleRectangleFaces} from '../../../iCAX-UI/SDK/Viewport/faceRectangleVisibility.mjs';
import {revealFaceListSelection} from './listNavigation.mjs';

export function extractionRectangleSelectionKey(context,view) {
  if(view.disposed||context.isCurrentProject?.()===false||view.pending||view.fiveAxisArea!=='toolpath'
    ||extractionDraft(view).extraction.mode==='Edges'||!view.fiveAxisExtractionOpen||!view.fiveAxisExtractionPick||!['extract','manual-extract'].includes(activeTool(view)))return null;
  return JSON.stringify([view.scene.domain.expected,selectedPlacement(view)?.id,extractionDraft(view).extraction.revision]);
}

export async function handleExtractionRectangleSelect(context,view,rectangle,event,ops) {
  const key=extractionRectangleSelectionKey(context,view);if(key==null)return;
  const state=extractionDraft(view).extraction,placement=selectedPlacement(view),viewport=view.viewport;
  let mesh;viewport.sceneObjects.get(placement.id)?.traverse(o=>{if(o.isMesh&&!mesh)mesh=o;});
  if(!mesh)return;
  viewport.scene.updateMatrixWorld(true);viewport.camera.updateMatrixWorld(true);
  // Resolve topology asynchronously, but keep the geometry and projection from
  // the completed gesture if the user navigates while the lookup is in flight.
  const geometry=mesh.geometry,world=mesh.matrixWorld.clone(),camera=viewport.camera.clone(),bounds=viewport.renderer.domElement.getBoundingClientRect();
  const through=!!state.throughSelection,occluders=through?[]:captureFaceOccluders(viewport,mesh);
  const current=()=>!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveAxisExtractionPick===state
    &&JSON.stringify([view.scene.domain.expected,selectedPlacement(view)?.id,state.revision])===key;
  let revealId=null;
  view.pending=true;view.error='';ops.renderProject(context,view);
  try {
    const payload={expected:view.scene.domain.expected,setupId:placement.setupId,placementId:placement.id};
    const sources=await context.sceneProxy.invoke('CamCut.QuerySources',payload);
    const mapped=await context.sceneProxy.invoke('CamPlacement.ReadReferenceFaces',payload);
    if(!current())return;
    const ranges=new Map();
    for(const range of mapped.faces){const id=String(range.faceId);if(!ranges.has(id))ranges.set(id,[]);ranges.get(id).push(range);}
    const faces=sources.faceUses.map(faceUse=>({faceUse,triangleRanges:ranges.get(String(faceUse.localId))||[]}));
    let matches=meshFacesMatchingRectangle(geometry,faces,world,camera,bounds,rectangle);
    if(!through)matches=visibleRectangleFaces(viewport.renderer,geometry,matches,world,camera,bounds,rectangle,occluders);
    const selected=new Map((event.ctrlKey||event.metaKey||event.shiftKey||event.altKey?state.faces:[]).map(f=>[f.localId,f]));
    for(const {faceUse} of matches){if(event.altKey)selected.delete(faceUse.localId);else selected.set(faceUse.localId,faceUse);}
    state.faces=[...selected.values()];state.faceRanges=Object.fromEntries(faces.filter(f=>selected.has(f.faceUse.localId)).map(f=>[f.faceUse.localId,f.triangleRanges]));
    state.selectedFace=matches.find(f=>selected.has(f.faceUse.localId))?.faceUse.localId??state.selectedFace;
    extractionChanged(view);
    revealId=state.selectedFace;
  }catch(error){if(current())view.error=error.message;}
  finally{view.pending=false;if(!view.disposed&&context.isCurrentProject?.()!==false){ops.renderProject(context,view);if(view.fiveAxisExtractionPick===state&&state.selectedFace===revealId)revealFaceListSelection(context.mount,'extraction-faces',revealId);}}
}
