// Native frame i stores the certified travel time from frame i - 1 to i.
// This clock changes no segment timing and performs no controller simulation.
export function advanceMachinePlayback(state,elapsedSeconds,lastFrame=state.report.frames.length-1){
 const frames=state.report.frames;
 if(!Number.isFinite(elapsedSeconds)||elapsedSeconds<0||!Number.isFinite(state.cursor)||state.cursor<0)throw Error('机床仿真计时无效，请重新开始仿真。');
 const end=Math.min(frames.length-1,lastFrame);
 let consumed=0,correction=0;
 while(consumed<elapsedSeconds&&state.cursor<end){
  const index=Math.floor(state.cursor),duration=Number(frames[index+1].durationSeconds);
  if(!Number.isFinite(duration)||duration<0)throw Error(`第 ${index+1} 段运动时间无效，仿真已停止，请重新规划。`);
  if(duration===0){if(frames[index].joints?.some((q,i)=>Math.abs(q-frames[index+1].joints[i])>1e-9))throw Error('零时长事件包含轴移动。');state.cursor=index+1;continue;}
  const left=(index+1-state.cursor)*duration;
  // Compensated accumulation avoids losing tiny segments when a single
  // display tick crosses many adaptively refined native instructions.
  const adjusted=left-correction,targetTime=consumed+adjusted,nextCorrection=(targetTime-consumed)-adjusted;
  // Scale the round-off budget to the actual consumed time, without imposing
  // a minimum segment duration (adaptive native segments may be below 1 ms).
  const roundOff=Number.EPSILON*Math.max(elapsedSeconds,duration)*8;
  if(elapsedSeconds+roundOff>=targetTime){state.cursor=index+1;consumed=targetTime;correction=nextCorrection;}
  else {state.cursor+=(elapsedSeconds-consumed)/duration;break;}
 }
 return state.cursor;
}
