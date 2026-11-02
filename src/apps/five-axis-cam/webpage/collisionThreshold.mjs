import {refreshMachiningState} from './domainActions.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {invalidateCheckResult} from './checkValidity.mjs';
const current=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
const setupOf=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
const valueOf=text=>/^(?:\d+(?:\.\d+)?|\.\d+)$/.test(String(text).trim())&&Number(text)<=100000?Number(text):null;
export function scheduleCollisionThreshold(context,view,ops){
 const s=view.fiveSimulation;clearTimeout(s.thresholdTimer);
 s.thresholdTimer=setTimeout(()=>{s.thresholdTimer=null;if(current(context,view)&&!view.pending&&valueOf(s.thresholdDraft)!==null)void flushCollisionThreshold(context,view,ops);},350);
}
export function flushCollisionThreshold(context,view,ops){
 const s=view.fiveSimulation;clearTimeout(s.thresholdTimer);s.thresholdTimer=null;
 if(s.thresholdSavePromise)return s.thresholdSavePromise.then(ok=>ok&&s.thresholdDraft!=null?flushCollisionThreshold(context,view,ops):ok);
 if(s.thresholdDraft==null)return Promise.resolve(!s.thresholdRequest);
 if(!current(context,view))return Promise.resolve(false);
 const promise=(async()=>{
  while(current(context,view)&&s.thresholdDraft!=null){
   const draft=s.thresholdDraft,value=valueOf(draft),setup=setupOf(view);
   if(value===null){s.thresholdError=s.message='碰撞阈值须为有效的非负数。';return false;}
   if(s.message===s.thresholdError)s.message='';s.thresholdError='';const domain=view.scene.domain;
   if(!s.thresholdRequest&&value===Number(setup.rapidSettings.collisionThresholdMm)){s.thresholdDraft=null;return true;}
   s.thresholdRequest??={commandId:crypto.randomUUID(),expected:domain.expected,setupId:domain.activeSetupId,settings:{...setup.rapidSettings,collisionThresholdMm:value}};
   try{
    const applied=Number(s.thresholdRequest.settings.collisionThresholdMm);
    await context.sceneProxy.invoke('CamRapid.Configure',s.thresholdRequest);s.thresholdRequest=null;
    if(s.thresholdDraft===draft&&value===applied)s.thresholdDraft=null;
    invalidateCheckResult(setup.simulationCheck);
    await saveCommittedProjectEdit(context,view,ops);
    if(current(context,view))await refreshMachiningState(context,view,ops);
   }catch(error){s.thresholdError=s.message=error.message||String(error);if(/CAM_/.test(s.thresholdError))s.thresholdRequest=null;return false;}
  }return true;
 })();
 s.thresholdSavePromise=promise;
 return promise.finally(()=>{if(s.thresholdSavePromise===promise)s.thresholdSavePromise=null;if(current(context,view))ops.renderProject(context,view);});
}
