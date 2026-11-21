import {renderTransformTools,renderTransformValue,positionTransformHud,focusTransformValue} from './transformHud.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {applyPlacementReceipt} from './placementReceipt.mjs';
import {refreshWorkpieceGeometry} from './workpieceGeometryRefresh.mjs';
import {floatingDialogStyle} from './floatingDialogs.mjs';
import {mergeDomainState,readMachiningState} from './domainActions.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { activeTool, selectTool, geometryEditing } from './workspaceModel.mjs';
import { selectedPlacement } from './registrationActions.mjs';
import {workpieceLocked} from './workpieceState.mjs';
import {placementSnapOptions,updatePlacementSnapFeedback} from './placementSnapping.mjs';
import {setupReferenceWorldFrame} from './setupWorldFrame.mjs';
import {operationPlacementMatrix,placementMovesAssembly} from './operationAssembly.mjs';
import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';

export const placementTools = ['translate', 'rotate', 'scale'];
const placementArea = (view,mode) => mode==='scale'?view.fiveAxisArea==='workpiece':view.fiveAxisArea==='operation';
const initialValue = d => d.mode === 'scale' ? '1' : '0';
const scaleFactors = d => {
  const value=readNumber(d.value);
  if(value<=0)throw Error('缩放比例必须大于 0。');
  return Array(3).fill(value/(d.appliedScale??1));
};
const sourceKey = view => JSON.stringify([view.scene?.domain?.expected, selectedPlacement(view)?.id]);
const current = (context, view) => !view.disposed && context.isCurrentProject?.() !== false;
const readNumber = value => {
  if (!String(value).trim() || !Number.isFinite(Number(value))) throw Error('请输入有效数值。');
  return Number(value);
};
const rowMajor = matrix => matrix.clone().transpose().toArray();
const format = n => String(Math.round(n * 1e6) / 1e6);
const orientation = matrix => new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().set(...matrix));
// The part center is only the editing pivot. It does not define either frame:
// world is the fixed Scene frame; object origin is model (0,0,0) mapped by placement.
const center = (d, matrix) => new THREE.Vector3(...d.localCenter).applyMatrix4(matrix);
const planeSelected = d => d.mode === 'translate' && ['xy','xz','yz'].includes(d.axis);
const identity = () => new THREE.Matrix4();
const draftParent = d => d.parentWorld?new THREE.Matrix4().set(...d.parentWorld):identity();
const draftWorld = (d,matrix) => draftParent(d).multiply(matrix);

// Workpiece activation belongs to the page, not to viewport selection. Restore
// handles after loading/reopening, a version change, or returning from another page.
export function preparePlacement(view) {
  if(!['workpiece','operation'].includes(view.fiveAxisArea)||!view.scene?.model?.isLoaded)return false;
  if(workpieceLocked(view)){disposePlacementPreview(view);view.fivePlacement=null;return false;}
  
  const mode=activeTool(view),d=view.fivePlacement;
  if(!placementTools.includes(mode)||view.pending||view.fiveWorkpieceLock?.request||view.fiveWorkpieceRemoval?.request||d?.request||d?.saving)return false;
  if(d?.open&&(d.panel||d.mode===mode)&&d.placementId===selectedPlacement(view)?.id&&d.key===sourceKey(view))return false;
  if(!view.viewport?.sceneObjects.get(selectedPlacement(view)?.id)?.geometry)return false;
  return openPlacement(view,mode)===true;
}

export function openPlacement(view, mode) {
  if (!placementArea(view,mode)||workpieceLocked(view)||view.fivePlacement?.request || view.pending) return;
  disposePlacementPreview(view);
  const placement = selectedPlacement(view);
  if (!placement) { view.error = '请先导入工件。'; return; }
  const object = view.viewport?.sceneObjects.get(placement.id);
  if (!object?.geometry) { view.error = '工件显示尚未就绪，请稍后重试。'; return; }
  let parent;
  try{parent=rowMajor(setupReferenceWorldFrame(view,placement.setupId));}catch(error){view.error=error.message;return;}
  object.geometry.computeBoundingBox();
  view.fivePlacement = {
    open:true, mode, placementId:placement.id, setupId:placement.setupId, key:sourceKey(view),
    base:rowMajor(operationPlacementMatrix(view,placement)), original:rowMajor(operationPlacementMatrix(view,placement)), parentWorld:parent,
    motionScope:placementMovesAssembly(view,placement.setupId)?'Assembly':'Workpiece',
    localCenter:object.geometry.boundingBox.getCenter(new THREE.Vector3()).toArray(),
    space:view.fivePlacementSpace ?? 'world', axis:null, value:mode==='scale'?'1':'0', planeValues:['0','0','0'], appliedScale:1,
  };
  view.error = '';
  return true;
}

// One value belongs to the picked handle. Numeric edits replace this gesture's
// amount; a new handle gesture starts from the last committed placement.
export function placementMatrix(d) {
  if(d.panel&&d.mode!=='scale')return new THREE.Matrix4().compose(
    new THREE.Vector3(...d.panelPosition.map(readNumber)),
    new THREE.Quaternion().setFromEuler(new THREE.Euler(...d.panelRotation.map(v=>THREE.MathUtils.degToRad(readNumber(v))),'XYZ')),
    new THREE.Vector3(1,1,1));
  const domainBase = new THREE.Matrix4().set(...d.base);
  if (!d.axis) return domainBase;
  if(d.mode==='scale'){
    const pivot=new THREE.Vector3(...d.localCenter);
    return domainBase.multiply(new THREE.Matrix4().makeTranslation(...pivot.toArray()))
      .multiply(new THREE.Matrix4().makeScale(...scaleFactors(d)))
      .multiply(new THREE.Matrix4().makeTranslation(...pivot.negate().toArray()));
  }
  const parent=draftParent(d),base=parent.clone().multiply(domainBase);
  const toDomain=matrix=>parent.clone().invert().multiply(matrix);
  if (planeSelected(d)) {
    const offset = new THREE.Vector3(...['x','y','z'].map((axis,i)=>d.axis.includes(axis)?readNumber(d.planeValues[i]):0));
    if (d.space === 'local') offset.applyQuaternion(orientation(rowMajor(base)));
    return toDomain(new THREE.Matrix4().makeTranslation(...offset.toArray()).multiply(base));
  }
  const amount = readNumber(d.value), axes = {x:[1,0,0],y:[0,1,0],z:[0,0,1]};
  const axis = new THREE.Vector3(...(d.axis==='e'?d.rotationAxis:axes[d.axis]));
  // World directions are immutable unit vectors. Only local directions inherit
  // the object's rotation; neither choice changes the Scene/root coordinate frame.
  if (d.axis!=='e' && d.space === 'local') axis.applyQuaternion(orientation(rowMajor(base)));
  let delta;
  if (d.mode === 'translate') delta = new THREE.Matrix4().makeTranslation(...axis.multiplyScalar(amount).toArray());
  else {
    const pivot = center(d,base);
    delta = new THREE.Matrix4().makeTranslation(...pivot.toArray())
      .multiply(new THREE.Matrix4().makeRotationAxis(axis,(amount % 360)*Math.PI/180))
      .multiply(new THREE.Matrix4().makeTranslation(...pivot.negate().toArray()));
  }
  const result = delta.multiply(base);
  if (!result.elements.every(Number.isFinite)) throw Error('数值超出范围。');
  return toDomain(result);
}

function transformParameterState(view){
  const placement=selectedPlacement(view);if(!placement)return null;
   let matrix=operationPlacementMatrix(view,placement),preview=false;
  if(activeTool(view)==='align'&&view.fiveAlignment?.report){matrix=new THREE.Matrix4().set(...view.fiveAlignment.report.transform);preview=true;}
  const d=view.fivePlacement;
  if(d?.open&&d.placementId===placement.id&&(d.panel||activeTool(view)===d.mode)&&d.key===sourceKey(view)){
    try{const candidate=d.request?.transform?new THREE.Matrix4().set(...d.request.transform):placementMatrix(d);
      preview=candidate.elements.some((v,i)=>Math.abs(v-matrix.elements[i])>1e-9);matrix=candidate;}catch{}
  }
  const position=new THREE.Vector3(),rotation=new THREE.Quaternion(),scale=new THREE.Vector3();matrix.decompose(position,rotation,scale);
  const angles=new THREE.Euler().setFromQuaternion(rotation,'XYZ');
  return {placement,preview,position:position.toArray(),rotation:[angles.x,angles.y,angles.z].map(v=>THREE.MathUtils.radToDeg(v)),scale:scale.toArray()};
}
const transformNumber=value=>Math.abs(value)<1e-9?'0':Number(value.toFixed(6)).toString();
function transformFieldValue(view,state,group,index){
  const d=view.fivePlacement;
  if(d?.open&&d.panel&&d.key===sourceKey(view)){
    if(group==='scale'&&d.mode==='scale')return d.value;
    if(d.mode!=='scale'&&group==='position')return d.panelPosition[index];
    if(d.mode!=='scale'&&group==='rotation')return d.panelRotation[index];
  }
  return transformNumber(state[group][index]);
}
export function renderTransformParameters(view){
  const state=transformParameterState(view);
  if(!state)return '<section class="five-transform-parameters" data-five-key="transform-parameters"><header>变换参数</header><p>请选择工件。</p></section>';
  const alignment=view.fiveAlignment,alignmentBusy=!!alignment&&(alignment.dirty||alignment.saving||alignment.request||alignment.selecting||alignment.picking||Object.keys(alignment.fieldErrors).length);
  const busy=view.pending||!!view.fiveWorkpieceRemoval?.request||!!view.fiveWorkpieceLock?.request||!!view.fivePlacement?.request||alignmentBusy,readonly=workpieceLocked(view)||busy;
  const group=(id,title,unit)=>`<section data-five-key="transform-${id}"><h4>${title}<small>${unit}</small></h4><div class="five-transform-vector">${['X','Y','Z'].map((axis,i)=>`<label class="axis-${axis.toLowerCase()}" for="five-transform-${attr(state.placement.id)}-${id}-${axis}">${axis}</label><input id="five-transform-${attr(state.placement.id)}-${id}-${axis}" data-five-key="${id}-${axis}" data-five-transform-value="${id}-${axis}" data-five-transform-field="${id}-${axis}" aria-label="${title} ${axis}" type="text" inputmode="decimal" autocomplete="off" value="${attr(transformFieldValue(view,state,id,i))}" data-model-value-authoritative="true" ${readonly?'readonly':''}>`).join('')}</div></section>`;
  return `<section class="five-transform-parameters" data-five-key="transform-parameters" data-five-transform-id="${attr(state.placement.id)}"><header>${geometryEditing(view)?'几何缩放':placementMovesAssembly(view,state.placement.setupId)?'总成位姿':'位姿'}</header><p>${geometryEditing(view)?'修改共享工件几何':placementMovesAssembly(view,state.placement.setupId)?'工件与工装整体放置':'工件 → 当前装夹坐标系'}<span data-five-transform-status>${state.preview?'预览中':'已保存'}</span></p>
    ${geometryEditing(view)?group('scale','几何缩放','×'):group('position','位置','mm')+group('rotation','旋转','°')}
    <small>${alignmentBusy?'自身坐标系修改实时生效并自动保存。':workpieceLocked(view)?'关联的装夹姿态已锁定；请在左侧解锁对应装夹后修改。':geometryEditing(view)?'XYZ 等比缩放，所有装夹共用新几何；应用后比例回到 1。':'Enter 或离开输入框应用，Esc 取消。放置只影响当前装夹。'}</small></section>`;
}
function syncTransformParameters(view){
  const panel=view.viewport?.renderer.domElement.closest('.five-workspace')?.querySelector('[data-five-key="transform-parameters"]');
  if(!panel)return;const state=transformParameterState(view);if(!state||panel.dataset.fiveTransformId!==state.placement.id)return;
  for(const group of ['position','rotation','scale'])for(const [i,axis] of ['X','Y','Z'].entries()){
    const node=panel.querySelector(`[data-five-transform-value="${group}-${axis}"]`);if(node){const value=String(transformFieldValue(view,state,group,i));if(node.value!==value)node.value=value;}}
  panel.querySelector('[data-five-transform-status]').textContent=state.preview?'预览中':'已保存';
}

function finishTransformEdit(view){
  if(!view.fivePlacement?.panel)return;
  disposePlacementPreview(view);view.fivePlacement=null;
  if(placementTools.includes(activeTool(view)))openPlacement(view,activeTool(view));
}
export async function transformParameterCommand(context,view,action,ops){
  if(!current(context,view)||view.pending||view.fiveAlignment?.open)return;
  if(action==='cancel'){
    if(view.fivePlacement?.request)return;
    finishTransformEdit(view);view.error='';ops.renderProject(context,view);return;
  }
  if(action==='apply'){
    const d=view.fivePlacement;if(!d?.panel)return;
    await placementCommand(context,view,'apply',ops);
    if(!d.request&&!view.error){finishTransformEdit(view);ops.renderProject(context,view);}return;
  }

}
export function transformParameterChanged(view,field,value){
  if(workpieceLocked(view)||view.pending||view.fiveWorkpieceLock?.request||view.fiveWorkpieceRemoval?.request||view.fivePlacement?.request||view.fiveAlignment?.open)return;
  const [group,axis]=field.split('-'),index='XYZ'.indexOf(axis);if(!['position','rotation','scale'].includes(group)||index<0||(group==='scale')!==(geometryEditing(view)))return;
  let d=view.fivePlacement;
  if(!d?.panel||!d.open||d.key!==sourceKey(view)||group==='scale'&&d.mode!=='scale'||group!=='scale'&&d.mode==='scale'){
    const state=transformParameterState(view);if(!state)return;
    openPlacement(view,group==='scale'?'scale':group==='rotation'?'rotate':'translate');d=view.fivePlacement;if(!d)return;
    Object.assign(d,{panel:true,panelPosition:state.position.map(transformNumber),panelRotation:state.rotation.map(transformNumber),axis:group==='scale'?'xyz':'x'});
  }
  if(group==='scale')d.value=value;else (group==='position'?d.panelPosition:d.panelRotation)[index]=value;
  view.error='';syncPlacementPreview(view);
}

export function disposePlacementPreview(view) {
  const d = view.fivePlacement;
  if (d?.previewViewport)for(const id of d.previewIds||[d.placementId])d.previewViewport.clearTransformPreview(id);
  if (d?.gizmoViewport?.transformManipulator?.options?.owner==='workpiece-placement') d.gizmoViewport.clearTransformManipulator();
  if (d) { d.previewViewport=null;d.previewIds=null; d.gizmoViewport=null; d.previewKey=''; }
}

export function placementSpaceChanged(view, space) {
  const d = view.fivePlacement;
  if (d?.open && (d.request || d.saving)) { view.viewport.setTransformSpace(d.space); return; }
  view.fivePlacementSpace = space;
  if (!d?.open || d.space === space) return;
  // Only Enter submits typed values. Changing axes/frame discards unsubmitted input.
  d.base=[...d.original]; d.value=initialValue(d); d.planeValues=['0','0','0']; d.appliedScale=1; d.space=space; view.error='';
}

const placeHud=(context,point)=>positionTransformHud(context,point,'placement');
const focusValue=context=>focusTransformValue(context,'data-five-placement-field');

function syncManipulator(context,view,ops,matrix) {
  const d=view.fivePlacement,vp=view.viewport;
  vp.setTransformManipulator({owner:'workpiece-placement',mode:d.mode,space:d.space,screenRotate:true,activeAxis:d.axis,planeTranslate:d.mode==='translate',uniformScale:d.mode==='scale',position:center(d,matrix).toArray(),
    quaternion:orientation(rowMajor(matrix)).toArray(),enabled:!d.request&&!d.saving,
    snap:placementSnapOptions(vp,new THREE.Vector3().setFromMatrixPosition(draftWorld(d,new THREE.Matrix4().set(...d.original))).toArray()),
    onPosition:p=>placeHud(context,p),
    onStart:({axis,direction})=>{
      d.gesture=Object.fromEntries(['base','value','planeValues','axis','rotationAxis','appliedScale'].map(k=>[k,structuredClone(d[k])]));
      d.base=[...d.original]; d.value=initialValue(d); d.planeValues=['0','0','0']; d.appliedScale=1; d.axis=axis.toLowerCase(); view.error='';
      d.rotationAxis=direction;
      ops.renderProject(context,view);
    },
    onChange:({translation,angle,scale,snap})=>{
      if(!d.gesture)return;
      if(d.mode==='translate'){
        const offset=new THREE.Vector3(...translation);
        if(d.space==='local')offset.applyQuaternion(orientation(rowMajor(draftWorld(d,new THREE.Matrix4().set(...d.base)))).invert());
        if (planeSelected(d)) d.planeValues=offset.toArray().map((value,i)=>d.axis.includes('xyz'[i])?format(value):'0');
        else d.value=format(offset[d.axis]);
      }else if(d.mode==='scale')d.value=format(Math.max(1e-6,scale['xyz'.indexOf(d.axis[0])]));
      else d.value=format(angle*180/Math.PI);
      updatePlacementSnapFeedback(context,snap);
      syncPlacementPreview(view);
      for (const input of context.mount.querySelectorAll('[data-five-placement-field]')) {
        const name=input.dataset.fivePlacementField;
        input.value=name==='value'?d.value:d.planeValues['xyz'.indexOf(name)];
      }
    },
    onEnd:({moved})=>{
      delete d.gesture;
      if(moved)void placementCommand(context,view,'apply',ops,true);
      else {ops.renderProject(context,view);focusValue(context);}
    },
    onCancel:()=>{if(d.gesture){Object.assign(d,d.gesture);delete d.gesture;ops.renderProject(context,view);}},
  });
  d.gizmoViewport=vp;
}

export function syncPlacementInteraction(context,view,ops) {
  syncTransformParameters(view);
  view.viewport?.setCameraNavigationEnabled(true);
  if(workpieceLocked(view)||view.fiveWorkpieceEnable?.request||view.fiveWorkpieceLock?.request||view.fiveWorkpieceRemoval?.request){disposePlacementPreview(view);return;}
  const d=view.fivePlacement;
  syncPlacementPreview(view);
  if(d?.oneShot){if(view.viewport?.transformManipulator?.options?.owner==='workpiece-placement')view.viewport.clearTransformManipulator();return;}
  if(!d?.open||d.panel||activeTool(view)!==d.mode||!d.previewViewport)return;
  let matrix;
  try {matrix=placementMatrix(d);}catch{matrix=new THREE.Matrix4().set(...d.original);}
  syncManipulator(context,view,ops,draftWorld(d,matrix));
}

export function syncPlacementPreview(view) {
  syncTransformParameters(view);
  const d=view.fivePlacement; if(!d)return;
  if(d.panel&&!placementArea(view,d.mode)||!d.panel&&activeTool(view)!==d.mode||!d.open||(!d.saving&&!d.request&&d.key!==sourceKey(view))){
    disposePlacementPreview(view); if(!d.request)d.open=false; return;
  }
  const vp=view.viewport; if(!vp)return;
  try{d.parentWorld=rowMajor(setupReferenceWorldFrame(view,d.setupId));}catch(error){view.error=error.message;disposePlacementPreview(view);return;}
  let matrix;
  try {matrix=d.request?.transform?new THREE.Matrix4().set(...d.request.transform):placementMatrix(d);}
  catch {matrix=new THREE.Matrix4().set(...d.original);}
  matrix=draftWorld(d,matrix);
  const signature=JSON.stringify(matrix.elements);
  if(d.previewViewport!==vp)disposePlacementPreview(view);
  if(d.previewKey!==signature||!vp.transformPreviews.has(d.placementId)){
    vp.setTransformPreview(d.placementId,matrix.elements);d.previewViewport=vp;d.previewKey=signature;d.previewIds=new Set([d.placementId]);
    if(d.motionScope==='Assembly'&&d.mode!=='scale'){
      const placement=view.scene.domain.placements.find(p=>p.id===d.placementId);
      const frame=matrix.clone().multiply(new THREE.Matrix4().set(...placement.transform).invert());
      for(const part of view.scene.domain.fixtures||[])if(part.setupId===d.setupId){vp.setTransformPreview(part.id,frame.clone().multiply(new THREE.Matrix4().set(...part.transform)).elements);d.previewIds.add(part.id);}
    }
  }
}

export function placementFieldChanged(view,name,value) {
  const d=view.fivePlacement;
  if(!d?.open||!d.axis||d.saving||d.request)return;
  if(planeSelected(d)){
    const index='xyz'.indexOf(name);if(index<0||!d.axis.includes(name))return;
    d.planeValues[index]=value;
  }else{if(name!=='value')return;d.value=value;}
  view.error='';syncPlacementPreview(view);
}

export function renderPlacement(view) {
  if(workpieceLocked(view))return '';
  const d=view.fivePlacement;if(!d?.open||d.panel)return '';
  if(!d.saving&&!d.request&&d.key!==sourceKey(view)){
    d.open=false;disposePlacementPreview(view);return '';
  }
  if(d.oneShot){
    const busy=!!(d.saving||d.request),check='<svg viewBox="0 0 20 20" aria-hidden="true"><path d="m4 10 4 4 8-8"/></svg>',close='<svg viewBox="0 0 20 20" aria-hidden="true"><path d="m5 5 10 10M15 5 5 15"/></svg>';
    return `<section class="five-position-dialog five-floating-dialog five-scale-operation" data-five-key="scale-operation" data-five-floating="scale" ${floatingDialogStyle(view,'scale')} role="dialog" aria-label="缩放工件"><header class="five-position-title" data-five-dialog-drag><strong>缩放</strong><button type="button" data-five-command="five.placement.close" aria-label="取消缩放" title="取消" ${busy?'disabled':''}>${close}</button></header><div class="five-scale-body"><label for="five-scale-factor">比例</label><input id="five-scale-factor" data-five-key="placement-input-value" data-five-placement-field="value" aria-label="缩放比例" type="text" inputmode="decimal" autocomplete="off" value="${attr(d.value)}" ${busy?'readonly':''}><small>×</small><button type="button" data-five-command="five.placement.apply" title="执行缩放" aria-label="执行缩放" ${d.saving?'disabled':''}>${check}</button><p>XYZ 等比缩放，所有装夹共用新几何。</p>${d.saving?'<p role="status">正在缩放…</p>':view.error?`<p class="five-placement-error" role="alert">${esc(view.error)}</p>`:''}</div></section>`;
  }
  const toolbar=renderTransformTools({owner:'placement',title:d.motionScope==='Assembly'?'装夹总成变换':'工件变换',mode:d.mode,busy:view.pending||d.request,modes:(geometryEditing(view)?[['scale','缩放']]:[['translate','平移'],['rotate','旋转']]).map(([mode,title])=>[mode,title,'five.tool.'+mode])});
  if(!d.axis)return toolbar;
  const title=d.mode==='translate'?'位移':d.mode==='scale'?'缩放比例':'角度',busy=d.saving||!!d.request;
  const fields=planeSelected(d)?[...d.axis].map(axis=>({name:axis,label:axis.toUpperCase(),value:d.planeValues['xyz'.indexOf(axis)],aria:`${axis.toUpperCase()} 位移`}))
    :[{name:'value',label:d.mode==='scale'?'XYZ':d.axis==='e'?'视图':d.axis.toUpperCase(),value:d.value,aria:title}];
  return toolbar+renderTransformValue({owner:'placement',mode:d.mode,axis:d.axis,fields,attribute:'data-five-placement-field',busy,error:view.error});
}

export async function placementCommand(context,view,action,ops,focusAfterDrag=false) {
  const d=view.fivePlacement;if(!d?.open||view.pending)return;
  if(d.saving)return;
  if(action==='close'){
    if(d.request)return;
    if(d.oneShot){disposePlacementPreview(view);view.fivePlacement=null;selectTool(view,'select');view.error='';ops.renderProject(context,view);return;}
    // Escape/right click discards this gesture, but the workpiece stays active.
    openPlacement(view,activeTool(view));view.error='';ops.renderProject(context,view);return;
  }
  if(action!=='apply'||!d.axis||!current(context,view))return;
  if(!d.request){
    if(d.key!==sourceKey(view)){d.open=false;disposePlacementPreview(view);ops.renderProject(context,view);return;}
    let transform;
    try {transform=rowMajor(placementMatrix(d));}catch(error){view.error=error.message;ops.renderProject(context,view);return;}
    if(!transform.some((v,i)=>Math.abs(v-d.original[i])>1e-10)){if(d.oneShot){disposePlacementPreview(view);view.fivePlacement=null;selectTool(view,'select');ops.renderProject(context,view);}return;}
    d.request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),
      setupId:d.setupId,placementId:d.placementId,transform};
    if(d.mode==='scale'){
      delete d.request.transform;
      const workpiece=view.scene.domain.workpieces.find(w=>w.id===selectedPlacement(view).workpieceId);
      Object.assign(d.request,{sourceGeometry:structuredClone(workpiece.geometryRef),factors:scaleFactors(d),center:[...d.localCenter]});
    }
  }
  const request=d.request,focusBefore=context.mount.ownerDocument.activeElement;
  d.saving=true;view.error='';ops.renderProject(context,view);
  try {
    let committed;
    try {
      const result=await context.sceneProxy.invoke(d.mode==='scale'?'CamWorkpiece.Scale':'CamPlacement.SetTransform',request);
      committed=result;
      if(d.mode==='scale')d.scaleTask=result;
    }
    catch(error){
      let receipt;
      try {receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});}catch{throw error;}
      if(d.mode==='scale'&&['Running','Completed'].includes(receipt.status))d.scaleTask=receipt;
      else if(receipt.status!=='Completed'){if(receipt.status==='NotFound')d.request=null;throw error;}
      else committed=receipt.result;
    }
    if(d.mode==='scale'){
      let task=d.scaleTask;
      while(task?.status==='Running'&&!view.disposed){
        await new Promise(resolve=>setTimeout(resolve,100));
        task=await context.sceneProxy.invoke('CamWorkpiece.QueryTask',{epoch:task.epoch,taskId:task.taskId});d.scaleTask=task;
      }
      if(view.disposed){
        if(task?.status==='Running')void context.sceneProxy.invoke('CamWorkpiece.CancelTask',{epoch:task.epoch,taskId:task.taskId}).catch(()=>{});
        return;
      }
      if(task&&task.status!=='Completed'){
        d.request=null;throw Error(task.status==='Superseded'?'工程已变化，请重新打开缩放。':task.error||'缩放已取消。');
      }
      // The new mesh already contains the scale. Remove the relative preview
      // before refreshing resources so it is never applied a second time.
      d.previewViewport?.clearTransformPreview(d.placementId);d.previewKey='';d.appliedScale=readNumber(d.value);
    }
    if(d.mode==='scale'){
      d.request=null;await saveCommittedProjectEdit(context,view,ops);
      if(current(context,view)&&await refreshWorkpieceGeometry(context,view,ops)===false)throw Error('缩放已保存，模型显示更新失败，请刷新后继续。');
    }else if(current(context,view)){
      if(!applyPlacementReceipt(view,request,committed)){
        const state=await readMachiningState(context,undefined,{waitForGeneration:false});
        if(current(context,view))mergeDomainState(view.scene,state);
      }
      d.request=null;
      // The autosave writer coalesces consecutive edits. Disk persistence must
      // never hold a placement gesture or trigger a complete Scene/View read.
      void saveCommittedProjectEdit(context,view,ops);
    }
    if(request.transform)d.original=[...request.transform];d.key=sourceKey(view);d.request=null;
    if(d.oneShot&&view.fivePlacement===d){disposePlacementPreview(view);view.fivePlacement=null;selectTool(view,'select');}
    ops.appendProjectLog(context,'ok',d.mode==='scale'?'工件已缩放，请重新提取刀路。':committed?.motionScope==='Assembly'?`工件与工装已整体${d.mode==='translate'?'平移':'旋转'}。`:`工件已${d.mode==='translate'?'平移':'旋转'}，后续加工结果需重新生成。`);
  }catch(error){
    if(current(context,view))view.error=d.request?'结果未确认，按 Enter 重试。'
      :/EXPECTED|EPOCH|SEQUENCE|REVISION/.test(error.message)?'工程已变化，请重新打开操作。':error.message;
  }finally{
    d.saving=false;
    if(current(context,view)){
      ops.renderProject(context,view);
      if(focusAfterDrag&&view.fivePlacement===d&&d.open&&context.mount.ownerDocument.activeElement===focusBefore)focusValue(context);
    }
  }
}
