import { currentDraft, activeTool } from './workspaceModel.mjs';
import { selectedPlacement } from './registrationActions.mjs';
import { cutPolicyDraft, processDraftDefaults } from './processPolicyDraft.mjs';
import { workspaceState } from './workspaceModel.mjs';
import { cutCreationTimeoutMs } from './operationTiming.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import {applyCutCreationPatch} from './domainActions.mjs';
import {hydrateCutDefinitions} from './pathSampleResource.mjs';
import {waitForTaskResult,queryTaskReceipt} from './taskProgressStream.mjs';
import {machiningTaskTimeoutMs} from './operationTiming.mjs';
import {focusSelectedFace} from './faceSelectionFocus.mjs';
import {revealFaceListSelection,revealSceneListSelection} from './listNavigation.mjs';
import {invalidateCheckResult} from './checkValidity.mjs';
import {scopedExtractionCandidates,validExtractionScope} from './extractionScope.mjs';

const sourceKey = view => {
  const p=selectedPlacement(view),workpiece=view.scene?.domain?.workpieces?.find(w=>w.id===p?.workpieceId);
  return JSON.stringify([view.scene?.domain?.expected?.epoch,p?.id,view.scene?.model?.geometryRef,workpiece?.previewRef]);
};
const extractionMessages={GEOMETRY_LOOP_OPEN:'所选边尚未闭合，请继续选择连接缺口的边。',GEOMETRY_LOOP_BRANCH:'所选边出现分叉，请移除多选的边。',GEOMETRY_LOOP_DISCONNECTED:'请选择同一个闭环上的边，不要混选多个环。',CAM_REGION_DISCONNECTED:'所选面不相邻，请选择一片连续的加工区域。',CAM_REGION_NO_BOUNDARY:'所选区域没有可提取的边界。',CAM_REGION_OPEN_BOUNDARY:'加工区域的边界未闭合，请补选相邻面。',CAD_REFERENCE_AMBIGUOUS:'此处关联多条边，请在边的中段附近选择。',CAD_REFERENCE_TOO_FAR:'请靠近模型边线选择。'};
const extractionError = error => /CAM_OBSERVATION_DIRECTION_TANGENT/.test(error.message)?'加工观察方向与所选面相切，无法确定加工侧；请调整方向后重新识别。':/CAM_OBSERVATION_DIRECTION_INVALID/.test(error.message)?'加工观察方向必须是有效的非零向量。':extractionMessages[error.message] || (/CAM_AUTOMATIC_SOLID_REQUIRES_FACE_SELECTION/.test(error.message) ? '此实体模型暂需手动选择加工面。' : /BUDGET_EXHAUSTED/.test(error.message) ? '模型超出本次提取范围，请手动选择加工面。' : error.message);
const sameProject = (context, view) => !view.disposed && context.isCurrentProject?.() !== false;
export function extractionDraft(view) {
  const draft = currentDraft(view, 'extract'), key = sourceKey(view);
  if (!draft.extraction || draft.extraction.sourceKey !== key) {
    draft.extraction = { id: crypto.randomUUID(), revision: 1, sourceKey: key, mode: 'Faces', boundaryScope:'all', observation: ['0','0','-1'], faces: [], edges: [], edgePreviews: {}, result: null, task: null, selected: [] };
  }
  return draft;
}
export function extractionChanged(view) {
  const state = extractionDraft(view).extraction;
  state.revision += 1; state.result = null; state.selected = [];
  if(!state.faces.some(f=>f.localId===state.selectedFace))state.selectedFace=null;
}
export function extractionObservationDirection(view){
  const values=extractionDraft(view).extraction.observation??['0','0','-1'];
  if(values.some(v=>!String(v).trim()||!Number.isFinite(Number(v))))throw Error('加工观察方向需要填写有效的 X、Y、Z 分量。');
  const raw=values.map(Number),scale=Math.max(...raw.map(Math.abs));if(!scale)throw Error('加工观察方向不能为零向量。');
  const scaled=raw.map(v=>v/scale),length=Math.hypot(...scaled);return scaled.map(v=>v/length);
}
export function extractionDirectionChanged(view,index,value){const state=extractionDraft(view).extraction;if(view.pending||state.confirmationRequest)return;(state.observation??=['0','0','-1'])[index]=value;extractionChanged(view);}
export function specification(view) {
  const draft = extractionDraft(view), placement = selectedPlacement(view);
  if (!placement || (draft.extraction.mode !== 'Automatic' && !(draft.extraction.mode==='Edges'?draft.extraction.edges.length:draft.extraction.faces.length))) throw Error('请在模型上选择需要加工的面或边。');
  const profile = view.scene.domain.setups.find(item => item.id === placement.setupId)?.planningProfileRef;
  if (!profile?.revision) throw Error('当前装夹没有有效的识别配置。');
  const value = { placementId: placement.id, setupId: placement.setupId, sourceGeometry: view.scene.model.geometryRef,
    extractionRuleRef: profile, geometryOnly: true,
    selection: draft.extraction.mode === 'Automatic' ? { mode: 'Automatic' } : draft.extraction.mode==='Edges'?{mode:'Edges',edgeUses:draft.extraction.edges}:{ mode: draft.extraction.mode, faceUses: draft.extraction.faces },
    representation: 'SkinSurface', workpieceFamily: 'SheetSurface', side: { outwardSign: 1,...(draft.extraction.mode==='Faces'?{observationDirection:extractionObservationDirection(view)}:{}) },
    requestedKinds: ['GeneralContour','Circle','Ellipse','Rectangle','Slot'] };
  return value;
}
const snapshotKey = view => JSON.stringify([view.scene?.domain?.expected, specification(view), extractionDraft(view).extraction.revision]);
const matchesSnapshot = (view, snapshot) => { try { return snapshot === snapshotKey(view); } catch { return false; } };
const confirmationKey = view => JSON.stringify([snapshotKey(view), extractionDraft(view).extraction.selected,extractionDraft(view).extraction.boundaryScope]);
export function extractionResult(view) {
  const state=extractionDraft(view).extraction,result=state.result;
  if (!result) return null;
  try {return result.snapshot===snapshotKey(view)?state.mode==='Faces'?{...result,candidates:scopedExtractionCandidates(result.candidates,state.boundaryScope)}:result:null;}catch{return null;}
}
async function automaticFaces(context,view,state,ops){
  const placement=selectedPlacement(view);if(!placement)throw Error('请先导入工件。');
  const direction=extractionObservationDirection(view);
  const request=state.faceRequest={cancelled:false},revision=state.revision,expected=structuredClone(view.scene.domain.expected),proxy=context.sceneProxy;
  const current=()=>sameProject(context,view)&&context.sceneProxy===proxy&&!request.cancelled&&state.faceRequest===request&&view.fiveAxisExtractionOpen
    &&extractionDraft(view).extraction===state&&state.revision===revision&&JSON.stringify(view.scene.domain.expected)===JSON.stringify(expected);
  view.pending=true;view.error='';view.progress={title:'正在批量拾取非加工面…',detail:'按加工观察方向选择表面，完成后可手动修改。'};ops.renderProject(context,view);
  try{
    let task=await proxy.invoke('CamCut.FindRetainedFaces',{commandId:crypto.randomUUID(),expected,placementId:placement.id,setupId:placement.setupId,observationDirection:direction},{timeoutMs:machiningTaskTimeoutMs});
    request.task=task;
    while(task.status==='Running'&&current()){
      await new Promise(r=>setTimeout(r,100));
      if(current())task=await queryTaskReceipt(context,task,{timeoutMs:machiningTaskTimeoutMs});request.task=task;
    }
    if(!current()){if(task.status==='Running')await proxy.invoke('CamPlan.CancelTask',{epoch:task.epoch,taskId:task.taskId});return;}
    if(task.status==='Cancelled')return;
    if(task.status!=='Completed')throw Error(task.error||'工件已变化，请重新批量拾取。');
    // Completion carries only a receipt. Selection, topology references and
    // viewport triangle mappings each come from their own query.
    const scope={expected,placementId:placement.id,setupId:placement.setupId};
    const [selection,sources,mapping]=await Promise.all([
      proxy.invoke('CamCut.ReadRetainedSelection',{epoch:task.epoch,taskId:task.taskId}),
      proxy.invoke('CamCut.QuerySources',scope),
      proxy.invoke('CamPlacement.ReadReferenceFaces',scope),
    ]);
    if(!current())return;
    const ids=new Set(selection.faceIds.map(String));
    if([...ids].some(id=>sources.faceUses.filter(f=>String(f.localId)===id).length!==1))throw Error('部分表面存在多重拓扑引用，请改用手动选择。');
    const added=sources.faceUses.filter(f=>ids.has(String(f.localId)));
    if(!added.length)throw Error('当前观察方向未找到可选表面，请调整方向或手动选择。');
    const selected=new Map(state.faces.map(f=>[f.localId,f]));for(const face of added)selected.set(face.localId,face);
    state.faces=[...selected.values()];state.faceRanges??={};
    for(const face of added)state.faceRanges[face.localId]=mapping.faces.filter(f=>String(f.faceId)===String(face.localId)).map(({first,count})=>({first,count}));
    state.selectedFace??=added[0].localId;view.fiveAxisExtractionPick=null;extractionChanged(view);
    ops.appendProjectLog(context,'ok',`已批量拾取 ${added.length} 个非加工面，可手动补选或移除`);
  }catch(error){if(current())view.error=extractionError(error);}
  finally{if(state.faceRequest===request){state.faceRequest=null;view.pending=false;view.progress=null;if(sameProject(context,view))ops.renderProject(context,view);}}
}
export async function extractionCommand(context, view, action, argument, ops) {
  const draft = extractionDraft(view), state = draft.extraction;
  if(action==='open'){
    await extractionCommand(context,view,'pick',null,ops);
    return;
  }

  if (action === 'close') {
    if (state.confirming || state.confirmationRequest) return;
    await extractionCommand(context,view,'cancel',null,ops);
    extractionChanged(view); state.faces=[];state.edges=[];state.faceRanges={};state.edgePreviews={}; view.fiveAxisExtractionOpen=false; view.fiveAxisExtractionPick=null; view.error=''; return;
  }
  if(action==='mode'){
    if(view.pending||state.confirmationRequest||!['Faces','Edges','Region'].includes(argument))return;
    state.mode=argument;state.faces=[];state.edges=[];state.faceRanges={};state.edgePreviews={};extractionChanged(view);
    view.error='';view.fiveAxisRegistrationPick=null;view.fiveAxisExtractionPick=state;return;
  }
  if(action==='scope'){
    if(view.pending||state.confirmationRequest||!validExtractionScope(argument)||state.boundaryScope===argument)return;
    state.boundaryScope=argument;
    const result=extractionResult(view),existing=new Set((view.scene.intentToolpaths||[]).map(c=>c.sourceKey));
    state.selected=(result?.candidates||[]).filter(c=>c.confirmationEligibility==='Ready'&&!existing.has(c.sourceKey)).map(c=>c.candidateId);
    view.error='';return;
  }
  if(action==='filter'){
    if(view.pending||!['all','outer','inner'].includes(argument))return;
    const result=extractionResult(view);if(!result)return;
    const existing=new Set((view.scene.intentToolpaths||[]).map(c=>c.sourceKey));
    state.selected=result.candidates.filter(c=>c.confirmationEligibility==='Ready'&&!existing.has(c.sourceKey)
      &&(argument==='all'||c.boundary.regionRelation===(argument==='outer'?'OuterLoop':'InnerLoop'))).map(c=>c.candidateId);return;
  }
  if (action === 'confirm') {
    if(view.pending)return;
    if(state.mode==='Faces'&&state.boundaryScope==='none'){view.error='请至少勾选内孔或外轮廓。';return;}
    if(!state.confirmationRequest&&!extractionResult(view)){
      await extractionCommand(context,view,'run',null,ops);
      if(state.mode==='Edges'||state.mode==='Region')return;
    }
    if(state.confirmationRequest||(extractionResult(view)&&state.selected.length))await extractionCommand(context,view,'create',null,ops);
    return;
  }
  if (action === 'automatic') { if(view.pending||state.confirmationRequest)return; state.mode='Automatic';state.faces=[];extractionChanged(view);action='run'; }
  if (action === 'create') {
    if (view.pending) return;
    const result = extractionResult(view), placement = selectedPlacement(view);
    if (!state.confirmationRequest && (!result || !state.selected.length)) throw Error('请先提取并选中有效候选。');
    if(!state.confirmationRequest&&state.selected.some(id=>!result.candidates.some(c=>c.candidateId===id&&c.confirmationEligibility==='Ready')))throw Error('识别范围已变化，请重新选择轮廓。');
    const policy = state.confirmationRequest ? null : cutPolicyDraft(processDraftDefaults);
    const key = state.confirmationRequest ? null : confirmationKey(view);
    state.confirming = true; view.pending = true; view.error = ''; ops.renderProject(context, view);
    try {
      if (!state.confirmationRequest) {
        const prepared = await context.sceneProxy.invoke('CamProcess.Prepare', { commandId: crypto.randomUUID(),
          expected: view.scene.domain.expected, setupId: placement.setupId, placementId: placement.id, ...policy });
        if (!sameProject(context, view) || key !== confirmationKey(view)) return;
        state.confirmationRequest = { commandId: crypto.randomUUID(), expected: view.scene.domain.expected,
          setupId: placement.setupId, placementId: placement.id, candidateSet: result.candidateSet,
          candidateIds: [...state.selected], draftDigest: result.digest, decisions: [], process: prepared.process, pose: prepared.pose };
      }
      let created;
      try { created = await context.sceneProxy.invoke('CamCut.Create', state.confirmationRequest, { timeoutMs: cutCreationTimeoutMs }); }
      catch (error) {
        let receipt;
        try { receipt = await context.sceneProxy.invoke('CamScene.QueryCommand', { epoch: state.confirmationRequest.expected.epoch, commandId: state.confirmationRequest.commandId }, { timeoutMs: cutCreationTimeoutMs }); }
        catch { throw Error('创建结果尚未确认，请点击“确定”核对，原请求会保留以防重复创建。'); }
        if (receipt.status === 'Completed') created = receipt.result;
        else {
          // A transport timeout does not prove that the mutation failed. Retry
          // the identical command so the native receipt prevents duplicates.
          const rejectedByServer = error.frame?.kind === 2 && Number(error.frame.status) !== 0;
          if (!rejectedByServer) throw Error('创建结果尚未确认，请点击“确定”核对，原请求会保留以防重复创建。');
          if (receipt.status === 'NotFound') state.confirmationRequest = null;
          throw error;
        }
      }
      state.confirmationRequest = null; state.result = null; state.selected = []; view.fiveAxisExtractionOpen=false;view.fiveAxisExtractionPick=null;
      await saveCommittedCutEdit(context,view,ops);
      if(!sameProject(context,view))return;
      const initial=await hydrateCutDefinitions(created.definitionProjection,context.sceneProxy);
      if(!sameProject(context,view))return;
      applyCutCreationPatch(view,created.statePatch,initial.items);
      view.scene.domain.intentGenerationTasks=[];view.scene.domain.intentGenerationErrors=[];
      // Existing machining certificates no longer cover the newly created cuts.
      const setup=view.scene.domain.setups.find(s=>s.id===created.setupId);
      setup.generatedGeometry={...setup.generatedGeometry,status:setup.generatedGeometry?.operations?.length?'Stale':'Missing',operations:[],transitions:[],rapidGeneratedCount:'0',rapidClearCount:'0'};
      invalidateCheckResult(setup.simulationCheck);setup.machineMotionPlanRef={};
      ops.renderProject(context,view);
      const proxy=context.sceneProxy,expected=JSON.stringify(created.statePatch.expected);
      const active=()=>sameProject(context,view)&&context.sceneProxy===proxy&&JSON.stringify(view.scene.domain.expected)===expected;
      const task=await waitForTaskResult(context,created.definitionTask,{active,queryMethod:'CamWorkpiece.QueryTask',cancelMethod:'CamWorkpiece.CancelTask',timeoutMs:machiningTaskTimeoutMs});
      if(!active())return;
      if(task?.status!=='Completed')throw Error(task?.error||'切割定义已创建，刀路准备未完成。');
      for(let offset=0;offset<created.createdIntentIds.length;offset+=256){
        const projection=await hydrateCutDefinitions(await proxy.invoke('CamCut.Read',{expected:created.statePatch.expected,intentIds:created.createdIntentIds.slice(offset,offset+256),projection:true},{timeoutMs:cutCreationTimeoutMs}),proxy);
        if(!active())return;
        applyCutCreationPatch(view,projection.statePatch,projection.items);
      }
      workspaceState(view).selectedIntentId = created.createdIntentIds[0] || '';
      workspaceState(view).selectedIntentIds=created.createdIntentIds.slice(0,1);
      ops.appendProjectLog(context, 'ok', `已添加 ${created.createdIntentIds.length} 条切割刀路`);
    } catch (error) {
      if (sameProject(context, view)) { view.error = extractionError(error); ops.appendProjectLog(context, 'error', view.error); }
    } finally {
      state.confirming = false; view.pending = false; if (sameProject(context, view)) ops.renderProject(context, view);
    }
    return;
  }
  if (state.confirmationRequest) throw Error('请先重试上次确认，以核对是否已创建切割定义。');
  if(view.pending&&['pick','clear','remove','remove-edge','select-face','focus-face','auto','through'].includes(action))return;
  if(action==='auto'){if(state.mode==='Faces')await automaticFaces(context,view,state,ops);return;}
  if(action==='through'){state.throughSelection=!state.throughSelection;return;}
  if(action==='select-face'){if(state.faces.some(f=>f.localId===argument))state.selectedFace=argument;return;}
  if(action==='focus-face'){
    if(!state.faces.some(f=>f.localId===argument))return;
    const source=view.viewport?.sceneObjects.get(selectedPlacement(view)?.id);
    if(focusSelectedFace(view,source,state.faceRanges?.[argument]))state.selectedFace=argument;
    return;
  }
  if (action === 'pick') {
    if (!selectedPlacement(view)) {view.error='请先导入工件。';return;}
    if(state.mode==='Automatic'){state.mode='Faces';extractionChanged(view);}
    view.fiveAxisRegistrationPick = null; view.fiveAxisExtractionPick = view.fiveAxisExtractionPick===state?null:state; return;
  }
  if (action === 'stop') { view.fiveAxisExtractionPick = null; return; }
  if (action === 'clear') { state.faces = [];state.edges=[];state.edgePreviews={};state.faceRanges={}; extractionChanged(view); return; }
  if(action==='remove-edge'){state.edges=state.edges.filter(e=>e.localId!==argument);delete state.edgePreviews[argument];extractionChanged(view);return;}
  if (action === 'remove') { state.faces = state.faces.filter(item => item.localId !== argument);delete state.faceRanges?.[argument];extractionChanged(view);return; }
  if (action === 'select') {
    if(view.pending)return;
    const result = extractionResult(view), candidate = result?.candidates.find(item => item.candidateId === argument);
    if (candidate?.confirmationEligibility !== 'Ready') return;
    state.selected = state.selected.includes(argument) ? state.selected.filter(id => id !== argument) : [...state.selected, argument]; return;
  }
  if (action === 'cancel') {
    if(state.faceRequest){const request=state.faceRequest;request.cancelled=true;state.faceRequest=null;view.pending=false;view.progress=null;
      if(request.task?.status==='Running')await context.sceneProxy.invoke('CamPlan.CancelTask',{epoch:request.task.epoch,taskId:request.task.taskId});}
    if (state.task?.status === 'Running') await context.sceneProxy.invoke('CamPlan.CancelTask', { epoch: state.task.epoch, taskId: state.task.taskId });
    return;
  }
  if (action !== 'run' || view.pending) return;
  const body = specification(view); state.revision += 1;
  const snapshot = snapshotKey(view), revision = state.revision;
  const bytes = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(JSON.stringify(body)));
  const digest = [...new Uint8Array(bytes)].map(n => n.toString(16).padStart(2, '0')).join('');
  if (!matchesSnapshot(view, snapshot)) return;
  state.result = null; state.selected = []; view.fiveAxisExtractionPick = null; view.pending = true; view.error = '';
  ops.renderProject(context, view);
  try {
    let task = await context.sceneProxy.invoke('CamCut.ExtractCandidates', { ...body, commandId: crypto.randomUUID(),
      expected: view.scene.domain.expected, draftId: state.id, draftRevision: String(revision), draftDigest: digest });
    state.task = task;
    while (task.status === 'Running' && !view.disposed) {
      await new Promise(resolve => setTimeout(resolve, 75));
      if (view.disposed) return;
      task = await context.sceneProxy.invoke('CamPlan.QueryStatus', { epoch: task.epoch, taskId: task.taskId }); state.task = task;
    }
    if (!sameProject(context, view) || sourceKey(view) !== state.sourceKey || state.revision !== revision || !matchesSnapshot(view, snapshot)) return;
    if (task.status === 'Cancelled') { ops.appendProjectLog(context, 'info', '已取消切割候选提取'); return; }
    if (task.status !== 'Completed') throw Error(task.status === 'Superseded' ? '工程已变化，请重新提取候选。' : task.error || '提取失败');
    const candidates = []; let pageToken;
    do {
      const page = await context.sceneProxy.invoke('CamCut.QueryCandidates', { epoch: task.epoch, candidateSet: task.result.candidateSet,
        pageSize: '100', ...(pageToken ? { pageToken } : {}) });
      candidates.push(...page.candidates); pageToken = page.nextPageToken;
    } while (pageToken && !view.disposed);
    if (!sameProject(context, view) || state.revision !== revision || !matchesSnapshot(view, snapshot)) return;
    const existing=new Set((view.scene.intentToolpaths||[]).filter(c=>c.placementId===selectedPlacement(view)?.id).map(c=>c.sourceKey));
    state.result = { snapshot, candidateSet: task.result.candidateSet, epoch: task.epoch, digest, candidates };
    const scoped=extractionResult(view)?.candidates||[];
    state.selected=scoped.filter(c=>c.confirmationEligibility==='Ready'&&!existing.has(c.sourceKey)).map(c=>c.candidateId);
    if(!state.selected.length){
      const unresolved=scoped.filter(c=>!existing.has(c.sourceKey)&&c.confirmationEligibility!=='Ready').length;
      view.error=!candidates.length?'未找到闭合边界，请手动选择加工面。':!scoped.length?(candidates.some(c=>c.confirmationEligibility!=='Ready')?'边界已提取，但尚不能判定内孔或外轮廓，请检查非加工面选择。':'所选识别范围没有匹配的边界，请调整识别范围或选面。'):unresolved?`${unresolved} 条新边界尚不能判定切割或打标，请调整所选加工面。`:'所选面的刀路已存在，无需重复添加。';
    }
    ops.appendProjectLog(context, 'ok', `已提取 ${candidates.length} 项边界候选`);
  } catch (error) {
    if (sameProject(context, view)) { view.error = extractionError(error); ops.appendProjectLog(context, 'error', view.error); }
  } finally {
    view.pending = false; state.task = null;
    if (sameProject(context, view)) ops.renderProject(context, view);
  }
}
export function extractionPickRequest(view,userData,hit){
  const state=view.fiveAxisExtractionPick,placement=selectedPlacement(view);
  if(!state||!placement||String(userData?.objectId)!==placement.id||state.sourceKey!==sourceKey(view)||!hit?.point||!hit.object||!Number.isInteger(hit.faceIndex))return null;
  const p=hit.object.worldToLocal(hit.point.clone());
  return {expected:structuredClone(view.scene.domain.expected),setupId:placement.setupId,placementId:placement.id,sourceGeometry:view.scene.model.geometryRef,
    query:{mode:'point',frame:'Workpiece',triangleIndex:String(hit.faceIndex),nearPoint:[p.x,p.y,p.z],maximumDistanceMm:2}};
}
export function handleExtractionPick(context, view, userData, hit, _event, _hits, ops) {
  const state = view.fiveAxisExtractionPick||(view.fiveAxisExtractionOpen&&['extract','manual-extract'].includes(activeTool(view))?extractionDraft(view).extraction:null);
  if (!state) return false;
  if(!view.fiveAxisExtractionPick){
    if(view.pending||String(userData?.objectId)!==selectedPlacement(view)?.id)return true;
    const face=state.faces.find(f=>state.faceRanges?.[f.localId]?.some(r=>hit?.faceIndex>=+r.first&&hit.faceIndex<+r.first+ +r.count));
    if(face){state.selectedFace=face.localId;ops.renderProject(context,view);revealFaceListSelection(context.mount,'extraction-faces',face.localId);}return true;
  }
  if (view.pending || !hit?.point || !hit.object || !Number.isInteger(hit.faceIndex)) return true;
  const placement = selectedPlacement(view);
  if (!placement || String(userData?.objectId) !== placement.id || state.sourceKey !== sourceKey(view)) {
    view.error = '请在当前加工工件上选择加工面。'; ops.renderProject(context, view); return true;
  }
  const request=extractionPickRequest(view,userData,hit),revision=state.revision;
  if(!request)return true;
  let revealFaceId=null,revealEdgeId=null;
  view.pending = true; view.error = '';
  void context.sceneProxy.invoke(state.mode==='Edges'?'CamCut.ResolveEdge':'CamCut.ResolveFace',request).then(result => {
    if (!sameProject(context, view) || view.fiveAxisExtractionPick !== state || state.revision !== revision || sourceKey(view) !== state.sourceKey) return;
    if(state.mode==='Edges'){
      const id=result.edgeUse.localId,index=state.edges.findIndex(e=>e.localId===id);
      if(index<0){state.edges.push(result.edgeUse);state.edgePreviews[id]=result.previewPoints;revealEdgeId=id;}
      else {state.edges.splice(index,1);delete state.edgePreviews[id];}
      extractionChanged(view);return;
    }
    const index = state.faces.findIndex(face => face.localId === result.faceUse.localId);
    state.faceRanges??={};
    if (index < 0) {state.faces.push(result.faceUse);state.selectedFace=result.faceUse.localId;revealFaceId=result.faceUse.localId;state.faceRanges[result.faceUse.localId]=result.triangleRanges;}
    else {state.faces.splice(index, 1);delete state.faceRanges[result.faceUse.localId];}
    extractionChanged(view);
  }).catch(error => { if (sameProject(context, view)) view.error = extractionError(error); }).finally(() => {
    view.pending = false; if (sameProject(context, view)) {
      ops.renderProject(context, view);
      if(view.fiveAxisExtractionPick===state&&state.selectedFace===revealFaceId)revealFaceListSelection(context.mount,'extraction-faces',revealFaceId);
      if(view.fiveAxisExtractionPick===state&&revealEdgeId!=null&&state.edges.some(e=>e.localId===revealEdgeId))revealSceneListSelection(context.mount,`Edges-${revealEdgeId}`);
    }
  });
  return true;
}
