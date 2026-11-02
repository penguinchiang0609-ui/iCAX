import test from 'node:test';
import assert from 'node:assert/strict';
import {ByteBuffer} from '../../apps/five-axis-cam/protocol/runtime/flatbuffers.mjs';
import {MachineIKTarget} from '../../apps/five-axis-cam/protocol/generated/machine-iktarget.mjs';
import {encodeMachineIKTarget,decodeMachineIKResult} from '../../apps/five-axis-cam/protocol/machineIKPDOProtocol.mjs';
import {encodeMachineIKResult,decodeMachineIKTarget} from './FiveAxisMachineIKPDOTestCodec.mjs';

const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
const editToken='2f07f963-f63b-43f1-a8ba-3d4ed2f920cf',epoch='db353f3f-a91b-4581-a51a-64682e94530c';
test('FK target preserves an explicit zero and cannot carry an IK pose at the same time',()=>{
 for(const jointPosition of [0,-.257,1732.125]){
  const bytes=encodeMachineIKTarget({editToken,epoch,sequence:1,jointPosition});
  assert.deepEqual(decodeMachineIKTarget(bytes),{schemaVersion:1,editToken,epoch,sequence:'1',jointPosition});
 }
 for(const jointPosition of [NaN,Infinity,'2'])assert.throws(()=>encodeMachineIKTarget({editToken,epoch,sequence:1,jointPosition}));
 assert.throws(()=>encodeMachineIKTarget({editToken,epoch,sequence:1,jointPosition:0,target:identity}));
});
function resultBytes(overrides={}){
 const data={schemaVersion:1,editToken,epoch,resultSequence:(1n<<64n)-1n,processedSequence:9007199254740993n,appliedSequence:9007199254740992n,mutationSequence:9007199254740995n,status:'Ready',collisionStatus:'Clear',reason:'实际关节已更新',positions:[0,12.125,-.004],tipWorld:identity,cacheReused:true,captureMs:1.25,prepareMs:2.5,solveMs:.125,collisionMs:3.75,commitMs:.2,totalMs:7.825,positionErrorMm:1e-10,rotationErrorRadians:2e-12,worldMatrices:[...identity,...identity],...overrides};
 return encodeMachineIKResult(data);
}
function field(bytes,index){const d=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),root=d.getUint32(0,true),vt=root-d.getInt32(root,true);return root+d.getUint16(vt+4+index*2,true);}

test('target uses the official generated F5IT schema and retains exact uint64 and double precision',()=>{
 const target=[...identity];target[3]=1200.000000000001;target[7]=-.000000000001;
 const bytes=encodeMachineIKTarget({editToken,epoch,sequence:(1n<<64n)-1n,target:new Float64Array(target)}),bb=new ByteBuffer(bytes);
 assert.equal(MachineIKTarget.bufferHasIdentifier(bb),true);
 const value=MachineIKTarget.getRootAsMachineIKTarget(bb);
 assert.equal(value.schemaVersion(),1);assert.equal(value.editToken(),editToken);assert.equal(value.epoch(),epoch);assert.equal(value.sequence(),(1n<<64n)-1n);
 assert.deepEqual(Array.from({length:value.targetLength()},(_,i)=>value.target(i)),target);
 const again=encodeMachineIKTarget({editToken,epoch,sequence:'9007199254740993',target});assert.equal(MachineIKTarget.getRootAsMachineIKTarget(new ByteBuffer(again)).sequence(),9007199254740993n);
 assert.deepEqual(decodeMachineIKTarget(again),{schemaVersion:1,editToken,epoch,sequence:'9007199254740993',target});
});

test('target rejects imprecise/overflowing sequences and malformed matrices before writing a PDO',()=>{
 for(const sequence of [0,0n,-1,1.5,Number.MAX_SAFE_INTEGER+1,'1e3','-1','',1n<<64n])assert.throws(()=>encodeMachineIKTarget({editToken,epoch,sequence,target:identity}));
 for(const target of [[],identity.slice(1),[...identity,1],identity.map((v,i)=>i===5?NaN:v),identity.map((v,i)=>i===5?Infinity:v)])assert.throws(()=>encodeMachineIKTarget({editToken,epoch,sequence:1,target}));
 assert.throws(()=>encodeMachineIKTarget({editToken:'',epoch,sequence:1,target:identity}));assert.throws(()=>encodeMachineIKTarget({editToken,epoch:null,sequence:1,target:identity}));
});

test('result decodes generated F5IR positions, true world matrices, metrics and every uint64 without Number conversion',()=>{
 const value=decodeMachineIKResult(resultBytes());
 assert.equal(value.resultSequence,'18446744073709551615');assert.equal(value.processedSequence,'9007199254740993');assert.equal(value.appliedSequence,'9007199254740992');assert.equal(value.mutationSequence,'9007199254740995');
 assert.equal(value.editToken,editToken);assert.equal(value.epoch,epoch);assert.equal(value.schemaVersion,1);assert.equal(value.status,'Ready');assert.equal(value.collisionStatus,'Clear');assert.equal(value.reason,'实际关节已更新');
 assert.deepEqual(value.positions,[0,12.125,-.004]);assert.deepEqual(value.tipWorld,identity);assert.deepEqual(value.worldMatrices,[...identity,...identity]);assert.equal(value.cacheReused,true);assert.equal(value.solveMs,.125);assert.equal(value.positionErrorMm,1e-10);
});

test('decoder respects typed-view byteOffset and detaches its result from the leased byte buffer',()=>{
 const bytes=resultBytes(),padded=new Uint8Array(bytes.length+11);padded.set(bytes,3);
 for(const input of [bytes.buffer,padded.subarray(3,bytes.length+3),new DataView(padded.buffer,3,bytes.length)])assert.deepEqual(decodeMachineIKResult(input).tipWorld,identity);
 const decoded=decodeMachineIKResult(padded.subarray(3,bytes.length+3));padded.fill(0);assert.deepEqual(decoded.positions,[0,12.125,-.004]);assert.deepEqual(decoded.worldMatrices,[...identity,...identity]);
});

test('decoder rejects wrong identifiers, unsupported schemas, truncated and corrupt tables/vectors/required strings',()=>{
 const original=resultBytes();
 // Removing unused trailing alignment bytes is legal FlatBuffers. Truncate
 // through the final required string instead of treating padding as content.
 for(const length of [0,7,20,original.length-8])assert.throws(()=>decodeMachineIKResult(original.subarray(0,length)));
 const identifier=original.slice();identifier[4]=0;assert.throws(()=>decodeMachineIKResult(identifier));assert.throws(()=>decodeMachineIKResult(resultBytes({schemaVersion:2})));
 const root=original.slice();new DataView(root.buffer).setUint32(0,0xfffffff0,true);assert.throws(()=>decodeMachineIKResult(root));
 const vector=original.slice(),vd=new DataView(vector.buffer),vf=field(vector,10),start=vf+vd.getUint32(vf,true);vd.setUint32(start,0x7fffffff,true);assert.throws(()=>decodeMachineIKResult(vector));
 const missing=original.slice(),md=new DataView(missing.buffer),mr=md.getUint32(0,true),vt=mr-md.getInt32(mr,true);md.setUint16(vt+6,0,true);assert.throws(()=>decodeMachineIKResult(missing));
 const invalidUtf8=original.slice(),sd=new DataView(invalidUtf8.buffer),sf=field(invalidUtf8,1),string=sf+sd.getUint32(sf,true);invalidUtf8[string+4]=0xff;assert.throws(()=>decodeMachineIKResult(invalidUtf8));
});

test('rejected and expired outcomes preserve their actual safe pose while invalid numeric state cannot reach rendering',()=>{
 for(const status of ['Unreachable','Collision','Unchecked','Invalidated','Expired','Cancelled']){
  const decoded=decodeMachineIKResult(resultBytes({status,collisionStatus:'Unchecked',reason:'已保留上一个有效姿态'}));assert.equal(decoded.status,status);assert.deepEqual(decoded.tipWorld,identity);
 }
 for(const overrides of [{positions:[NaN]},{tipWorld:identity.slice(1)},{tipWorld:identity.map((v,i)=>i===2?Infinity:v)},{worldMatrices:identity.slice(1)},{worldMatrices:identity.map((v,i)=>i===2?NaN:v)},{solveMs:Infinity}])assert.throws(()=>decodeMachineIKResult(resultBytes(overrides)));
 for(const status of ['Invalidated','Error'])assert.deepEqual(decodeMachineIKResult(resultBytes({status,positions:[],tipWorld:[],worldMatrices:[]})).tipWorld,[],'fatal invalidation does not invent a valid pose');
 assert.throws(()=>decodeMachineIKResult(resultBytes({status:'Ready',positions:[],tipWorld:[],worldMatrices:[]})));
});

test('blocked outcomes carry exact collider identity including disabled attachment index slots and mesh ownership',()=>{
 const first={ownerElementId:'x-owner',ownerElementName:'X 横滑座',boxId:'',boxName:'x_box_contact',boxIndex:2,representation:'Box',editable:true};
 const second={ownerElementId:'z-owner',ownerElementName:'Z 滑枕',boxId:'z-box',boxName:'z_box_1',boxIndex:0,representation:'Box',editable:true};
 const collisionPairs=[{first,second}],bytes=resultBytes({status:'Blocked',collisionStatus:'Blocked',collisionPairs,collisionDiagnosticsComplete:true});
 const value=decodeMachineIKResult(bytes);assert.deepEqual(value.collisionPairs,collisionPairs);assert.equal(value.collisionDiagnosticsComplete,true);
 const mesh={...second,boxId:'',boxName:'真实刀头网格',boxIndex:-1,representation:'OwnMesh',editable:false};
 assert.deepEqual(decodeMachineIKResult(resultBytes({status:'Blocked',collisionStatus:'Blocked',collisionPairs:[{first,second:mesh}]})).collisionPairs,[{first,second:mesh}]);
 assert.equal(decodeMachineIKResult(resultBytes({status:'Blocked',collisionStatus:'Blocked',collisionPairs})).collisionDiagnosticsComplete,false,'partial diagnoses retain proven pairs without claiming complete');
 for(const changed of [{...second,boxIndex:-2},{...second,representation:'Unknown'},{...mesh,editable:true},{...first,ownerElementId:''}])assert.throws(()=>decodeMachineIKResult(resultBytes({status:'Blocked',collisionStatus:'Blocked',collisionPairs:[{first,second:changed}]})));
 assert.throws(()=>decodeMachineIKResult(resultBytes({collisionPairs})),'a Clear motion must not carry old collision identities');
 const oversized=resultBytes({status:'Blocked',collisionStatus:'Blocked',collisionPairs:Array(33).fill({first,second})});assert.throws(()=>decodeMachineIKResult(oversized));
 const corrupt=bytes.slice(),data=new DataView(corrupt.buffer),vectorField=field(corrupt,22),vector=vectorField+data.getUint32(vectorField,true),pair=vector+4+data.getUint32(vector+4,true),vt=pair-data.getInt32(pair,true);data.setUint16(vt+4,0,true);assert.throws(()=>decodeMachineIKResult(corrupt),'missing nested first collider is invalid');
});
