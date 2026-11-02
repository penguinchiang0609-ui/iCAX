import { currentLeadPreview } from './cutLeads.mjs';
import { currentCompensation } from './cutCompensation.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { extractionResult, extractionDraft } from './extractionActions.mjs';
import { activeTool, workspaceState } from './workspaceModel.mjs';
import { selectedPlacement } from './registrationActions.mjs';
import { buildCutTree, cutSequenceNumbers } from './cutTreeModel.mjs';
import { showsGeneratedPreview } from './toolpathGeneration.mjs';
import { addCutDirectionMarkers } from './cutDirectionMarker.mjs';
import { sceneColors, sceneColorCss } from './scenePalette.mjs';
import {toolpathLayerColor,toolpathLayerVisible,syncToolpathLayerMaterials} from './toolpathLayers.mjs';
import {selectedFaceOverlay} from './faceSelectionPresentation.mjs';
import {machineViewportActive} from './machinePlaybackContext.mjs';

export function disposeCutPreview(view) {
  const group=view.fiveCutPreview?.group;
  if(group){group.removeFromParent();group.traverse(item=>{item.geometry?.dispose();item.material?.dispose();});}
  view.fiveCutPreview=null;
}
export function updateCutPreview(view) {
  if(machineViewportActive(view)){
    disposeCutPreview(view);
    if(view.fiveCutAnnotations){view.viewport?.setSpecificationAnnotations([]);view.fiveCutAnnotations=false;}
    return;
  }
  const viewport=view.viewport,placement=selectedPlacement(view);
  if(!viewport||!placement)return;
  const source=viewport.sceneObjects.get(placement.id);
  const saved=(view.scene.intentToolpaths||[]).filter(c=>toolpathLayerVisible(view,c.id)),result=view.fiveAxisExtractionOpen&&['extract','manual-extract'].includes(activeTool(view))?extractionResult(view):null;
  const existing=new Set(saved.map(c=>c.sourceKey));
  const leadPreview=currentLeadPreview(view);
  const compensation=new Map(currentCompensation(view).map(c=>[c.intentId,c])),compensated=new Set(compensation.keys());
  const placements=view.scene.domain?.placements||[],inverse=new THREE.Matrix4().set(...placement.transform).invert();
  const relativePoint=(cut,point)=>{
    const source=placements.find(p=>p.id===cut.placementId);
    return source&&source.id!==placement.id?new THREE.Vector3(...point).applyMatrix4(new THREE.Matrix4().set(...source.transform)).applyMatrix4(inverse).toArray():point;
  };
  const showingGenerated=showsGeneratedPreview(view);
  const lines=[...saved.filter(c=>c.enabled!==false&&(!showingGenerated||compensated.has(c.id))).map(c=>{const original=compensated.has(c.id),preview=original?null:leadPreview?.items.find(i=>i.intentId===c.id&&i.cutPoints?.length);return {id:c.id,original,enabled:c.enabled,machiningKind:c.machiningKind,points:(compensation.get(c.id)?.nominalPoints||preview?.cutPoints||c.previewPoints)?.map(p=>relativePoint(c,p)),reversed:preview?false:c.reversed};}),...(result?.candidates||[]).filter(c=>!existing.has(c.sourceKey)&&c.confirmationEligibility==='Ready').map(c=>({id:c.candidateId,enabled:extractionDraft(view).extraction.selected.includes(c.candidateId),machiningKind:c.machiningKind,points:c.boundary.previewPoints}))];
  const selected=workspaceState(view).selectedIntentIds||[workspaceState(view).selectedIntentId];
  const state=extractionDraft(view).extraction,faces=view.fiveAxisExtractionOpen&&['Faces','Region'].includes(state.mode)?state.faces:[];
  if(view.fiveAxisExtractionOpen&&state.mode==='Edges')for(const edge of state.edges)lines.push({id:`selected-edge-${edge.localId}`,points:state.edgePreviews[edge.localId]});
  const splitPoints=activeTool(view)==='split'&&view.fiveAxisSplitOpen&&view.fiveSplit?.key===JSON.stringify(view.scene?.domain?.expected)?view.fiveSplit.points:[];
  const splitLines=saved.filter(c=>c.enabled!==false&&c.split&&c.previewPoints?.length);
  const markers=[...splitLines.flatMap(c=>[relativePoint(c,c.previewPoints[0]),relativePoint(c,c.previewPoints.at(-1))]),...splitPoints.map(p=>p.point)];
  // Number visibility belongs to the CAM page, independently of its active tool.
  if(view.fiveAxisArea==='toolpath'&&!showingGenerated){
    source?.updateWorldMatrix(true,false);
    const matrix=source?.matrixWorld??new THREE.Matrix4().set(...placement.transform);
    const sequences=cutSequenceNumbers(buildCutTree(view));
    const numbered=saved.filter(c=>c.enabled!==false&&c.previewPoints?.length&&(c.machiningKind!=='Mark'||selected.includes(c.id)));
    viewport.setSpecificationAnnotations(numbered.map(c=>{
      const point=new THREE.Vector3(...relativePoint(c,c.startPoint)).applyMatrix4(matrix).toArray();
      return {id:`cut-number-${c.id}`,kind:'label',start:point,end:point,label:sequences.get(c.id),color:selected.includes(c.id)?sceneColors.selected:sceneColors.reference,pickThrough:true};
    }));
    for(const [i,label] of viewport.specificationLabels.entries()){
      const chosen=selected.includes(numbered[i].id);
      label.element.dataset.fiveIntentId=numbered[i].id;
      label.element.classList.add('five-path-number');
      label.element.style.background=sceneColorCss(chosen?sceneColors.selected:sceneColors.label);
      label.element.style.color=sceneColorCss(chosen?sceneColors.label:sceneColors.labelText);
      label.element.style.borderColor=sceneColorCss(chosen?sceneColors.selected:sceneColors.reference);
    }
    view.fiveCutAnnotations=true;
  }
  const starts=saved.filter(c=>c.enabled!==false&&c.startPoint).map(c=>relativePoint(c,c.startPoint));
  const key=JSON.stringify([viewport.id,source?.uuid,view.fiveAxisArea,placement.transform,lines,selected,markers,starts,state.selectedFace,faces.map(f=>[f.localId,state.faceRanges?.[f.localId]])]);
  if(view.fiveCutPreview?.key===key&&view.fiveCutPreview.viewport===viewport){syncToolpathLayerMaterials(view,view.fiveCutPreview.group);return;}
  disposeCutPreview(view);
  const group=new THREE.Group();group.name='five-axis-cutting-lines';group.matrixAutoUpdate=false;
  // Native display transforms also include machine support and registration.
  // Keep overlays in the rendered workpiece's coordinates, including live motion.
  if(!source)group.matrix.set(...placement.transform);
  for(const c of lines) {
    if(!c.points?.length)continue;
    const geometry=new THREE.BufferGeometry().setFromPoints(c.points.map(p=>new THREE.Vector3(...p)));
    const marking=c.machiningKind==='Mark',options={color:c.enabled===false?sceneColors.reference:toolpathLayerColor(view,c.id),depthTest:false,transparent:true,opacity:c.original ? .35 : selected.includes(c.id)?1:.85};
    const material=marking?new THREE.LineDashedMaterial({...options,dashSize:1.2,gapSize:.8}):new THREE.LineBasicMaterial(options);
    const line=new THREE.Line(geometry,material);if(marking)line.computeLineDistances();
    line.renderOrder=30;line.userData.cuttingLineId=c.id;line.userData.originalPath=!!c.original;line.raycast=()=>{};group.add(line);
    if(!c.original&&c.enabled!==false&&view.fiveAxisArea==='toolpath')addCutDirectionMarkers(group,c.points,c.id,options.color,c.reversed);
  }
  if(markers.length){
    const geometry=new THREE.BufferGeometry().setFromPoints(markers.map(p=>new THREE.Vector3(...p)));
    const material=new THREE.PointsMaterial({color:sceneColors.selected,size:9,sizeAttenuation:false,depthTest:false});
    material.onBeforeCompile=shader=>{shader.fragmentShader=shader.fragmentShader.replace('#include <clipping_planes_fragment>','#include <clipping_planes_fragment>\nif(length(gl_PointCoord-vec2(.5))>.5) discard;');};
    const points=new THREE.Points(geometry,material);points.renderOrder=35;points.raycast=()=>{};points.userData.splitMarkers=true;points.userData.pathMarkerIds=[...splitLines.flatMap(c=>[c.id,c.id]),...splitPoints.map(()=>null)];group.add(points);
  }
  if(starts.length){
    const geometry=new THREE.BufferGeometry().setFromPoints(starts.map(p=>new THREE.Vector3(...p)));
    const material=new THREE.PointsMaterial({color:sceneColors.reference,size:7,sizeAttenuation:false,depthTest:false});
    material.onBeforeCompile=shader=>{shader.fragmentShader=shader.fragmentShader.replace('#include <clipping_planes_fragment>','#include <clipping_planes_fragment>\nif(length(gl_PointCoord-vec2(.5))>.5) discard;');};
    const points=new THREE.Points(geometry,material);points.renderOrder=36;points.raycast=()=>{};points.userData.startMarkers=true;points.userData.pathMarkerIds=saved.filter(c=>c.enabled!==false&&c.startPoint).map(c=>c.id);group.add(points);
  }
  let sourceMesh;
  source?.traverse(item=>{if(item.isMesh&&!sourceMesh)sourceMesh=item;});
  if(sourceMesh&&faces.length){
    const mesh=selectedFaceOverlay(sourceMesh,faces.map(f=>({id:f.localId,ranges:state.faceRanges?.[f.localId]||[]})),state.selectedFace);
    mesh.userData.selectedMachiningFaces=true;group.add(mesh);
  }
  (source||viewport.scene).add(group);view.fiveCutPreview={key,group,viewport};
  syncToolpathLayerMaterials(view,group);
}

