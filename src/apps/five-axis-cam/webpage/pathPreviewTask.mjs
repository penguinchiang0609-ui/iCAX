// The scene captures immutable inputs, then computes previews on its worker.
// Polling keeps individual SDO calls short and lets history/edit requests run.
export async function pathPreview(scene,kind,payload,{valid=()=>true}={}){
  if(!valid())throw Error('CAM_PREVIEW_CANCELLED');
  let task=await scene.invoke('CamProcess.BeginPreview',{...payload,kind});
  try{
    while(task.status==='Running'){
      if(!valid())throw Error('CAM_PREVIEW_CANCELLED');
      await new Promise(resolve=>setTimeout(resolve,60));
      task=await scene.invoke('CamProcess.QueryPreview',{epoch:task.epoch,taskId:task.taskId});
    }
    if(!valid()||task.status==='Cancelled')throw Error('CAM_PREVIEW_CANCELLED');
    if(task.status!=='Completed')throw Error(task.error||'CAM_PREVIEW_FAILED');
    return task.result;
  }catch(error){
    void scene.invoke('CamProcess.QueryPreview',{epoch:task.epoch,taskId:task.taskId,release:true}).catch(()=>{});
    throw error;
  }
}
