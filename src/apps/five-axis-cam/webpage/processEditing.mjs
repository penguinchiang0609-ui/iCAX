import { activeTool, currentDraft, workspaceState, sceneItems } from './workspaceModel.mjs';
import { cutPolicyDraft,parameterOverrides,CUT_PARAMETER_FIELDS } from './processPolicyDraft.mjs';
import { renderKerfFields, renderPoseFields } from './processPolicyUI.mjs';
import { section, button, hint, facts,field } from './workspaceControls.mjs';
import { currentCompensation } from './cutCompensation.mjs';
import { generationError } from './toolpathGeneration.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import { waitForTaskResult } from './taskProgressStream.mjs';
import { machiningTaskTimeoutMs } from './operationTiming.mjs';

const tools = ['compensation','adjust','process'];
const fields = {
  process:CUT_PARAMETER_FIELDS.map(([name])=>name),
  compensation:['cutKerf','cutKerfOwner','cutMeasurement','cutCorner','cutMiterLimit'],
  adjust:['cutPoseInitialization','cutTransition','cutLeadTilt','cutSideTilt','cutLeadTolerance','cutSideTolerance','cutAngularTolerance','cutNormalPreference','cutAxisI','cutAxisJ','cutAxisK'],
};
const selected = view => sceneItems(view).intents.find(item => item.id === workspaceState(view).selectedIntentId);
export const hasProcessEditor = view => tools.includes(activeTool(view)) && (activeTool(view)!=='process'||(workspaceState(view).selectedIntentIds||[workspaceState(view).selectedIntentId]).length===1) && !!selected(view)?.processRef?.revision;
const key = view => { const item = selected(view); return JSON.stringify([view.scene?.domain?.expected?.epoch,item?.id,item?.processRef,item?.poseRef]); };
const compilationKey = view => JSON.stringify([key(view),view.scene?.domain?.expected]);
const current = (context,view) => !view.disposed && context.isCurrentProject?.() !== false;
function populate(d,item) {
  const { assignment:a, posePolicy:p, library:l } = item;
  for(const [i,k] of ['cutAxisI','cutAxisJ','cutAxisK'].entries())d[k]=String(p.direction?.[i]??(i===2?1:0));
  Object.assign(d,{cutProcessName:l.name,cutMaterial:l.material||'',cutThickness:l.thicknessMm == null?'':String(l.thicknessMm),cutGas:l.gas||'',
    cutKerf:String(a.kerf.widthMm),cutKerfOwner:a.kerf.owner,cutMeasurement:a.kerf.measurementSection,cutCorner:a.kerf.cornerMode,cutMiterLimit:String(a.kerf.miterLimit),
    cutOvercut:String(a.overcutLengthMm),cutGap:String(a.gapLengthMm),cutPoseInitialization:p.initialization||'',cutTransition:p.transitionMetric||'',cutNormalPreference:p.surfaceNormalPreferenceWeight==null?'':String(p.surfaceNormalPreferenceWeight)});
  const values={...l.parameters,...Object.fromEntries(a.overrides.map(o => [o.parameterId,o.value]))};
  for(const [id,name] of [['feedMmPerSecond','cutFeed'],['laserPowerW','cutPower'],['gasPressureBar','cutPressure'],['standOffMm','cutStandOff'],['focusOffsetMm','cutFocus'],['pierceTimeSeconds','cutPierceTime'],['pierceFootprintRadiusMm','cutPierceRadius']])d[name]=values[id]==null?'':String(values[id]);
  for(const [id,name] of [['leadAngleRad','cutLeadTilt'],['sideAngleRad','cutSideTilt'],['leadDeviationLimitRad','cutLeadTolerance'],['sideDeviationLimitRad','cutSideTolerance'],['angularToleranceRad','cutAngularTolerance']])d[name]=p[id]==null?'':String(Number((p[id]*180/Math.PI).toPrecision(12)));
  for(const [prefix,lead] of [['cutLead',a.leadIn],['cutExit',a.leadOut]]) {
    d[`${prefix}Kind`]=lead.kind;d[`${prefix}Placement`]=lead.placement;d[`${prefix}Length`]=String(lead.lengthMm);
    d[`${prefix}Angle`]=String(Number((lead.angleRad*180/Math.PI).toPrecision(12)));d[`${prefix}Radius`]=lead.radiusMm==null?'':String(lead.radiusMm);
  }
}
export async function loadProcessDraft(context,view,ops,force=false) {
  if(!hasProcessEditor(view)||view.pending)return;
  const d=currentDraft(view),snapshot=key(view),item=selected(view),revision=d.processEditRevision||0;
  if(view.fiveProcessRead?.snapshot===snapshot&&view.fiveProcessRead.draft===d&&!force)return;
  if(d.processRequest)throw Error('请先重试上次提交，以核对工程中的参数。');
  if(!force && (d.processPolicyKey===snapshot || (d.processPolicy && d.processDirty)))return;
  const request=view.fiveProcessRead={snapshot,draft:d};view.error='';ops.renderProject(context,view);
  try {
    const response=await context.sceneProxy.invoke('CamProcess.Read',{expected:view.scene.domain.expected,intentIds:[item.id]});
    if(!current(context,view)||view.fiveProcessRead!==request||snapshot!==key(view)||(d.processEditRevision||0)!==revision)return;
    d.processPolicy=response.items[0];d.processPolicyKey=snapshot;d.processDirty=false;d.processDirtyFields=[];populate(d,d.processPolicy);
  } catch(error) {if(current(context,view)&&view.fiveProcessRead===request)view.error=error.message;}
  finally {if(view.fiveProcessRead===request)view.fiveProcessRead=null;if(current(context,view))ops.renderProject(context,view);}
}
export function processDraftChanged(view,name) {
  if(!hasProcessEditor(view)||!name.startsWith('cut'))return;
  const d=currentDraft(view);d.processEditRevision=(d.processEditRevision||0)+1;d.processDirty=true;
  d.processDirtyFields=[...new Set([...(d.processDirtyFields||[]),name])];
  (d.processFieldVersions??={})[name]=d.processEditRevision;
}
export async function editProcessCommand(context,view,action,ops) {
  if(action==='compile'||action==='cancel-compile'){await compileProcess(context,view,action,ops);return;}
  if(action==='load'){await loadProcessDraft(context,view,ops,true);return;}
  if(!hasProcessEditor(view)||view.pending)return;
  const d=currentDraft(view),item=selected(view),tool=activeTool(view);
  if(!d.processRequest && d.processPolicyKey!==key(view))throw Error('工程参数已变化，请先读取当前工程参数。');
  let method='CamProcess.Update',body,appliedFields=fields[tool]||[];
  if(d.processRequest) { method=d.processRequest.method; }
  else if(tool==='process'&&['apply','restore-parameters'].includes(action)){
    appliedFields=action==='restore-parameters'?fields.process:appliedFields.filter(name=>d.processDirtyFields?.includes(name));
    if(!appliedFields.length)return;
    const values=action==='restore-parameters'?Object.fromEntries(appliedFields.map(name=>[name,''])):d;
    body={patch:{overrides:parameterOverrides(values,d.processPolicy.assignment,appliedFields)}};
  }else if(action==='apply') {
    // An operation validates only its edited category. Unsaved values in a
    // different panel neither block it nor silently enter this submission.
    const effective={...d};populate(effective,d.processPolicy);for(const name of appliedFields)effective[name]=d[name];
    const policy=cutPolicyDraft(effective,{includePose:tool==='adjust'});
    if(tool==='adjust'){method='CamProcess.UpdatePose';const {mode,keypoints,...patch}=policy.pose;body={patch};}
    else if(tool==='compensation')body={patch:{kerf:policy.assignment.kerf}};
    else return;
  } else return;
  d.processRequest ??= {method,appliedFields,fieldVersions:{...d.processFieldVersions},payload:{commandId:crypto.randomUUID(),expected:view.scene.domain.expected,intentIds:[item.id],...body}};
  const submitted=d.processRequest;
  view.pending=true;view.error='';ops.renderProject(context,view);
  try {
    try {await context.sceneProxy.invoke(d.processRequest.method,d.processRequest.payload);}
    catch(error) {
      let receipt;try {receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:d.processRequest.payload.expected.epoch,commandId:d.processRequest.payload.commandId});}catch {throw error;}
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')d.processRequest=null;throw error;}
    }
    d.processRequest=null;
    await saveCommittedCutEdit(context,view,ops);
    ops.appendProjectLog(context,'ok','工艺参数已保存，需重新生成刀路');
    if(current(context,view))await ops.refreshSceneState(context,view);
    if(!current(context,view)||selected(view)?.id!==item.id)return;
    const readKey=key(view),response=await context.sceneProxy.invoke('CamProcess.Read',{expected:view.scene.domain.expected,intentIds:[item.id]});
    if(!current(context,view)||readKey!==key(view))return;
    d.processPolicy=response.items[0];d.processPolicyKey=key(view);
    const refreshed={...d};populate(refreshed,d.processPolicy);
    const dirty=new Set(d.processDirtyFields||[]);
    for(const name of Object.keys(refreshed).filter(name=>name.startsWith('cut'))) {
      if(!dirty.has(name)||(submitted.appliedFields.includes(name) && d.processFieldVersions?.[name]===submitted.fieldVersions[name])) {
        d[name]=refreshed[name];dirty.delete(name);
      }
    }
    d.processDirtyFields=[...dirty];d.processDirty=dirty.size>0;
  } catch(error){if(current(context,view))view.error=generationError(error);}
  finally {view.pending=false;if(current(context,view))ops.renderProject(context,view);}
}
async function compileProcess(context,view,action,ops) {
  if(!hasProcessEditor(view))return;
  const d=currentDraft(view),item=selected(view);
  if(action==='cancel-compile') {
    if(d.processCompilationTask?.status==='Running')await context.sceneProxy.invoke('CamPlan.CancelTask',{epoch:d.processCompilationTask.epoch,taskId:d.processCompilationTask.taskId});
    return;
  }
  if(view.pending)return;
  const snapshot=compilationKey(view);view.pending=true;view.error='';d.processCompilation=null;ops.renderProject(context,view);
  try {
    let task=await context.sceneProxy.invoke('CamProcess.Compile',{commandId:crypto.randomUUID(),expected:view.scene.domain.expected,intentId:item.id});
    d.processCompilationTask=task;if(current(context,view))ops.renderProject(context,view);
    task=await waitForTaskResult(context,task,{active:()=>current(context,view),queryMethod:'CamPlan.QueryStatus',cancelMethod:'CamPlan.CancelTask',timeoutMs:machiningTaskTimeoutMs,
      onProgress:progress=>{d.processCompilationTask=progress;}});
    if(!task||!current(context,view)||snapshot!==compilationKey(view))return;
    if(task.status==='Cancelled'){ops.appendProjectLog(context,'info','已取消工艺编译');return;}
    if(task.status!=='Completed')throw Error(task.status==='Superseded'?'工程已变化，请重新编译。':task.error||'工艺编译失败');
    const result=await context.sceneProxy.invoke('CamProcess.QueryCompiled',{epoch:task.epoch,taskId:task.taskId});
    if(!current(context,view)||snapshot!==compilationKey(view))return;
    d.processCompilation={snapshot,result};ops.appendProjectLog(context,'ok','局部工艺几何已编译，机床运动尚未规划');
  } catch(error){if(current(context,view))view.error=error.message;}
  finally{view.pending=false;d.processCompilationTask=null;if(current(context,view))ops.renderProject(context,view);}
}
export function renderProcessEditor(view,d) {
  if(activeTool(view)==='adjust'&&selected(view)?.cutType==='StandardHole')return hint('标准孔保持固定方向。修改姿态需在“设置 → 高级”中切换加工方式。');
  const tool=activeTool(view),ready=d.processPolicyKey===key(view),policy=d.processPolicy;
  let html=section('process-library','工程工艺',facts([['工艺',policy?.library.name||'尚未读取'],['状态',policy?.readiness.status==='Incomplete'?'工艺未完整':'未经设备验证']])
    + (!ready?hint('工程参数尚未读取或版本已变化。读取后再应用当前操作。'):'')
    + button('five.process.load','读取工程参数',view.pending?'disabled':''));
  if(!policy)return html;
  if(tool==='process'){
    const parameter=([name,id,label])=>{const declared=Object.hasOwn(policy.library.parameters||{},id);return field(d,name,label,{disabled:!declared,placeholder:declared?'留空恢复继承':'当前工艺未配置'});};
    return section('process-current','当前刀路工艺',facts([['工艺',policy.library.name],['材料 / 厚度',[policy.library.material,policy.library.thicknessMm==null?'':`${policy.library.thicknessMm} mm`].filter(Boolean).join(' · ')||'未指定'],['辅助气体',policy.library.gas||'未指定']]))
      +section('process-parameters','切割参数',CUT_PARAMETER_FIELDS.slice(0,4).map(parameter).join(''))
      +`<details class="five-cam-process-advanced" data-five-key="process-advanced"><summary>焦点与穿孔</summary>${CUT_PARAMETER_FIELDS.slice(4).map(parameter).join('')}</details>`
      +section('process-apply','应用范围：所选刀路',hint(d.processDirty?'存在尚未应用的参数草稿。':'当前显示工程中保存的有效参数。')+`<div class="five-button-row">${button('five.process.apply',d.processRequest?'重试':'应用',view.pending||!ready?'disabled':'class="five-primary"')}${button('five.process.restore-parameters','恢复继承',view.pending||!ready?'disabled':'')}</div>`);
  }
  html+= tool==='compensation'?section('edit-kerf','切缝补偿',renderKerfFields(d))
    :section('edit-pose','连续姿态要求',renderPoseFields(d),'°');
  if(tool==='compensation'){
    const preview=currentCompensation(view).find(i=>i.intentId===selected(view)?.id);
    const previewError=preview?.error||view.fiveCompensation?.errors?.[selected(view)?.id]||view.fiveCompensation?.error;
    html+=hint(previewError?`刀补未能显示：${generationError(Error(previewError))}`:view.fiveCompensation?.loading?'正在计算刀补…':preview?'浅色线为原轮廓，实线为补偿刀路；颜色跟随刀路图层。':'应用切缝补偿后，场景同时显示原轮廓和补偿刀路。');
  }
  const disabled=view.pending || (!ready && !d.processRequest)?'disabled':'class="five-primary"';
  html+=section('process-apply','保存到工程',hint(d.processDirty?'存在尚未应用的参数草稿。':'当前显示工程中保存的参数。')
    +button('five.process.apply',d.processRequest?'重试上次提交':'应用到工程',disabled));
  const compiled=d.processCompilation?.snapshot===compilationKey(view)?d.processCompilation.result:null;
  html+=section('process-compile','工艺几何检查',hint('检查当前刀路中已保存的精确工艺几何；机床运动和整机验证需另行完成。')
    +button('five.process.compile','编译已保存的工艺',view.pending?'disabled':'')
    +(d.processCompilationTask?button('five.process.cancel-compile','取消编译'):'')
    +(compiled?facts([['范围','当前刀路'],['总长度',`${compiled.lengthMm.toPrecision(6)} mm`],
      ['路径段',String(compiled.spans.length)],['工艺事件',String(compiled.eventSequence.length)],['运动','尚未规划']])
      +hint('仍需检查邻近几何、切割头包络、机床可达性和设备工艺资格。'):''));
  return html;
}
