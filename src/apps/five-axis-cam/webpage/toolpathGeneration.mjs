import { refreshMachiningState } from './domainActions.mjs';
import {applyRapidDefaultStrategy,renderRapidSafety} from './rapidDefaultStrategy.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { escapeText as esc } from '../../_shared/workbench/utils/format.mjs';
import { activeTool, workspaceState, selectTool } from './workspaceModel.mjs';
import { button } from './workspaceControls.mjs';
import { leadErrorText } from './cutLeads.mjs';
import { orderingPreview } from './toolpathOrdering.mjs';
import { rapidPreview, generateRapids, openRapid, rapidPick, rapidError,rapidUnifiedCollisionClear } from './rapidPlanning.mjs';
import { sceneColors, sceneColorCss } from './scenePalette.mjs';
import {toolpathLayerColor,toolpathLayerVisible,syncToolpathLayerMaterials} from './toolpathLayers.mjs';
import { currentCompensation } from './cutCompensation.mjs';
import { machiningTaskTimeoutMs } from './operationTiming.mjs';
import { addCutDirectionMarkers } from './cutDirectionMarker.mjs';
import { toolAxisErrorText } from './toolAxisErrors.mjs';
import { hasRapidGeometry } from './rapidGeometry.mjs';
import {camMachinePlayback} from './machinePlaybackContext.mjs';

export function generatedGeometry(view){return view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId)?.generatedGeometry;}
export async function prepareToolpathAction(context,view,ops){
  const ready=()=>{const g=generatedGeometry(view);return g?.status==='Current'&&g.operations?.length&&(g.transitions||[]).every(t=>t.geometryStatus==='Generated');};
  if(ready())return true;
  const scene=view.scene,setupId=scene?.domain?.activeSetupId,proxy=context.sceneProxy;
  await generationCommand(context,view,'apply',ops);
  if(view.disposed||context.isCurrentProject?.()===false||view.scene!==scene||context.sceneProxy!==proxy||scene.domain.activeSetupId!==setupId)return false;
  if(view.error||view.fiveGeneration?.cancelRequested||!ready())return false;
  view.fiveGenerationOpen=false;return true;
}
export function showsGeneratedPreview(view){
  return !camMachinePlayback(view)&&view.fiveAxisArea==='toolpath'&&(['manual-order','auto-order'].includes(activeTool(view))||(['generate','keypoint','rapid','adjust','simulation','collision'].includes(activeTool(view))||view.fiveCamInspector==='collision')&&!!generatedGeometry(view)?.operations?.length);
}
export function generationError(error){
  const text=error?.message||String(error);
  const intentError=toolAxisErrorText(error);if(intentError!==text)return intentError;
  for(const [code,message] of [
    ['CAM_GENERATE_NO_CUTS','请先建立刀路。'],
    ['CAM_POSE_KEYPOINT_COMPENSATION_UNSUPPORTED','局部 IJK 暂不能与非零切缝补偿同时生成。请保留当前参数，调整此组合后重试。'],
    ['CAM_POSE_OPPOSED_KEYPOINT','局部方向与基准相反，请先调整整条刀路方向或增加中间方向。'],
    ['CAM_GENERATE_EXECUTOR_COMPENSATION_UNSUPPORTED','当前生成使用 CAM 补偿，尚不支持执行设备补偿。'],
    ['CAM_GENERATE_THICKNESS_COMPENSATION_UNSUPPORTED','当前补偿尚不支持厚度截面，请选择表面或光束法截面。'],
    ['CAM_GENERATE_SURFACE_KERF_REQUIRES_NORMAL_AXIS','表面切缝要求刀轴垂直加工面；请调整刀轴或选择光束法截面。'],
    ['CAM_GENERATE_RETAINED_SIDE_REQUIRED','请先设置保留侧（阴阳切），再生成带补偿的刀路。'],
    ['CAM_GENERATE_START_REMOVED_BY_COMPENSATION','原起点所在曲线已被补偿裁除，请重新选择起点。'],
    ['CAM_GENERATE_KERF_SIDE_CHANGED','刀轴使补偿侧沿轮廓发生变化，请调整刀轴或拆分刀路。'],
    ['CAM_GENERATE_KERF_SIDE_AMBIGUOUS','无法确定补偿方向，请调整刀轴。'],
    ['CAM_GENERATE_COMPENSATION_FAILED','补偿轮廓无法完成裁剪或连接，请检查切缝宽度、拐角与轮廓。'],
    ['CAM_GENERATE_POSE_UNSUPPORTED','当前生成支持板面法向、固定 IJK 和局部 IJK；所选其他姿态尚未实现。'],
    ['CAM_GENERATE_ALREADY_RUNNING','刀路正在计算，请等待完成或取消。'],['CAM_GENERATE_DISCONTINUOUS','切割路径不连续，请检查起点和引线。'],
    ['CAM_REVISION_CONFLICT','工程已变化，请刷新后重新生成。']])if(text.includes(code))return message;
  return leadErrorText(error);
}
export function renderGeneration(view){
  const result=generatedGeometry(view),running=view.fiveGeneration?.running||view.fiveRapidBatch?.running;
  const collision=view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId)?.simulationCheck;
  const currentCollision=collision?.validity==='Current'?collision:null,transitions=result?.transitions||[];
  const generatedCount=transitions.filter(t=>t.geometryStatus==='Generated').length,clearCount=transitions.filter(t=>t.geometryStatus==='Generated'&&t.validation==='Clear').length;
  const collisionSummary=rapidUnifiedCollisionClear(view)?'已生成 · 统一碰撞检测通过':view.fiveRapidBatch?.safe?`${clearCount}/${transitions.length} 段通过切割头碰撞检测${clearCount<transitions.length?` · ${transitions.length-clearCount} 段未通过`:''}`:currentCollision?.status==='Collision'?`${generatedCount} 段已生成 · 统一碰撞检测发现干涉`:currentCollision?.status==='Unchecked'?`${generatedCount} 段已生成 · 统一碰撞检测尚有未验证运动`:`${generatedCount} 段已生成，待碰撞检测 · 单段碰撞检测通过 ${clearCount}/${transitions.length}`;
  const cancelled=view.fiveGeneration?.cancelRequested;
  const failures=!running?(result?.transitions||[]).filter(t=>t.geometryStatus==='Failed'):[];
  const failed=!running&&(!!view.error||failures.length>0);
  const count=result?.operations?.length||view.scene.intentToolpaths?.filter(c=>c.enabled!==false).length||0;
  const summary=count===1?`1 条切割路径 · 无需工序间空移${result?.status==='Stale'?' · 切割路径已过期':''}`:
    result?.operations?.length?`${result.transitions?.length||0} 段空移 · ${count} 条切割路径${result.status==='Stale'?' · 已过期':''}`:`${Math.max(0,count-1)} 段待生成空移`;
  const phase=cancelled?'取消请求已发出，等待计算任务响应。':view.fiveRapidBatch?.running?(view.fiveRapidBatch.safe?'正在参照切割头碰撞盒并行生成安全空移…':'正在按策略和参数生成空移…'):'正在准备切割路径…';
  const failureRows=failures.map(t=>{
    const from=result.operations?.find(op=>op.intentId===t.from)?.order||'?',to=result.operations?.find(op=>op.intentId===t.to)?.order||'?';
    return `<div class="five-generation-failure" data-five-key="generation-failure-${esc(t.from)}-${esc(t.to)}"><strong>空移 ${esc(from)} → ${esc(to)}</strong><p>${esc(rapidError(t.reason))}</p>${button(`five.generate.failure:${t.from}/${t.to}`,'查看失败段')}</div>`;
  }).join('');
  return `<div class="five-position-dialog" data-five-key="generation-dialog" data-five-tool="generate" role="dialog" aria-modal="true" aria-label="生成空移">
    <header class="five-position-title"><strong>${failed?'空移未完成':'生成空移'}</strong>${button('five.generate.close','×','aria-label="关闭生成空移"')}</header>
    <div class="five-position-body five-generation-body"><div>${summary}</div>${renderRapidSafety(view)}
    ${!running&&result?.operations?.length?`<div class="five-generation-status">${transitions.length?`${collisionSummary} · `:''}整机未验证</div>`:''}
    ${running?`<div class="five-computation-progress" data-five-key="generation-progress"><div role="status">${phase}</div>${cancelled?'<small>可以关闭此窗口；关闭不会把任务标记为成功。</small>':`<div class="five-progress-track" role="progressbar" aria-label="生成空移进度" aria-valuetext="${phase}"><i></i></div>`}</div>`:''}${view.error?`<div class="five-position-error" role="alert">${esc(view.error)}</div>`:''}${failureRows}</div>
    <footer class="five-position-actions">${running&&!cancelled?button('five.generate.cancel','取消计算'):''}${button('five.generate.close',running?'关闭窗口':'关闭','class="five-primary"')}</footer></div>`;
}
export async function generationCommand(context,view,action,ops,{applyDefaults=true}={}){
  if(action==='close'){view.fiveGenerationOpen=false;ops.renderProject(context,view);return;}
  if(action.startsWith('failure:')){
    if(view.pending)return;
    const id=action.slice('failure:'.length);
    view.fiveGenerationOpen=false;selectTool(view,'rapid');openRapid(view);rapidPick(view,id);ops.renderProject(context,view);return;
  }
  if(action==='cancel'){
    if(view.fiveGeneration)view.fiveGeneration.cancelRequested=true;
    const task=view.fiveRapidBatch?.running?view.fiveRapidBatch.task:view.fiveGeneration?.task;
    // Publish the cancel/close controls before waiting for a busy Scene.
    if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);
    if(task?.status==='Running'){
      const method=view.fiveRapidBatch?.running?'CamRapid.CancelTask':'CamPlan.CancelTask';
      void context.sceneProxy.invoke(method,{epoch:task.epoch,taskId:task.taskId}).catch(error=>{
        if(!view.disposed&&context.isCurrentProject?.()!==false){view.error=`取消请求未确认：${error.message}`;ops.renderProject(context,view);}
      });
    }
    return;
  }
  if(action!=='apply'||view.pending||view.fiveGeneration?.running)return;
  const state=view.fiveGeneration??={};state.running=true;state.cancelRequested=false;state.task=null;view.pending=true;view.error='';view.fiveGenerationOpen=true;
  const current=()=>!view.disposed&&context.isCurrentProject?.()!==false;
  ops.renderProject(context,view);
  try{
    if(applyDefaults)await applyRapidDefaultStrategy(context,view,ops);
    if(state.needsRefresh){
      const refreshed=await refreshMachiningState(context,view,ops);
      if(!refreshed)throw Error(view.error||'读取工程状态失败，请查看日志。');
      state.needsRefresh=false;view.pending=true;
    }
    if(view.fiveRapidBatch?.request){await generateRapids(context,view,ops);return;}
    // Generating travel reuses current cutting paths. Only stale/missing paths
    // need the prerequisite calculation; the user has no separate generate step.
    if(!state.request&&generatedGeometry(view)?.status==='Current'){await generateRapids(context,view,ops);return;}
    state.request??={commandId:crypto.randomUUID(),expected:view.scene.domain.expected,setupId:view.scene.domain.activeSetupId};
    try{state.task=await context.sceneProxy.invoke('CamPlan.Generate',state.request,{timeoutMs:machiningTaskTimeoutMs});}
    catch(error){
      // A lost acceptance reply is resolved using the same immutable command.
      // A confirmed domain rejection can safely release this request.
      if(/CAM_|CURVE_|LEAD_|PROCESS_/.test(error.message)){state.request=null;throw error;}
      throw Error('生成结果尚未确认，请查看日志中的错误信息。');
    }
    if(state.cancelRequested&&state.task.status==='Running')await context.sceneProxy.invoke('CamPlan.CancelTask',{epoch:state.task.epoch,taskId:state.task.taskId});
    ops.renderProject(context,view);
    while(state.task.status==='Running'&&!view.disposed){
      await new Promise(resolve=>setTimeout(resolve,100));
      if(view.disposed)return;
      state.task=await context.sceneProxy.invoke('CamPlan.QueryStatus',{epoch:state.task.epoch,taskId:state.task.taskId},{timeoutMs:machiningTaskTimeoutMs});
    }
    const task=state.task;state.request=null;
    if(task.status==='Completed'){
      await saveCommittedCutEdit(context,view,ops);
      ops.appendProjectLog(context,'ok',Number(task.result.operationCount)===1?'1 条切割路径已生成；无需工序间空移。':`更新 ${task.result.calculatedCount} 条切割路径，复用 ${task.result.reusedCount} 条，正在更新空移`);
      if(current()){
        state.needsRefresh=true;
        const refreshed=await refreshMachiningState(context,view,ops);
        if(!refreshed)throw Error(view.error||'读取生成后的工程状态失败，请查看日志。');
        state.needsRefresh=false;view.pending=true;
        if(!state.cancelRequested)await generateRapids(context,view,ops);
      }
    }else if(task.status==='Cancelled')ops.appendProjectLog(context,'info','已取消刀路生成');
    else throw Error(task.status==='Superseded'?'计算期间工程已变化，本次结果未采用。':task.error||'刀路生成失败。');
  }catch(error){
    if(!state.request)state.needsRefresh=true;
    if(state.cancelRequested&&/^(Cancelled|TRAVEL_CANCELLED)$/.test(error.message)){ops.appendProjectLog(context,'info','已取消空移生成');return;}
    if(/CAM_EPOCH_CHANGED/.test(error.message)){state.request=null;state.task=null;}
    if(current()){
      view.error=state.request||view.fiveRapidBatch?.request?'生成结果尚未确认，请查看日志中的错误信息。':generationError(error);
      ops.appendProjectLog(context,'error',`生成失败：${error?.message||String(error)}`);
    }
  }
  finally{state.running=false;view.pending=false;if(current())ops.renderProject(context,view);}
}
export function disposeGeneratedPreview(view){
  const group=view.fiveGeneratedPreview?.group;if(group){group.removeFromParent();group.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});}view.fiveGeneratedPreview=null;
}
export function updateGeneratedPreview(view){
  const raw=rapidPreview(view,orderingPreview(view,generatedGeometry(view))),enabled=new Set((view.scene?.intentToolpaths||[]).filter(c=>c.enabled!==false&&toolpathLayerVisible(view,c.id)).map(c=>c.id));
  const result=raw&&{...raw,operations:raw.operations?.filter(op=>enabled.has(op.intentId)),transitions:raw.transitions?.filter(t=>enabled.has(t.from)&&enabled.has(t.to))},viewport=view.viewport,show=showsGeneratedPreview(view);
  // Without generated paths, updateCutPreview owns the definition numbers.
  // An active generated preview with no visible operations must clear its labels.
  if(!viewport||!show||!result?.operations?.length){if(viewport&&show){viewport.setSpecificationAnnotations([]);view.fiveCutAnnotations=false;}disposeGeneratedPreview(view);return;}
  const selection=workspaceState(view),selected=selection.selectedIntentIds||[selection.selectedIntentId],placements=view.scene.domain.placements;
  const compensated=new Set(currentCompensation(view).map(item=>item.intentId));
  const manual=activeTool(view)==='manual-order'&&view.fiveOrder?.open,unifiedClear=rapidUnifiedCollisionClear(view);
  const key=JSON.stringify([result.revision,result.status,result.operations.map(op=>op.intentId),[...compensated],result.ordering,result.orderDraft,result.transitions,view.fiveCamPage,activeTool(view)==='rapid'?view.fiveRapid?.id:null,manual,placements.map(p=>p.transform),selected,viewport.id,unifiedClear]);
  if(view.fiveGeneratedPreview?.key===key&&view.fiveCutAnnotations&&viewport.specificationLabels.length===result.operations.length){syncToolpathLayerMaterials(view,view.fiveGeneratedPreview.group);return;}disposeGeneratedPreview(view);
  const group=new THREE.Group();group.name='five-generated-paths';const stale=result.status==='Stale',annotations=[];
  const line=(points,transform,color,rapid=false,marking=false,outdated=false)=>{
    const geometry=new THREE.BufferGeometry().setFromPoints(points.map(p=>new THREE.Vector3(...p).applyMatrix4(transform)));
    const dashed=rapid||outdated||marking;
    const material=dashed?new THREE.LineDashedMaterial({color,dashSize:outdated ? .65 : marking ? 1.2 : 3,gapSize:outdated ? .6 : marking ? .8 : 2,depthTest:rapid,depthWrite:false,transparent:outdated,opacity:outdated ? .65 : 1}):new THREE.LineBasicMaterial({color,depthTest:false});
    const object=new THREE.Line(geometry,material);if(dashed)object.computeLineDistances();object.renderOrder=40;object.raycast=()=>{};group.add(object);return object;
  };
  for(const op of result.operations){
    const outdated=op.status?op.status==='Stale':stale;
    const placement=placements.find(p=>p.id===op.placementId);if(!placement)continue;const matrix=new THREE.Matrix4().set(...placement.transform);
    if(!compensated.has(op.intentId))for(const part of op.parts){
      const color=toolpathLayerColor(view,op.intentId);
      line(part.points,matrix,color,false,op.machiningKind==='Mark',outdated).userData.generatedIntentId=op.intentId;
      if(part.role==='Cut')addCutDirectionMarkers(group,part.points.map(p=>new THREE.Vector3(...p).applyMatrix4(matrix).toArray()),op.intentId,color);
    }
    const cutStart=op.parts.find(part=>['Cut','Mark'].includes(part.role)&&part.points?.length)?.points[0]||op.start;
    const point=new THREE.Vector3(...cutStart).applyMatrix4(matrix).toArray();
    annotations.push({id:`path-order-${op.intentId}`,kind:'label',start:point,end:point,label:op.order,color:selected.includes(op.intentId)?sceneColors.selected:outdated||(result.orderDraft&&!result.orderDraft.includes(op.intentId))?sceneColors.stale:sceneColors.reference,pickThrough:!manual});
  }
  const placement=placements.find(p=>p.id===result.operations[0].placementId);if(placement){const matrix=new THREE.Matrix4().set(...placement.transform);for(const t of result.transitions){
    if(result.orderPreview){line(t.points,new THREE.Matrix4(),sceneColors.reference,true).name='five-order-preview-link';continue;}
    const chosen=activeTool(view)==='rapid'&&view.fiveRapid?.id===`${t.from}/${t.to}`;
    // Geometry is visible independently of its later collision report.
    // A direct order connector is not generated motion geometry.
    if(!hasRapidGeometry(t))continue;
    const unverified=t.geometryStatus!=='Generated'||t.validation!=='Clear'&&!unifiedClear,rapid=line(t.points,new THREE.Matrix4(),t.validation==='Collision'?sceneColors.error:unverified?sceneColors.rapidUnverified:chosen?sceneColors.selected:sceneColors.reference,true);
    Object.assign(rapid.userData,{generatedRapid:true,rapidTransitionId:`${t.from}/${t.to}`,rapidCollision:t.validation==='Collision',rapidUnverified:unverified});
    const failed=Number(t.failedSegment);if(t.validation==='Collision'&&Number.isInteger(failed)&&t.points[failed+1])
      line(t.points.slice(failed,failed+2),t.frame==='Setup'?new THREE.Matrix4():matrix,sceneColors.error);
  }}
  viewport.scene.add(group);viewport.setSpecificationAnnotations(annotations);
  for(let i=0;i<viewport.specificationLabels.length;++i){const element=viewport.specificationLabels[i].element,op=result.operations[i];
    element.classList.add('five-path-number');
    element.dataset.fiveIntentId=op.intentId;
    const chosen=selected.includes(op.intentId),outdated=(op.status?op.status==='Stale':stale)||(result.orderDraft&&!result.orderDraft.includes(op.intentId));
    element.style.background=sceneColorCss(chosen?sceneColors.selected:sceneColors.label);
    element.style.color=sceneColorCss(chosen?sceneColors.label:outdated?sceneColors.reference:sceneColors.labelText);
    element.style.borderColor=sceneColorCss(chosen?sceneColors.selected:outdated?sceneColors.stale:sceneColors.reference);
    if(manual){element.dataset.fiveOrderId=op.intentId;element.style.pointerEvents='auto';element.style.cursor='pointer';element.tabIndex=0;element.setAttribute('role','button');element.setAttribute('aria-label',`刀序 ${op.order}`);}
    else {delete element.dataset.fiveOrderId;element.style.pointerEvents='';element.style.cursor='';element.removeAttribute('tabindex');element.removeAttribute('role');element.removeAttribute('aria-label');}
  }
  view.fiveCutAnnotations=true;view.fiveGeneratedPreview={key,group};
  syncToolpathLayerMaterials(view,group);
}
