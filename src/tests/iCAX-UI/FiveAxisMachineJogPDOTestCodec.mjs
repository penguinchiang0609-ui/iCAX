import {Builder,ByteBuffer} from '../../apps/five-axis-cam/protocol/runtime/flatbuffers.mjs';
import {MachineJogControl} from '../../apps/five-axis-cam/protocol/generated/machine-jog-control.mjs';
import {MachineJogResult} from '../../apps/five-axis-cam/protocol/generated/machine-jog-result.mjs';
export function decodeMachineJogControl(buffer){const r=MachineJogControl.getRootAsMachineJogControl(new ByteBuffer(new Uint8Array(buffer)));return {token:r.token(),epoch:r.epoch(),sequence:r.sequence().toString(),held:r.held()};}
export function encodeMachineJogResult(r){
 const b=new Builder(512),token=b.createString(r.token),epoch=b.createString(r.epoch),status=b.createString(r.status),reason=b.createString(r.reason||''),positions=MachineJogResult.createPositionsVector(b,r.positions||[]),world=MachineJogResult.createWorldMatricesVector(b,r.worldMatrices||[]);
 const root=MachineJogResult.createMachineJogResult(b,1,token,epoch,BigInt(r.resultSequence||0),BigInt(r.processedSequence||0),BigInt(r.mutationSequence||0),status,reason,positions,world,BigInt(r.ticks||0),r.collisionMs||0,r.tickMs||0,r.intervalMs||0);MachineJogResult.finishMachineJogResultBuffer(b,root);return b.asUint8Array().slice();
}
