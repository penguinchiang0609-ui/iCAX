// Persist committed native edits. History stays in the framework Database.
// The framework chooses its flushed journal when the archive resource closure
// is unchanged, and a complete snapshot when new resources/settings need it.
// Explicit Save still writes a complete portable archive.
import {projectPersistenceTimeoutMs} from './operationTiming.mjs';
const writers=new WeakMap();
export function saveEditedProject(context){
 const proxy=context.projectProxy;
 const path=()=>proxy?.state?.projectPath||context.project?.projectPath;
 if(!proxy?.autosave||!path()||path().includes('://'))return Promise.resolve(false);
 let state=writers.get(proxy);if(!state){state={revision:0,request:null};writers.set(proxy,state);}
 state.revision++;
 if(!state.request)state.request=(async()=>{
  await Promise.resolve();
  try{
   let saved;do{
    saved=state.revision;
    const receipt=await proxy.autosave(path(),{timeoutMs:projectPersistenceTimeoutMs});
    if(receipt?.deferred===true){
     // A following gesture may already own the repository. Its completion
     // joins this writer; never serialize a snapshot of a live drag.
     await new Promise(resolve=>setTimeout(resolve,100));saved=-1;
    }
   }while(saved!==state.revision);
   return true;
  }finally{state.request=null;}
 })();
 return state.request;
}
export async function flushEditedProjectSave(context){await writers.get(context.projectProxy)?.request;}
export function isEditedProjectSaving(context){return !!writers.get(context.projectProxy)?.request;}

export function clearCommittedSaveError(view,key='fiveWorkpieceSaveError'){
 if(view.error===view[key])view.error='';view[key]='';
}
export async function saveCommittedProjectEdit(context,view,ops,key='fiveWorkpieceSaveError'){
 try{const saved=await saveEditedProject(context);clearCommittedSaveError(view,key);return saved;}
 catch(error){view[key]=`自动保存失败：${error.message}。工程中的修改已保留，请重新保存。`;ops?.appendProjectLog?.(context,'error',view[key]);return false;}
}
