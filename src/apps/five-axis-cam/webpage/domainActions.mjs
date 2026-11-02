import {applyOrderReceipt} from './orderState.mjs';
import { chooseModelFile } from './workpieceFilePicker.mjs';
import {saveCommittedProjectEdit,saveEditedProject} from './projectEditPersistence.mjs';
import { machiningTaskTimeoutMs } from './operationTiming.mjs';
import { hydratePathSamples } from './pathSampleResource.mjs';
import { waitForTaskResult } from './taskProgressStream.mjs';
import {invalidateCheckResult} from './checkValidity.mjs';
import {retainUnaffectedCollisions} from './collisionPathResults.mjs';
export async function readMachiningState(context,initialState,{waitForGeneration=true}={}){
  const proxy=context.sceneProxy,active=()=>context.sceneProxy===proxy&&context.isCurrentProject?.()!==false;
  let state=initialState??await proxy.invoke('CamScene.ReadState',{}, {timeoutMs:machiningTaskTimeoutMs});
  while(waitForGeneration&&active()&&state.intentGenerationTasks?.some(task=>task.status==='Running')){
    await Promise.all(state.intentGenerationTasks.filter(task=>task.status==='Running').map(task=>waitForTaskResult(context,task,{active,queryMethod:'CamWorkpiece.QueryTask',cancelMethod:'CamWorkpiece.CancelTask',timeoutMs:machiningTaskTimeoutMs})));
    if(!active())return state;
    state=await proxy.invoke('CamScene.ReadState',{}, {timeoutMs:machiningTaskTimeoutMs});
  }
  return hydratePathSamples(state,proxy);
}
// Publish names, numbers and available geometry first. Reading the sidebar
// must not hold the whole workbench until background exact-path evaluation ends.
export function syncIntentGeneration(context,view,ops){
  const tasks=view.scene?.domain?.intentGenerationTasks?.filter(t=>t.status==='Running')||[];
  if(!tasks.length||view.pending||view.disposed||context.isCurrentProject?.()===false)return;
  const key=JSON.stringify(tasks.map(t=>[t.epoch,t.taskId])),s=view.fiveIntentPreparation??={};
  if(s.running||s.key===key)return;s.key=key;s.running=true;
  const proxy=context.sceneProxy,expected=JSON.stringify(view.scene.domain.expected),active=()=>!view.disposed&&context.isCurrentProject?.()!==false&&context.sceneProxy===proxy;
  s.promise=(async()=>{
    try{
      await Promise.all(tasks.map(task=>waitForTaskResult(context,task,{active,queryMethod:'CamWorkpiece.QueryTask',cancelMethod:'CamWorkpiece.CancelTask',timeoutMs:machiningTaskTimeoutMs})));
      if(!active())return;
      const state=await readMachiningState(context,undefined,{waitForGeneration:false});
      if(!active()||JSON.stringify(view.scene.domain.expected)!==expected)return;
      mergeDomainState(view.scene,state);ops.renderProject(context,view);await saveEditedProject(context);
    }catch(error){if(active())ops.appendProjectLog?.(context,'error',error.message);}
    finally{s.running=false;s.promise=null;if(active())ops.renderProject(context,view);}
  })();
}
// Product projections only. The native Scene owns all engineering facts.
export function projectWorkpiece(scene, placementId, enabledOnly=false) {
  const domain=scene.domain, placements=(domain?.placements||[]).filter(p=>p.setupId===domain.activeSetupId&&(!enabledOnly||domain.workpieces.some(w=>w.id===p.workpieceId&&w.enabled!==false)));
  const placement=placements.find(p=>p.id===domain.activePlacementId)||placements.find(p=>p.id===placementId)||placements[0];
  scene.activePlacementId=placement?.id||'';
  scene.intentToolpaths=placement?(domain?.intentToolpaths||[]).filter(c=>c.setupId===domain.activeSetupId):[];
  const item=domain?.workpieces?.find(w=>w.id===placement?.workpieceId);
  scene.model = item?.id ? { ...item, entityId: item.id,
    geometryRevision: item.geometryRef?.version, brepResourceId: item.geometryRef?.url,
    previewResourceUrl: item.previewRef?.url, previewResourceVersion: item.previewRef?.version,
    isLoaded: Boolean(item.geometryRef?.url) } : { isLoaded: false };
  return scene;
}
export function mergeDomainState(scene, payload) {
  if(payload?.definitions)scene.machineDefinitions=payload.definitions;
  if(payload?.machines)scene.machines=payload.machines;
  if(payload?.machineElement)scene.machineElement=payload.machineElement;
  if (!payload?.expected || !Array.isArray(payload.placements) || !Array.isArray(payload.workpieces)) return scene;
  scene.domain = payload;
  projectWorkpiece(scene,scene.activePlacementId);
  scene.job = { entityId: payload.jobId, setupId: payload.activeSetupId };
  scene.machines = payload.machines ?? scene.machines ?? []; scene.toolpaths = payload.toolpaths ?? [];
  scene.selection ??= {};
  scene.readiness = { workpieceReady: !!scene.model.isLoaded };
  return scene;
}
// Machining metadata does not change rendered workpiece/fixture resources.
function fixtureProjectionTargets(domain,setup,projection){
  const units=(domain.fixtureUnits||[]).filter(u=>u.setupId===setup.id);
  if(!projection||!projection.setup||!projection.units||Object.keys(projection.units).length!==units.length)return null;
  const targets=[[setup.fixtureDesign,projection.setup],...units.map(u=>[u.design,projection.units[u.id]])];
  for(const [design,state] of targets){
    if(!state)return null;
    for(const name of ['candidate','assembly']){
      const actual=design?.[name],next=state[name];
      if(!!actual!==!!next||actual&&(actual.id!==next.id||!['Current','Stale'].includes(next.validity)||!!actual.cutClearance!==!!next.cutClearance||!!actual.openingPreclearance!==!!next.openingPreclearance))return null;
    }
  }
  return targets;
}
function mergeFixtureProjection(targets){
  for(const [design,state] of targets)for(const name of ['candidate','assembly'])if(state[name]){
    const actual=design[name],next=state[name];actual.validity=next.validity;
    if(next.geometryValidity)actual.geometryValidity=next.geometryValidity;
    if(next.cutClearance)Object.assign(actual.cutClearance,next.cutClearance);
    if(next.openingPreclearance)Object.assign(actual.openingPreclearance,next.openingPreclearance);
  }
}
// Creation owns a scoped receipt; keep the workpiece meshes, machine list and
// viewport reader intact while installing only new definition rows.
export function applyCutCreationPatch(view,patch,items=[]){
  const domain=view.scene.domain,setup=domain.setups.find(s=>s.id===patch.setup.id);
  if(!setup||domain.expected.epoch!==patch.expected.epoch)throw Error('工程已变化，未应用旧工程的刀路结果。');
  const targets=fixtureProjectionTargets(domain,setup,patch.fixtureValidity);
  if(targets)mergeFixtureProjection(targets);
  const rows=new Map(items.map(c=>[c.id,c])),metadata=new Map(patch.intentToolpaths.map(c=>[c.id,c]));
  for(const cut of domain.intentToolpaths){if(rows.has(cut.id))Object.assign(cut,rows.get(cut.id));if(metadata.has(cut.id))Object.assign(cut,metadata.get(cut.id));rows.delete(cut.id);}
  domain.intentToolpaths.push(...rows.values());
  const generatedGeometry={...setup.generatedGeometry,...patch.setup.generatedGeometry};
  Object.assign(setup,patch.setup,{generatedGeometry});
  Object.assign(domain,{expected:patch.expected,inputRevision:patch.inputRevision,layers:patch.layers});
  projectWorkpiece(view.scene,view.scene.activePlacementId);
}
export async function refreshMachiningState(context,view,ops,request,result){
  if(view.disposed||context.isCurrentProject?.()===false)return false;
  if(result?.orderOnly){
    const projection=await context.sceneProxy.invoke('CamPlan.QueryOrder',{setupId:request.setupId,expected:result.expected});
    if(view.disposed||context.isCurrentProject?.()===false||JSON.stringify(view.scene.domain.expected)!==JSON.stringify(request.expected))return false;
    applyOrderReceipt(view,request,{...projection,changed:result.changed});return true;
  }
  const domain=view.scene?.domain,patch=result?.statePatch,setup=domain?.setups.find(s=>s.id===patch?.setup?.id);
  if(patch?.deferredPose&&(!setup||JSON.stringify(domain.expected)!==JSON.stringify(request?.expected)))return false;
  if(setup&&patch.deferredPose&&JSON.stringify(domain.expected)===JSON.stringify(request?.expected)){
    const affected=new Set(patch.invalidatedIntentIds||[]),geometry=setup.generatedGeometry;
    if(affected.size){
      if(geometry){
        geometry.status=geometry.operations?.length?'Stale':'Missing';
        geometry.operations=geometry.operations?.map(op=>affected.has(op.intentId)?{...op,status:'Stale'}:op);
        geometry.transitions=geometry.transitions?.map(t=>affected.has(t.from)||affected.has(t.to)?{...t,endpointStatus:'Stale',geometryStatus:'Stale',validation:'Unchecked',reason:'CAM_RAPID_STALE'}:t);
        geometry.rapidGeneratedCount=String(geometry.transitions?.filter(t=>t.geometryStatus==='Generated').length||0);
        geometry.rapidClearCount=String(geometry.transitions?.filter(t=>t.validation==='Clear').length||0);
      }
      for(const cut of domain.intentToolpaths)if(affected.has(cut.id)){cut.poseDeferred=true;cut.poseEditRevision=request.commandId;}
      if(!retainUnaffectedCollisions(setup,[...affected]))invalidateCheckResult(setup.simulationCheck);
      for(const design of [setup.fixtureDesign,...(domain.fixtureUnits||[]).filter(u=>u.setupId===setup.id).map(u=>u.design)]){
        for(const assembly of [design?.candidate,design?.assembly].filter(Boolean)){
          assembly.validity='Stale';
          for(const proof of [assembly.cutClearance,assembly.openingPreclearance])invalidateCheckResult(proof);
        }
      }
    }
    Object.assign(setup,patch.setup);Object.assign(domain,{expected:patch.expected,inputRevision:patch.inputRevision});
    projectWorkpiece(view.scene,view.scene.activePlacementId);
    // The caller installs its selected-point receipt before rendering. Reading
    // the whole scene here would turn a parameter save into a calculation.
    return true;
  }
  // Apply only the receipt for the exact input snapshot used by this command.
  // Unknown snapshots and structural edits use the normal complete Scene read.
  const reordered=['Up','Down','Group','Ungroup','Move'].includes(request?.action);
  if(setup&&!reordered&&JSON.stringify(domain.expected)===JSON.stringify(request?.expected)){
    const designs=[setup.fixtureDesign,...(domain.fixtureUnits||[]).filter(u=>u.setupId===setup.id).map(u=>u.design)];
    const assemblies=designs.flatMap(d=>[d?.candidate,d?.assembly]).filter(Boolean);
    const fixtureTargets=fixtureProjectionTargets(domain,setup,patch.fixtureValidity);
    const removed=new Set(patch.removedIntentIds||[]),allExisting=domain.intentToolpaths.filter(c=>c.setupId===setup.id);
    const cuts=new Map(patch.intentToolpaths.map(c=>[c.id,c])),existing=allExisting.filter(c=>!removed.has(c.id));
    // A deletion receipt must account for every old row, and may never add a
    // new path or remove a row belonging to another setup.
    const sameCuts=existing.length===cuts.size&&existing.every(c=>cuts.has(c.id))&&[...removed].every(id=>allExisting.some(c=>c.id===id)&&!cuts.has(id));
    const geometry=setup.generatedGeometry,connectionStates=patch.transitionStates;
    const sameConnections=!connectionStates||(geometry?.transitions?.length||0)===Object.keys(connectionStates).length&&(geometry?.transitions||[]).every(t=>Object.hasOwn(connectionStates,`${t.from}/${t.to}`))&&JSON.stringify(geometry?.operations?.map(o=>o.intentId)||[])===JSON.stringify(patch.operationOrder);
    if(sameCuts&&sameConnections){
      const generatedGeometry={...setup.generatedGeometry,...patch.setup.generatedGeometry};
      if(patch.transitionState)generatedGeometry.transitions=generatedGeometry.transitions?.map(t=>({...t,...patch.transitionState}));
      if(connectionStates)generatedGeometry.transitions=generatedGeometry.transitions?.map(t=>({...t,...connectionStates[`${t.from}/${t.to}`]}));
      if(patch.operationStates)generatedGeometry.operations=generatedGeometry.operations?.map(op=>({...op,status:patch.operationStates[op.intentId]}));
      if(patch.invalidatedIntentIds&&!connectionStates){
        const affected=new Set(patch.invalidatedIntentIds);
        generatedGeometry.transitions=generatedGeometry.transitions?.map(t=>affected.has(t.from)||affected.has(t.to)?{...t,endpointStatus:'Stale',geometryStatus:'Stale',validation:'Unchecked',reason:'CAM_RAPID_STALE'}:t);
        generatedGeometry.rapidGeneratedCount=String(generatedGeometry.transitions?.filter(t=>t.geometryStatus==='Generated').length||0);
        generatedGeometry.rapidClearCount=String(generatedGeometry.transitions?.filter(t=>t.validation==='Clear').length||0);
      }
      Object.assign(setup,patch.setup,{generatedGeometry});
      for(const cut of existing)Object.assign(cut,cuts.get(cut.id));
      if(removed.size)domain.intentToolpaths=domain.intentToolpaths.filter(c=>!removed.has(c.id));
      Object.assign(domain,{expected:patch.expected,inputRevision:patch.inputRevision});
      if(fixtureTargets)mergeFixtureProjection(fixtureTargets);
      mergeDomainState(view.scene,domain);
      if(fixtureTargets||!assemblies.length){ops.renderProject(context,view);return true;}
    }
  }
  // Unknown snapshots and structural projections require a complete native
  // read. Drop cached certificates before waiting, including when no compact
  // revision was safe to apply. A failed read must leave delivery disabled.
  // Known receipts above retain their acknowledged revision on this path.
  const pendingSetups=setup?[setup]:(domain?.setups||[]),pendingIDs=new Set(pendingSetups.map(s=>s.id));
  const pendingDesigns=[...pendingSetups.map(s=>s.fixtureDesign),...(domain?.fixtureUnits||[]).filter(u=>pendingIDs.has(u.setupId)).map(u=>u.design)];
  for(const design of pendingDesigns)for(const assembly of [design?.candidate,design?.assembly].filter(Boolean)){
    assembly.validity='Stale';invalidateCheckResult(assembly.cutClearance);invalidateCheckResult(assembly.openingPreclearance);
  }
  const renderKey=domain=>JSON.stringify([domain?.workpieces?.map(w=>[w.id,w.previewRef]),domain?.placements,domain?.fixtures]);
  const proxy=context.sceneProxy,previousExpected=JSON.stringify(view.scene?.domain?.expected),previous=renderKey(view.scene?.domain),state=await readMachiningState(context);
  if(view.disposed||context.isCurrentProject?.()===false||context.sceneProxy!==proxy||JSON.stringify(view.scene?.domain?.expected)!==previousExpected)return false;
  if(previous!==renderKey(state))return ops.refreshSceneState(context,view);
  mergeDomainState(view.scene,state);ops.renderProject(context,view);return true;
}
export async function importModelPath(context, view, sourcePath, ops) {
  if (view.pending || view.disposed) return false;
  view.pending = true; view.error = ''; view.sourcePath = sourcePath;
  view.fiveAxisImportActive = true; view.fiveAxisImportCancelling = false;
  ops.renderProject(context, view);
  try {
    const state = await context.sceneProxy.invoke('CamScene.ReadState', {},{timeoutMs:machiningTaskTimeoutMs});
    const request = { commandId: crypto.randomUUID(), expected: state.expected, setupId: state.activeSetupId, sourcePath };
    let task = await context.sceneProxy.invoke('CamWorkpiece.Import', request);
    view.fiveAxisImportTask = task;
    ops.renderProject(context, view);
    while (task.status === 'Running' && !view.disposed) {
      await new Promise(resolve => setTimeout(resolve, 100));
      if (view.disposed) return false;
      task = await context.sceneProxy.invoke('CamWorkpiece.QueryTask', { epoch: task.epoch, taskId: task.taskId });
      view.fiveAxisImportTask = task;
    }
    if (view.disposed) return false;
    if (task.status === 'Completed') {
      await saveCommittedProjectEdit(context,view,ops);
      ops.renderProject(context, view);
      ops.appendProjectLog(context, 'ok', '加工工件已导入');
      await ops.refreshSceneState(context, view);
      view.fiveActivePlacementId=task.result.placementId;
      projectWorkpiece(view.scene,view.fiveActivePlacementId);
      await ops.fitViewAfterRenderPublish(context, view, { entityIds: [view.scene.activePlacementId] });
      return true;
    }
    if (task.status === 'Cancelled') ops.appendProjectLog(context, 'info', '已取消模型导入');
    else if (task.status === 'Superseded') throw Error('导入期间工程已修改，本次结果未写入，请重新导入。');
    else throw Error(task.error || '模型导入失败');
  } catch (error) {
    view.error = error?.message ?? String(error);
    ops.appendProjectLog(context, 'error', view.error);
  } finally {
    view.pending = false; view.fiveAxisImportTask = null;
    view.fiveAxisImportActive = false; view.fiveAxisImportCancelling = false;
    if (!view.disposed) ops.renderProject(context, view);
  }
  return false;
}
export async function chooseWorkpiece(context, view, ops) {
  if (view.pending || view.fiveAxisImportChoosing) return;
  const sceneProxy = context.sceneProxy;
  view.fiveAxisImportChoosing = true;
  try {
    const path = await chooseModelFile(context, view);
    if (path && !view.disposed && context.sceneProxy === sceneProxy && context.isCurrentProject?.() !== false)
      await importModelPath(context, view, path, ops);
  } finally { view.fiveAxisImportChoosing = false; }
}
export async function cancelImport(context, view, ops) {
  const task = view.fiveAxisImportTask;
  if (task?.status !== 'Running' || view.fiveAxisImportCancelling) return;
  view.fiveAxisImportCancelling = true;
  ops.renderProject(context, view);
  try { await context.sceneProxy.invoke('CamWorkpiece.CancelTask', { epoch: task.epoch, taskId: task.taskId }); }
  catch (error) { view.fiveAxisImportCancelling = false; if (!view.disposed) ops.renderProject(context, view); throw error; }
}
