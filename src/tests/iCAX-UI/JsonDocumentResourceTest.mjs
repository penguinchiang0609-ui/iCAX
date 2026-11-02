import test from 'node:test';
import assert from 'node:assert/strict';
import {parseJsonDocument,readJsonDocument} from '../../iCAX-UI/SDK/Resources/jsonDocument.mjs';
import {Builder} from '../../apps/five-axis-cam/protocol/runtime/flatbuffers.mjs';

function document(body,{version=1,identifier='ICJD',json=JSON.stringify(body)}={}){
 const b=new Builder(128),text=b.createString(json);b.startObject(2);b.addFieldInt32(0,version,1);b.addFieldOffset(1,text,0);b.finish(b.endObject(),identifier);return b.asUint8Array().slice();
}
const ref={url:'resource://query/detail',version:'7'};
const response=(body,version='7')=>new Response(body,{headers:{'ICAX-Resource-Version':version}});

test('query documents retain nested geometry and numbers and reject invalid envelopes',()=>{
 const value={partId:'承托板',profile:{vertices:[[.000675,2,3]],holes:[]},supported:true};
 const bytes=document(value);assert.deepEqual(parseJsonDocument(bytes.buffer),value);
 for(const invalid of [bytes.slice(0,7),bytes.slice(0,-2),document(value,{identifier:'ICMJ'}),document(value,{version:2}),document(null),document([]),document(value,{json:'{bad'})])assert.throws(()=>parseJsonDocument(invalid.buffer));
 const corrupt=bytes.slice();new DataView(corrupt.buffer).setUint32(0,0xffffffff,true);assert.throws(()=>parseJsonDocument(corrupt.buffer));
});

test('reads pin the resource version, share a request and retry rejected versions',async()=>{
 let calls=0,bad=true;const client={async get(url,options){calls++;assert.equal(url,ref.url);assert.equal(options.headers['ICAX-Resource-Version'],'7');return response(document({value:3}),bad?'8':'7');}};
 await assert.rejects(readJsonDocument(client,ref),/版本/);bad=false;
 const [a,b]=await Promise.all([readJsonDocument(client,ref),readJsonDocument(client,ref)]);assert.deepEqual(a,{value:3});assert.strictEqual(a,b);assert.equal(calls,2);
 await assert.rejects(readJsonDocument(client,{...ref,version:0}),/固定版本/);
});

test('cancellation discards a late read for its caller without cancelling another consumer',async()=>{
 let release;const client={get:()=>new Promise(resolve=>release=()=>resolve(response(document({value:9}))))},controller=new AbortController();
 const cancelled=readJsonDocument(client,ref,{signal:controller.signal}),other=readJsonDocument(client,ref);controller.abort();release();
 await assert.rejects(cancelled,{name:'AbortError'});assert.deepEqual(await other,{value:9});
});

test('each caller applies its document limit even after a cached or concurrent read',async()=>{
 const bytes=document({value:'x'.repeat(1000)}),client={async get(){return response(bytes);}};
 const first=readJsonDocument(client,ref),limited=readJsonDocument(client,ref,{maxDocumentBytes:100});
 assert.equal((await first).value.length,1000);await assert.rejects(limited,/范围/);await assert.rejects(readJsonDocument(client,ref,{maxDocumentBytes:100}),/范围/);
});

test('bounded caches evict old documents and isolate different resource clients',async()=>{
 const reads=[];const client={async get(url){reads.push(url);return response(document({url}));}};
 for(let i=0;i<17;i++)await readJsonDocument(client,{...ref,url:ref.url+i});
 await readJsonDocument(client,{...ref,url:ref.url+'0'});assert.equal(reads.length,18);
 const other={async get(){return response(document({different:true}));}};assert.deepEqual(await readJsonDocument(other,ref),{different:true});
});
