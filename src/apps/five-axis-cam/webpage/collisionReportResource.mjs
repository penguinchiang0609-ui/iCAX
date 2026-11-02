import {parseMachineJSONResource} from './machinePlanResource.mjs';
const caches=new WeakMap(),tuple=(v,n)=>Array.isArray(v)&&v.length===n&&v.every(Number.isFinite);
async function load(client,reference){
  const version=String(reference?.version??'');
  if(!client?.get||!reference?.url||!/^\d+$/.test(version)||BigInt(version)<=0n)throw Error('碰撞检测报告缺少固定版本。');
  let cache=caches.get(client);if(!cache)caches.set(client,cache=new Map());
  const key=JSON.stringify([reference.url,version]);
  if(!cache.has(key)){
    const pending=(async()=>{
      const response=await client.get(reference.url,{headers:{'ICAX-Resource-Version':version,Accept:'application/vnd.icax.flatbuffer'}});
      if(!response.ok||response.headers.get('ICAX-Resource-Version')!==version)throw Error('碰撞检测报告读取失败或版本不一致。');
      return parseMachineJSONResource(await response.arrayBuffer());
    })();cache.set(key,pending);pending.catch(()=>{if(cache.get(key)===pending)cache.delete(key);});
    while(cache.size>2)cache.delete(cache.keys().next().value);
  }return cache.get(key);
}
export async function hydrateCollisionReports(payload,sceneProxy){
  for(const setup of payload?.setups??[]){
    const metadata=setup.simulationCheck;if(!metadata?.reportResourceRef)continue;
    const body=await load(sceneProxy?.resources,metadata.reportResourceRef);
    if(body.schema!=='cam.simulation-collision'||String(body.schemaVersion)!=='1'||body.status!==metadata.status||body.validity!==metadata.validity||body.collisionCount!==metadata.collisionCount||body.uncheckedCount!==metadata.uncheckedCount||!Array.isArray(body.frames)||body.frames.length!==Number(metadata.frameCount)||!Array.isArray(body.segments)||body.segments.length!==Number(metadata.segmentCount)||body.segments.length!==body.frames.length-1)
      throw Error('碰撞检测报告与当前轨迹不一致。');
    if(body.frames.some(f=>!tuple(f.position,3)||!tuple(f.rotation,4))||body.segments.some((s,i)=>Number(s.index)!==i||!['Clear','Collision','Unchecked'].includes(s.status)))
      throw Error('碰撞检测报告包含无效的轨迹数据。');
    setup.simulationCheck={...body,reportResourceRef:metadata.reportResourceRef};
    if(body.validity==='Current'&&setup.generatedGeometry&&(body.partialTrajectory||setup.generatedGeometry.status==='Stale'))setup.generatedGeometry.partialDisplay=true;
  }return payload;
}
