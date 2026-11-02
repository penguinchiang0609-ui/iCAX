import {Builder,ByteBuffer} from './runtime/flatbuffers.mjs';
import {MachineJogControl} from './generated/machine-jog-control.mjs';
import {MachineJogResult} from './generated/machine-jog-result.mjs';
import {unsignedSequence,pdoText,verifyPDOFlatBuffer} from './pdoFlatBuffer.mjs';

export function encodeMachineJogControl({token,epoch,sequence,held}){
 const seq=unsignedSequence(sequence);if(seq===0n||typeof held!=='boolean')throw TypeError('Jog PDO requires a positive sequence and explicit held state');
 const builder=new Builder(192),tokenOffset=builder.createString(pdoText(token,'token')),epochOffset=builder.createString(pdoText(epoch,'epoch'));
 const root=MachineJogControl.createMachineJogControl(builder,1,tokenOffset,epochOffset,seq,held);
 MachineJogControl.finishMachineJogControlBuffer(builder,root);return builder.asUint8Array().slice();
}
export function decodeMachineJogResult(input){
 const bytes=verifyPDOFlatBuffer(input,'F5JR',['u32','required','required','u64','u64','u64','required','string','vector','vector','u64','double','double','double']);
 const r=MachineJogResult.getRootAsMachineJogResult(new ByteBuffer(bytes));
 if(r.schemaVersion()!==1||!r.token()||!r.epoch()||!r.status())throw Error('实时点动数据版本或身份无效。');
 const positions=Array.from({length:r.positionsLength()},(_,i)=>r.positions(i)),worldMatrices=Array.from({length:r.worldMatricesLength()},(_,i)=>r.worldMatrices(i));
 const timings={collisionMs:r.collisionMs(),tickMs:r.tickMs(),intervalMs:r.intervalMs()};
 if(worldMatrices.length%16||![...positions,...worldMatrices,...Object.values(timings)].every(Number.isFinite))throw Error('实时点动姿态数据无效。');
 return {schemaVersion:1,token:r.token(),epoch:r.epoch(),resultSequence:r.resultSequence().toString(),processedSequence:r.processedSequence().toString(),mutationSequence:r.mutationSequence().toString(),status:r.status(),reason:r.reason()||'',positions,worldMatrices,ticks:r.ticks().toString(),...timings};
}
