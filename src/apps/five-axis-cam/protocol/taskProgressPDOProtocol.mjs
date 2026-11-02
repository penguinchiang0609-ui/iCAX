import {Builder,ByteBuffer} from './runtime/flatbuffers.mjs';
import {TaskProgress} from './generated/task-progress.mjs';
import {unsignedSequence,pdoText,verifyPDOFlatBuffer} from './pdoFlatBuffer.mjs';

const statuses=new Set(['Running','Completed','Cancelled','Failed','Superseded']);
const detailKeys=new Set(['collisionProgress','repairProgress','machineProgress']);
function detailsOf(value){
 if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).some(k=>!detailKeys.has(k)||!value[k]||typeof value[k]!=='object'||Array.isArray(value[k])))throw Error('任务进度内容无效。');
 return value;
}
// Encoding is also used by the binary endpoint tests. This payload deliberately
// cannot contain a task result, trajectory frames, resources or scene snapshots.
export function encodeTaskProgress({taskId,epoch,sequence,status,kind='',progress=0,error='',terminationReason='',...details}){
 const seq=unsignedSequence(sequence);if(!seq||!statuses.has(status)||!Number.isInteger(Number(progress))||Number(progress)<0||Number(progress)>0xffffffff)throw TypeError('Invalid task telemetry');
 const builder=new Builder(512),strings=[pdoText(taskId,'taskId'),pdoText(epoch,'epoch'),status,kind,error,terminationReason,JSON.stringify(detailsOf(details))].map(s=>builder.createString(s));
 const root=TaskProgress.createTaskProgress(builder,1,strings[0],strings[1],seq,strings[2],strings[3],Number(progress),strings[4],strings[5],strings[6]);
 TaskProgress.finishTaskProgressBuffer(builder,root);const bytes=builder.asUint8Array().slice();if(bytes.length>65536)throw RangeError('Task telemetry exceeds its slot');return bytes;
}
export function decodeTaskProgress(input){
 const bytes=verifyPDOFlatBuffer(input,'F5TP',['u32','required','required','u64','required','string','u32','string','string','string']);
 if(bytes.length>65536)throw Error('任务进度超出通道容量。');
 const value=TaskProgress.getRootAsTaskProgress(new ByteBuffer(bytes));
 if(value.schemaVersion()!==1||!value.taskId()||!value.epoch()||!value.sequence()||!statuses.has(value.status()))throw Error('任务进度版本或身份无效。');
 const details=detailsOf(JSON.parse(value.detailsJson()||'{}'));
 return {schemaVersion:1,taskId:value.taskId(),epoch:value.epoch(),sequence:value.sequence().toString(),status:value.status(),kind:value.kind()||'',progress:String(value.progress()),error:value.error()||'',terminationReason:value.terminationReason()||'',...details};
}
