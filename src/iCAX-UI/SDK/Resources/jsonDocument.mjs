// Versioned JSON documents are fetched only when a user needs the detail.
// SDO carries their immutable resource reference, never the document body.
export function parseJsonDocument(buffer,{identifier='ICJD'}={}){
  const view=new DataView(buffer),range=(at,size)=>{if(!Number.isSafeInteger(at)||at<0||!Number.isSafeInteger(size)||size<0||at+size>view.byteLength)throw Error('查询资源不完整。');};
  const u32=at=>{range(at,4);return view.getUint32(at,true);},offset=at=>{const n=at+u32(at);range(n,4);return n;};
  const field=(table,index)=>{range(table,4);const vt=table-view.getInt32(table,true);range(vt,4);const size=view.getUint16(vt,true),entry=4+index*2;range(vt,size);if(entry+2>size)return 0;const n=view.getUint16(vt+entry,true);if(!n)return 0;range(table+n,4);return table+n;};
  range(0,8);if(String.fromCharCode(...new Uint8Array(buffer,4,4))!==identifier)throw Error('查询资源格式无效。');
  const root=offset(0),version=field(root,0);if((version?u32(version):1)!==1)throw Error('查询资源版本不支持。');
  const json=field(root,1);if(!json)throw Error('查询资源缺少文档。');
  const start=offset(json),length=u32(start);range(start+4,length+1);if(view.getUint8(start+4+length)!==0)throw Error('查询文档无效。');
  const body=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(new Uint8Array(buffer,start+4,length)));
  if(!body||typeof body!=='object'||Array.isArray(body))throw Error('查询文档无效。');return body;
}
const caches=new WeakMap();
export async function readJsonDocument(client,reference,{identifier='ICJD',signal,maxDocumentBytes=64*1024*1024}={}){
  const version=String(reference?.version??'');
  if(!client?.get||typeof reference?.url!=='string'||!reference.url||!/^\d+$/.test(version)||BigInt(version)<=0n)throw Error('查询资源缺少固定版本。');
  if(signal?.aborted)throw signal.reason??new DOMException('Query cancelled','AbortError');
  if(!Number.isSafeInteger(maxDocumentBytes)||maxDocumentBytes<=0)throw new TypeError('Invalid document budget');
  let cache=caches.get(client);if(!cache)caches.set(client,cache={entries:new Map(),bytes:0});
  const key=JSON.stringify([identifier,reference.url,version]);
  const evict=()=>{while(cache.entries.size>16||cache.bytes>16*1024*1024){const oldest=cache.entries.keys().next().value,record=cache.entries.get(oldest);cache.bytes-=record.bytes;cache.entries.delete(oldest);}};
  if(!cache.entries.has(key)){
    const record={bytes:0,pending:null};
    record.pending=(async()=>{const response=await client.get(reference.url,{headers:{'ICAX-Resource-Version':version,Accept:'application/vnd.icax.flatbuffer'}});if(!response.ok||response.headers.get('ICAX-Resource-Version')!==version)throw Error('查询资源读取失败或版本不一致。');const buffer=await response.arrayBuffer();if(buffer.byteLength>maxDocumentBytes)throw Error('查询详情超出本次读取范围，请缩小选择。');const body=parseJsonDocument(buffer,{identifier});if(cache.entries.get(key)===record){record.bytes=buffer.byteLength;cache.bytes+=record.bytes;evict();}return body;})();
    cache.entries.set(key,record);record.pending.catch(()=>{if(cache.entries.get(key)===record){cache.bytes-=record.bytes;cache.entries.delete(key);}});evict();
  }
  const record=cache.entries.get(key),document=await record.pending;if(record.bytes>maxDocumentBytes)throw Error('查询详情超出本次读取范围，请缩小选择。');if(signal?.aborted)throw signal.reason??new DOMException('Query cancelled','AbortError');return document;
}
