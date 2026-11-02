// CamPathSamples.fbs: large numeric arrays use the resource channel at their
// original double precision. SDO only carries descriptors and fixed references.
import {hydrateCollisionReports} from './collisionReportResource.mjs';
const caches=new WeakMap();
export function parsePathSamples(buffer){
  const view=new DataView(buffer),decoder=new TextDecoder('utf-8',{fatal:true});
  const range=(at,size)=>{if(!Number.isSafeInteger(at)||at<0||size<0||at+size>view.byteLength)throw Error('刀路采样资源不完整。');};
  const u32=at=>{range(at,4);return view.getUint32(at,true);};
  const offset=at=>{const result=at+u32(at);range(result,4);return result;};
  const field=(table,index)=>{range(table,4);const vt=table-view.getInt32(table,true);range(vt,4);const size=view.getUint16(vt,true),entry=4+index*2;range(vt,size);if(entry+2>size)return 0;const n=view.getUint16(vt+entry,true);if(!n)return 0;range(table+n,4);return table+n;};
  const vector=(table,index,width)=>{const f=field(table,index);if(!f)return {at:0,length:0};const start=offset(f),length=u32(start);range(start+4,length*width);return {at:start+4,length};};
  const tables=(table,index)=>{const v=vector(table,index,4);return Array.from({length:v.length},(_,i)=>offset(v.at+i*4));};
  const string=(table,index)=>{const f=field(table,index);if(!f)throw Error('刀路采样缺少标识。');const start=offset(f),n=u32(start);range(start+4,n+1);if(view.getUint8(start+4+n)!==0)throw Error('刀路采样标识无效。');return decoder.decode(new Uint8Array(buffer,start+4,n));};
  const triples=(table,index)=>{const v=vector(table,index,8);if(v.length%3)throw Error('刀路采样坐标数量无效。');const out=[];for(let i=0;i<v.length;i+=3){const p=[0,1,2].map(j=>view.getFloat64(v.at+(i+j)*8,true));if(!p.every(Number.isFinite))throw Error('刀路采样坐标无效。');out.push(p);}return out;};
  range(0,8);if(String.fromCharCode(...new Uint8Array(buffer,4,4))!=='ICPS')throw Error('刀路采样资源格式无效。');
  const root=offset(0),version=field(root,0);if((version?u32(version):1)!==1)throw Error('刀路采样资源版本不支持。');
  const entries=new Map();for(const table of tables(root,1)){const id=string(table,0);if(entries.has(id))throw Error('刀路采样标识重复。');
    const parts=tables(table,1).map(p=>{const role=string(p,0),points=triples(p,1),directions=triples(p,2);if((role!=='Definition'&&!points.length)||(role==='Definition'?directions.length!==0:points.length!==directions.length||directions.some(n=>Math.abs(Math.hypot(...n)-1)>1e-8)))throw Error('刀路采样姿态无效。');return {role,points,directions};});entries.set(id,parts);
  }return entries;
}
async function load(client,reference){
  if(!client?.get)throw Error('刀路采样资源通道不可用。');
  let cache=caches.get(client);if(!cache)caches.set(client,cache=new Map());
  const key=JSON.stringify([reference.url,String(reference.version)]);
  if(!cache.has(key)){
    const promise=(async()=>{const response=await client.get(reference.url,{headers:{'ICAX-Resource-Version':String(reference.version),Accept:'application/vnd.icax.flatbuffer'}});
      if(!response.ok||response.headers.get('ICAX-Resource-Version')!==String(reference.version))throw Error('刀路采样资源读取失败或版本不一致，请刷新后重试。');
      return parsePathSamples(await response.arrayBuffer());})();
    cache.set(key,promise);promise.catch(()=>{if(cache.get(key)===promise)cache.delete(key);});
    while(cache.size>4)cache.delete(cache.keys().next().value);
  }return cache.get(key);
}
export async function hydratePathSamples(payload,sceneProxy){
  for(const setup of payload?.setups||[]){const g=setup.generatedGeometry;if(!g?.sampleGeometryRef)continue;
    const samples=await load(sceneProxy.resources,g.sampleGeometryRef);
    payload.intentToolpaths=payload.intentToolpaths.map(cut=>{if(cut.setupId!==setup.id)return cut;const parts=samples.get(`definition:${cut.id}`);if(parts?.length!==1||parts[0].role!=='Definition'||parts[0].points.length!==Number(cut.previewPointCount)||(!parts[0].points.length&&cut.sourceState==='Current'))throw Error('切割定义采样与当前刀路不一致。');return {...cut,previewPoints:parts[0].points};});
    if(g.operations)g.operations=g.operations.map(op=>{const parts=samples.get(op.intentId);if(!parts||parts.length!==op.parts.length)throw Error('刀路采样与当前工序不一致。');
      return {...op,parts:op.parts.map((part,i)=>{if(parts[i].role!==part.role||parts[i].points.length!==Number(part.pointCount))throw Error('刀路采样数量不一致。');return {...part,...parts[i]};})};});
    if(g.transitions)g.transitions=g.transitions.map(t=>{const editingCandidate=['Failed','Stale'].includes(t.geometryStatus)&&t.editingOnly===true&&Number(t.pointCount)>=2;if(!['Generated','Draft'].includes(t.geometryStatus)&&!editingCandidate)return t;const parts=samples.get(`rapid:${t.from}/${t.to}`);
      if(parts?.length!==1||parts[0].role!=='Rapid'||parts[0].points.length!==Number(t.pointCount))throw Error('空移采样与当前路径不一致。');return {...t,points:parts[0].points,directions:parts[0].directions};});
  }return hydrateCollisionReports(payload,sceneProxy);
}
export async function hydrateCutDefinitions(payload,sceneProxy){
  const samples=await load(sceneProxy.resources,payload.sampleGeometryRef);
  payload.items=payload.items.map(cut=>{
    const parts=samples.get(`definition:${cut.id}`);
    if(parts?.length!==1||parts[0].role!=='Definition'||parts[0].points.length!==Number(cut.previewPointCount)||(!parts[0].points.length&&cut.sourceState==='Current'))throw Error('切割定义采样与当前刀路不一致。');
    return {...cut,previewPoints:parts[0].points};
  });
  return payload;
}
