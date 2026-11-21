import {headWorkspaceSelection,headWorkspaceHead,hasHeadWorkspacePreview,clearHeadWorkspaceSelection} from './headWorkspaceSelection.mjs';
import {headFitsOperation} from './headMachineCompatibility.mjs';
import {flushHeadColliderSaves} from './cuttingHeadColliders.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {refreshMachiningState} from './domainActions.mjs';

export const headScopeLabels={Setup:'本工序',Workpiece:'本工件',Project:'本工程',Product:'本产品'};
const setupOf=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
const queryKey=view=>JSON.stringify([setupOf(view)?.id,view.scene?.domain?.expected]);
const active=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
export function headScopeState(view){
 const setupId=setupOf(view)?.id;
 let s=view.fiveHeadAssignments;
 if(!s||s.setupId!==setupId)s=view.fiveHeadAssignments={setupId,scope:'Setup',serial:(s?.serial||0)+1};
 return s;
}
export function headScopeCandidate(view){
 if(hasHeadWorkspacePreview(view)){
  const selection=headWorkspaceSelection(view),choice=selection.choice;
  if(selection.loading||selection.error)return null;
  if(choice==='')return {kind:'None'};
  if(!headFitsOperation(view,selection.head))return null;
  const instanceId=view.fiveHeadEditor?.choices?.get(choice);
  if(instanceId)return {kind:'Instance',instanceId};
  if(choice.startsWith('builtin:'))return {kind:'Builtin',builtinDefinitionId:choice.slice(8)};
  if(choice.startsWith('profile:'))return {kind:'Configuration',entryId:choice.slice(8)};
  return {kind:'Instance',instanceId:choice};
 }
 const instanceId=setupOf(view)?.headModel?.instance?.id;
 if(instanceId&&!headFitsOperation(view,headWorkspaceHead(view)))return null;
 return instanceId?{kind:'Instance',instanceId}:{kind:'None'};
}
function blocked(view,s){
 const setup=setupOf(view);
 return !setup||view.pending||s.loading||s.busy||!!setup.machineSetup?.machineId||setup.headModel?.collisionSource==='MachineInstance'||view.fiveSimulation?.busy||view.fiveSimulation?.request||view.fiveHeadEditor?.busy||view.fiveHeadEditor?.request||view.fiveHeadEditor?.importPrompt;
}
export function canSetHeadScope(view){
 const s=headScopeState(view);if(blocked(view,s))return false;
 if(s.request)return s.request.payload.action==='set';
 const candidate=headScopeCandidate(view),setting=s.data?.scopes?.[s.scope];
 if(!candidate||!setting)return false;
 if(candidate.kind==='None')return setting.mode!=='None';
 return !(candidate.kind==='Instance'&&setting.mode==='Instance'&&setting.instanceId===candidate.instanceId);
}
export function canInheritHeadScope(view){
 const s=headScopeState(view);if(blocked(view,s))return false;
 if(s.request)return s.request.payload.action==='inherit';
 const setting=s.data?.scopes?.[s.scope];return !!setting&&setting.mode!=='Inherit';
}
export async function refreshHeadAssignments(context,view,ops){
 const s=headScopeState(view);if(!s.setupId||!active(context,view))return;
 const serial=++s.serial,key=queryKey(view),setupId=s.setupId;
 const current=()=>active(context,view)&&view.fiveHeadAssignments===s&&setupOf(view)?.id===setupId&&s.serial===serial;
 s.loading=true;s.queryError='';
 if(view.fiveCamPage==='head')ops.renderProject(context,view);
 try{
  const data=await context.sceneProxy.invoke('CamHead.Assignments',{setupId});
  if(current()){s.data=data;s.loadedKey=key;}
 }catch(error){if(current()){s.queryError=error.message;s.loadedKey=key;}}
 finally{
  if(view.fiveHeadAssignments===s&&s.serial===serial){
   s.loading=false;if(!current())s.loadedKey=null;
   if(current()&&view.fiveCamPage==='head')ops.renderProject(context,view);
  }
 }
}
export function syncHeadAssignments(context,view,ops){
 const s=headScopeState(view);
 if(view.fiveCamPage!=='head'||view.fiveAxisArea!=='toolpath'||!active(context,view)){s.active=false;return;}
 if(!s.active){s.active=true;s.loadedKey=null;}
 if(s.setupId&&!s.loading&&!s.busy&&!s.request&&s.loadedKey!==queryKey(view))void refreshHeadAssignments(context,view,ops);
}
export function headScopeField(context,view,value,ops){
 const s=headScopeState(view);
 if(!Object.hasOwn(headScopeLabels,value)||!active(context,view)||s.busy||s.request||view.pending)return;
 s.scope=value;s.error='';ops.renderProject(context,view);
}
function candidateIsEffective(candidate,head){
 if(candidate.kind==='None')return !head?.instance;
 if(candidate.kind==='Instance')return head?.instance?.id===candidate.instanceId;
 if(candidate.kind==='Builtin')return head?.builtinDefinitionId===candidate.builtinDefinitionId;
 return head?.instance?.profile?.id===candidate.entryId;
}
export async function headScopeCommand(context,view,action,ops){
 const s=headScopeState(view);
 if(!active(context,view)||!['set','inherit'].includes(action)||(action==='set'?!canSetHeadScope(view):!canInheritHeadScope(view)))return;
 const setupId=s.setupId,scope=s.scope,selection=headWorkspaceSelection(view),selectionSerial=selection.serial;
 const candidate=action==='set'?headScopeCandidate(view):null;
 const current=()=>active(context,view)&&view.fiveHeadAssignments===s&&setupOf(view)?.id===setupId;
 let ownsPending=false;s.busy=true;s.error='';selection.applying=true;ops.renderProject(context,view);
 try{
  await flushHeadColliderSaves(context,view,ops);
  if(!current()||s.scope!==scope||view.pending)return;
  if(!s.request&&action==='set'&&candidate?.kind!=='None'&&!headFitsOperation(view,headWorkspaceHead(view)))throw Error('所选切割头不适用于本工序机型。');
  if(!s.request){
   const payload={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId,scope,action,...candidate};
   if(scope==='Product')payload.preferencesRevision=s.data.productPreferencesRevision;
   s.request={payload};
  }
  const request=s.request;
  // A retry always preserves the captured scope, selector and command receipt.
  view.pending=true;ownsPending=true;ops.renderProject(context,view);
  await context.sceneProxy.invoke('CamHead.Assign',request.payload);s.request=null;
  if(request.payload.scope!=='Product')await saveCommittedProjectEdit(context,view,ops);
  if(active(context,view)&&request.payload.scope!=='Product'){
   if(!await refreshMachiningState(context,view,ops))throw Error('设置已提交，请刷新以读取最新刀头。');
  }
  if(current()){
   await refreshHeadAssignments(context,view,ops);
   if(view.fiveHeadSelection===selection&&selection.serial===selectionSerial&&(request.payload.action==='inherit'||candidateIsEffective(candidate,setupOf(view)?.headModel)))clearHeadWorkspaceSelection(view);
   ops.appendProjectLog?.(context,'ok',request.payload.scope==='Product'?(request.payload.action==='inherit'?'已清除以后新工程的默认刀头':'已设置以后新工程的默认刀头'):headScopeLabels[request.payload.scope]+(request.payload.action==='inherit'?'已恢复继承':'的刀头设置已更新'));
  }
 }catch(error){
  s.error=error.message;
  if(/CAM_|请输入|请填写/.test(s.error)){
   s.request=null;
   if(current())await refreshHeadAssignments(context,view,ops);
  }else if(s.request)s.error='操作结果尚未确认，请重试原操作。';
 }finally{
  s.busy=false;selection.applying=false;if(ownsPending)view.pending=false;
  if(active(context,view))ops.renderProject(context,view);
 }
}
