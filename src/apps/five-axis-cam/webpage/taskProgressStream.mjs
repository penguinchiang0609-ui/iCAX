import {decodeTaskProgress} from '../protocol/taskProgressPDOProtocol.mjs';

export async function queryTaskReceipt(context,task,{method='CamPlan.QueryStatus',timeoutMs=120000}={}){
 const request={epoch:task.epoch,taskId:task.taskId},options={timeoutMs};
 try{return await context.sceneProxy.invoke(method,request,options);}
 catch(error){if(error.name!=='SDOTimeoutError')throw error;return context.sceneProxy.invoke(method,request,options);}
}

// Progress is an overwritable snapshot. Only the terminal receipt is read over
// the reliable command channel; no amount of unchanged progress creates mail.
export async function waitForTaskResult(context,receipt,{active=()=>true,onProgress=()=>{},cancel,queryMethod='CamSimulation.QueryTask',cancelMethod='CamSimulation.CancelTask',timeoutMs=120000,schedule=fn=>setTimeout(fn,50),unschedule=id=>clearTimeout(id)}={}){
 if(receipt.status!=='Running')return receipt;
 const scene=context.sceneProxy,pdo=scene.pdo;
 let cancelPromise,abandoned=false;
 const cancelOnce=()=>cancelPromise??=Promise.resolve().then(()=>cancel?cancel():scene.invoke(cancelMethod,{epoch:receipt.epoch,taskId:receipt.taskId},{timeoutMs}));
 const abandonProgress=async()=>{
  if(abandoned)return;abandoned=true;
  const request={epoch:receipt.epoch,taskId:receipt.taskId,releaseProgress:true};
  if(!cancelPromise)cancelPromise=scene.invoke(cancelMethod,request,{timeoutMs});
  else{await cancelPromise.catch(()=>{});await scene.invoke(cancelMethod,request,{timeoutMs});}
  return cancelPromise;
 };
 if(!pdo?.enabled||!receipt.progressPDO){await abandonProgress();throw Error('当前任务缺少实时进度通道，请重新开始。');}
 let stopped=false,timer,watchdog,sequence=0n,reading=false,detached=false;
 try{
  const terminal=await new Promise((resolve,reject)=>{
   const armWatchdog=()=>{clearTimeout(watchdog);watchdog=setTimeout(()=>{
    stopped=true;if(timer!=null)unschedule(timer);
    void abandonProgress().catch(()=>{}).finally(()=>reject(Error('任务实时进度长时间未更新，已请求取消；请确认工程状态后重试。')));
   },timeoutMs);};
   armWatchdog();
   const pulse=async()=>{
    timer=null;if(stopped||reading)return;reading=true;
    try{
     if(!active()){detached=true;await cancelOnce();}
     const frame=await pdo.withReadDescriptor(receipt.progressPDO,(buffer,meta)=>{
      if(!buffer?.byteLength||meta?.dataVersion&&BigInt(meta.dataVersion)<=sequence)return null;
      return decodeTaskProgress(buffer);
     });
     if(stopped)return;
     if(!active()){detached=true;await cancelOnce();}
     if(frame&&frame.taskId===receipt.taskId&&frame.epoch===receipt.epoch&&BigInt(frame.sequence)>sequence){
      sequence=BigInt(frame.sequence);armWatchdog();const task={...receipt,...frame};delete task.result;
      if(!detached)onProgress(task);
      if(task.status!=='Running'){resolve(task);return;}
     }
     timer=schedule(pulse);
    }catch(error){reject(error);}finally{reading=false;}
   };
   void pulse();
  });
  clearTimeout(watchdog);
  // The terminal PDO state was published after native finalization. One read
  // retrieves the complete authoritative result and retires its progress slot.
  const task=await scene.invoke(queryMethod,{epoch:receipt.epoch,taskId:receipt.taskId},{timeoutMs});
  if(task.epoch!==receipt.epoch||task.taskId!==receipt.taskId||task.status==='Running')throw Error('任务最终回执与实时进度不一致。');
  // A command committed just before departure still owns its save obligation.
  // Other detached outcomes have no old-project presentation to install.
  return detached&&task.status!=='Completed'?null:task;
 }catch(error){await abandonProgress().catch(()=>{});throw error;}
 finally{stopped=true;clearTimeout(watchdog);if(timer!=null)unschedule(timer);}
}
