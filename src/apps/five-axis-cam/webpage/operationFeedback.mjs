import {activeTool,TOOLS} from './workspaceModel.mjs';
import {escapeText as esc} from '../../_shared/workbench/utils/format.mjs';

// A blocking owner must remain visible even after its original editor hides.
export function blockingOperation(view){
 if(view.fiveFixtureDesignRequest)return {title:view.fiveFixtureDesignRequest.title||'正在计算支撑参数',detail:'计算完成后会自动恢复操作。',inline:true};
 const s=view.fiveSimulation;
 if(s?.checking)return {title:s.collisionCancelRequested?'正在停止碰撞与限位检测':'正在检测碰撞与限位',detail:'进度和取消操作显示在碰撞与限位页。',cancel:!s.collisionCancelRequested,inline:true};
 if(s?.repairing)return {title:s.repairCancelRequested?'正在停止自动修正':'正在修正当前问题',detail:'进度和取消操作显示在碰撞与限位页。',cancel:!s.repairCancelRequested,inline:true};
 const rapid=view.fiveRapid,batch=view.fiveRapidBatch;
 if(rapid?.request?.taskId||rapid?.safeGenerating&&view.pending&&!rapid.running)return {title:view.pending?'正在保存空移结果':'空移保存结果待确认',detail:'本段已通过检测，正在更新并保存工程。',retry:view.pending?null:'five.rapid.apply'};
 if(batch?.running&&batch.task?.status==='Completed')return {title:'正在保存空移结果',detail:'正在更新本次生成结果并保存工程。'};
 const requests=[
  [view.fiveToolAxisRequest&&!view.fiveAxisPointEdit?.request&&!view.fiveAxisPointSaving,'刀轴姿态'],[view.fiveTreeRequest,'刀路列表'],
  [view.fiveGeneration?.request||view.fiveGeneration?.running,'生成刀路'],[view.fiveRapid?.request||view.fiveRapidBatch?.running,'空移规划'],
  [view.fiveStart?.request,'刀路起点'],[view.fiveLeads?.request||view.fiveLeads?.saving,'引线设置'],
  [view.fiveOrder?.request||view.fiveOrder?.running,'排序'],
  [view.fiveJobRequest,'加工任务'],[view.fixtureRequest,'夹具'],[s?.request,'切割头'],
  [view.fiveWorkpieceSwitching,'切换工件'],[view.fiveMachine?.busy,'机床操作']
 ];
 const owner=requests.find(([busy])=>busy)?.[1];
 if(owner==='切换工件'&&(view.fiveOperationNavigationPreparing||view.fiveOperationAreaSync))return {title:'正在切换页面',detail:'正在读取目标页面的工件与工序。',inline:true};
 // fixtureRequest is the live command promise, not an uncertain receipt.
 // Scene readback may clear pending while that owner is still calculating,
 // publishing or saving its result.
 const processing=view.pending||owner==='机床操作'||owner==='夹具'&&!!view.fixtureRequest;
 const retry=view.fiveAxisPointEdit?.request?'five.points.apply':view.fiveStart?.request?'five.start.retry':view.fiveLeads?.request?'five.leads.retry':view.fiveRapid?.request?'five.rapid.apply':view.fiveGeneration?.request?'five.generate.apply':null;
 if(view.fiveMachine?.planning&&(!owner||owner==='机床操作'))return {title:'正在规划机床运动',detail:'请在机床加工仿真区域查看进度或取消规划。',inline:true};
 if(owner)return {title:processing?`正在处理${owner}`:`${owner}结果待确认`,detail:processing?'处理结束后会自动恢复操作。':'重试确认当前操作结果，已保存的数据会保留。',retry};
 if(view.sceneRefreshPending)return {title:'正在刷新工程状态',detail:'正在读取模型、刀路和机床列表。',cancel:true,inline:true};
 if(view.fiveAxisPointSaving||view.fiveAxisPointEdit?.request||view.fiveAxisPointLookup)return {title:'正在保存关键点',detail:'保存状态显示在当前姿态编辑区。',inline:true};
 if(view.fivePlacement?.saving||view.fivePlacement?.request)return {title:view.fivePlacement.mode==='scale'?'正在缩放':'正在保存位置',detail:'',inline:true};
 if(view.pending)return {title:view.progress?.title||`正在${TOOLS[activeTool(view)]?.title||'保存工程'}`,detail:'处理结束后会自动恢复操作。'};
 return null;
}
export function operationUnavailableReason(view){const op=blockingOperation(view);return op?`${op.title}。${op.detail}`:'';}
export function renderBlockingOperation(view){
 const op=blockingOperation(view);if(!op||op.inline||view.pending||view.fiveSimulation?.checking||view.fiveSimulation?.repairing||view.fixtureRequest&&view.progress?.title)return '';
 return `<section class="five-blocking-operation" data-five-key="blocking-operation" role="status" aria-live="polite"><div><strong>${esc(op.title)}</strong><p>${esc(op.detail)}</p></div>${op.retry?`<button type="button" data-five-command="${op.retry}">重试</button>`:''}</section>`;
}
export async function cancelBlockingOperation(view){
 const s=view.fiveSimulation;
 if(s?.checking){s.collisionCancelRequested=true;await s.cancelCheck?.();return;}
 if(s?.repairing){s.repairCancelRequested=true;await s.cancelRepair?.();return;}
 if(view.sceneRefreshPending){
  // The read may finish later; its generation fence prevents a stale install.
  view.sceneRefreshRequest=(view.sceneRefreshRequest||0)+1;view.sceneRefreshPending=false;
  view.pending=!!view.fivePathOperationPending||!!view.fiveSplit?.picking||!!view.fiveSplit?.committing;
 }
}
