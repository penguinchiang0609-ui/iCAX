import {chooseModelFile} from './workpieceFilePicker.mjs';
import {revealSceneWorkpiece} from './sceneListReveal.mjs';
import {revealSceneListSelection} from './listNavigation.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import { activeTool, selectTool } from './workspaceModel.mjs';
import { designChanged, designCommand, designPick, highlightDesign } from './fixtureDesign.mjs';
import {enabledWorkpiecesOnly,workpieceEnabled} from './workpieceState.mjs';
import {fixtureLibraryCommand,closeFixtureChooser} from './fixtureLibrary.mjs';
import {fixturePlateCommand} from './fixturePlateEditor.mjs';
import {mergeDomainState} from './domainActions.mjs';
import {fixtureTaskRequestOptions,queryFixtureTask} from './fixtureTaskRequests.mjs';
import {loadFixtureUnitDetails} from './fixtureQueries.mjs';

export const fixtureParts = view => (view.scene?.domain?.fixtures??[]).filter(f=>f.setupId===view.scene.domain.activeSetupId);
export const fixtures = view => enabledWorkpiecesOnly(view)&&!workpieceEnabled(view)?[]:(view.scene?.domain?.fixtureUnits ?? []).filter(f => f.setupId === view.scene.domain.activeSetupId);
const state = view => view.fiveAxisFixtures ??= { selected: '', drafts: {}, clearance: '2' };
export function selectedFixture(view) {
  const list = fixtures(view), ui = state(view);
  if (!list.some(f => f.id === ui.selected)) ui.selected = list[0]?.id ?? '';
  const item=list.find(f => f.id === ui.selected);
  if(view.fiveFixtureSelectedPart&&!item?.partIds.includes(view.fiveFixtureSelectedPart))view.fiveFixtureSelectedPart='';
  return item;
}
const format = n => String(Math.round(n * 1e8) / 1e8);
const editFields=['name','x','y','z','rx','ry','rz'],poseFields=editFields.slice(1),inputBindings=new WeakMap();
function itemDraft(view,item,reset=false){
  if (!item) return null;
  const ui = state(view), previous = ui.drafts[item.id], scope=`${view.scene.domain.expected.epoch}:${item.setupId}`,key = `${scope}:${item.inputRevision}`;
  const edits=view.fiveFixtureEdits,editing=previous?.inputTarget&&inputBindings.get(previous.inputTarget)?.draft===previous&&previous.inputTarget.ownerDocument?.activeElement===previous.inputTarget;
  if (!reset && previous?.scope===scope && (previous.key === key || previous.dirty || editing || edits?.inflight?.draft===previous || edits?.jobs.some(job=>job.draft===previous))) return previous;
  const m = item.transform, ry = Math.asin(Math.max(-1, Math.min(1, -m[8]))), gimbal = Math.abs(Math.cos(ry)) < 1e-8;
  const draft={ key,scope,revision: 0, dirty: false, name: item.name,
    x: format(m[3]), y: format(m[7]), z: format(m[11]),
    rx: format((gimbal ? 0 : Math.atan2(m[9], m[10])) * 180 / Math.PI), ry: format(ry * 180 / Math.PI),
    rz: format((gimbal ? Math.atan2(-m[1], m[5]) : Math.atan2(m[4], m[0])) * 180 / Math.PI),fieldRevisions:{}};
  draft.base=Object.fromEntries(editFields.map(k=>[k,draft[k]]));return ui.drafts[item.id]=draft;
}
export function fixtureDraft(view, reset = false) {return itemDraft(view,selectedFixture(view),reset);}
const dirtyDraft=d=>d.dirty=editFields.some(k=>d[k]!==d.base[k]);
function editScope(context,view,item,draft){
 const proxy=context.sceneProxy,projectId=context.project?.projectId,projectProxy=context.projectProxy,epoch=view.scene.domain.expected.epoch,setupId=item.setupId;
 const active=()=>current(context,view)&&context.sceneProxy===proxy&&context.projectProxy===projectProxy&&context.project?.projectId===projectId&&view.scene?.domain?.expected?.epoch===epoch&&view.scene.domain.activeSetupId===setupId&&state(view).drafts[item.id]===draft&&fixtures(view).some(f=>f.id===item.id);
 return {context,view,proxy,projectId,epoch,setupId,unitId:item.id,draft,active,frozen:{...context,sceneProxy:proxy,projectProxy,project:{...context.project},isCurrentProject:active}};
}
export function beginFixtureInput(context,view,target){
 const field=target?.dataset?.fiveFixtureField;if(!editFields.includes(field)||!current(context,view))return null;
 const ui=state(view);if(ui.proxy&&ui.proxy!==context.sceneProxy)ui.drafts={};ui.proxy=context.sceneProxy;
 const item=selectedFixture(view),draft=fixtureDraft(view);if(!item||!draft)return null;
 const binding={...editScope(context,view,item,draft),field,before:draft[field]};draft.inputTarget=target;inputBindings.set(target,binding);return binding;
}
export function fixtureFieldChanged(view, name, value,target,context) {
  if(name.startsWith('design:')){designChanged(view,name.slice(7),value);return;}
  if (name === 'clearance') { state(view).clearance = value; return; }
  let binding=target?(inputBindings.get(target)||context&&beginFixtureInput(context,view,target)):null;
  if(binding&&!binding.active()&&context&&current(context,view)&&context.sceneProxy===binding.proxy&&context.projectProxy===binding.frozen.projectProxy&&context.project?.projectId===binding.projectId&&view.scene?.domain?.expected?.epoch===binding.epoch&&view.scene.domain.activeSetupId===binding.setupId&&selectedFixture(view)?.id===binding.unitId)binding=beginFixtureInput(context,view,target);
  if(target&&(!binding?.active()||selectedFixture(view)?.id!==binding.unitId))return;
  const d = binding?.draft||fixtureDraft(view);
  if (d && editFields.includes(name)) { d[name] = value; d.fieldRevisions[name]=(d.fieldRevisions[name]||0)+1;++d.revision;dirtyDraft(d); }
}
function fixtureEditState(view){return view.fiveFixtureEdits??={jobs:[],request:null,inflight:null};}
function queueFixtureEdit(scope,field,ops){
 const {view,draft}=scope,kind=field==='name'?'name':'pose',fields=kind==='name'?['name']:poseFields,s=fixtureEditState(view);
 const values=Object.fromEntries(fields.map(k=>[k,draft[k]])),signature=JSON.stringify(values),queued=s.jobs.find(j=>j.draft===draft&&j.kind===kind);
 if(queued?.signature===signature||s.inflight?.draft===draft&&s.inflight.kind===kind&&s.inflight.signature===signature)return s.request||Promise.resolve();
 const item=fixtures(view).find(f=>f.id===scope.unitId),baseCurrent=draft.key===`${draft.scope}:${item?.inputRevision}`;
 if(!queued&&(!s.inflight||s.inflight.kind!==kind)&&baseCurrent&&fields.every(k=>draft[k]===draft.base[k]))return s.request||Promise.resolve();
 const job={...scope,kind,fields,values,signature,versions:{...draft.fieldRevisions},ops};if(queued)s.jobs.splice(s.jobs.indexOf(queued),1,job);else s.jobs.push(job);
 if(!s.request){const task=Promise.resolve().then(()=>drainFixtureEdits(s));s.request=task;view.fiveFixtureEditRequest=task;
  void task.finally(()=>{if(s.request!==task)return;s.request=null;view.fiveFixtureEditRequest=null;if(view.fiveFloatingInputCommit===task)view.fiveFloatingInputCommit=null;const finalScope=s.lastScope||scope;if(finalScope.active()){ops.renderProject(finalScope.context,view);finalScope.context.actions?.refreshProjectHistoryControls?.();}}).catch(()=>{});
  view.fiveFloatingInputCommit=task;
 }return s.request;
}
async function readFixtureEditState(job){
 const {view,proxy}=job;
 for(let attempt=0;attempt<3&&job.active();++attempt){const expected=JSON.stringify(view.scene.domain.expected),payload=await proxy.invoke('CamScene.ReadState',{});
  if(!job.active())return false;if(JSON.stringify(view.scene.domain.expected)!==expected)continue;
  if(payload.expected?.epoch!==job.epoch||payload.activeSetupId!==job.setupId)return false;
  mergeDomainState(view.scene,payload);return true;
 }return false;
}
async function drainFixtureEdits(s){
 while(s.jobs.length){const job=s.jobs.shift(),{view,draft,ops}=job;if(!job.active())continue;s.inflight=job;s.lastScope=job;
  while((view.pending||view.fixtureRequest)&&job.active())await new Promise(resolve=>setTimeout(resolve,20));if(!job.active()){s.inflight=null;continue;}
  try{
   const item=fixtures(view).find(f=>f.id===job.unitId);if(item.locked)throw Error('请先解锁夹具单元。');
   const request={commandId:crypto.randomUUID(),expected:view.scene.domain.expected,setupId:job.setupId,unitId:job.unitId};
   if(job.kind==='name'){request.name=job.values.name.trim();if(!request.name)throw Error('请填写工装名称。');}
   else{if(item.kind==='Generated')throw Error('生成支撑的拼装位置固定，不能平移或旋转。');request.transform=fixtureTransform(job.values);}
   await invoke(job.frozen,'CamFixture.UnitUpdate',request);
   const saveState={};await saveCommittedProjectEdit(job.frozen,saveState);
   if(!job.active())continue;
   if(saveState.fiveWorkpieceSaveError)view.error=view.fiveWorkpieceSaveError=saveState.fiveWorkpieceSaveError;
   if(!await readFixtureEditState(job)){if(job.active())throw Error('夹具修改已提交，但状态尚未刷新，请刷新工程后继续。');continue;}
   for(const field of job.fields){draft.base[field]=field==='name'?request.name:job.values[field];if((draft.fieldRevisions[field]||0)===(job.versions[field]||0)&&draft[field]===job.values[field])draft[field]=draft.base[field];}
   draft.key=`${draft.scope}:${fixtures(view).find(f=>f.id===job.unitId)?.inputRevision}`;dirtyDraft(draft);
   if(ops.refreshActiveAreaView)await ops.refreshActiveAreaView(job.frozen,view);
   if(job.active()){ops.renderProject(job.context,view);job.context.actions?.refreshProjectHistoryControls?.();}
  }catch(error){if(job.active()){view.error=errorText(error);ops.renderProject(job.context,view);}s.jobs.length=0;throw error;}
  finally{s.inflight=null;}
 }
}
export function finishFixtureInput(context,view,target,cancel,ops){
 const binding=inputBindings.get(target)||beginFixtureInput(context,view,target);if(!binding?.active())return Promise.resolve(false);
 const {draft,field}=binding;
 if(cancel){draft[field]=binding.before;draft.fieldRevisions[field]=(draft.fieldRevisions[field]||0)+1;++draft.revision;dirtyDraft(draft);if(selectedFixture(view)?.id===binding.unitId)target.value=draft[field];ops.renderProject(context,view);return Promise.resolve(true);}
 binding.before=draft[field];return queueFixtureEdit(binding,field,ops);
}
export async function flushFixtureEdits(context,view,ops){
 for(const item of fixtures(view)){const draft=itemDraft(view,item);if(!draft?.dirty)continue;const scope=editScope(context,view,item,draft);
  if(draft.name!==draft.base.name)queueFixtureEdit(scope,'name',ops);
  if(item.kind!=='Generated'&&poseFields.some(k=>draft[k]!==draft.base[k]))queueFixtureEdit(scope,'x',ops);
 }
 await fixtureEditState(view).request;
 if(fixtures(view).some(item=>state(view).drafts[item.id]?.dirty))throw Error(view.error||'夹具输入尚未保存，请修正后重试。');
}
export const fixtureClearance = view => state(view).clearance;
const numeric = (value, label) => {
  if (!String(value).trim() || !Number.isFinite(Number(value))) throw Error(`请填写有效的${label}。`);
  return Number(value);
};
export function fixtureTransform(d) {
  const [x,y,z] = ['x','y','z'].map(k => numeric(d[k], `${k.toUpperCase()} 坐标`));
  const [rx,ry,rz] = ['rx','ry','rz'].map(k => numeric(d[k], '旋转角度') * Math.PI / 180);
  const [a,b,c,s,t,u] = [Math.cos(rx),Math.cos(ry),Math.cos(rz),Math.sin(rx),Math.sin(ry),Math.sin(rz)];
  return [c*b,c*t*s-u*a,c*t*a+u*s,x, u*b,u*t*s+c*a,u*t*a-c*s,y, -t,b*s,b*a,z, 0,0,0,1];
}
const messages = {
  CAM_REVISION_CONFLICT: '工程已变化，请刷新后重新操作。', CAM_FIXTURE_ENABLED_REQUIRED: '请先导入工装，并开启至少一个工装的干涉检查。',
  CAM_FIXTURE_WORKPIECE_REQUIRED: '请先导入加工工件。', CAM_FIXTURE_CLEARANCE_INVALID: '检查间隙须在 0 至 1000 mm 之间。',
  CAM_CLEARANCE_SERVICE_UNAVAILABLE: '几何间隙检查服务未加载。', CAM_IMPORT_BUSY: '请等待当前计算结束。',
};
const errorText = error => Object.entries(messages).find(([key]) => error.message?.includes(key))?.[1] ?? error.message ?? String(error);
const current = (context,view) => !view.disposed && context.isCurrentProject?.() !== false;
async function invoke(context,method,request) {
  try { return await context.sceneProxy.invoke(method,request,fixtureTaskRequestOptions); }
  catch (error) {
    // Recover an executed command after a lost response without duplicating it.
    const receipt = await context.sceneProxy.invoke('CamScene.QueryCommand', { epoch: request.expected.epoch, commandId: request.commandId },fixtureTaskRequestOptions);
    if (receipt.status === 'Completed') return /\.(Import|Check)$/.test(method) ? {status:'Completed',result:receipt.result} : receipt.result;
    if (receipt.status === 'Running') return context.sceneProxy.invoke(method,request,fixtureTaskRequestOptions);
    throw error;
  }
}
async function waitTask(context, view, task, ops) {
  view.fiveAxisFixtureTask = task; ops.renderProject(context,view);
  while (task.status === 'Running' && !view.disposed) {
    await new Promise(resolve => setTimeout(resolve,100));
    if (view.disposed) return null;
    task = await queryFixtureTask(context,task);
    view.fiveAxisFixtureTask = task;
  }
  if (task.status === 'Cancelled') return null;
  if (task.status === 'Superseded') throw Error('计算期间工程已变化，请重新执行。');
  if (task.status !== 'Completed') throw Error(task.error || '工装操作失败。');
  return task.result;
}
export async function fixtureCommand(context, view, action, ops) {
  if(action.startsWith('light-'))return;
  // Selecting a plate is local inspection, so it must keep keyboard focus
  // instead of entering the request state used by model mutations.
  if(action.startsWith('part-select:')){if(!view.fixtureRequest)return fixturePlateCommand(context,view,action,ops);return;}
  if(action.startsWith('part-')&&view.fiveFixtureEditRequest)await view.fiveFixtureEditRequest;
  if(action!=='cancel'&&view.fiveFixtureEditRequest)await view.fiveFixtureEditRequest;
  if(['cancel','design.cancel','design.close'].includes(action))return runFixtureCommand(context,view,action,ops);
  if(view.fixtureRequest)return;
  const request=Promise.resolve().then(()=>runFixtureCommand(context,view,action,ops));
  view.fixtureRequest=request;
  try{return await request;}finally{view.fixtureRequest=null;if(current(context,view))ops.renderProject(context,view);}
}
async function runFixtureCommand(context, view, action, ops) {
  if(action.startsWith('part-'))return fixturePlateCommand(context,view,action,ops);
  if(action==='parts-edit'||action==='parts-done'){
    if(view.pending||view.fiveFixturePlate?.open||!current(context,view))return;
    const unit=selectedFixture(view);if(unit?.kind!=='Generated')return;
    view.fiveFixtureSelectedPart=action==='parts-done'?'':unit.partIds.find(id=>view.scene.domain.fixtures.some(p=>p.id===id&&p.enabled!==false&&p.visible!==false))||unit.partIds[0]||'';
    selectTool(view,'fixture');ops.renderProject(context,view);return;
  }
  if(action==='add'){
    if(view.pending)return;
    if(view.fiveFixtureDesignOpen)await designCommand(context,view,'close',ops);
    selectTool(view,'fixture');view.fiveFixtureDesignOpen=false;
    return fixtureLibraryCommand(context,view,'choose',ops);
  }
  if(action.startsWith('row.')){
    if(view.pending)return;
    const [kind,id]=action.slice(4).split(':');if(!fixtures(view).some(f=>f.id===id))return;
    state(view).selected=id;action=kind;
  }
  if(action.startsWith('move-mode:')){view.fiveFixtureMoveMode=action.slice(10);ops.renderProject(context,view);return;}
  if(action.startsWith('library.'))return fixtureLibraryCommand(context,view,action.slice(8),ops);
  const showFixtures=async()=>{
    closeFixtureChooser(context,view);
    if(view.fiveFixtureLibrary)view.fiveFixtureLibrary.open=false;
    selectTool(view,'fixture');
    view.fiveAxisRegistrationOpen=false;view.fiveAxisRegistrationPick=null;view.fiveAxisExtractionPick=null;
    view.fiveAxisStartOpen=false;if(!view.fiveStart?.request)view.fiveStart=null;
    if(view.activeAreaId!=='workpiece')await context.actions?.selectRibbonTab?.('workpiece');
  };
  if(action==='generate'){
    if(view.pending)return;
    await showFixtures();if(!view.fiveFixtureDesignOpen){view.fixtureDesignDraft=null;view.fiveFixtureGenerated=null;view.fiveFixtureEditingUnit=null;view.fiveFixtureSession=(view.fiveFixtureSession||0)+1;view.fiveFixtureNewUnit=true;}view.fiveFixtureDesignOpen=true;view.fiveFixtureDesignPage='parameters';view.error='';
    ops.renderProject(context,view);return;
  }
  if(action.startsWith('edit:')){
    if(view.pending)return;const id=action.slice(5),unit=fixtures(view).find(u=>u.id===id);if(unit?.kind!=='Generated'||unit.locked||unit.enabled===false)return;
    await loadFixtureUnitDetails(context,view,unit,{supportFaces:true});if(!current(context,view))return;
    await showFixtures();state(view).selected=id;view.fiveFixtureEditingUnit=id;view.fiveFixtureNewUnit=false;view.fiveFixtureDesignOpen=true;view.fixtureDesignDraft=null;view.fiveFixtureGenerated=null;view.fiveFixtureSession=(view.fiveFixtureSession||0)+1;view.error='';ops.renderProject(context,view);return;
  }
  if(action.startsWith('focus.')) {
    if(view.pending)return;
    await showFixtures();if(action==='focus.design'&&!view.fiveFixtureDesignOpen){view.fixtureDesignDraft=null;view.fiveFixtureGenerated=null;view.fiveFixtureEditingUnit=null;view.fiveFixtureSession=(view.fiveFixtureSession||0)+1;view.fiveFixtureNewUnit=true;}view.fiveFixtureDesignOpen=action==='focus.design';view.fiveFixtureDesignPage='parameters';view.error='';
    if(action==='focus.check'){selectTool(view,'translate');view.fiveSetupCheckOpen=true;}
    ops.renderProject(context,view);
    const key=action==='focus.check'?'fixture-check':'fixture-design';
    context.mount.querySelector(`[data-five-key="${key}"]`)?.scrollIntoView({block:'start'});
    return;
  }
  if(action.startsWith('design.'))return designCommand(context,view,action.slice(7),ops);
  if (action === 'cancel') {
    const task = view.fiveAxisFixtureTask;
    if (task?.status === 'Running') await context.sceneProxy.invoke('CamFixture.CancelTask', { epoch:task.epoch, taskId:task.taskId });
    return;
  }
  if (action.startsWith('select:')) {
    if (!fixtures(view).some(f => f.id === action.slice(7))) return;
    state(view).selected = action.slice(7);view.fiveFixtureDesignOpen=false;await showFixtures();
    ops.renderProject(context,view); return;
  }
  if (view.pending || !current(context,view)) return;
  if (action === 'reset') { fixtureDraft(view,true); ops.renderProject(context,view); return; }
  let sourcePath;
  if (action === 'import') {
    await showFixtures();
    sourcePath = await chooseModelFile(context,view);
    if (!sourcePath || !current(context,view)) return;
  }
  const item = selectedFixture(view), d = fixtureDraft(view), edited = d?.revision;
  const domain = view.scene.domain, request = { commandId:crypto.randomUUID(),expected:domain.expected,setupId:domain.activeSetupId };
  if(action==='check' && Object.values(state(view).drafts).some(d=>d.dirty))throw Error('请先完成或取消夹具字段输入。');
  let method;
  if (action === 'import') { method='Import'; request.sourcePath=sourcePath; }
  else if (action === 'check') {
    method='Check';request.clearanceMm=numeric(state(view).clearance,'检查间隙');
    if(request.clearanceMm<0||request.clearanceMm>1000)throw Error(messages.CAM_FIXTURE_CLEARANCE_INVALID);
  } else {
    if (!item) throw Error('请先选择工装。');
    request.unitId=item.id;method=action==='remove'?'UnitRemove':'UnitUpdate';
    if(action==='apply') { request.name=d.name.trim(); if(!request.name)throw Error('请填写工装名称。');if(item.kind!=='Generated')request.transform=fixtureTransform(d); }
    else if(action==='visible')request.visible=!item.visible;
    else if(action==='collision')request.collisionEnabled=!item.collisionEnabled;
    else if(action==='lock')request.locked=!item.locked;
    else if(action==='enable')request.enabled=item.enabled===false;
    else if(action!=='remove')return;
  }
  view.pending=true;view.error='';view.progress={title:action==='check'?'正在检查装夹间隙…':action==='import'?'正在导入工装…':'正在应用工装设置…'};
  ops.renderProject(context,view);
  try {
    let result=await invoke(context,`CamFixture.${method}`,request);
    if(['import','check'].includes(action))result=await waitTask(context,view,result,ops);
    if(!result||view.disposed)return;
    await saveCommittedProjectEdit(context,view,ops);
    if(action==='import')state(view).selected=result.unitId;
    if(action==='apply'&&d.revision===edited)d.dirty=false;
    if(action==='remove')delete state(view).drafts[item.id];
    if(current(context,view)) {
      await ops.refreshSceneState(context,view);
      if(action==='import'){state(view).selected=result.unitId;await ops.fitViewAfterRenderPublish(context,view,{entityIds:[view.scene.activePlacementId,...fixtureParts(view).map(f=>f.id)]});}
    }
    ops.appendProjectLog(context,'ok',({import:'工装已导入',apply:'工装位置已应用',remove:'工装已移除',check:'装夹间隙检查完成',visible:'工装显示已更新',collision:'工装检查状态已更新'})[action]);
  } catch(error) { if(current(context,view))view.error=errorText(error); }
  finally { view.pending=false;view.fiveAxisFixtureTask=null;view.progress=null;if(current(context,view))ops.renderProject(context,view); }
}
export function fixturePicked(context, view, userData, _hit, _event, _hits, ops) {
  if(designPick(context,view,userData,_hit,_event,_hits,ops))return true;
  if(view.fiveAxisArea!=='workpiece'||['registration','align'].includes(activeTool(view)))return false;
  const id=String(userData?.objectId ?? ''),unit=fixtures(view).find(f=>f.partIds.includes(id));
  if(unit) {
    const editing=unit.kind==='Generated'&&state(view).selected===unit.id&&unit.partIds.includes(view.fiveFixtureSelectedPart);
    state(view).selected=unit.id;view.fiveFixtureSelectedPart=editing?id:'';view.fiveFixtureDesignOpen=false;selectTool(view,'fixture');ops.renderProject(context,view);
    revealSceneWorkpiece(context,view,ops,{unitId:unit.id});
    revealSceneListSelection(context.mount,`fixture-part-${id}`);
    return true;
  }
  return false;
}
export function highlightFixture(view) {
  highlightDesign(view);
  const id=view.fiveAxisArea==='workpiece'&&activeTool(view)==='fixture'&&!view.fiveFixtureDesignOpen?selectedFixture(view)?.id:'';
  const parts=selectedFixture(view)?.partIds||[],part=view.fiveFixtureSelectedPart;
  const focus=parts.includes(part)&&view.scene.domain.fixtures.some(f=>f.id===part&&f.enabled!==false&&f.visible!==false);
  view.viewport?.setEmphasizedObjectIds?.(id?(focus?[part]:parts):[],{visual:{color:focus?0x22c4aa:0x568ac4,emissive:focus?0x145448:0x183048,emissiveIntensity:focus?0.3:0.1},dimOthers:id&&focus?0.14:undefined});
}
