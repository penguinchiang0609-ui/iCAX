import {refreshMachiningState} from './domainActions.mjs';
import {saveEditedProject} from './projectEditPersistence.mjs';

const activeSetup=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
export const rapidSafetyEnabled=view=>view.fiveRapidSafety?.get(activeSetup(view)?.id)===true;
export function selectRapidSafety(view,safe){
 const setup=activeSetup(view);if(!setup||view.pending||view.fiveGeneration?.running||view.fiveRapidBatch?.running||view.fiveRapidBatch?.request)return;
 (view.fiveRapidSafety??=new Map()).set(setup.id,!!safe);
}
export function renderRapidSafety(view){
 const busy=view.pending||view.fiveGeneration?.running||view.fiveRapidBatch?.running||view.fiveRapidBatch?.request;
 return `<label class="five-rapid-safety" data-five-key="rapid-safety" title="勾选后参照当前切割头的碰撞盒避障生成；不勾选则按参数快速生成，完成后待检测"><input type="checkbox" data-five-rapid-safe aria-label="安全" ${rapidSafetyEnabled(view)?'checked':''} ${busy?'disabled':''}>安全</label>`;
}

// The toolbar selector is a choice for the next generation, not a command to
// mutate the project. Keep choices per setup across tab changes and redraws.
export function rapidDefaultStrategy(view){
  const setup=activeSetup(view);
  return view.fiveRapidDefaultStrategies?.get(setup?.id)?.strategy??setup?.rapidSettings?.strategy;
}
export function selectRapidDefaultStrategy(view,strategy){
  const setup=activeSetup(view);
  if(!setup||!['Quick','Frog','Fly'].includes(strategy)||view.pending||view.fiveRapid?.request)return;
  const choices=view.fiveRapidDefaultStrategies??=new Map();
  if(choices.get(setup.id)?.request)return;
  if(strategy===setup.rapidSettings?.strategy)choices.delete(setup.id);
  else choices.set(setup.id,{strategy});
}
export function acceptRapidDefaultStrategy(view,setupId,strategy){
  if(view.fiveRapidDefaultStrategies?.get(setupId)?.strategy===strategy)view.fiveRapidDefaultStrategies.delete(setupId);
}

// Called only by an explicit Generate action. Keep an immutable command for
// receipt recovery; a failed generation must not lose the user's choice.
export async function applyRapidDefaultStrategy(context,view,ops){
  const setup=activeSetup(view),draft=view.fiveRapidDefaultStrategies?.get(setup?.id);
  if(!draft)return;
  draft.request??={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:setup.id,settings:{...setup.rapidSettings,strategy:draft.strategy}};
  const request=draft.request;
  try{await context.sceneProxy.invoke('CamRapid.Configure',request);}
  catch(error){
    if(/CAM_.*(?:EPOCH|REVISION)/.test(error.message)){draft.request=null;throw error;}
    const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});
    if(receipt.status!=='Completed'){if(receipt.status==='NotFound')draft.request=null;throw error;}
  }
  if(!await refreshMachiningState(context,view,ops))throw Error('读取空移设置失败，请重试。');
  acceptRapidDefaultStrategy(view,setup.id,draft.strategy);
  await context.actions?.refreshNativeProjectHistory?.({projectId:context.project?.projectId});
  await saveEditedProject(context);
}
