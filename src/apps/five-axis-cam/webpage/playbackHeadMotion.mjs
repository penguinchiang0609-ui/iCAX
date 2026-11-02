import {checkedHeadAssemblyPose} from './cuttingHeadAssembly.mjs';

export function cancelPlaybackHeadMotion(context,s){
  const motion=s.nativeHeadMotion;s.nativeHeadMotion=null;
  if(motion?.timer)clearTimeout(motion.timer);
  if(motion?.task?.status==='Running')void context.sceneProxy.invoke('CamSimulation.CancelTask',{epoch:motion.task.epoch,taskId:motion.task.taskId}).catch(()=>{});
}
export function playbackHeadPose(s){
  if(s.collisionReport?.validity==='Current'){
    const checked=checkedHeadAssemblyPose(s.collisionReport,s.cursor);if(checked)return checked;
  }
  return s.nativeHeadMotion?.sourceKey===s.sourceKey&&s.nativeHeadMotion?.report?checkedHeadAssemblyPose(s.nativeHeadMotion.report,s.cursor):null;
}
export function ensurePlaybackHeadMotion(context,view,s,ops,paint){
  const head=view.scene?.domain?.setups?.find(setup=>setup.id===view.scene.domain.activeSetupId)?.headModel;
  if(!head?.assembly||head.collisionSource==='MachineInstance'||playbackHeadPose(s)||!s.frames.length)return;
  const key=JSON.stringify([s.sourceKey,view.scene.domain.expected,head]);
  if(s.nativeHeadMotion?.key===key)return;
  cancelPlaybackHeadMotion(context,s);
  const motion=s.nativeHeadMotion={key,sourceKey:s.sourceKey,report:{frames:s.frames.map(f=>({position:f.position.toArray()})),segments:[]}};
  const current=()=>!view.disposed&&context.isCurrentProject?.()!==false&&s.nativeHeadMotion===motion;
  const poll=async()=>{
    if(!current())return;
    try{
      const response=await context.sceneProxy.invoke('CamSimulation.QueryPlayback',{epoch:motion.task.epoch,taskId:motion.task.taskId,throughSegment:String(Math.min(Math.floor(s.cursor),Math.max(0,s.frames.length-2))),...(motion.after!==undefined?{afterSegment:String(motion.after)}:{})});
      if(!current())return;motion.task=response;motion.report.headCoverage=response.headCoverage;
      for(const row of response.segments||[]){motion.report.segments.push(row);motion.after=Math.max(motion.after??-1,Number(row.index));}
      if(['Failed','Superseded','Cancelled'].includes(response.status)){motion.error=response.error||'当前轨迹姿态已变化，请重新进入轨迹预览。';s.message=motion.error;ops.renderProject(context,view);return;}
      paint();if(response.status==='Running')motion.timer=setTimeout(poll,150);
    }catch(error){if(current()){motion.error=error.message;s.message=error.message;ops.renderProject(context,view);}}
  };
  void (async()=>{
    try{motion.task=await context.sceneProxy.invoke('CamSimulation.StartPlayback',{commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:view.scene.domain.activeSetupId,firstSegment:'0'});
      if(!current()){if(motion.task.status==='Running')void context.sceneProxy.invoke('CamSimulation.CancelTask',{epoch:motion.task.epoch,taskId:motion.task.taskId});return;}await poll();
    }catch(error){if(current()){motion.error=error.message;s.message=error.message;ops.renderProject(context,view);}}
  })();
}
