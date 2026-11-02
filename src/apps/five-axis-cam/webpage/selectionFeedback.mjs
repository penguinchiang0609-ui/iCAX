import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {activeTool} from './workspaceModel.mjs';
import {selectedPlacement} from './registrationActions.mjs';
import {nearestCutLine} from './cutSelection.mjs';
import {extractionPickRequest} from './extractionActions.mjs';
import {toolAxisPointAtPointer,canDeleteToolAxisPoint} from './toolAxisPoints.mjs';
import {rapidPathPointAtPointer} from './rapidPathEditing.mjs';
import {rapidTransitionAtPointer} from './rapidPlanning.mjs';
import {axisInfluenceAtPointer} from './toolAxisInfluence.mjs';
import {createToolAxisPointGlyph} from './toolAxisDisplay.mjs';
import {orderingAtPointer} from './toolpathOrdering.mjs';

const kind=mode=>mode==='plane'?'face':['axis','direction'].includes(mode)?'axis':mode==='line'?'line':'point';
const alive=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
export function selectionFeedbackMode(view){
  if(view.pending||view.fixtureRequest||view.fiveSetupRequest||view.fiveTreeRequest||view.fiveWorkpieceSwitching||view.fiveToolAxisRequest)return null;
  const tool=activeTool(view),alignment=view.fiveAlignment;
  if(tool==='align'&&alignment?.open){
    if(!alignment.selecting||alignment.saving||alignment.picking||alignment.request)return null;
    return {owner:'alignment',kind:alignment.selecting==='origin'?'point':'axis'};
  }
  if(tool==='registration'&&view.fiveLocatingOpen!==false)return view.fiveLocating?.picking!=null?{owner:'locating',kind:'point'}:null;
  if(view.fiveAxisRegistrationOpen&&['rough','fine'].includes(tool)&&view.fiveAxisRegistrationPick)return {owner:'registration',kind:kind(view.fiveAxisRegistrationPick.mode)};
  if(tool==='fixture'&&view.fixtureDesignPicking)return {owner:'fixture',kind:'face'};
  if(view.fiveAxisArea!=='toolpath')return null;
  if(view.fivePoseSelection?.active)return {owner:'pose',kind:'axis'};
  if(view.fiveAxisExtractionPick)return {owner:'extraction',kind:view.fiveAxisExtractionPick.mode==='Edges'?'line':'face'};
  if(tool==='keypoint')return view.fiveAxisPointEdit?.rotation?.gesture?null:{owner:'keypoint',kind:!view.fiveAxisPointDelete&&view.fiveAxisPointEdit?.influence?.picking?'axis':'point'};
  if(tool==='path-start'&&view.fiveAxisStartOpen&&!view.fiveStart?.request)return {owner:'start',kind:'point'};
  if(tool==='split'&&view.fiveAxisSplitOpen&&!view.fiveSplit?.request)return {owner:'split',kind:'point'};
  if(tool==='rapid')return view.fiveRapid?.open&&!view.fiveRapid.settings&&!view.fiveRapid.running&&!view.fiveRapid.request&&!view.fiveRapid.pathEdit?.gesture?{owner:'rapid',kind:view.fiveRapid.pathEdit?'point':'line'}:null;
  if(tool==='manual-order'&&view.fiveOrder?.open&&!view.fiveOrder.request)return {owner:'order',kind:'line'};
  if(tool==='simulation'||view.fiveOrder?.request||view.fiveReverseRequest||view.fiveClear?.request||view.fiveLeads?.request||view.fiveLeads?.saving)return null;
  return view.scene?.intentToolpaths?.length?{owner:'cut',kind:'line'}:null;
}
const scope=view=>JSON.stringify([view.viewport?.id,view.scene?.domain?.expected,view.scene?.domain?.activeSetupId,selectedPlacement(view)?.id,selectionFeedbackMode(view)]);
function removeOverlay(state){state.group?.removeFromParent();state.group?.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});state.group=null;state.target=null;}
function cancelPending(state){clearTimeout(state.timer);state.timer=null;state.queued=null;state.latest=null;++state.token;}
function clear(state){cancelPending(state);state.faceHit=null;removeOverlay(state);}
export function disposeSelectionFeedback(view){
  const state=view.fiveSelectionFeedback;if(!state)return;
  clear(state);state.dead=true;state.canvas.removeAttribute('data-five-pick');view.fiveSelectionFeedback=null;
}
export function syncSelectionFeedback(context,view){
  const vp=view.viewport;if(!vp)return;
  const mode=alive(context,view)?selectionFeedbackMode(view):null,key=scope(view);
  let state=view.fiveSelectionFeedback;
  if(state?.canvas!==vp.renderer.domElement){disposeSelectionFeedback(view);state=null;}
  if(!state)state=view.fiveSelectionFeedback={canvas:vp.renderer.domElement,token:0};
  if(state.key!==key){clear(state);state.key=key;}
  if(mode)state.canvas.setAttribute('data-five-pick',mode.kind);else state.canvas.removeAttribute('data-five-pick');
  // One final owner after all tool syncs: an inactive editor cannot disable
  // hover for the editor which has just become active.
  vp.setHoverEnabled(!!mode);
}
function group(state,parent,target){
  removeOverlay(state);const g=state.group=new THREE.Group();g.name='five-selection-hover';g.userData.selectionHover=target;state.target=target;parent.add(g);return g;
}
function inert(g){g.traverse(o=>{o.raycast=()=>{};o.renderOrder=90;});}
function faceOverlay(state,source,ranges,id){
  if(state.target?.faceId===id&&state.group?.parent===source)return;
  const g=group(state,source,{kind:'face',faceId:id}),p=source.geometry.attributes.position,index=source.geometry.index,vertices=[];
  for(const range of ranges)for(let n=+range.first*3;n<(+range.first+ +range.count)*3;n++){
    const i=index?index.getX(n):n;vertices.push(p.getX(i),p.getY(i),p.getZ(i));
  }
  const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(vertices,3));
  g.add(new THREE.Mesh(geometry,new THREE.MeshBasicMaterial({color:0xffcd68,transparent:true,opacity:.65,side:THREE.DoubleSide,depthWrite:false,polygonOffset:true,polygonOffsetFactor:-4,polygonOffsetUnits:-4})));
  inert(g);
}
function marker(g,point){
  const geometry=new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(...point)]),material=new THREE.PointsMaterial({color:0x68e6f3,size:16,sizeAttenuation:false,depthTest:false,depthWrite:false});
  material.onBeforeCompile=shader=>{shader.fragmentShader=shader.fragmentShader.replace('#include <clipping_planes_fragment>','#include <clipping_planes_fragment>\nfloat radius=length(gl_PointCoord-vec2(.5));if(radius>.5||radius<.29)discard;');};
  g.add(new THREE.Points(geometry,material));
}
function stroke(view,g,points){
  if(points.length<2)return;
  // Cylinders retain a visible two-pixel stroke on Windows, where WebGL's
  // native line width is always one. Endpoints stay on the actual polyline.
  const geometry=new THREE.CylinderGeometry(1,1,1,6),material=new THREE.MeshBasicMaterial({color:0x68e6f3,depthTest:false,depthWrite:false});
  const mesh=new THREE.InstancedMesh(geometry,material,points.length-1),m=new THREE.Matrix4(),q=new THREE.Quaternion(),up=new THREE.Vector3(0,1,0),vp=view.viewport;
  g.updateWorldMatrix(true,false);const worldScale=g.matrixWorld.getMaxScaleOnAxis();
  for(let i=1;i<points.length;i++){
    const a=new THREE.Vector3(...points[i-1]),b=new THREE.Vector3(...points[i]),center=a.clone().add(b).multiplyScalar(.5),delta=b.sub(a),length=delta.length(),world=center.clone().applyMatrix4(g.matrixWorld),camera=vp.camera;
    const distance=-world.applyMatrix4(camera.matrixWorldInverse).z;
    const perPixel=(camera.isOrthographicCamera?(camera.top-camera.bottom)/camera.zoom:2*distance*Math.tan(THREE.MathUtils.degToRad(camera.getEffectiveFOV()/2)))/Math.max(1,stateHeight(vp));
    q.setFromUnitVectors(up,length?delta.divideScalar(length):up);m.compose(center,q,new THREE.Vector3(perPixel*1.3/worldScale,length,perPixel*1.3/worldScale));mesh.setMatrixAt(i-1,m);
  }
  g.add(mesh);
}
const stateHeight=vp=>vp.renderer.domElement.getBoundingClientRect().height;
function pathOverlay(view,state,parent,target,points,point=null){
  const g=group(state,parent,target);if(points)stroke(view,g,points);if(point)marker(g,point);inert(g);
}
function placementObject(view,intentId){const cut=view.scene.intentToolpaths.find(c=>c.id===intentId);return cut&&view.viewport.sceneObjects.get(cut.placementId);}
const containsTriangle=(ranges,index)=>ranges?.some(r=>index>=+r.first&&index<+r.first+ +r.count);
const sameFaceSource=(a,b)=>a&&b&&a.source===b.source&&a.geometry===b.geometry;
function queue(context,view,state,task){
  const token=++state.token;state.latest=task;state.queued={task,token,key:state.key};removeOverlay(state);
  // Face requests start even while the pointer keeps crossing tessellation
  // triangles. A trailing debounce would postpone feedback until movement stops.
  if(!task.face){clearTimeout(state.timer);state.timer=null;}
  if(!state.busy&&state.timer==null)state.timer=setTimeout(()=>{state.timer=null;void resolve(context,view,state);},35);
}
async function resolve(context,view,state){
  if(state.busy||!state.queued)return;state.busy=true;
  try{while(state.queued&&!state.dead){
    const {task,token,key}=state.queued;state.queued=null;
    try{const result=await task.run();if(alive(context,view)&&view.fiveSelectionFeedback===state&&key===state.key&&state.key===scope(view)){
      // The pointer may have moved to another triangle of the same CAD face
      // while native topology was resolving. That result is still current.
      const latest=state.latest,sameFace=sameFaceSource(task.face,latest?.face)&&containsTriangle(result.triangleRanges,latest.face.index);
      if(token===state.token||sameFace){
        if(sameFace){state.queued=null;clearTimeout(state.timer);state.timer=null;}
        latest.draw(result);
      }
    }}
    catch{/* Invalid geometry is not a hover target; clicking retains tool error handling. */}
  }}finally{state.busy=false;}
}
export function handleSelectionFeedbackHover(context,view,data,hit,event,_hits,pointer){
  const state=view.fiveSelectionFeedback,mode=selectionFeedbackMode(view);if(!state)return false;
  const valid=mode&&alive(context,view)&&pointer?.pointerActive!==false&&event?.type==='pointermove'&&!event.buttons;
  if(!valid){clear(state);return false;}
  state.canvas.setAttribute('data-five-pick',mode.kind);
  if(['alignment','locating','registration','pose'].includes(mode.owner)){removeOverlay(state);return false;}
  if(mode.owner==='fixture'){
    const source=view.viewport.sceneObjects.get(view.fixtureDesignPlacement),face=data?.objectId===view.fixtureDesignPlacement&&Number.isInteger(hit?.faceIndex)?view.fixtureDesignFaces?.find(f=>hit.faceIndex>=+f.first&&hit.faceIndex<+f.first+ +f.count):null;
    if(source&&face)faceOverlay(state,source,view.fixtureDesignFaces.filter(f=>String(f.faceId)===String(face.faceId)),String(face.faceId));else clear(state);return true;
  }
  if(mode.owner==='extraction'){
    const request=extractionPickRequest(view,data,hit);if(!request){clear(state);return true;}
    const source=hit.object,method=mode.kind==='line'?'CamCut.ResolveEdge':'CamCut.ResolveFace';
    const key=JSON.stringify([method,source.geometry.uuid,hit.faceIndex,...(mode.kind==='line'?[event.clientX,event.clientY]:[])]);
    const face=mode.kind==='face'?{source,geometry:source.geometry,index:hit.faceIndex}:null;
    if(sameFaceSource(face,state.faceHit)&&containsTriangle(state.faceHit.result.triangleRanges,face.index)){
      const result=state.faceHit.result;cancelPending(state);faceOverlay(state,source,result.triangleRanges,String(result.faceUse.localId));return true;
    }
    if(state.latest?.key===key)return true;
    queue(context,view,state,{key,face,run:()=>context.sceneProxy.invoke(method,request),draw:r=>{
      if(face){state.faceHit={...face,result:r};faceOverlay(state,source,r.triangleRanges,String(r.faceUse.localId));}
      else pathOverlay(view,state,source,{kind:'line',edgeId:r.edgeUse.localId},r.previewPoints);
    }});return true;
  }
  if(mode.owner==='rapid'){
    const edit=view.fiveRapid.pathEdit;
    if(edit){const p=rapidPathPointAtPointer(view,event);if(p)pathOverlay(view,state,view.viewport.scene,{kind:'point',index:p.index},null,p.point);else clear(state);}
    else {const t=rapidTransitionAtPointer(view,event);if(t)pathOverlay(view,state,view.viewport.scene,{kind:'line',transitionId:t.id},t.points);else clear(state);}return true;
  }
  if(mode.owner==='order'){
    const pick=orderingAtPointer(view,event),source=pick&&view.viewport.sceneObjects.get(pick.placementId);clear(state);
    if(source){const g=group(state,source,{kind:'line',intentId:pick.id});for(const part of pick.parts)stroke(view,g,part.points);inert(g);}return true;
  }
  if(mode.owner==='keypoint'){
    if(mode.kind==='axis'){
      const d=view.fiveAxisPointEdit,pick=axisInfluenceAtPointer(view,d,event),source=placementObject(view,d.id);clear(state);
      if(pick&&source){const g=group(state,source,{kind:'axis',index:pick.index}),original=view.fiveAxisPointPreview?.group.children.find(o=>o.userData.influenceStation===pick.station),axis=createToolAxisPointGlyph(view,d.id,pick.station,original?.userData.toolAxisSamples[0].direction||pick.station.direction);axis.material.uniforms.color.value.setHex(0x68e6f3);axis.material.uniforms.size.value=1.6;g.add(axis);inert(g);}return true;
    }
    const point=toolAxisPointAtPointer(view,event);if(point&&(!view.fiveAxisPointDelete||canDeleteToolAxisPoint(point))){clear(state);pathOverlay(view,state,view.viewport.scene,{kind:'point',pointId:point.id},null,point.setupPoint);return true;}
    if(view.fiveAxisPointDelete){clear(state);return true;}
    if(!view.fiveAxisPointInsert&&!view.fiveAxisPointEdit?.picking){clear(state);return true;}
  }
  const pick=nearestCutLine(view,event);if(!pick){clear(state);return true;}
  const source=placementObject(view,pick.intentId);if(!source){clear(state);return true;}
  if(mode.kind==='line'){
    const cut=view.scene.intentToolpaths.find(c=>c.id===pick.intentId);pathOverlay(view,state,source,{kind:'line',intentId:pick.intentId},cut.previewPoints);return true;
  }
  const method={start:'CamCut.ResolveStart',split:'CamCut.ResolveSplitPoint',keypoint:'CamCut.ResolveToolAxisPoint'}[mode.owner];
  const key=JSON.stringify([method,pick]);if(state.latest?.key===key)return true;
  const request={expected:structuredClone(view.scene.domain.expected),...pick,frame:'Workpiece',...(mode.owner==='start'?{mode:'ClosestPoint'}:mode.owner==='split'?{mode:'Auto'}:{})};
  queue(context,view,state,{key,run:()=>context.sceneProxy.invoke(method,request),draw:r=>pathOverlay(view,state,source,{kind:'point',intentId:pick.intentId,anchor:r.anchor||r.start},null,r.point)});return true;
}
