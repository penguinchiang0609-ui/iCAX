import test from 'node:test';
import assert from 'node:assert/strict';
import {MachineIKPDOStream} from '../../apps/five-axis-cam/webpage/machineIKPDOStream.mjs';
import {ViewClient} from '../../iCAX-UI/SDK/View/viewClient.mjs';
import {encodeMachineIKResult,decodeMachineIKTarget} from './FiveAxisMachineIKPDOTestCodec.mjs';

const matrix=x=>[1,0,0,x,0,1,0,0,0,0,1,0,0,0,0,1];
const turn=()=>new Promise(resolve=>setImmediate(resolve));
function fixture(options={}){
 const frames=new Map(),writes=[],results=[],errors=[];let nextFrame=0,output=null,busy=false,holdRead=null,releaseRead=null,mail=0;
 const descriptor={editToken:'gesture',epoch:'scene',targetPDO:{id:'input',version:1,payloadSize:1024},resultPDO:{id:'output',version:1,payloadSize:4096}};
 const pdo={enabled:true,invoke(){mail++;throw Error('A PDO stream cannot send SDO mail');},async withWriteDescriptor(slot,writer){
  if(busy)return false;const buffer=new ArrayBuffer(slot.payloadSize),length=writer(buffer);writes.push(decodeMachineIKTarget(buffer.slice(0,length)));return true;
 },async withReadDescriptor(slot,reader){
  if(holdRead)await holdRead;
  if(!output)return null;const bytes=encodeMachineIKResult(output);return reader(bytes,{dataVersion:output.resultSequence});
 }};
 const stream=new MachineIKPDOStream(pdo,descriptor,{onResult:r=>results.push(r),onError:e=>errors.push(e),requestFrame:cb=>{const id=++nextFrame;frames.set(id,cb);return id;},cancelFrame:id=>frames.delete(id),timeoutMs:30,...options});
 return {stream,frames,writes,results,errors,get mail(){return mail;},busy:value=>busy=value,hold(){holdRead=new Promise(resolve=>releaseRead=resolve);},release(){releaseRead?.();holdRead=null;},
  output(overrides={}){output={editToken:'gesture',epoch:'scene',resultSequence:'1',processedSequence:'1',appliedSequence:'1',mutationSequence:'1',status:'Ready',collisionStatus:'Clear',positions:[1],tipWorld:matrix(1),worldMatrices:matrix(1),...overrides};},
  trigger(){const entry=frames.entries().next().value;assert.ok(entry,'one frame is scheduled');frames.delete(entry[0]);entry[1]();return stream.pulsing;},
  async tick(){await this.trigger();await turn();}
 };
}

test('stream ignores stale token, epoch, output versions and regressing processed sequence',async t=>{
 const f=fixture();t.after(()=>f.stream.stop());f.stream.update(matrix(1));
 f.output({editToken:'other'});await f.tick();assert.equal(f.results.length,0);assert.equal(f.stream.resultSequence,0n);
 f.output({epoch:'old-scene',resultSequence:'2'});await f.tick();assert.equal(f.results.length,0);
 f.output({resultSequence:'3'});await f.tick();assert.equal(f.results.length,1);assert.equal(f.stream.processedSequence,1n);
 f.output({resultSequence:'2'});await f.tick();assert.equal(f.results.length,1);
 f.output({resultSequence:'4',processedSequence:'0',appliedSequence:'0'});await f.tick();assert.equal(f.results.length,1);assert.equal(f.stream.resultSequence,3n);
 f.output({resultSequence:'18446744073709551615'});await f.tick();assert.equal(f.results.length,2);assert.equal(f.stream.resultSequence,(1n<<64n)-1n);assert.equal(f.errors.length,0);
});
test('FK stream overwrites scalar joint targets including zero and waits for their native acknowledgement',async t=>{
 const f=fixture();t.after(()=>f.stream.stop());f.stream.descriptor.motionMode='FK';
 assert.throws(()=>f.stream.update(matrix(0)));f.busy(true);f.stream.update(.5);await f.tick();f.stream.update(0);f.busy(false);await f.tick();
 assert.deepEqual(f.writes.map(x=>x.jointPosition),[0]);
 f.output({processedSequence:'2',appliedSequence:'2',positions:[0]});await f.tick();await f.stream.flush();assert.equal(f.stream.processedSequence,2n);assert.equal(f.mail,0);
});

test('a busy input slot keeps only the latest replacement and flush resolves only after its acknowledgement',async t=>{
 const f=fixture();t.after(()=>f.stream.stop());f.busy(true);f.stream.update(matrix(1));await f.tick();assert.equal(f.stream.stats.busyWrites,1);assert.equal(f.writes.length,0);
 for(let i=2;i<=80;i++)f.stream.update(matrix(i));f.busy(false);await f.tick();assert.deepEqual(f.writes.map(x=>x.target[3]),[80]);
 let done=false;const flushing=f.stream.flush().then(r=>{done=true;return r;});await turn();assert.equal(done,false);
 f.output({resultSequence:'1',processedSequence:'79',appliedSequence:'79'});await f.tick();assert.equal(done,false);
 f.output({resultSequence:'2',processedSequence:'80',appliedSequence:'80',positions:[80],tipWorld:matrix(80),worldMatrices:matrix(80)});await f.tick();assert.equal((await flushing).processedSequence,'80');assert.equal(done,true);assert.equal(f.mail,0);
});

test('final target acknowledgement timeout rejects without sending a command or claiming completion',async t=>{
 const f=fixture({timeoutMs:10});t.after(()=>f.stream.stop());f.stream.update(matrix(8));await f.tick();
 await assert.rejects(f.stream.flush(),/尚未得到机床回执/);assert.equal(f.stream.processedSequence,0n);assert.equal(f.stream.waiters.size,0);assert.equal(f.results.length,0);assert.equal(f.mail,0);
});

test('stop cancels a queued frame, rejects pending ACK waits and prevents later writes',async()=>{
 const f=fixture();f.stream.update(matrix(1));const rejected=assert.rejects(f.stream.flush(),/通道已结束/);assert.equal(f.frames.size,1);
 await f.stream.stop();await rejected;await f.stream.pulse();assert.equal(f.frames.size,0);assert.equal(f.writes.length,0);assert.equal(f.results.length,0);assert.equal(f.mail,0);assert.throws(()=>f.stream.update(matrix(2)),/通道已结束/);
});

test('a result read which completes after stop never calls the UI or starts another frame',async()=>{
 const f=fixture();f.hold();f.output();f.stream.update(matrix(1));const pending=f.trigger();const stopped=f.stream.stop();f.release();await pending;await stopped;
 assert.equal(f.writes.length,1,'only the already-started write may complete');assert.equal(f.results.length,0);assert.equal(f.errors.length,0);assert.equal(f.frames.size,0);await f.stream.pulse();assert.equal(f.writes.length,1);
});

test('fatal invalidation wakes ACK waiters and reports one error without applying a fake pose',async t=>{
 const f=fixture();t.after(()=>f.stream.stop());f.stream.update(matrix(1));const waiting=assert.rejects(f.stream.flush(),/configuration changed/);
 f.output({status:'Invalidated',reason:'configuration changed',positions:[],tipWorld:[],worldMatrices:[]});await f.tick();await waiting;
 assert.equal(f.errors.length,1);assert.equal(f.results[0].status,'Invalidated');assert.deepEqual(f.results[0].worldMatrices,[]);assert.equal(f.frames.size,0);assert.equal(f.mail,0);
});

test('a receipt beyond the last target is rejected instead of satisfying a final acknowledgement',async t=>{
 const f=fixture();t.after(()=>f.stream.stop());f.stream.update(matrix(1));f.output({processedSequence:'2',appliedSequence:'2'});await f.tick();assert.equal(f.errors.length,1);assert.match(f.errors[0].message,/序号超出/);assert.equal(f.results.length,0);assert.equal(f.stream.processedSequence,0n);
});

test('ViewReader pause retains its reference, suppresses automatic reads and resumes exactly one timer',async t=>{
 const intervals=new Map();let intervalId=0,heads=0,releases=0,opens=0;
 t.mock.method(globalThis,'setInterval',(callback,ms)=>{const id=++intervalId;intervals.set(id,{callback,ms});return id;});
 t.mock.method(globalThis,'clearInterval',id=>intervals.delete(id));
 const proxy={async invoke(method){if(method==='View.GetOrCreate'){opens++;return {viewId:'view',resource:{url:'resource://view',version:'0'}};}assert.equal(method,'View.Release');releases++;return {released:true};},resources:{async head(){heads++;return {ok:true,headers:new Headers({'ICAX-Resource-Version':'0'})};},async get(){throw Error('An unchanged view needs no GET');}}};
 const client=new ViewClient(proxy),reader=await client.start({sources:[{sourceId:'machine',where:'WHERE FALSE'}]});t.after(()=>client.dispose());await reader.poll();assert.equal(opens,1);assert.equal(intervals.size,1);
 reader.setPollingPaused(true);const pausedReads=heads;assert.equal(intervals.size,0);await turn();assert.equal(heads,pausedReads);assert.equal(releases,0);
 reader.setPollInterval(33);assert.equal(intervals.size,0);await reader.poll();assert.equal(heads,pausedReads+1,'explicit final poll remains available while automatic reads are paused');
 reader.setPollingPaused(false);reader.setPollingPaused(false);assert.equal(intervals.size,1);assert.equal([...intervals.values()][0].ms,33);
 [...intervals.values()][0].callback();await turn();assert.equal(heads,pausedReads+2);assert.equal(releases,0);await reader.stop();await client.dispose();assert.equal(releases,1);assert.equal(intervals.size,0);
});
