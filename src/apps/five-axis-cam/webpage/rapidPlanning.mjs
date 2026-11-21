import {camPathKind} from './workspaceModel.mjs';
import {saveEditedProject,clearCommittedSaveError} from './projectEditPersistence.mjs';
import {revealSceneListSelection} from './listNavigation.mjs';
import {rapidDefaultStrategy,acceptRapidDefaultStrategy,rapidSafetyEnabled} from './rapidDefaultStrategy.mjs';
import { refreshMachiningState } from './domainActions.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {placementWorldMatrix,setupWorldFrame} from './setupWorldFrame.mjs';
import { activeTool, selectTool } from './workspaceModel.mjs';
import { escapeText as esc } from '../../_shared/workbench/utils/format.mjs';
import { button } from './workspaceControls.mjs';
import { machiningTaskTimeoutMs } from './operationTiming.mjs';
import {renderRapidCollisionCheck,checkRapidCollision,rapidCollisionDisplay,forgetRapidCollisionChecks} from './rapidCollisionCheck.mjs';
import {hasRapidGeometry} from './rapidGeometry.mjs';
import {rapidDependencyKey} from './rapidDependencies.mjs';
import {rapidListModel} from './rapidListModel.mjs';
import {collisionPathResults,collisionPathWarning,collisionPathStatus} from './collisionPathResults.mjs';
import {sceneColors,sceneColorCss} from './scenePalette.mjs';
import { hasManualRapidPath, manualRapidPath, manualRapidDraft, rapidPathFieldChanged, renderRapidPathEditor, rapidPathCommand, handleRapidPathPick, syncRapidPathEditing as syncPathEditing, disposeRapidPathEditing,refreshRapidPathEdit,finishRapidHudEdit } from './rapidPathEditing.mjs';
export {disposeRapidPathEditing};

const setup=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
const geometry=view=>setup(view)?.generatedGeometry;
const key=rapidDependencyKey;
const pair=t=>`${t.from}/${t.to}`;
const manualKey=value=>JSON.stringify(value,(_key,item)=>item&&typeof item==='object'&&!Array.isArray(item)?Object.fromEntries(Object.entries(item).sort(([a],[b])=>a.localeCompare(b))):item);
const pendingEdit=s=>!!(s?.pathEdit?.changed||s?.policyChanged||s?.settingsChanged||s?.automaticChanged);
function persistRapidPath(context,s){
 const fileSave=saveEditedProject(context);s.fileSaving=fileSave;s.needsFileSave=true;
 void fileSave.then(()=>{if(s.fileSaving===fileSave){s.fileSaving=null;s.needsFileSave=false;}},()=>{if(s.fileSaving===fileSave)s.fileSaving=null;});
 return fileSave;
}
export function scheduleRapidPathSave(context,view,ops,delay=300){
 const s=view.fiveRapid;if(!pendingEdit(s)||s.pathEdit?.gesture||s.hudEditing)return;
 s.saveBinding={context,ops};clearTimeout(s.saveTimer);
 s.saveTimer=setTimeout(()=>{s.saveTimer=null;void flushRapidPathSave(context,view,ops,true).catch(error=>{view.error=rapidError(error);if(current(context,view))ops.renderProject(context,view);});},delay);
 context.actions?.refreshProjectHistoryControls?.();
}
export async function flushRapidPathSave(context,view,ops,background=false){
 const s=view.fiveRapid;if(!s)return;clearTimeout(s.saveTimer);s.saveTimer=null;
 if(s.hudEditing){if(background)return;throw Error('请先确认或取消浮动输入。');}
 if(s.pathEdit?.gesture){if(background)return;throw Error('请先结束空移点拖动。');}
 if(s.savingPath){const saved=await s.savingPath;await saved?.fileSave;if(pendingEdit(s)||!background&&(s.pathEdit?.gesture||s.hudEditing))return flushRapidPathSave(context,view,ops,background);return;}
 if(!pendingEdit(s)){const fileSave=s.fileSaving||(s.needsFileSave?persistRapidPath(context,s):null);await fileSave;if(pendingEdit(s)||!background&&(s.pathEdit?.gesture||s.hudEditing))return flushRapidPathSave(context,view,ops,background);return;}
 s.savingPath=(async()=>{await Promise.resolve();try{
  let committed=false;
  while(pendingEdit(s)){
   if(s.pathEdit?.gesture||s.hudEditing)break;
   const e=s.pathEdit,revision=e?.revision,editRevision=s.editRevision;
   const request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:view.scene.domain.activeSetupId};
   let method;
   if(s.settings){method='CamRapid.Configure';request.settings=numericValues(s.values);}
   else{
    const t=geometry(view)?.transitions.find(t=>pair(t)===s.id);if(!t)throw Error('空移段已不存在。');
    Object.assign(request,{geometryRevision:geometry(view).revision,from:t.from,to:t.to,policy:numericValues(s.policy)});
    if(e?.changed){method='CamRapid.EditPath';request.manualPath=manualRapidPath(view);}
    else{method='CamRapid.EditPolicy';request.automatic=!!s.automaticChanged;}
   }
   await context.sceneProxy.invoke(method,request);
   if(s.settings)acceptRapidDefaultStrategy(view,request.setupId,request.settings.strategy);
   committed=true;
   if(!await refreshMachiningState(context,view,{...ops,renderProject:()=>{}}))throw Error('读取已保存空移路径失败。');
   s.key=key(view);s.expected=structuredClone(view.scene.domain.expected);
   if(e&&s.pathEdit===e)e.savedRevision=revision;
   const unchanged=s.pathEdit===e&&e?.revision===revision&&!e?.gesture&&!s.hudEditing&&s.editRevision===editRevision;
   if(unchanged){s.preview=null;s.dirty=false;}
   if(e&&s.pathEdit===e&&e.revision===revision&&!e.gesture&&!s.hudEditing){e.changed=false;s.topologyError=false;const source=geometry(view)?.transitions.find(t=>pair(t)===s.id);if(e.construction&&source)refreshRapidPathEdit(view,source);}
   if(s.editRevision===editRevision){s.policyChanged=s.settingsChanged=s.automaticChanged=false;}
   await context.actions?.refreshNativeProjectHistory?.({projectId:context.project?.projectId});
   if(current(context,view))ops.renderProject(context,view);
  }
  const path=context.projectProxy?.state?.projectPath||context.project?.projectPath;
  if(committed&&path&&!path.includes('://')){
   return {fileSave:persistRapidPath(context,s)};
  }
 }finally{s.savingPath=null;context.actions?.refreshProjectHistoryControls?.();if(current(context,view)&&view.fiveRapid===s)ops.renderProject(context,view);}})();
 const saved=await s.savingPath;await saved?.fileSave;
 if(pendingEdit(s)||!background&&(s.pathEdit?.gesture||s.hudEditing))return flushRapidPathSave(context,view,ops,background);
}
export function endRapidPathEdit(context,view,ops){
 const s=view.fiveRapid,e=s?.pathEdit;if(!e||s.editClosing)return;
 if(e.gesture){view.error='请先结束空移点拖动。';ops.renderProject(context,view);return;}
 if(s.hudEditing)finishRapidHudEdit(view,false);
 try{manualRapidPath(view);numericValues(s.policy);}catch(error){view.error=rapidError(error);ops.renderProject(context,view);return;}
 s.cancel?.();s.token++;s.running=false;s.preview=null;s.editClosing=true;view.error='';
 disposeRapidPathEditing(view);ops.renderProject(context,view);
 // Hide controls immediately, retaining the draft until its native command and
 // archive save are acknowledged. Neither a slow save nor a failed save may
 // discard the user's final coordinates or create another undo transaction.
 const ending=Promise.resolve().then(()=>flushRapidPathSave(context,view,ops)).then(()=>{
  if(current(context,view)&&view.fiveRapid===s)clearCommittedSaveError(view);
  if(s.pathEdit===e){s.pathEdit=null;s.useAutomatic=false;s.dirty=false;const t=geometry(view)?.transitions?.find(t=>pair(t)===s.id);if(t)s.policy=structuredClone(t.policy);}
 }).catch(error=>{if(current(context,view)&&view.fiveRapid===s)view.error=rapidError(error);}).finally(()=>{
  if(s.editClosingTask===ending)s.editClosingTask=null;s.editClosing=false;
  if(current(context,view)&&view.fiveRapid===s)ops.renderProject(context,view);
 });
 s.editClosingTask=ending;
}
// Each accepted insertion/deletion owns one native transaction. While it is
// being acknowledged, further point clicks are ignored rather than combined
// into the coordinate-input debounce or queued against stale point indices.
export function saveRapidPathTopology(context,view,ops){
 const s=view.fiveRapid;if(!s||s.topologySaving)return s?.topologySaving;
 clearTimeout(s.saveTimer);s.saveTimer=null;
 const saving=Promise.resolve().then(async()=>{
  const retryFileOnly=s.topologyError&&!pendingEdit(s)&&!s.needsFileSave&&!s.fileSaving;await flushRapidPathSave(context,view,ops);
  if(retryFileOnly)await saveEditedProject(context);
  s.topologyError=false;view.error='';
 }).catch(error=>{
  s.topologyError=true;if(current(context,view)&&view.fiveRapid===s)view.error=rapidError(error);
 }).finally(()=>{if(s.topologySaving===saving)s.topologySaving=null;if(current(context,view)&&view.fiveRapid===s)ops.renderProject(context,view);});
 s.topologySaving=saving;return saving;
}
// Invalid text is an uncommitted input, never a second undo stack. Reset it
// from native state without consuming a framework history step or clearing redo.
export async function cancelInvalidRapidInput(context,view,ops){
 const s=view.fiveRapid;if(!s?.open||!['rapid','settings'].includes(activeTool(view)))return false;
 let invalid=false;try{
  if(s.pathEdit)manualRapidPath(view);
  const p=numericValues(s.settings?s.values:s.policy||{});
  if(s.settings&&(!['Quick','Frog','Fly'].includes(p.strategy)||Object.entries(p).some(([name,n])=>name!=='strategy'&&(isSafeCoordinate(name)?Math.abs(n)>1e8:n<0||n>100000||(!name.startsWith('envelope')&&!['marginMm','collisionThresholdMm','followHeightMm'].includes(name)&&n===0)))||((p.envelopeRadiusMm===0)!==(p.envelopeLengthMm===0))))invalid=true;
  if(!s.settings&&(!['Quick','Frog','Fly'].includes(p.strategy)||p.marginMm<0||p.marginMm>1000||p.liftHeightMm<=0||p.liftHeightMm>100000))invalid=true;
 }catch{invalid=true;}
 if(!invalid)return false;
 clearTimeout(s.saveTimer);s.saveTimer=null;await s.savingPath?.catch(()=>{});
 const id=s.id,selected=s.pathEdit?.selected,editing=!!s.pathEdit;
 if(!await refreshMachiningState(context,view,{...ops,renderProject:()=>{}}))throw Error('读取空移状态失败，请刷新后重试。');
 s.dirty=false;openRapid(view,s.settings);if(!s.settings){rapidPick(view,id);if(editing){await rapidCommand(context,view,'path-edit',ops);const e=view.fiveRapid.pathEdit;if(e){e.selected=Math.min(selected,e.points.length-1);e.values=e.points[e.selected].map(v=>String(Number(v.toFixed(6))));}}}
 view.error='';ops.renderProject(context,view);return true;
}
export const rapidKinds=[['Quick','快速规划'],['Frog','蛙跳'],['Fly','飞切']];
const isSafeCoordinate=name=>name==='safeZMm';
const options=(items,value)=>items.map(([id,label])=>`<option value="${esc(id)}" ${id===value?'selected':''}>${esc(label)}</option>`).join('');
const label=(view,t)=>{const ops=geometry(view)?.operations||[];return `${t.fromOrder||ops.find(o=>o.intentId===t.from)?.order||'?'} → ${t.toOrder||ops.find(o=>o.intentId===t.to)?.order||'?'}`;};
const current=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
const valid=view=>view.fiveRapid?.key===key(view)&&!!geometry(view)?.transitions?.some(t=>pair(t)===view.fiveRapid?.id&&(geometry(view)?.status==='Current'||t.endpointStatus==='Current'));
export function rapidUnifiedCollisionClear(view){
 const report=setup(view)?.simulationCheck,transitions=geometry(view)?.transitions||[];
 return report?.validity==='Current'&&report.status==='Clear'&&Number(report.collisionCount)===0&&Number(report.uncheckedCount)===0&&transitions.length>0&&transitions.every(t=>hasRapidGeometry(t)&&report.frames?.some(frame=>frame.role==='Rapid'&&frame.intentId===t.to));
}
const rapidStatus=(t,view)=>{
 const checked=['Generated','Draft'].includes(t.geometryStatus)&&collisionPathStatus(collisionPathResults(view),`rapid:${pair(t)}`);
 if(checked)return `${t.planningMethod==='Manual'?'手动路径':'已生成'} · ${checked}`;
 return t.fixedRouteCheck?`${t.planningMethod==='Manual'?'手动路径':'已生成'} · ${t.validation==='Clear'?'碰撞检测通过':t.validation==='Collision'?'检测到碰撞':'未通过碰撞检测'}`:t.geometryStatus==='Draft'?`${t.planningMethod==='Manual'?'手动':'自动'}路径 · 待碰撞检测`:t.geometryStatus==='Generated'?(t.validation==='Clear'?(t.planningMethod==='Manual'?'手动路径 · 碰撞检测通过':'已生成 · 碰撞检测通过'):t.validation==='Collision'?'已生成 · 检测到碰撞':rapidUnifiedCollisionClear(view)?'已生成 · 统一碰撞检测通过':setup(view)?.simulationCheck?.validity==='Current'&&['Collision','Unchecked'].includes(setup(view).simulationCheck.status)?'已生成 · 尚未单独检测':'已生成，待碰撞检测'):t.geometryStatus==='Stale'?(t.manualPath?'手动路径已保留 · 连接待更新':'连接已变化 · 待更新'):t.geometryStatus==='Failed'?(hasRapidGeometry(t)?'候选路径 · 未通过检查（可编辑）':'路径未通过检查'):'尚未生成';
};
const numericValues=values=>Object.fromEntries(Object.entries(values).map(([name,value])=>{
  if(name==='strategy')return [name,value];
  if(String(value).trim()===''||!Number.isFinite(Number(value)))throw Error('请输入有效的数值。');return [name,Number(value)];
}));
export function rapidError(error){
  const text=error?.message||String(error||'');
  for(const [code,message] of [
    ['RAPID_NO_FEASIBLE_ROUTE_WITHIN_PARAMETERS','在当前空移方式和抬升高度下未找到无碰撞路径，请调整参数后重试。'],
    ['RAPID_ENDPOINT_BLOCKED','空移起止姿态未满足刀头间隙约束，请调整刀路姿态、切割头或装夹。'],
    ['RAPID_MANUAL_PATH_BLOCKED','手动路径未通过碰撞检查，请移动路径点后重试。'],['RAPID_QUICK_ROUTE_BLOCKED','快速规划路径有干涉，请调整退刀距离、安全高度或装夹。'],
    ['CAM_RAPID_SAFE_COORDINATE_RANGE','请输入有效的 Z+ 安全高度。'],['CAM_RAPID_SAFE_Z_BELOW_RETRACT','Z+ 安全高度不能低于法向退刀后的转姿位置。'],['CAM_RAPID_BOTTOM_APPROACH_UNSUPPORTED','快速规划支持顶面和 X/Y 四个侧面，底面请手动规划空移。'],
    ['CAM_RAPID_EDIT_POINTS_INVALID','路径点或方向无效，请检查坐标。'],['RAPID_EDIT_POINT_LIMIT','最多支持 2048 个中间点。'],
    ['CAM_COLLISION_HEAD_CALIBRATION_REQUIRED','请先标定导入切割头，再生成避障空移。'],
    ['CAM_HEAD_COLLISION_BOXES_REQUIRED','切割头没有已启用的碰撞盒，请先在机床实例中配置并启用切割头碰撞盒。'],
    ['CAM_RAPID_NO_VALID_ROUTE','本段没有可保存的避障空移，请调整参数并重新预览。'],
    ['CAM_RAPID_COLLISION_CHECK_REQUIRED','路径已生成，待碰撞检测。'],
    ['CAM_RAPID_STRATEGY_INVALID','请选择有效的空移策略。'],
    ['CAM_RAPID_CROSS_WORKPIECE_REQUIRES_RETRACT','跨工件空移须使用快速规划，不能沿单个工件贴面行进。'],['TRAVEL_ENVELOPE_REQUIRED','请在设置中配置切割头包络。'],['CAM_RAPID_NOT_PLANNED','尚未生成空移。'],['CAM_RAPID_STALE','空移已过期，请重新生成。'],
    ['TRAVEL_CLEARANCE_VIOLATION','刀头包络与障碍物间隙不足。'],['TRAVEL_CLEARANCE_UNRESOLVED','该段间隙无法确认，请增大余量或调整方式。'],
    ['TRAVEL_ENDPOINT_CLEARANCE_VIOLATION','空移起止位置的刀头间隙不足，请调整刀路姿态、切割头或装夹。'],
    ['TRAVEL_AMBIGUOUS_SURFACE','曲面投影不唯一，无法确定贴面路径。'],['TRAVEL_SURFACE_DISCONTINUOUS','贴面路径不连续，请调整连接方式。'],
    ['TRAVEL_SURFACE_NOT_FOUND','未找到连续支撑曲面。'],['TRAVEL_OPPOSED_DIRECTIONS','两端姿态相反，尚不能确定转姿方向。'],
    ['TRAVEL_CLEARANCE_BUDGET','避碰计算超出限额，本段未验证。'],['TRAVEL_SURFACE_BUDGET','曲面计算超出限额，本段未验证。'],
    ['CAM_RAPID_GEOMETRY_REQUIRED','请先重新生成空移。'],['CAM_RAPID_PARAMETER_RANGE','请输入有效的距离和速度。'],['CAM_RAPID_ENVELOPE_REQUIRED','包络半径和总长须同时填写。']])if(text.includes(code))return message;
  if(text.includes('CAM_COLLISION_HEAD_CALIBRATION_REQUIRED'))return '请先在轨迹仿真的切割头选项中标定喷嘴端点和光束方向。';
  if(text.includes('TRAVEL_NO_EXTERIOR_ROUTE'))return '未找到可用的外围绕行路径，请调整装夹或刀路。';
  if(/STALE|REVISION|EPOCH|Superseded/.test(text))return '工程已变化，请重新打开空移调整。';
  return text;
}
export function openRapid(view,settings=false,dialog=false,selectedId=null){
  if(view.fiveRapid?.request)return;
  if(settings&&view.fiveRapid?.settings&&view.fiveRapid.dirty&&view.fiveRapid.key===key(view)){view.fiveRapid.open=true;return;}
  const selected=selectedId??(view.fiveRapid?.id||view.fiveRapidSelectedId);view.fiveRapid?.cancel?.();
  const state={open:true,settings,dialog,key:key(view),expected:structuredClone(view.scene.domain.expected),values:structuredClone(setup(view)?.rapidSettings),token:0};
  view.fiveRapid=state;view.error='';
  if(settings&&state.values)state.values.strategy=rapidDefaultStrategy(view);
  if(!settings){const ts=rapidListModel(view).transitions,t=ts.find(t=>pair(t)===selected)||ts[0];if(t){state.id=pair(t);state.policy=structuredClone(t.policy);view.fiveRapidSelectedId=state.id;}
  }
  state.key=key(view);
}
export function rapidPick(view,id){
  const state=view.fiveRapid;if(activeTool(view)!=='rapid'||!state?.open)return false;
  if(view.pending||state.request)return true;
  if(pendingEdit(state)||state.savingPath||state.fileSaving||state.needsFileSave){
    const binding=state.saveBinding;if(!binding)return true;
    const switching=state.switchingPath;state.switchingPath=id;
    if(!switching)void flushRapidPathSave(binding.context,view,binding.ops).then(()=>{
      const selected=state.switchingPath;state.switchingPath=null;
      if(view.fiveRapid===state&&current(binding.context,view)){
        rapidPick(view,selected);binding.ops.renderProject(binding.context,view);
        if(state.sceneRevealId===state.id&&activeTool(view)==='rapid'&&state.open){revealSceneListSelection(binding.context.mount,`rapid-${state.id}`);state.sceneRevealId=null;}
      }
    }).catch(error=>{state.switchingPath=null;view.error=rapidError(error);if(current(binding.context,view))binding.ops.renderProject(binding.context,view);});
    return true;
  }
  const t=rapidListModel(view).transitions.find(t=>pair(t)===id);if(!t)return true;
  if(state.id===id&&state.pathEdit)return true;
  state.cancel?.();state.running=false;state.pathEdit=null;state.useAutomatic=false;state.id=id;view.fiveRapidSelectedId=id;state.policy=structuredClone(t.policy);state.preview=null;state.dirty=false;state.token++;state.key=key(view);state.expected=structuredClone(view.scene.domain.expected);view.error='';return true;
}
export function rapidFieldChanged(view,field,value){
  const state=view.fiveRapid;if(!state||view.pending||state.request)return;
  if(rapidPathFieldChanged(view,field,value))return;
  if(field==='connection'){rapidPick(view,value);return;}
  const values=state.settings?state.values:state.policy;if(!Object.hasOwn(values,field))return;
  if(String(values[field])===String(value))return;
  state.cancel?.();state.running=false;
  values[field]=value;state.dirty=true;state.preview=null;state.token++;view.error='';
  state.editRevision=(state.editRevision||0)+1;
  if(state.settings)state.settingsChanged=true;else state.policyChanged=true;
  state.savePath?.();
}
export function rapidPreview(view,result){
  if(!result?.transitions)return result;
  const state=view.fiveRapid;
  const editing=activeTool(view)==='rapid'&&state?.open&&valid(view);
  const draft=editing?manualRapidDraft(view):null,preview=editing&&!state.safeGenerating&&!state.safeCommitPending?state.preview:null;
  return {...result,transitions:result.transitions.map(t=>rapidCollisionDisplay(view,editing&&pair(t)===state.id&&(preview||draft)?{...t,...preview?.result,...draft,policy:state.policy,strategy:state.policy.strategy}:t))};
}
export function renderRapidTree(view,{heading=true}={}){
  const query=(view.fiveAxisUI?.search||'').toLocaleLowerCase();
  const model=rapidListModel(view),ts=(rapidPreview(view,model)?.transitions||[]).filter(t=>!query||label(view,t).toLocaleLowerCase().includes(query));
  const empty=model.operations.length===1?'当前只有一条切割路径，无需工序间空移。':'当前没有空移连接。',collisions=collisionPathResults(view);
  return `${heading?`<div class="five-fixture-heading"><strong>空移刀路</strong><span>${ts.length}</span></div>`:''}${ts.length?ts.map(t=>`<button type="button" class="five-tree-row ${view.fiveRapid?.id===pair(t)?'selected':''}" aria-pressed="${view.fiveRapid?.id===pair(t)}" ${view.pending||view.fiveRapid?.request?'disabled':''} data-five-key="rapid-${pair(t)}" data-five-rapid-id="${pair(t)}"><span ${t.geometryStatus==='Failed'||t.geometryStatus==='Draft'?`style="color:${sceneColorCss(t.validation==='Collision'?sceneColors.error:sceneColors.rapidUnverified)}"`:''}>${t.geometryStatus==='Failed'?'⚠':'⇢'}</span><span><strong>${label(view,t)}</strong><small>${rapidStatus(t,view)}</small></span>${collisionPathWarning(collisions,`rapid:${pair(t)}`)}</button>`).join(''):`<p class="five-empty-row">${query?'没有匹配的空移连接。':empty}</p>`}`;
}
export function renderRapidParameters(view){
  const id=view.fiveRapid?.id||view.fiveRapidSelectedId,t=rapidPreview(view,rapidListModel(view))?.transitions?.find(t=>pair(t)===id);
  if(!t)return '<section class="five-cut-parameters" data-five-key="rapid-parameters"><header>空移参数</header><p>请选择一段空移刀路。</p></section>';
  const policy=t.policy||{},status=rapidStatus(t,view);
  const rows=[['空移方式',rapidKinds.find(([id])=>id===policy.strategy)?.[1]||'未设置'],...(t.definitionOnly?[['状态',status]]:[['碰撞冗余距离',`${policy.marginMm??'—'} mm`],...(policy.strategy==='Frog'?[['抬升高度',`${policy.liftHeightMm} mm`]]:[]),['检查结果',status],...(t.lengthMm!=null?[['路径长度',`${Number(t.lengthMm).toFixed(1)} mm`]]:[])])];
  return `<section class="five-cut-parameters" data-five-key="rapid-parameters"><header>空移段 ${esc(label(view,t))}</header>${rows.map(([title,value])=>`<div class="five-rapid-summary-row"><span>${esc(title)}</span><strong>${esc(value)}</strong></div>`).join('')}${t.reason?`<p>${esc(rapidError(t.reason))}</p>`:''}</section>`;
}
export function renderRapid(view,panel=false){
  const state=view.fiveRapid;if(!state?.open)return '';
  const disabled=view.pending||state.request||state.safeGenerating?'disabled':'',title=state.settings?'空移策略':'空移路径编辑';
  const field=(name,title,values,unit='mm')=>`<label class="five-rapid-field" data-five-key="rapid-field-${name}"><span>${title}</span><input data-five-key="rapid-value-${name}" data-five-rapid-field="${name}" type="text" inputmode="decimal" value="${esc(values[name])}" aria-label="${title}" ${disabled?'readonly aria-busy="true"':''}><small>${unit}</small></label>`;
  const strategy=values=>`<label class="five-rapid-field" data-five-key="rapid-field-strategy"><span>空移方式</span><select data-five-rapid-field="strategy" aria-label="空移方式" ${disabled}>${options(rapidKinds,values.strategy)}</select></label>`;
  const list=rapidListModel(view),t=list.transitions.find(t=>pair(t)===state.id),shown=(!state.safeGenerating&&!state.safeCommitPending&&state.preview?.result)||manualRapidDraft(view)||(!state.dirty?t:null),result=shown&&rapidCollisionDisplay(view,{...t,...shown});
  const manual=hasManualRapidPath(view),updateLabel=manual?'更新连接并检查':'自动生成安全空移',needsUpdate=manual;
  const obstacle=result?.obstacleId?(view.scene.domain.fixtures?.find(f=>f.id===result.obstacleId)?.name||'工件'):'';
  const heading=state.settings?'生成参数':panel?'空移属性':title;
  if(!state.settings&&t?.definitionOnly)return `<div class="five-position-dialog" data-five-key="rapid-dialog" data-five-tool="rapid" role="${panel?'region':'dialog'}" aria-label="${heading}"><header class="five-position-title"><strong>${heading}</strong>${panel?'':button('five.rapid.close','×','aria-label="关闭空移属性"')}</header><div class="five-position-body five-rapid-body"><label class="five-rapid-field" data-five-key="rapid-field-connection"><span>当前空移段</span><select data-five-rapid-field="connection" aria-label="当前空移段" ${disabled}>${options(list.transitions.map(t=>[pair(t),label(view,t)]),state.id)}</select></label><div class="five-rapid-status" role="status">未生成</div></div></div>`;
  return `<div class="five-position-dialog" data-five-key="rapid-dialog" data-five-tool="${state.settings?'settings':'rapid'}" role="${panel?'region':'dialog'}" aria-label="${heading}">
    <header class="five-position-title"><strong>${heading}</strong>${panel&&!state.settings?'':button('five.rapid.close','×',`aria-label="关闭${heading}" ${view.pending?'disabled':''}`)}</header>
    <div class="five-position-body five-rapid-body" data-five-key="rapid-body">${state.settings?`
      ${strategy(state.values)}${state.values.strategy==='Quick'?field('normalRetractDistanceMm','法向退刀距离',state.values)+field('sideRetractDistanceMm','XY 安全退刀距离',state.values)+field('safeZMm','Z+ 安全高度',state.values):field('followHeightMm','跟随高度',state.values)+(state.values.strategy==='Frog'?field('liftHeightMm','蛙跳抬升高度',state.values):'')}
      `:t?`<label class="five-rapid-field" data-five-key="rapid-field-connection"><span>当前空移段</span><select data-five-rapid-field="connection" aria-label="当前空移段" ${disabled}>${options(geometry(view).transitions.map(t=>[pair(t),label(view,t)]),state.id)}</select></label>
      <fieldset class="five-rapid-policy" ${hasManualRapidPath(view)?'disabled':''}>${strategy(state.policy)}${field('marginMm','碰撞冗余距离',state.policy)}
      ${state.policy.strategy==='Frog'?field('liftHeightMm','蛙跳抬升高度',state.policy):''}
      </fieldset><div class="five-rapid-update" data-five-key="rapid-update">${button('five.rapid.safe-generate',state.safeGenerating?'正在自动避障并检测…':'自动生成安全空移',`class="five-primary" ${view.pending||state.running||state.request||state.regenerating||state.savingPath||state.topologySaving||state.pathEdit?.gesture||state.hudEditing?'disabled':''}`)}${needsUpdate?button(manual?'five.rapid.update':'five.rapid.regenerate',state.regenerating?(manual?'正在更新连接并检查…':'正在重新生成…'):updateLabel,`${view.pending||state.running||state.request?'disabled':''}`):''}</div>${renderRapidCollisionCheck(view,rapidError)}${renderRapidPathEditor(view,disabled)}${result?`<div class="five-rapid-status" role="status">${rapidStatus(result,view)}</div>${result.geometryStatus==='Failed'&&!result.fixedRouteCheck&&hasRapidGeometry(result)?`<small data-five-key="rapid-candidate-hint">橙红色候选路径尚未通过避障检查。${state.pathEdit?'移动中间点后':'点击“编辑路径”移动中间点，再'}点击“预览”重新检查。</small>`:''}${result.reason&&!result.fixedRouteCheck?`<small>${esc(rapidError(result.reason))}</small>`:''}${obstacle?`<small>障碍物：${esc(obstacle)}</small>`:''}${result.lengthMm!=null?`<small>${Number(result.lengthMm).toFixed(1)} mm</small>`:''}`:''}`:'<p>没有可调整的空移段。</p>'}
      ${state.running?'<div role="status">正在计算…</div>':''}${view.error?`<div role="alert" class="five-position-error">${esc(view.error)}</div>`:''}
      ${!state.settings&&!valid(view)&&!view.error?`<div class="five-rapid-status">相邻切割已变化，点击“${updateLabel}”更新。${manual?'已保存的手动路径会保留。':''}</div>`:''}</div>
    ${state.settings?'':`<footer class="five-position-actions">${button('five.rapid.preview','预览',`${disabled} ${state.running||!valid(view)||!t?'disabled':''}`)}${button('five.rapid.apply',state.request?'重试':'采用检查结果',`class="five-primary" ${view.pending||state.running||(!state.request&&(!state.preview||state.preview.result.geometryStatus!=='Generated'||state.preview.result.validation!=='Clear'||!valid(view)))?'disabled':''}`)}${panel?(state.running?button('five.rapid.close','取消计算',view.pending?'disabled':''):''):button('five.rapid.close',state.running?'取消计算':'取消',view.pending?'disabled':'')}</footer>`}</div>`;
}
async function waitTask(context,view,task,alive,group='CamRapid'){
  while(task.status==='Running'&&alive()){
    await new Promise(resolve=>setTimeout(resolve,90));if(!alive())return null;
    task=await context.sceneProxy.invoke(group+'.QueryStatus',{epoch:task.epoch,taskId:task.taskId},{timeoutMs:machiningTaskTimeoutMs});
  }
  if(task?.status!=='Completed'&&alive())throw Error(task.error||task.status);return task;
}
export async function generateRapids(context,view,ops){
  const result=geometry(view);if(result?.status!=='Current'||!result.transitions?.length)return;
  const state=view.fiveRapidBatch??={};state.request??={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:view.scene.domain.activeSetupId,geometryRevision:result.revision,safe:rapidSafetyEnabled(view)};
  const safe=state.request.safe===true;state.safe=safe;
  const requestKey=JSON.stringify(state.request.expected);
  const wasPending=view.pending;state.running=true;state.task=null;view.pending=true;ops.renderProject(context,view);let task;
  try{task=await context.sceneProxy.invoke('CamRapid.Generate',state.request,{timeoutMs:machiningTaskTimeoutMs});state.task=task;
    if(view.fiveGeneration?.running&&view.fiveGeneration.cancelRequested&&task.status==='Running')await context.sceneProxy.invoke('CamRapid.CancelTask',{epoch:task.epoch,taskId:task.taskId});
    task=await waitTask(context,view,task,()=>!view.disposed);state.task=task;state.request=null;
    if(current(context,view)){
      if(Number(task.result.calculatedCount)>0)forgetRapidCollisionChecks(view,result.transitions.map(pair));
      const unchanged=task.result.calculatedCount==='0'&&JSON.stringify(view.scene?.domain?.expected)===requestKey&&geometry(view)?.transitions?.every(t=>t.geometryStatus==='Generated');
      if(!unchanged&&!(await refreshMachiningState(context,view,ops)))throw Error(view.error||'读取空移结果失败，请刷新后重试。');
      const failed=Number(task.result.count)-Number(safe?task.result.clearCount:task.result.generatedCount);if(failed)view.error=safe?`${failed} 段空移未找到通过当前切割头碰撞检测的安全路径，请查看对应空移段。`:`${failed} 段空移未能按当前规则生成，请查看对应空移段。`;
      ops.appendProjectLog(context,failed?'error':'info',failed?view.error:`更新 ${task.result.calculatedCount} 段空移，复用 ${task.result.reusedCount} 段 · ${safe?'安全空移碰撞检测通过':'已生成，待碰撞检测'}`);
      await saveEditedProject(context);
    }
  }catch(error){if(/CAM_|TRAVEL_|Superseded|Cancelled/.test(error.message))state.request=null;throw error;}
  finally{state.running=false;view.pending=wasPending;}
}
export async function rapidCommand(context,view,action,ops,preservedManual){
  const state=view.fiveRapid;if(!state)return;
  if(action==='path-cancel'){endRapidPathEdit(context,view,ops);return;}
  if(action==='safe-generate'&&(view.pending||state.running||state.request||state.regenerating||state.settings||!state.id))return;
  if(action==='check'){await checkRapidCollision(context,view,ops,rapidError);return;}
  if(action==='cancel-check'){state.cancel?.();return;}
  if(!action.startsWith('path-')||['path-cancel','path-automatic'].includes(action))await flushRapidPathSave(context,view,ops);
  if(action==='path-automatic'){
    state.cancel?.();state.pathEdit=null;state.useAutomatic=true;state.preview=null;state.running=false;state.token++;
    state.automaticChanged=true;state.editRevision=(state.editRevision||0)+1;
    await flushRapidPathSave(context,view,ops);return;
  }
  if(await rapidPathCommand(context,view,action,ops,()=>rapidCommand(context,view,'preview',ops)))return;
  if(action==='regenerate'||action==='update'||action==='safe-generate'){
    if(view.pending||state.running||state.settings||!state.id)return;
    const automatic=action==='safe-generate',manual=!automatic&&hasManualRapidPath(view)?structuredClone(manualRapidPath(view)):undefined;
    if(action==='update'&&!manual){view.error='未找到已保存的手动路径，更新已停止。';ops.renderProject(context,view);return;}
    const selected=state.id,previousPreview=state.preview,previousTask=state.task;state.regenerating=true;state.safeGenerating=automatic;view.error='';
    try{
      if(!valid(view)||geometry(view)?.status!=='Current'||state.cutRequest){
        view.pending=true;state.running=true;ops.renderProject(context,view);
        if(!await refreshMachiningState(context,view,ops))throw Error(view.error||'读取当前刀路失败。');
        if(geometry(view)?.status!=='Current'||state.cutRequest){
          state.cutRequest??={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:view.scene.domain.activeSetupId};
          const task=await waitTask(context,view,await context.sceneProxy.invoke('CamPlan.Generate',state.cutRequest,{timeoutMs:machiningTaskTimeoutMs}),()=>current(context,view),'CamPlan');
          if(!task||!current(context,view))return;state.cutRequest=null;
          if(!await refreshMachiningState(context,view,ops))throw Error(view.error||'读取更新后的切割失败。');
          ops.appendProjectLog(context,'info',`更新 ${task.result.calculatedCount} 条切割路径，复用 ${task.result.reusedCount} 条`);
          await saveEditedProject(context);
        }
        if(!current(context,view))return;
        const t=geometry(view)?.transitions?.find(t=>pair(t)===selected);
        if(!t)throw Error('切割顺序已更新，原连接已不存在，请选择当前空移段。');
        state.key=key(view);state.expected=structuredClone(view.scene.domain.expected);state.policy=structuredClone(t.policy);state.preview=null;state.dirty=false;
      }
      view.pending=false;state.running=false;
      if(automatic){
        const candidate=await rapidCommand(context,view,'preview-auto',ops);
        if(!candidate||!current(context,view)||view.fiveRapid!==state||state.id!==selected)return;
        // Keep the stored/manual route visible until the checked replacement
        // is acknowledged. A failed or cancelled search never becomes a draft.
        state.safeCandidate=candidate;state.safeCommitPending=true;
        await rapidCommand(context,view,'apply',ops);
        if(state.safeCommitPending&&!state.request){state.safeCommitPending=false;state.safeCandidate=null;state.preview=previousPreview;state.task=previousTask;}
        return;
      }
      await rapidCommand(context,view,'preview',ops,manual);
      if(manual&&state.preview&&manualKey(state.preview.manualPath)!==manualKey(manual))throw Error('手动路径与检查结果不一致，未采用检查结果。');
      if(state.preview&&!view.error)await rapidCommand(context,view,'apply',ops);
    }catch(error){if(/CAM_|CURVE_|LEAD_|PROCESS_|Cancelled|Superseded/.test(error.message))state.cutRequest=null;if(current(context,view))view.error=rapidError(error);}
    finally{state.regenerating=false;state.safeGenerating=false;state.running=false;view.pending=false;if(current(context,view))ops.renderProject(context,view);}return;
  }
  if(action==='close'){
    if(view.pending||state.request)return;state.open=false;state.token++;view.error='';
    state.cancel?.();state.running=false;
    if(camPathKind(view)==='rapid'){selectTool(view,'rapid');openRapid(view);}
    ops.renderProject(context,view);return;
  }
  if(view.pending||state.running)return;
  if(action==='preview'||action==='preview-auto'){
    if(!valid(view))return;const t=geometry(view).transitions.find(t=>pair(t)===state.id);if(!t)return;
    const automatic=action==='preview-auto',token=++state.token;state.running=true;if(!automatic)state.preview=null;view.error='';ops.renderProject(context,view);
    const alive=()=>!view.disposed&&view.fiveRapid===state&&state.open&&token===state.token;
    let task;const cancel=()=>{if(task?.status==='Running')void context.sceneProxy.invoke('CamRapid.CancelTask',{epoch:task.epoch,taskId:task.taskId}).catch(()=>{});};state.cancel=cancel;
    try{const manual=automatic?undefined:preservedManual||manualRapidPath(view);state.expected=structuredClone(view.scene.domain.expected);task=await context.sceneProxy.invoke('CamRapid.Preview',{commandId:crypto.randomUUID(),expected:state.expected,setupId:view.scene.domain.activeSetupId,geometryRevision:geometry(view).revision,from:t.from,to:t.to,policy:numericValues(state.policy),...(manual?{manualPath:manual}:{})},{timeoutMs:machiningTaskTimeoutMs});
      if(!alive()){cancel();return;}state.task=task;
      task=await waitTask(context,view,task,alive);
      if(alive()&&valid(view)){
        const preview=Object.values(task.result.connections)[0];
        if(automatic){if(preview?.result.geometryStatus==='Generated'&&preview.result.validation==='Clear')return {task,preview};view.error=`${rapidError(preview?.result.reason||'自动避障未通过碰撞检测。')}原空移路径已保留。`;return;}
        state.task=task;state.preview=preview;state.dirty=false;if(state.pathEdit?.construction&&state.preview.result.construction&&state.preview.result.points?.length)refreshRapidPathEdit(view,state.preview.result);if(state.preview.result.geometryStatus!=='Generated'||state.preview.result.validation!=='Clear')view.error=rapidError(state.preview.result.reason||'当前路径尚未通过碰撞检测。');
      }
    }catch(error){if(alive()&&current(context,view))view.error=rapidError(error);}
    finally{if(token===state.token)state.running=false;if(alive()&&current(context,view))ops.renderProject(context,view);}return;
  }
  if(!['apply','save-settings'].includes(action))return;
  const applyPreview=state.safeCandidate?.preview||state.preview;
  if(!state.request&&(!state.settings&&(!applyPreview||applyPreview.result.geometryStatus!=='Generated'||applyPreview.result.validation!=='Clear'||!valid(view))))return;
  const method=state.settings?'CamRapid.Configure':'CamRapid.Apply';let saved=false;
  try{state.request??={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:view.scene.domain.activeSetupId,...(state.settings?{settings:numericValues(state.values)}:{taskId:(state.safeCandidate?.task||state.task).taskId})};}
  catch(error){view.error=error.message;ops.renderProject(context,view);return;}
  const request=state.request;view.pending=true;view.error='';ops.renderProject(context,view);
  try{
    try{await context.sceneProxy.invoke(method,request);}catch(error){
      if(/CAM_EPOCH_CHANGED|CAM_REVISION_CONFLICT/.test(error.message)){state.request=null;throw error;}
      const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')state.request=null;throw error;}
    }
    state.request=null;if(!state.settings)forgetRapidCollisionChecks(view,[state.id]);if(state.safeCommitPending){state.pathEdit=null;state.useAutomatic=false;state.safeCommitPending=false;state.safeCandidate=null;state.preview=null;}if(state.settings)acceptRapidDefaultStrategy(view,request.setupId,request.settings.strategy);if(camPathKind(view)!=='rapid')state.open=false;ops.appendProjectLog(context,'ok',state.settings?'空移设置已保存':'空移调整已保存');
    if(current(context,view)&&!await refreshMachiningState(context,view,ops))throw Error('读取空移结果失败，请重试。');
    await context.actions?.refreshNativeProjectHistory?.({projectId:context.project?.projectId});
    const path=context.projectProxy?.state?.projectPath||context.project?.projectPath;
    if(path&&!path.includes('://'))await saveEditedProject(context);
    saved=true;
  }catch(error){if(current(context,view))view.error=state.request?'结果尚未确认，请重试。':rapidError(error);}
  finally{view.pending=false;if(current(context,view)){if(saved&&camPathKind(view)==='rapid'){openRapid(view,state.settings);if(!state.settings){rapidPick(view,state.id);if(state.pathEdit){const t=geometry(view).transitions.find(t=>pair(t)===state.id);if(t?.planningMethod==='Manual'){view.fiveRapid.pathEdit=state.pathEdit;if(state.pathEdit.construction)refreshRapidPathEdit(view,t);else Object.assign(view.fiveRapid.pathEdit,{points:structuredClone(t.points),directions:structuredClone(t.directions),changed:false});}}}}ops.renderProject(context,view);}}
}
export function syncRapidPathEditing(context,view,ops){if(view.fiveRapid){view.fiveRapid.saveBinding={context,ops};view.fiveRapid.savePath=(delay=300)=>scheduleRapidPathSave(context,view,ops,delay);view.fiveRapid.saveTopology=()=>saveRapidPathTopology(context,view,ops);}syncPathEditing(context,view,ops,()=>scheduleRapidPathSave(context,view,ops,0));}
export function handleRapidPick(context,view,_data,_hit,event,_hits,ops){
  if(event?.button!==0)return false;
  if(handleRapidPathPick(context,view,_data,_hit,event,_hits,ops)){revealSceneListSelection(context.mount,`rapid-${view.fiveRapid?.id}`);return true;}
  if(activeTool(view)!=='rapid'||!view.fiveRapid?.open||!event||view.fiveAxisArea!=='toolpath')return false;
  const hit=rapidTransitionAtPointer(view,event);
  if(hit){view.fiveRapid.sceneRevealId=hit.id;rapidPick(view,hit.id);ops.renderProject(context,view);if(view.fiveRapid.id===hit.id){revealSceneListSelection(context.mount,`rapid-${hit.id}`);view.fiveRapid.sceneRevealId=null;}}return true;
}
export function rapidTransitionAtPointer(view,event){
  const result=rapidPreview(view,geometry(view)),viewport=view.viewport,box=viewport.renderer.domElement.getBoundingClientRect();let best=9,hit=null;
  for(const t of result?.transitions||[]){const op=result.operations.find(o=>o.intentId===t.from),placement=view.scene.domain.placements.find(p=>p.id===op?.placementId);if(!placement)continue;
    if(!hasRapidGeometry(t))continue;
    const matrix=t.frame==='Setup'?setupWorldFrame(view,placement.setupId):placementWorldMatrix(view,placement);let last;
    for(const point of t.points||[]){const q=new THREE.Vector3(...point).applyMatrix4(matrix).project(viewport.camera),b=[box.x+(q.x+1)*box.width/2,box.y+(1-q.y)*box.height/2,q.z];
      if(last&&Math.abs(last[2])<=1&&Math.abs(b[2])<=1){const dx=b[0]-last[0],dy=b[1]-last[1],u=Math.max(0,Math.min(1,((event.clientX-last[0])*dx+(event.clientY-last[1])*dy)/(dx*dx+dy*dy||1))),distance=Math.hypot(event.clientX-last[0]-u*dx,event.clientY-last[1]-u*dy);if(distance<best){best=distance;hit={id:pair(t),points:t.points.map(p=>new THREE.Vector3(...p).applyMatrix4(matrix).toArray())};}}last=b;}
  }
  return hit;
}


