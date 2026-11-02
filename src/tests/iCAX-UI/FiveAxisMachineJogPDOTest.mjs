import test from 'node:test';
import assert from 'node:assert/strict';
import {MachineJogPDOStream} from '../../apps/five-axis-cam/webpage/machineJogPDOStream.mjs';
import {encodeMachineJogControl,decodeMachineJogResult} from '../../apps/five-axis-cam/protocol/machineJogPDOProtocol.mjs';
import {decodeMachineJogControl,encodeMachineJogResult} from './FiveAxisMachineJogPDOTestCodec.mjs';
const matrix=x=>[1,0,0,x,0,1,0,0,0,0,1,0,0,0,0,1];
function fixture(){
 const frames=new Map(),writes=[],results=[],errors=[];let clock=0,id=0,busy=false,output=null;
 const descriptor={token:'token',epoch:'epoch',controlPDO:{id:'input',version:1,payloadSize:1024},resultPDO:{id:'output',version:1,payloadSize:4096}};
 const pdo={enabled:true,async withWriteDescriptor(d,write){if(busy)return false;const buffer=new ArrayBuffer(d.payloadSize),n=write(buffer),r=decodeMachineJogControl(buffer.slice(0,n));assert.equal(r.sequence,d.dataVersion);writes.push(r);return true;},async withReadDescriptor(d,read){return output?read(encodeMachineJogResult(output),{dataVersion:output.resultSequence}):null;}};
 const stream=new MachineJogPDOStream(pdo,descriptor,{onResult:r=>results.push(r),onError:e=>errors.push(e),requestFrame:cb=>{const i=id++;frames.set(i,cb);return i;},cancelFrame:i=>frames.delete(i),now:()=>clock});
 return {stream,writes,results,errors,frames,setBusy:value=>busy=value,output:value=>output={token:'token',epoch:'epoch',resultSequence:'1',processedSequence:'1',mutationSequence:'2',status:'Running',positions:[3],worldMatrices:matrix(3),ticks:'4',...value},async tick(time){clock=time;const [i,fn]=frames.entries().next().value;frames.delete(i);fn();await stream.pulsing;}};
}
test('dead-man PDO uses NEW exact uint64 versions for unchanged held state and retains matrices and metrics',()=>{
 const seq='18446744073709551615',encoded=encodeMachineJogControl({token:'t',epoch:'e',sequence:seq,held:true});assert.deepEqual(decodeMachineJogControl(encoded),{token:'t',epoch:'e',sequence:seq,held:true});
 for(const value of [0,-1,2**53,'18446744073709551616'])assert.throws(()=>encodeMachineJogControl({token:'t',epoch:'e',sequence:value,held:true}));
 const bytes=encodeMachineJogResult({token:'t',epoch:'e',status:'Running',resultSequence:seq,processedSequence:seq,mutationSequence:seq,ticks:seq,positions:[1.234],worldMatrices:matrix(1.234),collisionMs:1.2,tickMs:2.5,intervalMs:16});
 const r=decodeMachineJogResult(bytes);assert.equal(r.ticks,seq);assert.equal(r.resultSequence,seq);assert.equal(r.mutationSequence,seq);assert.deepEqual(r.worldMatrices,matrix(1.234));assert.equal(r.tickMs,2.5);assert.throws(()=>decodeMachineJogResult(bytes.subarray(0,22)));const wrong=bytes.slice();wrong[4]=0;assert.throws(()=>decodeMachineJogResult(wrong));
});
test('held state publishes at most one heartbeat per 60ms while accepted results are read every frame without mail',async()=>{
 const f=fixture();f.stream.start();await f.tick(0);assert.deepEqual(f.writes.map(r=>r.sequence),['1']);
 for(const t of [16,32,48])await f.tick(t);assert.equal(f.writes.length,1);f.output();await f.tick(64);assert.deepEqual(f.writes.map(r=>r.sequence),['1','2']);assert.equal(f.results.length,1);assert.deepEqual(f.results[0].worldMatrices,matrix(3));
 await f.tick(80);assert.equal(f.results.length,1,'unchanged result is not reapplied');await f.tick(128);assert.equal(f.writes.length,3);assert.ok(f.writes.every(r=>r.held));await f.stream.stop({release:true});assert.equal(f.writes.at(-1).held,false);assert.equal(f.frames.size,0);assert.equal(f.stream.stopped,true);
});
test('busy slots drop heartbeat attempts without a queue or reliable fallback and release never waits for a slot',async()=>{
 const f=fixture();f.setBusy(true);f.stream.start();for(const t of [0,16,32])await f.tick(t);assert.equal(f.writes.length,0);assert.equal(f.stream.stats.busyWrites,3);
 f.setBusy(false);await f.tick(48);assert.equal(f.writes.length,1);assert.equal(f.writes[0].sequence,'4');assert.equal(f.frames.size,1);f.setBusy(true);await f.stream.stop({release:true});assert.equal(f.frames.size,0);assert.equal(f.stream.stopped,true);assert.equal(f.writes.length,1);
});
test('foreign and old outputs are ignored; invalid receipts stop scheduling without applying another pose',async()=>{
 const f=fixture();f.stream.start();f.output({token:'foreign',processedSequence:'0'});await f.tick(0);assert.equal(f.results.length,0);f.output({epoch:'old',processedSequence:'0'});await f.tick(16);assert.equal(f.results.length,0);
 f.output({resultSequence:'3',processedSequence:'1'});await f.tick(64);assert.equal(f.results.length,1);f.output({resultSequence:'2',processedSequence:'1'});await f.tick(80);assert.equal(f.results.length,1);
 f.output({resultSequence:'4',processedSequence:'99'});await f.tick(96);assert.equal(f.errors.length,1);assert.equal(f.frames.size,0);assert.equal(f.results.length,1);await f.stream.stop();
});
