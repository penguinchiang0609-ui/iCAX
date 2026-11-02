import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {setupTransformToWorld} from './setupWorldFrame.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {refreshWorkpieceGeometry} from './workpieceGeometryRefresh.mjs';
import {activeTool,selectTool} from './workspaceModel.mjs';
import {selectedPlacement} from './registrationActions.mjs';
import {registrationError} from './registrationDraft.mjs';
import {commandAvailability} from './commandAvailability.mjs';
import {disposePlacementPreview} from './workpiecePlacement.mjs';
import {syncSceneSnapping,sceneSnapCandidate,presentSceneSnaps,clearSceneSnapPresentation,sceneSnapStatus,retrySceneSnapping} from './sceneSnapping.mjs';
import {inferAlignmentCandidate} from './alignmentInference.mjs';
import {alignmentCoordinateCandidate,alignmentCoordinateTargets} from './alignmentCoordinatePicking.mjs';
import {persistedWorkpieceFrame,frameStrings,frameVector,editFrameAxis,frameTransform,frameChanged,frameInReference,frameReferenceVector} from './workpieceFrame.mjs';

const key=view=>JSON.stringify([view.scene?.domain?.expected,selectedPlacement(view)?.id]);
const alive=(context,view,d)=>view.fiveAlignment===d&&d.open&&!view.disposed&&context.isCurrentProject?.()!==false&&activeTool(view)==='align'&&d.key===key(view);
const pointModes=['endpoint','midpoint','center','sphere-center','datum-point','point'];
const allowed=d=>d.selecting==='origin'?pointModes:['line','axis','direction','plane'];
const disposeGroup=group=>{group?.removeFromParent();group?.traverse(o=>{if(o.userData.alignmentGeometry)o.geometry?.dispose();if(o.userData.alignmentTexture)o.material?.map?.dispose();o.material?.dispose();});};
const errorText=error=>/SOURCE_CHANGED|STALE|REVISION|EPOCH/.test(error.message)?'工件已变化，请重新修改坐标系。':/CAM_ALIGNMENT_FRAME_INVALID/.test(error.message)?'坐标轴须为互相垂直的单位向量，并保持右手方向。':registrationError(error);

export function openAlignment(view){
  disposeAlignment(view);const placement=selectedPlacement(view),object=view.viewport?.sceneObjects.get(placement?.id);
  if(!placement||!object){view.error='请先选中已显示的工件。';return;}
  disposePlacementPreview(view);if(view.fivePlacement)view.fivePlacement.open=false;
  selectTool(view,'align');const frame=persistedWorkpieceFrame(view);
  view.fiveAlignment={open:true,key:key(view),placementId:placement.id,setupId:placement.setupId,frame,savedFrame:structuredClone(frame),raw:frameStrings(frame),fieldErrors:{},dirty:false,selecting:null,hoverToken:0,error:'',base:[...placement.transform]};
  view.error='';
}
export function disposeAlignment(view){
  const d=view.fiveAlignment;if(!d)return;d.open=false;++d.hoverToken;clearTimeout(d.timer);clearTimeout(d.saveTimer);d.hoverQueued=null;
  d.viewport?.clearTransformPreview(d.placementId);disposeGroup(d.ghost);disposeGroup(d.feedback);
  view.viewport?.setHoverEnabled(false);clearSceneSnapPresentation(view);view.fiveAlignment=null;
}
function begin(view){
  if(view.pending||view.fiveAlignment?.request&&!view.fiveAlignment.saving)return null;
  if(!view.fiveAlignment){if(commandAvailability(view,'five.tool.align')?.disabled)return null;openAlignment(view);}
  return view.fiveAlignment;
}
function stopPicking(d){
  ++d.hoverToken;clearTimeout(d.timer);d.hoverQueued=null;d.selecting=null;d.picking=false;d.hover=null;d.hoverObject=null;d.candidate=null;d.error='';
}
function update(d){const relative=frameInReference(d.frame,d.savedFrame);d.dirty=frameChanged(relative);d.report=d.dirty?{transform:frameTransform(relative)}:null;}
export function alignmentFieldChanged(view,field,value){
  const [row,component]=field.split('-'),index='XYZ'.indexOf(component);
  if(!['origin','x','z'].includes(row)||index<0)return;
  const d=begin(view);if(!d)return;
  stopPicking(d);d.reference=null;
  if(d.editing!==row){d.editing=row;d.editBase=structuredClone(d.frame);}
  d.raw[row][index]=value;
  try{
    const vector=frameVector(d.raw[row]);
    if(row==='origin')d.frame.origin=vector;
    else{d.frame=editFrameAxis(d.editBase,row,vector,d.firstAxis);d.firstAxis??=row;}
    delete d.fieldErrors[row];d.error='';d.saveError=false;
    const linked=frameStrings(d.frame);for(const name of ['origin','x','y','z'])if(name!==row&&!d.fieldErrors[name])d.raw[name]=linked[name];
    update(d);
  }catch(error){d.fieldErrors[row]=error.message;}
}
export function finishAlignmentField(view,field){
  const d=view.fiveAlignment,row=field.split('-')[0];if(!d||d.saving||d.request)return;
  if(!d.fieldErrors[row])d.raw[row]=frameStrings(d.frame)[row];
  d.editing=null;d.editBase=null;
}
function candidate(view,data,hit,event){
  const d=view.fiveAlignment,found=alignmentCoordinateCandidate(view,d,event)||inferAlignmentCandidate(view,d,hit,event);
  if(found)return found;
  return d.selecting==='origin'?sceneSnapCandidate(d.context,view,{objectId:d.placementId,mode:'point',allowedModes:['point']},hit,event):null;
}
function setPrompt(context,d,text){const node=context.mount.querySelector('[data-align-prompt]');if(node)node.textContent=text||node.dataset.alignDefault;}
async function resolveHover(context,view,d,c,token){
  d.hoverQueued={c,token};if(d.hoverFlight)return;d.hoverFlight=true;
  try{while(d.hoverQueued&&alive(context,view,d)){
    const work=d.hoverQueued;d.hoverQueued=null;
    try{const result=await work.c.resolve();if(alive(context,view,d)&&work.token===d.hoverToken){d.hover=result;d.hoverObject=work.c.objectId;drawFeedback(view,d);setPrompt(context,d,`已吸附 · ${result.label||'几何特征'}`);}}
    catch{if(alive(context,view,d)&&work.token===d.hoverToken)drawFeedback(view,d);}
  }}finally{d.hoverFlight=false;}
}
export function handleAlignmentHover(context,view,data,hit,event){
  const d=view.fiveAlignment;if(!d?.open||activeTool(view)!=='align')return false;
  if(!d.selecting||d.saving||d.picking)return true;
  d.cursor=Number.isFinite(event?.clientX)?{x:event.clientX,y:event.clientY}:null;
  clearTimeout(d.timer);d.hoverQueued=null;const token=++d.hoverToken;
  const c=event?.type==='pointermove'&&!event.buttons?candidate(view,data,hit,event):null;
  d.candidate=c;d.hover=null;d.hoverObject=null;
  if(!c){drawFeedback(view,d);setPrompt(context,d,sceneSnapStatus(view,d.placementId));return true;}
  if(c.result){d.hover=c.result;d.hoverObject=c.objectId;drawFeedback(view,d);setPrompt(context,d,`已吸附 · ${c.result.label||'几何特征'}`);return true;}
  d.timer=setTimeout(()=>void resolveHover(context,view,d,c,token),65);return true;
}
export function handleAlignmentPick(context,view,data,hit,event,_hits,ops){
  const d=view.fiveAlignment;if(!d?.open||activeTool(view)!=='align')return false;
  if(!d.selecting||d.saving||d.picking)return true;
  const c=candidate(view,data,hit,event);
  if(!c&&sceneSnapStatus(view,d.placementId)){ops.renderProject(context,view);return true;}
  if(!c){d.error=d.selecting==='origin'?'请选择坐标系原点、端点、圆心或面上的点。':'请选择坐标轴、直边或平面法向。';ops.renderProject(context,view);return true;}
  const row=d.selecting,token=++d.hoverToken;clearTimeout(d.timer);d.hoverQueued=null;d.picking=true;d.error='';ops.renderProject(context,view);
  void (async()=>{try{
    const result=await c.resolve();if(!alive(context,view,d)||token!==d.hoverToken)return;
    if(row==='origin')d.frame.origin=frameReferenceVector(result.point,d.savedFrame,true);else{d.frame=editFrameAxis(d.frame,row,frameReferenceVector(result.direction,d.savedFrame),d.firstAxis);d.firstAxis??=row;}
    delete d.fieldErrors[row];const linked=frameStrings(d.frame);for(const name of ['origin','x','y','z'])if(!d.fieldErrors[name])d.raw[name]=linked[name];d.editing=null;d.editBase=null;
    d.reference={row,result};d.selecting=null;d.hover=null;d.hoverObject=null;update(d);
  }catch(error){if(alive(context,view,d)&&token===d.hoverToken)d.error=errorText(error);}
  finally{if(alive(context,view,d)&&token===d.hoverToken){d.picking=false;ops.renderProject(context,view);scheduleAlignmentSave(context,view,ops,0);}}})();return true;
}
const request=(view,d)=>({expected:structuredClone(view.scene.domain.expected),setupId:d.setupId,placementId:d.placementId,resetWorkpieceFrame:true,
  registration:{method:'Alignment',modelFrame:'Workpiece',targetFrame:'Setup',unit:'mm',frame:structuredClone(d.frame)}});
export function scheduleAlignmentSave(context,view,ops,delay=450){
  const d=view.fiveAlignment;if(!d)return;clearTimeout(d.saveTimer);d.saveTimer=null;
  if(Object.keys(d.fieldErrors).length||!d.dirty||d.selecting||d.picking)return;
  d.saveTimer=setTimeout(()=>{d.saveTimer=null;void flushAlignmentEdits(context,view,ops);},delay);
}
export async function flushAlignmentEdits(context,view,ops){
  const d=view.fiveAlignment;if(!d)return true;clearTimeout(d.saveTimer);d.saveTimer=null;
  if(d.saveTask)return d.saveTask;
  if(Object.keys(d.fieldErrors).length||d.picking||view.pending)return false;
  if(d.selecting)stopPicking(d);
  if(!d.dirty&&!d.request)return true;
  d.saveTask=(async()=>{
    try{while(view.fiveAlignment===d&&d.open&&(d.dirty||d.request)){
      if(Object.keys(d.fieldErrors).length||d.picking||d.selecting||view.pending)return false;
      if(!d.request){d.requestFrame=structuredClone(d.frame);d.request={...request(view,d),commandId:crypto.randomUUID()};}
      d.saving=true;d.error='';d.saveError=false;ops.renderProject(context,view);
      try{
        try{await context.sceneProxy.invoke('CamPlacement.ApplyRegistration',d.request);}catch(error){
          const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:d.request.expected.epoch,commandId:d.request.commandId});
          if(receipt.status!=='Completed'){if(receipt.status==='NotFound')d.request=null;throw error;}}
        d.request=null;d.savedFrame=d.requestFrame;d.reference=null;
        if(await refreshWorkpieceGeometry(context,view,ops)===false)throw Error(view.error||'坐标系已保存，工件显示刷新失败，请刷新后继续。');
        d.key=key(view);d.base=[...selectedPlacement(view).transform];update(d);
        await saveCommittedProjectEdit(context,view,ops);
      }catch(error){d.saveError=true;d.error=d.request?'保存结果尚未确认，请重试保存。':errorText(error);return false;}
    }return !Object.keys(d.fieldErrors).length;}
    finally{d.saving=false;d.saveTask=null;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
  })();return d.saveTask;
}
export async function alignmentCommand(context,view,action,ops){
  if(action==='close'){
    const d=view.fiveAlignment;if(!d)return;
    if(d.selecting){stopPicking(d);ops.renderProject(context,view);scheduleAlignmentSave(context,view,ops,0);return;}
    if(!await flushAlignmentEdits(context,view,ops))return;
    disposeAlignment(view);selectTool(view,'select');ops.renderProject(context,view);return;
  }
  if(action==='retry-save'){await flushAlignmentEdits(context,view,ops);return;}
  const d=begin(view);if(!d)return;
  if(action==='retry-snaps'){retrySceneSnapping(view,d.placementId);d.error='';ops.renderProject(context,view);return;}
  if(action.startsWith('pick.')){
    if(d.saving)return;
    const row=action.slice(5);if(!['origin','x','z'].includes(row))return;
    const previous=d.selecting;stopPicking(d);d.selecting=previous===row?null:row;d.editing=null;d.editBase=null;
    ops.renderProject(context,view);return;
  }
  if(action.startsWith('flip.')){
    const axis=action.slice(5);if(!['x','z'].includes(axis)||Object.keys(d.fieldErrors).length)return;
    stopPicking(d);d.frame=editFrameAxis(d.frame,axis,d.frame[axis].map(n=>-n),d.firstAxis);d.firstAxis??=axis;d.raw=frameStrings(d.frame);d.fieldErrors={};d.editing=null;update(d);ops.renderProject(context,view);scheduleAlignmentSave(context,view,ops,0);return;
  }
}

function drawFeedback(view,d){
  const hint=d.context?.mount.querySelector('[data-align-inference-hint]');
  if(hint){hint.hidden=!d.hover||!d.cursor;if(!hint.hidden){const rect=view.viewport.renderer.domElement.getBoundingClientRect();hint.textContent=d.hover.label||'几何特征';hint.style.left=`${Math.max(8,Math.min(d.cursor.x-rect.left+18,rect.width-220))}px`;hint.style.top=`${Math.max(8,Math.min(d.cursor.y-rect.top+18,rect.height-64))}px`;}}
  view.sceneSnapping?.presentation?.layer?.highlight(d.hover);
  disposeGroup(d.feedback);const group=d.feedback=new THREE.Group();group.name='alignment-feedback';view.viewport.scene.add(group);
  const object=view.viewport.sceneObjects.get(d.placementId);if(!object)return;
  object.updateWorldMatrix(true,false);if(!object.geometry.boundingBox)object.geometry.computeBoundingBox();const length=Math.max(1,object.geometry.boundingBox.getSize(new THREE.Vector3()).length()*.15);
  const frame=frameInReference(d.frame,d.savedFrame),anchor=new THREE.Vector3(...frame.origin).applyMatrix4(object.matrixWorld);
  for(const [axis,color] of [['x',0xf15b64],['y',0x4cbb7b],['z',0x4e8fff]]){
    const active=d.hover?.featureId===`coordinate:current:${axis}`,direction=new THREE.Vector3(...frame[axis]).transformDirection(object.matrixWorld),arrow=new THREE.ArrowHelper(direction,anchor,length,active?0x61decc:color);
    arrow.traverse(o=>{if(o.geometry){o.geometry=o.geometry.clone();o.userData.alignmentGeometry=true;}if(o.material)o.material.depthTest=false;});group.add(arrow);
  }
  if(d.selecting==='origin'){
    for(const target of alignmentCoordinateTargets(view,d).filter(t=>t.mode==='datum-point')){
      const marker=new THREE.Points(new THREE.BufferGeometry().setFromPoints([target.worldPoint]),new THREE.PointsMaterial({color:d.hover?.featureId===target.featureId?0x61decc:0xf4f7f8,size:9,sizeAttenuation:false,depthTest:false}));marker.userData.alignmentGeometry=true;group.add(marker);
    }
  }
  const feature=d.hover||(!d.selecting?d.reference?.result:null);
  if(feature){
    const color=d.hover?0x61decc:0xffc451,local=new THREE.Group();local.matrixAutoUpdate=false;local.matrix.copy(object.matrixWorld);group.add(local);
    const directionPick=(d.selecting||d.reference?.row)!=='origin';
    const points=feature.lineEnds?.length===2?feature.lineEnds:feature.edgeSamples;
    if(points?.length>1){const line=new THREE.Line(new THREE.BufferGeometry().setFromPoints(points.map(p=>new THREE.Vector3(...p))),new THREE.LineBasicMaterial({color,depthTest:false}));line.userData.alignmentGeometry=true;local.add(line);}
    const ranges=feature.mode==='plane'||feature.query?.construction?.kind==='PlaneNormal'?feature.triangleRanges:null;
    if(ranges?.length){const pos=object.geometry.getAttribute('position'),vertices=[];for(const range of ranges)for(let n=+range.first*3;n<(+range.first+ +range.count)*3;n++){const i=object.geometry.index?object.geometry.index.getX(n):n;vertices.push(pos.getX(i),pos.getY(i),pos.getZ(i));}
      const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(vertices,3));const face=new THREE.Mesh(geometry,new THREE.MeshBasicMaterial({color,transparent:true,opacity:.35,side:THREE.DoubleSide,depthWrite:false,polygonOffset:true,polygonOffsetFactor:-3,polygonOffsetUnits:-3}));face.userData.alignmentGeometry=true;local.add(face);}
    if(directionPick){
      const center=new THREE.Vector3(...feature.point),camera=view.viewport.camera,rect=view.viewport.renderer.domElement.getBoundingClientRect();
      if(d.hover&&points?.length>=2&&d.cursor){
        const raycaster=new THREE.Raycaster();raycaster.setFromCamera({x:(d.cursor.x-rect.left)/rect.width*2-1,y:1-(d.cursor.y-rect.top)/rect.height*2},camera);
        const ray=raycaster.ray.clone().applyMatrix4(object.matrixWorld.clone().invert());
        ray.distanceSqToSegment(new THREE.Vector3(...points[0]),new THREE.Vector3(...points.at(-1)),null,center);
      }
      const world=center.clone().applyMatrix4(object.matrixWorld);
      const distance=Math.max(camera.near,-world.applyMatrix4(camera.matrixWorldInverse).z),perPixel=(camera.isOrthographicCamera?(camera.top-camera.bottom)/camera.zoom:2*distance*Math.tan(THREE.MathUtils.degToRad(camera.getEffectiveFOV()/2)))/Math.max(1,rect.height)/object.matrixWorld.getMaxScaleOnAxis();
      const direction=new THREE.Vector3(...feature.direction).normalize(),arrowLength=perPixel*46,arrow=new THREE.ArrowHelper(direction,center.addScaledVector(direction,-arrowLength*.5),arrowLength,color,perPixel*11,perPixel*7);
      arrow.name='alignment-direction-hover';arrow.userData.alignmentDirection=[...feature.direction];
      arrow.traverse(o=>{if(o.geometry){o.geometry=o.geometry.clone();o.userData.alignmentGeometry=true;}if(o.material){o.material.depthTest=false;o.material.depthWrite=false;}});local.add(arrow);
    }else{
      const point=new THREE.Points(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(...feature.point)]),new THREE.PointsMaterial({color,size:10,sizeAttenuation:false,depthTest:false}));point.name='alignment-origin-hover';point.userData.alignmentGeometry=true;local.add(point);
    }
  }
  group.traverse(o=>{o.raycast=()=>{};o.renderOrder=70;});
}
export function syncAlignment(context,view){
  const d=view.fiveAlignment;if(!d?.open)return;
  if(d.saving||d.request){view.viewport?.setHoverEnabled(false);return;}
  if(!alive(context,view,d)){disposeAlignment(view);if(activeTool(view)==='align')selectTool(view,'select');return true;}
  d.context=context;d.viewport=view.viewport;const object=view.viewport?.sceneObjects.get(d.placementId);if(!object)return;
  if(d.selecting){syncSceneSnapping(context,view,error=>{if(alive(context,view,d)){d.error=errorText(error);setPrompt(context,d,d.error);}});presentSceneSnaps(context,view,{objectId:d.placementId,allowedModes:allowed(d),inference:true,candidateDisplay:d.selecting==='origin'?'points':'directions'});}
  else clearSceneSnapPresentation(view);
  view.viewport.setHoverEnabled(!!d.selecting&&!d.picking);view.viewport.setCameraNavigationEnabled(true);
  if(d.report){
    if(!d.ghost){d.ghost=new THREE.Mesh(object.geometry,new THREE.MeshBasicMaterial({color:0xb0c1cb,wireframe:true,transparent:true,opacity:.07,depthWrite:false}));d.ghost.matrixAutoUpdate=false;d.ghost.matrix.copy(setupTransformToWorld(view,d.setupId,new THREE.Matrix4().set(...d.base)));d.ghost.raycast=()=>{};object.parent.add(d.ghost);}
    view.viewport.setTransformPreview(d.placementId,setupTransformToWorld(view,d.setupId,new THREE.Matrix4().set(...d.report.transform)).elements);
  }else{view.viewport.clearTransformPreview(d.placementId);disposeGroup(d.ghost);d.ghost=null;}
  drawFeedback(view,d);
}
export {renderAlignment,renderAlignmentHint} from './workpieceAlignmentPanel.mjs';
