import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {button} from './workspaceControls.mjs';
import {buildPlayback} from './trajectoryPlayback.mjs';
import {selectTool} from './workspaceModel.mjs';
import {selectWorkpiece,workpieceSelectionBusy} from './workpieceSelection.mjs';
import {simulationCommand,pauseSimulation} from './trajectorySimulation.mjs';
import {floatingDialogStyle} from './floatingDialogs.mjs';

export function jobSteps(view){
  const d=view.scene?.domain;if(!d)return [];
  return d.setups.map(setup=>{
    const placement=d.placements.find(p=>p.setupId===setup.id),workpiece=d.workpieces.find(w=>w.id===placement?.workpieceId);
    const enabled=setup.enabled!==false&&!!placement&&!!workpiece&&workpiece.enabled!==false;
    let reason=!placement||!workpiece?'尚未导入工件':!enabled?(setup.enabled===false?'装夹已禁用':'工件已禁用'):'';
    if(!reason)try{reason=buildPlayback(setup,d.placements).reason;}catch(error){reason=error.message;}
    const geometry=setup.generatedGeometry;
    const outputReason=!placement||!workpiece?'尚未导入工件':!enabled?reason:geometry?.status!=='Current'?(geometry?.status==='Stale'?'刀路已过期，请重新生成。':'请先生成刀路。'):!geometry.operations?.length?'没有可输出刀路':'';
    return {setup,placement,workpiece,enabled,reason,outputReason};
  });
}
const jobSignature=view=>JSON.stringify([view.scene?.domain?.expected?.epoch,view.scene?.domain?.inputRevision,
  view.scene?.domain?.setups?.map(s=>[s.id,s.sequence,s.enabled,s.inputRevision,s.generatedGeometry?.resource,s.generatedGeometry?.status])]);
export function renderJobExecution(view){
  let run=view.fiveJobPlayback;
  if(run&&!run.switching&&run.signature!==jobSignature(view)){pauseSimulation(view);view.fiveJobPlayback=null;run=null;view.fiveJobOpen=true;view.fiveJobError='工序或加工数据已变化，请重新开始整任务仿真。';}
  const banner=run?`<div class="five-job-playback" data-five-key="job-playback" role="status">
    <strong>整任务仿真 · 工序 ${run.index+1} / ${run.ids.length}</strong>
    <span>${esc(run.boundary?(run.index===run.ids.length-1?'全部工序预览结束':'M00 · 本工序结束，请重新装夹并定位'):run.message||'按当前装夹轨迹预览')}</span>
    ${run.boundary&&run.index<run.ids.length-1?button('five.job.continue','确认换装夹，继续预览'):''}
    ${button('five.job.stop','结束整任务仿真')}</div>`:'';
  if(!view.fiveJobOpen)return banner;
  const rows=jobSteps(view),busy=workpieceSelectionBusy(view)||!!run,enabled=rows.filter(r=>r.enabled);
  return `${banner}<div class="five-position-dialog five-floating-dialog five-job-dialog" data-five-key="job-dialog" data-five-floating="job" ${floatingDialogStyle(view,'job')} role="dialog" aria-label="装夹工序与输出">
    <header class="five-position-title" data-five-dialog-drag><strong>装夹工序与输出</strong>${button('five.job.close','×','aria-label="关闭工序管理"')}</header>
    <div class="five-position-body"><p>每道工序对应一次装夹。刀路、姿态和单独仿真在该装夹内编辑；整任务按下列顺序执行。</p>
    <ol class="five-job-list">${rows.map((r,i)=>`<li data-five-key="job-${attr(r.setup.id)}"><div><strong>${i+1} · ${esc(r.setup.name)}</strong><small>${esc(r.workpiece?.name||'')} · ${esc(r.reason||'轨迹可预览')} · ${esc(r.outputReason||'可输出切割点 XYZ/IJK')}</small></div><div class="five-button-row">
      ${button(`five.job.edit:${r.setup.id}`,'编辑本工序',busy||!r.placement?'disabled':'')}
      ${button(`five.job.up:${r.setup.id}`,'上移',busy||i===0||rows[i-1]?.workpiece?.id!==r.workpiece?.id?'disabled':'')}
      ${button(`five.job.down:${r.setup.id}`,'下移',busy||i===rows.length-1||rows[i+1]?.workpiece?.id!==r.workpiece?.id?'disabled':'')}</div>
      ${i<rows.length-1?'<small class="five-job-boundary">M00 · 停机、换装夹、重新定位</small>':''}</li>`).join('')}</ol>
    <p>按工序顺序输出装夹坐标系中的 XYZ 和单位刀轴 IJK（mm）。IJK 按模态保持，仅在变化时更新；固定刀轴优先保留精确曲线，变化刀轴使用 CAM 姿态序列。每条刀路单独成段，工序切换之间插入 M00。</p>
    ${view.fiveJobError?`<p role="alert">${esc(view.fiveJobError)}</p>`:''}
    </div><footer class="five-position-actions">
    ${view.fiveJobRequest?button('five.job.retry','核对工序顺序',view.pending?'disabled':''):''}
    ${button('five.job.simulate','按工序顺序仿真',busy||!enabled.length||enabled.some(r=>r.reason)?'disabled':'')}
    ${button('five.job.export','输出切割点 XYZ/IJK',busy||!enabled.length||enabled.some(r=>r.outputReason)?'disabled':'')}
    </footer></div>`;
}
const valid=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
function finish(context,view,ops){
  const run=view.fiveJobPlayback;if(!run||run.ids[run.index]!==view.scene.domain.activeSetupId)return;
  run.boundary=true;ops.renderProject(context,view);
}
async function enter(context,view,ops){
  const run=view.fiveJobPlayback,row=jobSteps(view).find(r=>r.setup.id===run.ids[run.index]);
  if(!row||row.reason)throw Error(row?.reason||'工序已移除，请重新开始整任务仿真。');
  run.switching=true;
  try{await selectWorkpiece(context,view,row.placement.id,ops);}finally{run.switching=false;}
  if(!valid(context,view)||view.fiveJobPlayback!==run)return;
  if(view.scene.domain.activeSetupId!==row.setup.id)throw Error(view.error||'装夹切换失败。');
  await context.actions?.selectRibbonTab?.('toolpath');
  selectTool(view,'simulation');run.boundary=false;run.message='';
  run.onEnd=()=>finish(context,view,ops);ops.renderProject(context,view);
  if(await simulationCommand(context,view,'play',ops)===false)throw Error('本工序缺少有效的机床运动计划，请先完成机床规划。');
}
export async function jobCommand(context,view,action,ops){
  if(action==='open'){view.fiveJobOpen=true;view.fiveJobError='';ops.renderProject(context,view);return;}
  if(action==='close'){view.fiveJobOpen=false;ops.renderProject(context,view);return;}
  if(action==='stop'){pauseSimulation(view);view.fiveJobPlayback=null;ops.renderProject(context,view);return;}
  if(view.pending||workpieceSelectionBusy(view)&&!view.fiveJobRequest)return;
  if(view.fiveJobRequest&&action!=='retry')return;
  try{
    view.fiveJobError='';
    if(action==='continue'){
      const run=view.fiveJobPlayback;if(!run?.boundary||run.index>=run.ids.length-1)return;
      run.index++;await enter(context,view,ops);return;
    }
    if(view.fiveJobPlayback)return;
    const rows=jobSteps(view),ids=rows.map(r=>r.setup.id);
    if(action.startsWith('edit:')){
      const row=rows.find(r=>r.setup.id===action.slice(5));if(!row?.placement)return;
      await selectWorkpiece(context,view,row.placement.id,ops);view.fiveJobOpen=false;
      await context.actions?.selectRibbonTab?.('toolpath');return;
    }
    if(action==='retry'||action.startsWith('up:')||action.startsWith('down:')){
      const [direction,id]=action.split(':'),i=ids.indexOf(id),j=i+(direction==='up'?-1:1);
      if(action!=='retry'){if(i<0||j<0||j>=ids.length||rows[i].workpiece?.id!==rows[j].workpiece?.id)return;[ids[i],ids[j]]=[ids[j],ids[i]];}
      await setSetupOrder(context,view,ids,ops);return;
    }
    const enabled=rows.filter(r=>r.enabled);
    if(!enabled.length)throw Error('没有已启用的装夹工序。');
    if(action==='simulate'){
      if(enabled.some(r=>r.reason))throw Error('请先完成每道工序的装夹、刀路和空移。');
      view.fiveJobOpen=false;view.fiveJobPlayback={ids:enabled.map(r=>r.setup.id),index:0,boundary:false,signature:jobSignature(view)};
      await enter(context,view,ops);return;
    }
    if(action==='export'){
      const blocked=enabled.find(r=>r.outputReason);if(blocked)throw Error(`${blocked.setup.name}：${blocked.outputReason}`);
      const bridge=context.appProxy?.bridge??context.sceneProxy?.bridge;
      if(!bridge?.saveFileDialog)throw Error('当前宿主不支持选择输出文件。');
      const destination=await bridge.saveFileDialog({title:'输出切割点轨迹与 IJK',defaultPath:'加工任务-切割点.cl',defaultExtension:'cl',filters:[{name:'切割点轨迹 XYZ/IJK',extensions:['cl']}]});
      if(!destination||!valid(context,view))return;
      view.pending=true;ops.renderProject(context,view);
      const result=await context.sceneProxy.invoke('CamSetup.ExportProgram',{expected:view.scene.domain.expected,destination,coordinateFrame:'Setup',referencePoint:'CutPoint'});
      ops.appendProjectLog(context,'ok',`已输出 ${result.setupCount} 个装夹的切割点 XYZ/IJK（装夹坐标系，工序间 M00）：${result.path}`);
    }
  }catch(error){view.fiveJobError=error.message;view.fiveJobOpen=true;pauseSimulation(view);view.fiveJobPlayback=null;}
  finally{view.pending=false;if(valid(context,view))ops.renderProject(context,view);}
}
export async function setSetupOrder(context,view,ids,ops,expected=view.scene.domain.expected){
 if(view.pending||view.fiveJobPlayback)return;
 view.pending=true;view.error='';ops.renderProject(context,view);
 view.fiveJobRequest??={commandId:crypto.randomUUID(),expected:structuredClone(expected),setupIds:ids};
 try{
  const request=view.fiveJobRequest;
  try{await context.sceneProxy.invoke('CamSetup.SetSequence',request);}
  catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});if(receipt.status!=='Completed'){if(receipt.status==='NotFound')view.fiveJobRequest=null;throw error;}}
  view.fiveJobRequest=null;await saveCommittedProjectEdit(context,view,ops);await ops.refreshSceneState(context,view);
  ops.appendProjectLog(context,'ok','装夹姿态顺序已保存，整任务仿真和输出按此顺序执行。');
 }catch(error){view.error=error.message;}
 finally{view.pending=false;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
}
