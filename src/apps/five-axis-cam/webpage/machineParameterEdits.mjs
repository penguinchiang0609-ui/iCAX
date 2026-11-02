import {saveEditedProject} from './projectEditPersistence.mjs';
import {refreshMachiningState} from './domainActions.mjs';
import {setMachineCollisionFeedback,machineCollisionFeedbackGeometrySignature} from './machineCollisionFeedback.mjs';

// Pending input only. The native repository owns every committed undo record.
const state=view=>view.fiveMachineParameterEdits??={entries:new Map()};
const render=(context,view,ops)=>{if(!view.disposed&&context.isCurrentProject?.()!==false){ops.renderProject(context,view);context.actions?.refreshProjectHistoryControls?.();}};
export function queueMachineParameterEdit(context,view,ops,key,build,accept,delay=300){
 const s=state(view),old=s.entries.get(key);clearTimeout(old?.timer);
 const entry={key,build,accept,revision:(old?.revision||0)+1};s.entries.set(key,entry);s.binding={context,ops};
 try{entry.edit=build();}catch(error){entry.error=error.message;view.fiveMachine.error=entry.error;render(context,view,ops);return;}
 view.fiveMachine.error='';
 entry.timer=setTimeout(()=>{entry.timer=null;entry.ready=true;void drain(context,view,ops);},delay);
 context.actions?.refreshProjectHistoryControls?.();
}
async function drain(context,view,ops){
 const s=state(view);if(s.request)return s.request;
 s.request=(async()=>{await Promise.resolve();try{
  for(;;){const entry=[...s.entries.values()].find(e=>e.ready&&!e.error);if(!entry)break;entry.ready=false;
   try{
    const request=structuredClone(entry.edit.request);
    if(entry.edit.expected)request.expected=structuredClone(view.scene.domain.expected);
    const geometrySignature=entry.edit.method==='CamSimulation.MoveMachine'?machineCollisionFeedbackGeometrySignature(view,view.fiveMachine?.machineId):null;
    const result=await context.sceneProxy.invoke(entry.edit.method,request);
    if(entry.edit.method==='CamSimulation.MoveMachine')setMachineCollisionFeedback(view,view.fiveMachine?.machineId,result,{geometrySignature});
    if(entry.edit.method==='CamSimulation.MoveMachine'&&(result.status!=='Ready'||result.collisionStatus!=='Clear'))throw Error(result.reason||'该移动未通过安全检查。');
    if(!await refreshMachiningState(context,view,{...ops,renderProject:()=>{}}))throw Error('读取已保存参数失败，请重试。');
    // CamScene.ReadState contains machining facts; joint limits and post data
    // belong to Machine.List and must be refreshed before accepting the draft.
    const machines=await context.sceneProxy.invoke('Machine.List',{});
    if(view.disposed||context.isCurrentProject?.()===false)throw Error('工程已切换。');
    view.scene.machines=machines.machines;
    if(s.entries.get(entry.key)===entry){s.entries.delete(entry.key);entry.accept?.();}
    // Keep a newer placement draft while the response for the previous edit arrives.
    if([...s.entries.keys()].some(k=>k.startsWith('setup:'))){const setup=view.scene.domain.setups.find(x=>x.id===view.scene.domain.activeSetupId);view.fiveMachine.setupKey=JSON.stringify([setup?.id,setup?.machineSetup]);}
    s.fileDirty=true;
    await context.actions?.refreshNativeProjectHistory?.({projectId:context.project?.projectId});
   }catch(error){entry.error=error.message;view.fiveMachine.error=entry.error;}
   render(context,view,ops);
  }
 }finally{s.request=null;render(context,view,ops);}})();
 await s.request;
 if([...s.entries.values()].some(e=>e.ready&&!e.error))return drain(context,view,ops);
 if(s.fileDirty)await saveFile(context,view,ops);
}
async function saveFile(context,view,ops){
 const s=state(view);if(s.fileSaving)return s.fileSaving;
 const path=context.projectProxy?.state?.projectPath||context.project?.projectPath;if(!path||path.includes('://'))return;
 s.fileSaving=(async()=>{try{while(s.fileDirty){s.fileDirty=false;await saveEditedProject(context);}s.fileError='';}catch(e){s.fileDirty=true;s.fileError=e.message;view.fiveMachine.error='项目保存失败：'+e.message;}finally{s.fileSaving=null;render(context,view,ops);}})();
 return s.fileSaving;
}
export function finishMachineParameterField(view){const s=view.fiveMachineParameterEdits;if(!s?.binding)return;for(const e of s.entries.values())if(e.timer){clearTimeout(e.timer);e.timer=null;e.ready=!e.error;}void drain(s.binding.context,view,s.binding.ops);}
export async function flushMachineParameterEdits(context,view,ops){
 const s=view.fiveMachineParameterEdits;if(!s)return;
 for(const e of s.entries.values()){clearTimeout(e.timer);e.timer=null;e.ready=!e.error;}
 await drain(context,view,ops);await s.fileSaving;
 const failed=[...s.entries.values()].find(e=>e.error);if(failed)throw Error(failed.error);
 if(s.fileError)throw Error(s.fileError);
}
export function cancelInvalidMachineParameters(view){
 const s=view.fiveMachineParameterEdits;if(!s)return false;let cancelled=false;
 for(const [key,e]of s.entries)if(e.error){
  clearTimeout(e.timer);s.entries.delete(key);cancelled=true;
  if(key.startsWith('origin:')){
   const id=key.slice(7),m=view.fiveMachine;if(m?.originDraft)delete m.originDraft[id];
   if(view.fiveMachineSelectedPartId===id){
    const position=view.scene?.machines?.flatMap(machine=>machine.elements||[]).find(element=>element.entityId===id)?.restPosition;
    position?.forEach((value,index)=>{const input=s.binding?.context.mount?.querySelector(`[data-five-machine-field="origin:${'xyz'[index]}"]`);if(input)input.value=String(value);});
   }
  }
 }
 if(cancelled){const m=view.fiveMachine;m.error='';m.postDirty=false;m.postMachine=null;m.limitDraft={};m.jointDraft={};m.setupKey=null;}
 return cancelled;
}
// A rejected movement is an unapplied draft. Retire only that draft when the
// user opens the collision box responsible for the rejection.
export function cancelRejectedMachineMoves(view){
 const s=view.fiveMachineParameterEdits;if(!s)return false;let cancelled=false;
 for(const [key,entry] of s.entries)if(entry.error&&entry.edit?.method==='CamSimulation.MoveMachine'){
  clearTimeout(entry.timer);s.entries.delete(key);cancelled=true;
  if(key.startsWith('joint:')&&view.fiveMachine?.jointDraft)delete view.fiveMachine.jointDraft[key.slice(6)];
  if(view.fiveMachine?.error===entry.error)view.fiveMachine.error='';
 }
 return cancelled;
}
