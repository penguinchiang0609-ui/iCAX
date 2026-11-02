// Formal normalized quintics shared by playback, observation and resource QA.
// Positions are native mm/radian; time derivatives always use seconds.
export function polynomialValue(coefficients,u,derivative=0,duration=1){
 if(!Array.isArray(coefficients)||coefficients.length!==6||!coefficients.every(Number.isFinite)||!Number.isFinite(duration)||duration<=0||!Number.isFinite(u))throw Error('机床连续轨迹数据无效，请重新规划。');
 let value=0;for(let k=5;k>=derivative;--k){let scale=1;for(let j=0;j<derivative;++j)scale*=k-j;value=value*u+coefficients[k]*scale;}return value/duration**derivative;
}
export function machineSegmentJoints(previous,next,fraction,derivative=0){
 if(!next||next===previous)return derivative?previous.joints.map(()=>0):previous.joints.slice();
 if(next.durationSeconds===0){if(previous.joints.some((q,i)=>Math.abs(q-next.joints[i])>1e-9))throw Error('零时长事件包含轴移动，请重新规划。');return derivative?previous.joints.map(()=>0):previous.joints.slice();}
 const motion=next.motion;
 if(motion?.interpolation!=='QuinticNormalized'||motion.coefficients?.length!==previous.joints.length)throw Error('运动计划缺少连续轴轨迹，请重新规划。');
 return motion.coefficients.map(c=>polynomialValue(c,Math.max(0,Math.min(1,fraction)),derivative,next.durationSeconds));
}
export function machinePlaybackJoints(frames,cursor){
 const index=Math.max(0,Math.min(frames.length-1,Math.floor(cursor))),first=frames[index],next=frames[index+1];
 return machineSegmentJoints(first,next,cursor-index);
}
export function validateMachinePolynomial(previous,next){
 if(next.durationSeconds===0){machineSegmentJoints(previous,next,0);return;}
 for(const u of [0,1]){const actual=machineSegmentJoints(previous,next,u),expected=u?next.joints:previous.joints;if(actual.some((q,i)=>Math.abs(q-expected[i])>1e-6*(1+Math.abs(expected[i]))))throw Error('连续轨迹与端点不一致，请重新规划。');}
}
