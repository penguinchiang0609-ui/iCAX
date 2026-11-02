import {Builder} from '../../../apps/five-axis-cam/protocol/runtime/flatbuffers.mjs';
export function encodeMachineJSONResource(body){
 const builder=new Builder(512),json=builder.createString(JSON.stringify(body));builder.startObject(2);builder.addFieldInt32(0,1,1);builder.addFieldOffset(1,json,0);const root=builder.endObject();builder.finish(root,'ICMJ');return builder.asUint8Array().slice();
}
export function machineResourceTransport(){
 const documents=new Map();let serial=0;
 const wire=(body,field)=>{const reference={url:`resource://machine-document/${++serial}`,version:'1'};documents.set(reference.url,encodeMachineJSONResource(body));const metadata={...body,[field]:reference};delete metadata.frames;delete metadata.motion;delete metadata.content;delete metadata.inputKey;return metadata;};
 const resources={async get(url,options){const reference=documents.get(url);return new Response(reference,{status:reference?200:404,headers:{'ICAX-Resource-Version':options.headers['ICAX-Resource-Version']}});}};
 return {wire,resources};
}
