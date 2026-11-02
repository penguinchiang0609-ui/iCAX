import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {activeTool} from './workspaceModel.mjs';
import {escapeText as esc} from '../../_shared/workbench/utils/format.mjs';
import {sceneColors} from './scenePalette.mjs';
import {renderRibbonCommandIcon} from '../../../iCAX-UI/SDK/AppShell/app/ribbonIcons.mjs';
import {rapidPointPose,rapidPointPoseField,renderRapidPointPose,validateRapidPointPose} from './rapidPathPose.mjs';
import {directionToEuler,toolAxisNotation} from './toolAxisAngles.mjs';
import {renderTransformTools,renderTransformValue,positionTransformHud,positionWorldTransformHud} from './transformHud.mjs';
import {isConstructionEdit,createRapidPathEdit,constructionHandleMoved,constructionLiftChanged,constructionManualPath,adoptConstructionReadback} from './rapidConstructionEditing.mjs';
import {hasRapidGeometry} from './rapidGeometry.mjs';
import {rapidDependencyKey} from './rapidDependencies.mjs';
import {isRapidEndpoint,updateRapidEndpoints} from './rapidEndpointEditing.mjs';

const setup=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
const transition=view=>setup(view)?.generatedGeometry?.transitions?.find(t=>`${t.from}/${t.to}`===view.fiveRapid?.id);
const locked=e=>e.selectedPoints?.length===0;
const format=n=>String(Number(n.toFixed(6)));
const coords=e=>{e.values=e.points[e.selected].map(format);};
const current=(context,view,state)=>!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveRapid===state;
const invalidate=view=>{const s=view.fiveRapid;s.cancel?.();s.token++;s.preview=null;s.running=false;s.dirty=true;view.error='';};
const snapshot=e=>({points:structuredClone(e.points),directions:structuredClone(e.directions),selected:e.selected,selectedPoints:structuredClone(e.selectedPoints),changed:e.changed,revision:e.revision,
 construction:structuredClone(e.construction),sourceStart:structuredClone(e.sourceStart),sourceEnd:structuredClone(e.sourceEnd),endpointOverrides:structuredClone(e.endpointOverrides),samplePoints:structuredClone(e.samplePoints),sampleDirections:structuredClone(e.sampleDirections),liftInput:e.liftInput});
const selectedPoints=e=>(e.selectedPoints||[e.selected]).filter(i=>i>=0&&i<e.points.length);
const settleAcknowledgedEdit=(s,e)=>{if(e.revision!==e.savedRevision)return false;e.changed=false;s.dirty=!!(s.policyChanged||s.settingsChanged||s.automaticChanged);s.preview=null;return true;};
function movePoints(e,position,before=e){
 if(isConstructionEdit(e)&&!isRapidEndpoint(e)){constructionHandleMoved(e,position,before);return;}
 const delta=position.map((v,i)=>v-before.points[before.selected][i]);
 for(const index of selectedPoints(before))e.points[index]=before.points[index].map((v,i)=>v+delta[i]);
 updateRapidEndpoints(e,selectedPoints(before));
}
export function refreshRapidPathEdit(view,source){
 const e=view.fiveRapid?.pathEdit;if(!e||!isConstructionEdit(e)||!source?.construction||e.gesture||e.changed||view.fiveRapid.hudEditing)return false;
 return adoptConstructionReadback(e,source);
}
export function beginRapidHudEdit(view){
 const s=view.fiveRapid,e=s?.pathEdit;if(!e||view.fiveHistoryBusy||locked(e)||e.inserting||e.deleting||s.topologySaving)return false;
 if(s.hudEditing?.edit===e)return true;
 clearTimeout(s.saveTimer);s.saveTimer=null;s.hudEditing={edit:e,before:{...snapshot(e),values:[...e.values],poseDraft:e.poseDraft?structuredClone(e.poseDraft):undefined},dirty:s.dirty,preview:s.preview,error:view.error};return true;
}
export function finishRapidHudEdit(view,cancel=false){
 const s=view.fiveRapid,h=s?.hudEditing;if(!h)return false;delete s.hudEditing;
 if(h.edit!==s.pathEdit)return false;
 if(cancel){Object.assign(h.edit,h.before);s.dirty=h.dirty;s.preview=h.preview;view.error=h.error||'';}
 settleAcknowledgedEdit(s,h.edit);
 return true;
}
const changed=(view,topology=false)=>{const e=view.fiveRapid.pathEdit;e.changed=true;e.revision++;invalidate(view);if(!e.gesture){if(topology)view.fiveRapid.saveTopology?.();else view.fiveRapid.savePath?.();}};
const select=(view,index,toggle=false)=>{const e=view.fiveRapid.pathEdit;if((index!==e.selected||toggle)&&!canChangePoint(view))return;
 if(toggle&&!isConstructionEdit(e)){const selection=new Set(selectedPoints(e));if(selection.has(index))selection.delete(index);else selection.add(index);e.selectedPoints=[...selection].sort((a,b)=>a-b);e.selected=e.selectedPoints.includes(index)?index:e.selectedPoints[0]??index;}
 else{const preserve=!toggle&&selectedPoints(e).includes(index);e.selected=index;e.selectedPoints=preserve?selectedPoints(e):[index];}
 e.typing=false;coords(e);};
const validCoordinates=e=>e.values.every(v=>String(v).trim()&&Number.isFinite(Number(v))&&Math.abs(Number(v))<=1e8);
function canChangePoint(view){if(view.fiveRapid.topologySaving)return false;if(view.fiveRapid.topologyError){view.error='路径点尚未保存，请先重试保存。';return false;}const e=view.fiveRapid.pathEdit;if(!validCoordinates(e)){view.error='请输入有效的路径点坐标。';return false;}try{if(isConstructionEdit(e))constructionManualPath(e);if(!isConstructionEdit(e)||isRapidEndpoint(e))validateRapidPointPose(e);return true;}catch(error){view.error=error.message;return false;}}
export function cancelRapidPathMode(view){const e=view.fiveRapid?.pathEdit;if(!e||!e.inserting&&!e.deleting)return false;e.inserting=false;e.deleting=false;return true;}
export function hasManualRapidPath(view){return !!view.fiveRapid?.pathEdit||!view.fiveRapid?.useAutomatic&&!!transition(view)?.manualPath;}
export function manualRapidPath(view){
  const e=view.fiveRapid?.pathEdit;if(!e)return view.fiveRapid?.useAutomatic?undefined:transition(view)?.manualPath;
  if(e.values.some(v=>!String(v).trim()||!Number.isFinite(Number(v))||Math.abs(Number(v))>1e8))throw Error('请输入有效的路径点坐标。');
  if(isConstructionEdit(e)){if(isRapidEndpoint(e))validateRapidPointPose(e);return constructionManualPath(e);}
  validateRapidPointPose(e);
  return {points:structuredClone(e.points.slice(1,-1)),directions:structuredClone(e.directions.slice(1,-1)),...structuredClone(e.endpointOverrides||{})};
}
export function manualRapidDraft(view){
  const s=view.fiveRapid,e=s?.pathEdit;if(!e)return null;
  const saved=transition(view),checked=s.preview?.result||(!e.changed&&!e.gesture?(saved?.planningMethod==='Manual'?saved:e.initialResult):null),clear=checked?.geometryStatus==='Generated'&&checked.validation==='Clear';
  const checkedShape=hasRapidGeometry(checked)?checked:null;
  return {points:isConstructionEdit(e)?checkedShape?.points||e.samplePoints:e.points,directions:isConstructionEdit(e)?checkedShape?.directions||e.sampleDirections:e.directions,frame:'Setup',referencePoint:'ToolTip',planningMethod:'Manual',geometryStatus:checked?.geometryStatus==='Failed'?'Failed':clear?'Generated':'Draft',validation:clear?'Clear':checked?.validation==='Collision'?'Collision':'Unchecked',reason:checked?.reason,obstacleId:checked?.obstacleId,failedSegment:checked?.failedSegment,...(!clear?{editingOnly:true,productionQualified:false,machineValidation:'Unchecked'}:{}),manualDraft:true};
}
export function rapidPathFieldChanged(view,name,value){
  const s=view.fiveRapid,e=s?.pathEdit;if(!e||view.pending||view.fiveHistoryBusy||s.request)return false;
  if(s.topologySaving||s.topologyError)return true;
  if(isConstructionEdit(e)&&name==='pathLiftHeight'){
    if(isRapidEndpoint(e)||e.construction.kind==='Follow'||e.construction.turnPoint!==undefined)return true;
    e.typing=true;
    if(constructionLiftChanged(e,value)){coords(e);changed(view);}else{e.changed=true;e.revision++;invalidate(view);}
    s.savePath?.();return true;
  }
  if(isConstructionEdit(e)&&!isRapidEndpoint(e)&&['notation','0','1','2','angle-0','angle-1','angle-2'].includes(name))return true;
  const previousDirection=[...e.directions[e.selected]];
  if(rapidPointPoseField(view,name,value)){if(name!=='notation'&&!locked(e)){
    if(!view.error&&selectedPoints(e).length>1){const q=new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(...previousDirection),new THREE.Vector3(...e.directions[e.selected]));for(const index of selectedPoints(e))if(index!==e.selected)e.directions[index]=new THREE.Vector3(...e.directions[index]).applyQuaternion(q).normalize().toArray();}
    if(!view.error)updateRapidEndpoints(e,selectedPoints(e));
    e.changed=true;e.revision++;const error=view.error;invalidate(view);view.error=error;s.savePath?.();}return true;}
  if(name==='pathPoint'){select(view,Number(value));return true;}
  if(!/^path[XYZ]$/.test(name))return false;if(locked(e))return true;
  const i='XYZ'.indexOf(name.at(-1));e.values[i]=value;e.changed=true;e.revision++;
  if(e.values.every(v=>String(v).trim()&&Number.isFinite(Number(v)))){
    const p=e.values.map(Number);if(p.some(v=>Math.abs(v)>1e8)){invalidate(view);view.error='路径点坐标超出范围。';s.savePath?.();return true;}
    if(p.some((v,j)=>v!==e.points[e.selected][j])){e.typing=true;movePoints(e,p);if(isConstructionEdit(e)&&e.construction.kind==='Follow')coords(e);changed(view);}
  }else invalidate(view);
  s.savePath?.();
  return true;
}
export function renderRapidPathEditor(view,disabled){
  const s=view.fiveRapid,e=s?.editClosing?null:s?.pathEdit,busy=disabled||s?.topologySaving||s?.editClosing,b=(action,label,off=false,pressed,icon)=>`<button type="button" data-five-key="rapid-path-action-${action}" data-five-command="five.rapid.path-${action}" ${pressed===undefined?'':`aria-pressed="${pressed}"`} ${off||busy&&pressed!==true?'disabled':''}>${icon?renderRibbonCommandIcon(icon):''}<span>${label}</span></button>`;
  if(!e)return `<section class="five-rapid-point-editor" data-five-key="rapid-point-editor"><strong>${hasManualRapidPath(view)?'已保存手动路径':'路径形状'}</strong><div class="five-rapid-point-actions">${b('edit','编辑路径')}${hasManualRapidPath(view)?b('automatic','恢复自动路径'):''}</div><small>可调整本条空移的位置与姿态，包括两端；修改后单独复检。</small></section>`;
  if(isConstructionEdit(e))return `<section class="five-rapid-point-editor" data-five-key="rapid-point-editor"><strong>${e.construction.turnPoint!==undefined?'转向位置':'曲线形状'}</strong>
    <small role="status" data-five-key="rapid-point-mode-hint">${e.construction.turnPoint!==undefined?'拖动转向位置，退刀高度保持不变。':e.construction.kind==='Follow'?'拖动侧向引导柄；松开后按工件曲面重新计算。随形高度遵循工艺设置。':'拖动形状柄，同时调整高度和侧向引导。'} 起终点可单独调整位置和姿态。</small>
    <label class="five-rapid-field"><span>当前控制柄</span><select data-five-rapid-field="pathPoint" ${busy?'disabled':''}>${e.points.map((_,i)=>`<option value="${i}" ${i===e.selected?'selected':''}>${i===0?'起点':i===2?'终点':e.construction.turnPoint!==undefined?'转向位置':'形状控制柄'}</option>`).join('')}</select></label>
    ${e.construction.kind!=='Follow'?`<label class="five-rapid-field" data-five-key="rapid-shape-lift"><span>${e.construction.kind==='Arch'?'拱高':'退刀高度'}</span><input data-five-key="rapid-shape-lift-value" data-five-rapid-field="pathLiftHeight" type="text" inputmode="decimal" aria-label="曲线高度" value="${esc(e.liftInput??format(e.construction.liftHeightMm))}" ${busy||isRapidEndpoint(e)||locked(e)||e.construction.turnPoint!==undefined?'readonly':''}><small>mm</small></label>`:''}
    <div class="five-rapid-point-coordinates">${['X','Y','Z'].map((axis,i)=>`<label data-five-key="rapid-point-${axis}"><span>${axis}</span><input type="text" inputmode="decimal" data-five-key="rapid-point-value-${axis}" data-five-rapid-field="path${axis}" aria-label="控制柄 ${axis}" value="${esc(e.values[i])}" ${busy||s.topologyError||locked(e)?'readonly':''}></label>`).join('')}</div><small>装夹坐标 · mm · 只修改本条空移。</small>
    <div class="five-rapid-point-actions">${s.topologyError?b('retry','重试保存'):''}${b('cancel','结束编辑')}${b('automatic','恢复自动路径')}</div>${isRapidEndpoint(e)?`<div class="five-pose-action-grid">${b('position','移动位置',false,!e.poseEditing,'move')}${b('pose','调整姿态',false,!!e.poseEditing,'tool-axis')}</div>${renderRapidPointPose(view,busy||s.topologyError)}`:''}</section>`;
  const hint=e.inserting?'连续插入：点击当前空移线段依次插入折点并自动保存；再次点击“插入点”或按 Esc 退出。':e.deleting?'连续删除：点击中间点依次删除并自动保存；起终点不能删除。再次点击“删除点”或按 Esc 退出。':'Shift 单击多选，单击线段选择相邻中间点；拖动一次移动整组。双击线段插入点，检查时不自动改线。';
  return `<section class="five-rapid-point-editor" data-five-key="rapid-point-editor"><strong>${e.pathKind==='Polyline'?'折线控制点':'手动编辑路径'}</strong>
    <div class="five-rapid-point-actions">${b('select-all','整体移动中间段',e.points.length<=2,selectedPoints(e).length===e.points.length-2)}${b('select-single','只移动当前点',locked(e),selectedPoints(e).length===1)}</div><small data-five-key="rapid-point-selection">已选 ${selectedPoints(e).length} 个关键点；只修改本条空移。</small>
    <div class="five-pose-action-grid" role="group" aria-label="空移点编辑">${b('insert','插入点',false,!!e.inserting,'keypoint-add')}${b('delete','删除点',e.points.length<=2&&!e.deleting,!!e.deleting,'keypoint-delete')}</div><small role="status" data-five-key="rapid-point-mode-hint">${hint}</small>
    <label class="five-rapid-field"><span>当前路径点</span><select data-five-rapid-field="pathPoint" ${busy?'disabled':''}>${e.points.map((_,i)=>`<option value="${i}" ${i===e.selected?'selected':''}>${i===0?'起点':i===e.points.length-1?'终点':`中间点 ${i}`}</option>`).join('')}</select></label>
    <div class="five-rapid-point-coordinates">${['X','Y','Z'].map((axis,i)=>`<label data-five-key="rapid-point-${axis}"><span>${axis}</span><input type="text" inputmode="decimal" data-five-key="rapid-point-value-${axis}" data-five-rapid-field="path${axis}" aria-label="路径点 ${axis}" value="${esc(e.values[i])}" ${busy||s.topologyError||locked(e)?'readonly':''}></label>`).join('')}</div><small>装夹坐标 · mm${locked(e)?' · 请先选择一个关键点':''}${s.topologySaving?' · 正在保存当前点…':''}</small>
    <div class="five-pose-action-grid" role="group" aria-label="空移点操作">${b('position','移动位置',false,!e.poseEditing,'move')}${b('pose','调整姿态',false,!!e.poseEditing,'tool-axis')}</div>${renderRapidPointPose(view,busy||s.topologyError)}
    <div class="five-rapid-point-actions">${s.topologyError?b('retry','重试保存'):''}${b('cancel','结束编辑')}${b('automatic','恢复自动路径')}</div></section>`;
}
export function renderRapidPathTransform(view){
 const s=view.fiveRapid,e=s?.pathEdit;
 if(view.fiveAxisArea!=='toolpath'||activeTool(view)!=='rapid'||!s?.open||!e||s.editClosing||locked(e)||s.settings||s.key!==rapidDependencyKey(view))return '';
 const shape=isConstructionEdit(e)&&!isRapidEndpoint(e),mode=!shape&&e.poseEditing?'rotate':'translate',busy=!!(view.pending||s.request||s.topologySaving||s.topologyError),selecting=e.inserting||e.deleting;
 const tools=renderTransformTools({owner:'rapid',title:shape?(e.construction.turnPoint!==undefined?'转向位置':'曲线形状控制'):selectedPoints(e).length>1?'空移整组变换':'空移点变换',mode:selecting?'':mode,busy,modes:shape?[['translate','调整形状','five.rapid.path-position']]:[['translate','平移','five.rapid.path-position'],['rotate','旋转','five.rapid.path-pose']]});
 if(selecting)return tools;
 let values=e.values,names=['pathX','pathY','pathZ'],axes=['X','Y','Z'],unit='mm',error='';
 if(!shape&&e.poseEditing){const d=rapidPointPose(e),euler=toolAxisNotation(view)==='Euler';names=euler?['angle-0','angle-1','angle-2']:['0','1','2'];axes=euler?['X','Y','Z']:['I','J','K'];unit=euler?'°':' ';
  if(euler){try{values=d.eulerInput?.values||directionToEuler(d.values).map(format);}catch{values=['','','0'];}}else values=d.values;error=d.error;
 }
 return tools+renderTransformValue({owner:'rapid',mode,title:shape?(e.construction.turnPoint!==undefined?'装夹坐标 · 转向位置':'装夹坐标 · 曲线控制柄'):e.poseEditing?(unit==='°'?'装夹坐标 · 姿态角':'装夹坐标 · 刀轴方向'):'装夹坐标 · 位置',fields:axes.map((label,i)=>({label,name:names[i],value:values[i],aria:`浮动空移${!shape&&e.poseEditing?'姿态':'位置'} ${label}`,unit,readonly:!shape&&e.poseEditing&&unit==='°'&&i===2})),attribute:'data-five-rapid-field',busy,error});
}
export async function rapidPathCommand(context,view,action,ops,check){
  if(!action.startsWith('path-'))return false;const s=view.fiveRapid;if(!s||!current(context,view,s)||s.settings)return true;
  const name=action.slice(5);
  if(name==='insert'&&s.pathEdit?.inserting||name==='delete'&&s.pathEdit?.deleting){cancelRapidPathMode(view);ops.renderProject(context,view);return true;}
  if(view.pending||s.request)return true;
  const t=transition(view);if(!t)return true;
  if(name==='edit'){
    if(s.pathEdit)return true;const token=++s.token,polyline=!s.useAutomatic&&!!t.manualPath?.points;let source=hasRapidGeometry(s.preview?.result)?s.preview.result:t;
    if(!hasRapidGeometry(source)||!polyline&&!source.construction&&source.planningMethod!=='BoxEnvelope'&&!['Failed','Draft'].includes(source.geometryStatus)){
      s.running=true;ops.renderProject(context,view);
      try{source=await context.sceneProxy.invoke('CamRapid.ReadEditPath',{expected:structuredClone(view.scene.domain.expected),setupId:setup(view).id,geometryRevision:setup(view).generatedGeometry.revision,from:t.from,to:t.to,policy:Object.fromEntries(Object.entries(s.policy).map(([k,v])=>[k,k==='strategy'?v:Number(v)])),...(!s.useAutomatic&&t.manualPath?{manualPath:t.manualPath}:{})});}
      catch(error){if(current(context,view,s)&&token===s.token){view.error=error.message;s.running=false;ops.renderProject(context,view);}return true;}
      finally{if(current(context,view,s)&&token===s.token)s.running=false;}
    }
    if(!current(context,view,s)||token!==s.token)return true;
    if(!hasRapidGeometry(source)){view.error='当前连接没有可编辑的路径，请点击“重新生成当前空移”后重试。';ops.renderProject(context,view);return true;}
    if(!source.construction&&source.points.length>2050){view.error='路径点过多，无法进入手动编辑。';ops.renderProject(context,view);return true;}
    try{s.pathEdit=createRapidPathEdit(source,polyline||!source.construction);if(source.geometryStatus==='Failed')s.pathEdit.initialResult=Object.fromEntries(['geometryStatus','validation','reason','obstacleId','failedSegment'].map(name=>[name,source[name]]));}
    catch(error){view.error=error.message;ops.renderProject(context,view);return true;}
    s.pathEdit.endpointOverrides=structuredClone(source.endpointOverrides||Object.fromEntries(['start','end'].filter(k=>t.manualPath?.[k]).map(k=>[k,t.manualPath[k]])));coords(s.pathEdit);s.useAutomatic=false;s.preview=null;view.error='';
  }else if(name==='cancel'){invalidate(view);s.pathEdit=null;s.useAutomatic=false;s.dirty=false;s.policy=structuredClone(t.policy);}
  else if(name==='retry')await s.saveTopology?.();
  else if(s.pathEdit){const e=s.pathEdit;
    if(name==='select-all'&&!isConstructionEdit(e)&&canChangePoint(view)){e.selectedPoints=e.points.map((_,i)=>i).slice(1,-1);if(!e.selectedPoints.includes(e.selected))e.selected=e.selectedPoints[0]??0;coords(e);}
    else if(name==='select-single'&&!isConstructionEdit(e)&&canChangePoint(view)){e.selectedPoints=[e.selected];}
    else if(name==='insert'&&!isConstructionEdit(e)){e.inserting=!e.inserting;e.deleting=false;}
    else if(name==='delete'&&!isConstructionEdit(e)){e.deleting=!e.deleting&&e.points.length>2;e.inserting=false;}
    else if(name==='position'||name==='pose'&&(!isConstructionEdit(e)||isRapidEndpoint(e))){if(canChangePoint(view)){e.poseEditing=name==='pose';e.inserting=false;e.deleting=false;}}
    else if(name==='delete-point'&&!isConstructionEdit(e)&&!locked(e)&&canChangePoint(view))removeSelectedPoints(view);
    e.typing=false;
  }
  ops.renderProject(context,view);return true;
}
function nearPoint(view,event){
  const e=view.fiveRapid.pathEdit,vp=view.viewport,r=vp.renderer.domElement.getBoundingClientRect();let best=11,index=-1;
  e.points.forEach((point,i)=>{const p=new THREE.Vector3(...point).project(vp.camera);if(Math.abs(p.z)>1)return;const d=Math.hypot(r.x+(p.x+1)*r.width/2-event.clientX,r.y+(1-p.y)*r.height/2-event.clientY);if(d<best){best=d;index=i;}});return index;
}
function nearSegment(view,event){
  const e=view.fiveRapid.pathEdit,vp=view.viewport,r=vp.renderer.domElement.getBoundingClientRect();let best=10,hit=null;
  for(let i=1;i<e.points.length;i++){const a=new THREE.Vector3(...e.points[i-1]).project(vp.camera),b=new THREE.Vector3(...e.points[i]).project(vp.camera);if(Math.abs(a.z)>1||Math.abs(b.z)>1)continue;
    const x=r.x+(a.x+1)*r.width/2,y=r.y+(1-a.y)*r.height/2,dx=(b.x-a.x)*r.width/2,dy=(a.y-b.y)*r.height/2,u=Math.max(0,Math.min(1,((event.clientX-x)*dx+(event.clientY-y)*dy)/(dx*dx+dy*dy||1))),distance=Math.hypot(event.clientX-x-u*dx,event.clientY-y-u*dy);
    if(distance<best){best=distance;hit={i,u};}}
  if(hit){const ray=new THREE.Raycaster();ray.setFromCamera(new THREE.Vector2((event.clientX-r.x)/r.width*2-1,1-(event.clientY-r.y)/r.height*2),vp.camera);const p=new THREE.Vector3(),a=new THREE.Vector3(...e.points[hit.i-1]),b=new THREE.Vector3(...e.points[hit.i]);ray.ray.distanceSqToSegment(a,b,undefined,p);hit.point=p.toArray();hit.u=a.distanceTo(b)>1e-9?a.distanceTo(p)/a.distanceTo(b):.5;}return hit;
}
export function rapidPathPointAtPointer(view,event){
  const e=view.fiveRapid?.pathEdit;if(!e)return null;
  if(e.inserting){const hit=nearSegment(view,event);return hit?{point:hit.point,index:hit.i}:null;}
  const index=nearPoint(view,event);return index>=0&&(!e.deleting||index>0&&index<e.points.length-1)?{point:e.points[index],index}:null;
}
function insert(view,hit){
  const e=view.fiveRapid.pathEdit;if(!canChangePoint(view))return;if(e.points.length>=2050){view.error='最多支持 2048 个中间点。';return;}
  if(e.points.some(p=>p.every((v,i)=>Math.abs(v-hit.point[i])<1e-8)))return;
  const a=new THREE.Vector3(...e.directions[hit.i-1]),b=new THREE.Vector3(...e.directions[hit.i]),q=new THREE.Quaternion().setFromUnitVectors(a,b);a.applyQuaternion(new THREE.Quaternion().slerp(q,hit.u)).normalize();
  e.points.splice(hit.i,0,hit.point);e.directions.splice(hit.i,0,a.toArray());e.selected=hit.i;e.selectedPoints=[hit.i];coords(e);changed(view,true);
}
function removePoint(view,index){const e=view.fiveRapid.pathEdit;if(index<=0||index>=e.points.length-1)return;e.points.splice(index,1);e.directions.splice(index,1);e.selected=Math.min(index,e.points.length-2);e.selectedPoints=e.selected>0?[e.selected]:[];coords(e);changed(view,true);}
function removeSelectedPoints(view){const e=view.fiveRapid.pathEdit,selection=selectedPoints(e).filter(i=>!isRapidEndpoint(e,i)).sort((a,b)=>b-a);if(!selection.length)return;for(const index of selection){e.points.splice(index,1);e.directions.splice(index,1);}e.selected=Math.min(selection.at(-1),e.points.length-2);e.selectedPoints=[e.selected];coords(e);changed(view,true);}
export function handleRapidPathPick(context,view,_data,_hit,event,_hits,ops){
  const s=view.fiveRapid,e=s?.pathEdit;if(view.fiveCamInspector==='collision'||activeTool(view)!=='rapid'||view.fiveAxisArea!=='toolpath'||!e||!event||event.button!==0||!current(context,view,s)||s.key!==rapidDependencyKey(view))return false;if(view.pending||s.request||s.running||s.topologySaving)return true;
  const index=nearPoint(view,event),toggle=!!(event.shiftKey||event.ctrlKey||event.metaKey);
  if(e.inserting){if(index<0){const hit=nearSegment(view,event);if(hit)insert(view,hit);}}else if(e.deleting){if(index>0&&index<e.points.length-1&&canChangePoint(view))removePoint(view,index);}
  else if(index>=0){if(e.selectionDown?.index===index&&Date.now()-e.selectionDown.time<1000)delete e.selectionDown;else select(view,index,toggle);}
  else if(!isConstructionEdit(e)){const hit=nearSegment(view,event);if(hit&&canChangePoint(view)){const selection=[hit.i-1,hit.i].filter(i=>i>0&&i<e.points.length-1);if(selection.length){e.selectedPoints=toggle?[...new Set([...selectedPoints(e),...selection])]:selection;e.selected=selection[0];e.typing=false;coords(e);}}}
  ops.renderProject(context,view);return true;
}
export function disposeRapidPathEditing(view){
  const p=view.fiveRapidPathOverlay;if(!p){cancelRapidPathMode(view);return;}const vp=p.viewport;
  if(p.edit.gesture){const gesture=p.edit.gesture;Object.assign(p.edit,gesture.before);delete p.edit.gesture;coords(p.edit);if(view.fiveRapid?.pathEdit===p.edit){view.fiveRapid.dirty=gesture.rapidDirty;view.fiveRapid.preview=gesture.preview;}}
  p.edit.inserting=false;p.edit.deleting=false;p.axis?.removeFromParent();p.axis?.line.geometry.dispose();p.axis?.cone.geometry.dispose();p.axis?.line.material.dispose();p.axis?.cone.material.dispose();
  if(vp.transformManipulator===p.manipulator)vp.clearTransformManipulator();p.group.removeFromParent();p.geometry.dispose();p.materials.forEach(m=>m.dispose());
  p.canvas.removeEventListener('dblclick',p.double,true);p.canvas.removeEventListener('pointerdown',p.down,true);view.fiveRapidPathOverlay=null;
}
function updateRapidGesture(context,view,e){
 const s=view.fiveRapid,overlay=view.fiveRapidPathOverlay,preview=view.fiveGeneratedPreview,vp=view.viewport;
 if(!current(context,view,s)||!e.gesture||s?.pathEdit!==e||overlay?.edit!==e||overlay.viewport!==vp)return false;
 const line=preview?.group.children.find(o=>o.userData.rapidTransitionId===s.id);
 if(!line)return false;
 const points=isConstructionEdit(e)?e.samplePoints:e.points;if(!points?.length)return false;
 let positions=line.geometry.getAttribute('position');
 if(positions?.count!==points.length){const old=line.geometry;line.geometry=new THREE.BufferGeometry();positions=new THREE.Float32BufferAttribute(new Float32Array(points.length*3),3);line.geometry.setAttribute('position',positions);old.dispose();}
 points.forEach((p,i)=>positions.setXYZ(i,...p));positions.needsUpdate=true;line.geometry.computeBoundingSphere();
 let distances=line.geometry.getAttribute('lineDistance');if(!distances){distances=new THREE.Float32BufferAttribute(new Float32Array(points.length),1);line.geometry.setAttribute('lineDistance',distances);}
 let distance=0;distances.setX(0,0);for(let i=1;i<points.length;i++){distance+=Math.hypot(positions.getX(i)-positions.getX(i-1),positions.getY(i)-positions.getY(i-1),positions.getZ(i)-positions.getZ(i-1));distances.setX(i,distance);}distances.needsUpdate=true;
 const stroke=line.children.find(o=>o.userData.pathSelectionStroke);
 if(stroke){
  if(stroke.geometry.getAttribute('segmentStart')?.count!==points.length-1){const old=stroke.geometry;stroke.geometry=new THREE.InstancedBufferGeometry();stroke.geometry.setAttribute('position',old.getAttribute('position').clone());old.dispose();}
  for(const [name,size] of [['segmentStart',3],['segmentEnd',3],['segmentDistance',2]]){
   let attribute=stroke.geometry.getAttribute(name);if(attribute?.count!==points.length-1){attribute=new THREE.InstancedBufferAttribute(new Float32Array((points.length-1)*size),size);stroke.geometry.setAttribute(name,attribute);}
   for(let i=1;i<points.length;i++)if(name==='segmentStart')attribute.setXYZ(i-1,...points[i-1]);else if(name==='segmentEnd')attribute.setXYZ(i-1,...points[i]);else attribute.setXY(i-1,distances.getX(i-1),distances.getX(i));
   attribute.needsUpdate=true;
  }
  stroke.geometry.instanceCount=points.length-1;
 }
 // The next complete render must rebuild from the authoritative draft, even
 // when Escape restores the same snapshot that originally created this group.
 preview.key=null;
 overlay.group.children.forEach((mesh,i)=>mesh.position.fromArray(e.points[i]));
 overlay.axis.position.fromArray(e.points[e.selected]);overlay.axis.setDirection(new THREE.Vector3(...e.directions[e.selected]).normalize());
 const values={pathX:e.values[0],pathY:e.values[1],pathZ:e.values[2]};
 if(isConstructionEdit(e))values.pathLiftHeight=format(e.construction.liftHeightMm);
 else{const d=rapidPointPose(e),angles=directionToEuler(d.values.map(Number));for(let i=0;i<3;i++){values[String(i)]=d.values[i];values['angle-'+i]=format(angles[i]);}}
 for(const input of context.mount?.querySelectorAll?.('[data-five-rapid-field]')||[]){const value=values[input.dataset.fiveRapidField];if(value!==undefined&&input!==input.ownerDocument.activeElement&&input.value!==value)input.value=value;}
 // Reuse the SDK's single-scene redraw. No workbench, CAD, other cutting
 // line, annotation, or sidebar reconstruction is needed for pointer motion.
 if(vp.transformManipulator===overlay.manipulator&&overlay.manipulator.options)vp.setTransformManipulator(overlay.manipulator.options);
 return true;
}
export function syncRapidPathEditing(context,view,ops,check){
  const s=view.fiveRapid,e=s?.pathEdit,vp=view.viewport,valid=s?.key===rapidDependencyKey(view);
  if(!vp||!e||s.editClosing||!s.open||!valid||view.fiveAxisArea!=='toolpath'||activeTool(view)!=='rapid'){disposeRapidPathEditing(view);return;}
  let overlay=view.fiveRapidPathOverlay;
  if(overlay&&(overlay.edit!==e||overlay.viewport!==vp)){disposeRapidPathEditing(view);overlay=null;}
  if(!overlay){
    const group=new THREE.Group(),geometry=new THREE.SphereGeometry(1,12,8),materials=[sceneColors.reference,sceneColors.toolAxis,sceneColors.selected].map(color=>new THREE.MeshBasicMaterial({color,depthTest:false,depthWrite:false})),canvas=vp.renderer.domElement;
    group.name='rapid-edit-points';vp.scene.add(group);const axis=new THREE.ArrowHelper(new THREE.Vector3(0,0,1),new THREE.Vector3(),20,sceneColors.toolAxis);axis.line.geometry=axis.line.geometry.clone();axis.cone.geometry=axis.cone.geometry.clone();axis.name='rapid-edit-tool-axis';axis.traverse(o=>{o.raycast=()=>{};if(o.material){o.material.depthTest=false;o.material.depthWrite=false;}o.renderOrder=85;});vp.scene.add(axis);overlay={edit:e,viewport:vp,group,geometry,materials,canvas,axis};view.fiveRapidPathOverlay=overlay;
    overlay.double=event=>{if(isConstructionEdit(e)||event.button!==0||!current(context,view,s)||view.pending||s.request||s.running||s.topologySaving)return;if(e.inserting||e.deleting){event.preventDefault();event.stopImmediatePropagation();return;}const hit=nearSegment(view,event);if(!hit)return;event.preventDefault();event.stopImmediatePropagation();insert(view,hit);ops.renderProject(context,view);};
    overlay.down=event=>{if(event.button!==0||!current(context,view,s)||e.inserting||e.deleting||view.pending||s.request||s.running||s.topologySaving||s.topologyError)return;
      if(vp.transformManipulator?.hover?.({clientX:event.clientX,clientY:event.clientY,button:0,buttons:0}))return;
      const i=nearPoint(view,event);if(i>=0){select(view,i,!!(event.shiftKey||event.ctrlKey||event.metaKey));e.selectionDown={index:i,time:Date.now()};ops.renderProject(context,view);}};
    canvas.addEventListener('dblclick',overlay.double,true);canvas.addEventListener('pointerdown',overlay.down,true);
  }
  while(overlay.group.children.length>e.points.length)overlay.group.children.at(-1).removeFromParent();
  e.points.forEach((p,i)=>{let mesh=overlay.group.children[i];if(!mesh){mesh=new THREE.Mesh(overlay.geometry,overlay.materials[1]);mesh.renderOrder=80;mesh.frustumCulled=false;mesh.raycast=()=>{};mesh.onBeforeRender=(_renderer,_scene,camera)=>{const height=Math.max(1,vp.renderer.domElement.clientHeight),world=camera.isOrthographicCamera?(camera.top-camera.bottom)/camera.zoom:2*mesh.position.distanceTo(camera.position)*Math.tan(THREE.MathUtils.degToRad(camera.fov/2));mesh.scale.setScalar(world/height*(mesh.userData.selected?7:5));mesh.updateMatrixWorld();if(mesh.userData.active){overlay.axis.setLength(world/height*45,world/height*10,world/height*5);overlay.axis.updateMatrixWorld(true);}};overlay.group.add(mesh);}mesh.position.fromArray(p);mesh.userData.rapidEditPoint=i;mesh.userData.selected=selectedPoints(e).includes(i);mesh.userData.active=i===e.selected;mesh.material=overlay.materials[mesh.userData.selected?2:i===0||i===e.points.length-1?0:1];});
  overlay.axis.position.fromArray(e.points[e.selected]);overlay.axis.setDirection(new THREE.Vector3(...e.directions[e.selected]).normalize());
  vp.setCameraNavigationEnabled(true);
  if(locked(e)||e.inserting||e.deleting){positionWorldTransformHud(context,vp,e.points[e.selected],'rapid');if(vp.transformManipulator===overlay.manipulator)vp.clearTransformManipulator();overlay.manipulator=null;return;}
  const placement=view.scene.domain.placements.find(p=>p.id===transition(view)?.fromPlacementId),quaternion=placement?new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().set(...placement.transform)).toArray():[0,0,0,1];
  const shape=isConstructionEdit(e)&&!isRapidEndpoint(e),follow=shape&&e.construction.kind==='Follow',pose=!shape&&e.poseEditing;
  const handleQuaternion=follow?new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0,0,1),new THREE.Vector3(...e.directions[1])).toArray():quaternion;
  let inputValid=validCoordinates(e);try{if(shape)constructionManualPath(e);else validateRapidPointPose(e);}catch{inputValid=false;}
  vp.setTransformManipulator({mode:pose?'rotate':'translate',space:follow?'local':vp.transformSpace||'world',axes:follow?['X','Y']:undefined,quaternion:handleQuaternion,freeTranslate:!pose&&!follow,planeTranslate:!pose&&!follow,screenRotate:!shape,sizePixels:80,position:e.points[e.selected],enabled:inputValid&&!view.pending&&!s.request&&!s.topologySaving&&!s.topologyError,
    onPosition:p=>positionTransformHud(context,p,'rapid'),
    onStart:({axis,direction}={})=>{if(view.pending||view.fiveHistoryBusy||s.request||!canChangePoint(view))return false;let rotationAxis=direction;if(pose&&axis&&axis!=='E'){const basis={X:[1,0,0],Y:[0,1,0],Z:[0,0,1]}[axis],v=new THREE.Vector3(...basis);if(vp.transformSpace==='local')v.applyQuaternion(new THREE.Quaternion(...quaternion));rotationAxis=v.toArray();}e.gesture={before:snapshot(e),rapidDirty:s.dirty,preview:s.preview,base:[...e.points[e.selected]],direction:[...e.directions[e.selected]],axis:rotationAxis,index:e.selected};e.typing=false;invalidate(view);ops.renderProject(context,view);},
    onChange:({translation,angle})=>{if(!current(context,view,s)||s.pathEdit!==e||!e.gesture)return;if(pose){if(!e.gesture.axis||!Number.isFinite(angle))return;const q=new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(...e.gesture.axis).normalize(),angle);for(const index of selectedPoints(e.gesture.before))e.directions[index]=new THREE.Vector3(...e.gesture.before.directions[index]).applyQuaternion(q).normalize().toArray();updateRapidEndpoints(e,selectedPoints(e.gesture.before));}else movePoints(e,e.gesture.base.map((v,i)=>v+translation[i]),e.gesture.before);coords(e);changed(view);if(!updateRapidGesture(context,view,e))ops.renderProject(context,view);},
    onEnd:({moved})=>{if(!e.gesture)return;delete e.gesture;if(!moved)settleAcknowledgedEdit(s,e);ops.renderProject(context,view);if(moved)void check();},
    onCancel:()=>{if(e.gesture){const gesture=e.gesture;Object.assign(e,gesture.before);delete e.gesture;s.dirty=gesture.rapidDirty;s.preview=gesture.preview;settleAcknowledgedEdit(s,e);coords(e);if(e.changed)s.savePath?.();ops.renderProject(context,view);}}
  });overlay.manipulator=vp.transformManipulator;
}
