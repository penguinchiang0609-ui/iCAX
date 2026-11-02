// Test endpoint codecs use the same official generated schemas as production.
// They do not hook into the live bridge or manufacture native collision proof.
import {Builder,ByteBuffer} from '../../apps/five-axis-cam/protocol/runtime/flatbuffers.mjs';
import {MachineIKTarget} from '../../apps/five-axis-cam/protocol/generated/machine-iktarget.mjs';
import {MachineIKResult} from '../../apps/five-axis-cam/protocol/generated/machine-ikresult.mjs';
import {MachineCollisionCollider} from '../../apps/five-axis-cam/protocol/generated/machine-collision-collider.mjs';
import {MachineCollisionPair} from '../../apps/five-axis-cam/protocol/generated/machine-collision-pair.mjs';

export function decodeMachineIKTarget(input){
 const bytes=input instanceof ArrayBuffer?new Uint8Array(input):new Uint8Array(input.buffer,input.byteOffset,input.byteLength),bb=new ByteBuffer(bytes);
 if(bytes.length<8||!MachineIKTarget.bufferHasIdentifier(bb))throw Error('Invalid F5IT test input');
 const value=MachineIKTarget.getRootAsMachineIKTarget(bb);
 const jointPosition=value.jointPosition();
 if(value.schemaVersion()!==1||value.targetLength()!==(jointPosition===null?16:0))throw Error('Invalid F5IT test schema');
 return {schemaVersion:1,editToken:value.editToken(),epoch:value.epoch(),sequence:value.sequence().toString(),...(jointPosition===null?{target:Array.from({length:16},(_,i)=>value.target(i))}:{jointPosition})};
}
export function encodeMachineIKResult(result){
 const d={schemaVersion:1,resultSequence:0,processedSequence:0,appliedSequence:0,mutationSequence:0,status:'Idle',collisionStatus:'',reason:'',positions:[],tipWorld:[],worldMatrices:[],cacheReused:false,captureMs:0,prepareMs:0,solveMs:0,collisionMs:0,commitMs:0,totalMs:0,positionErrorMm:0,rotationErrorRadians:0,...result};
 const b=new Builder(512),token=b.createString(d.editToken),scene=b.createString(d.epoch),status=b.createString(d.status),collision=b.createString(d.collisionStatus),reason=b.createString(d.reason);
 const positions=MachineIKResult.createPositionsVector(b,d.positions),tip=MachineIKResult.createTipWorldVector(b,d.tipWorld),world=MachineIKResult.createWorldMatricesVector(b,d.worldMatrices);
 const collider=ref=>MachineCollisionCollider.createMachineCollisionCollider(b,b.createString(ref.ownerElementId),b.createString(ref.ownerElementName),b.createString(ref.boxId||''),b.createString(ref.boxName),ref.boxIndex,b.createString(ref.representation),ref.editable);
 const pairs=(d.collisionPairs||[]).map(pair=>{const first=collider(pair.first),second=collider(pair.second);MachineCollisionPair.startMachineCollisionPair(b);MachineCollisionPair.addFirst(b,first);MachineCollisionPair.addSecond(b,second);return MachineCollisionPair.endMachineCollisionPair(b);});
 const collisionPairs=MachineIKResult.createCollisionPairsVector(b,pairs);
 const root=MachineIKResult.createMachineIKResult(b,d.schemaVersion,token,scene,BigInt(d.resultSequence),BigInt(d.processedSequence),BigInt(d.appliedSequence),BigInt(d.mutationSequence),status,collision,reason,positions,tip,d.cacheReused,d.captureMs,d.prepareMs,d.solveMs,d.collisionMs,d.commitMs,d.totalMs,d.positionErrorMm,d.rotationErrorRadians,world,collisionPairs,!!d.collisionDiagnosticsComplete);
 MachineIKResult.finishMachineIKResultBuffer(b,root);return b.asUint8Array().slice();
}
