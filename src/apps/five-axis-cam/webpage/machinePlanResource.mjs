// ICMJ carries the complete immutable machine document through the resource
// channel. All joint coordinates and timings remain standard JSON doubles.
import {validateMachinePolynomial} from './machineTrajectory.mjs';
const caches=new WeakMap();
export function parseMachineJSONResource(buffer){
 const view=new DataView(buffer),range=(at,size)=>{if(!Number.isSafeInteger(at)||at<0||!Number.isSafeInteger(size)||size<0||at+size>view.byteLength)throw Error('机床运动资源不完整。');};
 const u32=at=>{range(at,4);return view.getUint32(at,true);},offset=at=>{const n=at+u32(at);range(n,4);return n;};
 const field=(table,index)=>{range(table,4);const vt=table-view.getInt32(table,true);range(vt,4);const size=view.getUint16(vt,true),entry=4+index*2;range(vt,size);if(entry+2>size)return 0;const n=view.getUint16(vt+entry,true);if(!n)return 0;range(table+n,4);return table+n;};
 range(0,8);if(String.fromCharCode(...new Uint8Array(buffer,4,4))!=='ICMJ')throw Error('机床运动资源格式无效。');
 const root=offset(0),version=field(root,0);if((version?u32(version):1)!==1)throw Error('机床运动资源版本不支持。');
 const json=field(root,1);if(!json)throw Error('机床运动资源缺少完整文档。');
 const start=offset(json),length=u32(start);range(start+4,length+1);if(view.getUint8(start+4+length)!==0)throw Error('机床运动文档无效。');
 const body=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(new Uint8Array(buffer,start+4,length)));
 if(!body||typeof body!=='object'||Array.isArray(body))throw Error('机床运动文档无效。');return body;
}
async function load(client,reference){
 const version=String(reference?.version??'');
 if(!client?.get||typeof reference?.url!=='string'||!reference.url||!/^\d+$/.test(version)||BigInt(version)<=0n)throw Error('机床运动资源缺少固定版本。');
 let cache=caches.get(client);if(!cache)caches.set(client,cache=new Map());const key=JSON.stringify([reference.url,version]);
 if(!cache.has(key)){
  const request=(async()=>{const response=await client.get(reference.url,{headers:{'ICAX-Resource-Version':version,Accept:'application/vnd.icax.flatbuffer'}});
   if(!response.ok||response.headers.get('ICAX-Resource-Version')!==version)throw Error('机床运动资源读取失败或版本不一致，请重新读取。');
   return parseMachineJSONResource(await response.arrayBuffer());})();
  cache.set(key,request);request.catch(()=>{if(cache.get(key)===request)cache.delete(key);});while(cache.size>2)cache.delete(cache.keys().next().value);
 }return cache.get(key);
}
const numbers=(value,length)=>Array.isArray(value)&&value.length===length&&value.every(Number.isFinite);
function validatePlan(body,metadata){
 if(body.schema!=='cam.machine-motion-plan'||String(body.schemaVersion)!=='2'||body.interpolation!=='QuinticNormalized'||!body.modelKey||body.modelKey!==metadata.modelKey||body.status!==metadata.status||body.playbackAllowed!==metadata.playbackAllowed)throw Error('机床运动资源与当前计划不一致，请重新规划。');
 const count=Number(metadata.sampleCount),nodes=body.nodes,frames=body.frames;
 if(!Number.isSafeInteger(count)||count<0||!Array.isArray(frames)||frames.length!==count||Number(body.sampleCount)!==count||!Array.isArray(nodes)||!nodes.length)throw Error('机床运动帧数与当前计划不一致。');
 for(let i=0;i<nodes.length;i++){const n=nodes[i];if(typeof n.id!=='string'||!n.id||!Number.isInteger(n.parent)||n.parent< -1||n.parent>=i||!numbers(n.origin,16)||!numbers(n.axis,3)||!['fixed','prismatic','revolute','continuous'].includes(n.type))throw Error('机床运动链数据无效。');}
 if(!Number.isInteger(body.tool)||!nodes[body.tool]||!Number.isInteger(body.support)||!nodes[body.support]||body.tool===body.support)throw Error('机床运动端点无效。');
 for(let i=0;i<frames.length;i++){const f=frames[i];if(!numbers(f.joints,nodes.length)||typeof f.laser!=='boolean'||!Number.isFinite(f.durationSeconds)||(i?f.durationSeconds<0:f.durationSeconds!==0)||f.position&&!numbers(f.position,3))throw Error(`第 ${i} 帧机床运动数据无效。`);if(i)validateMachinePolynomial(frames[i-1],f);}
 if(body.playbackAllowed&&(!frames.length||!Number.isFinite(body.durationSeconds)||body.durationSeconds<0))throw Error('机床运动时间无效。');
}
export async function hydrateMachinePlan(metadata,sceneProxy,{active=()=>true}={}){
 if(!active())return null;
 if(['Missing','Stale'].includes(metadata?.validity)&&metadata.playbackAllowed===false&&!metadata.planResourceRef)return metadata;
 const body=await load(sceneProxy?.resources,metadata?.planResourceRef);if(!active())return null;validatePlan(body,metadata);
 const {planResourceRef:bodyRef,inputKey:bodyInputKey,...document}=body;
 if(Object.hasOwn(metadata,'validity'))document.validity=metadata.validity;
 if(Object.hasOwn(metadata,'revision'))document.revision=metadata.revision;
 return document;
}
export async function hydrateMachineProgram(metadata,sceneProxy,{active=()=>true}={}){
 if(!active())return null;const body=await load(sceneProxy?.resources,metadata?.programResourceRef);if(!active())return null;
 const count=Number(metadata.instructionCount),motion=body.motion,rows=motion?.instructions,nodes=motion?.machine?.nodes;
 if(!body.modelKey||body.modelKey!==metadata.modelKey||motion?.machine?.modelKey!==metadata.modelKey||body.format!==metadata.format||body.extension!==metadata.extension||typeof body.content!=='string'||!Number.isSafeInteger(count)||count<0||Number(body.instructionCount)!==count||!Array.isArray(rows)||rows.length!==count+1||!Array.isArray(nodes))throw Error('运动指令资源与当前机床计划不一致。');
 for(let i=0;i<rows.length;i++){const r=rows[i];if(r.index!==i||!numbers(r.joints,nodes.length)||typeof r.process!=='boolean'||!Number.isFinite(r.durationSeconds)||(i?r.durationSeconds<0:r.durationSeconds!==0)||!r.axes||Object.values(r.axes).some(v=>!Number.isFinite(v)))throw Error(`第 ${i} 段运动指令无效。`);}
 if(motion.format!=='ICAX-JOINT-MOTION-2'||motion.interpolation!=='QuinticNormalized')throw Error('运动指令缺少连续轨迹。');for(let i=1;i<rows.length;i++)validateMachinePolynomial(rows[i-1],rows[i]);
 const {programResourceRef:bodyRef,...document}=body;return document;
}
