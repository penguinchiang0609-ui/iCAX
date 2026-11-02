import {Builder,ByteBuffer} from './runtime/flatbuffers.mjs';
import {MachineIKTarget} from './generated/machine-iktarget.mjs';
import {MachineIKResult} from './generated/machine-ikresult.mjs';

const U64_MAX=(1n<<64n)-1n;
const utf8=new TextDecoder('utf-8',{fatal:true});
const bad=()=>{throw Error('机床姿态 PDO 数据无效。');};
function unsigned(value){
 if(typeof value==='number'&&!Number.isSafeInteger(value))throw TypeError('PDO sequence must retain exact integer precision');
 if(!['number','bigint','string'].includes(typeof value)||typeof value==='string'&&!/^\d+$/.test(value))throw TypeError('PDO sequence must be an unsigned 64-bit integer');
 const n=BigInt(value);if(n<0n||n>U64_MAX)throw RangeError('PDO sequence is outside uint64');return n;
}
function text(value,name){if(typeof value!=='string'||!value||new TextEncoder().encode(value).length>1024)throw TypeError('PDO '+name+' is required');return value;}
function bytesOf(input){
 if(input instanceof ArrayBuffer)return new Uint8Array(input);
 if(ArrayBuffer.isView(input))return new Uint8Array(input.buffer,input.byteOffset,input.byteLength);
 throw TypeError('PDO result must be an ArrayBuffer or typed view');
}
// The generated TypeScript accessors do not include a verifier. Validate their
// exact table/string/vector bounds before using the generated schema types.
function verifyResult(bytes){
 if(bytes.length<8||bytes.length>1024*1024)bad();
 const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
 const range=(at,size)=>{if(!Number.isSafeInteger(at)||!Number.isSafeInteger(size)||at<0||size<0||at+size>bytes.length)bad();};
 const colliderTypes=['required','required','string','required','i32','required','bool'];
 const pairTypes=['collider','collider'];
 const verifyTable=(root,types)=>{
 range(root,4);const vt=root-data.getInt32(root,true);range(vt,4);
 const vtSize=data.getUint16(vt,true),objectSize=data.getUint16(vt+2,true);
 if(vtSize<4||vtSize%2||objectSize<4||vtSize>types.length*2+4)bad();range(vt,vtSize);range(root,objectSize);
 types.forEach((kind,index)=>{
  const entry=4+index*2,relative=entry<vtSize?data.getUint16(vt+entry,true):0;
  if(!relative){if(kind==='required'||kind==='collider')bad();return;}
  const width=kind==='bool'?1:['u64','double'].includes(kind)?8:4;
  if(relative<4||relative+width>objectSize)bad();const field=root+relative;range(field,width);
  if(['required','string','vector','pairs','collider'].includes(kind)){
   const offset=data.getUint32(field,true);if(offset<4)bad();const at=field+offset;range(at,4);const length=data.getUint32(at,true);
   if(kind==='collider'){verifyTable(at,colliderTypes);return;}
   if(kind==='pairs'){if(length>32)bad();range(at+4,length*4);for(let i=0;i<length;i++){const entry=at+4+i*4,offset=data.getUint32(entry,true);if(offset<4)bad();verifyTable(entry+offset,pairTypes);}return;}
   if(kind==='vector'){range(at+4,length*8);return;}
   range(at+4,length+1);if(data.getUint8(at+4+length)!==0)bad();utf8.decode(bytes.subarray(at+4,at+4+length));
  }
 });};
 const root=data.getUint32(0,true);if(root<8)bad();
 verifyTable(root,['u32','required','required','u64','u64','u64','u64','required','string','string','vector','vector','bool',...Array(8).fill('double'),'vector','pairs','bool']);
}
export function encodeMachineIKTarget({editToken,epoch,sequence,target,jointPosition=null}){
 const values=Array.from(target||[]);
 if(jointPosition!==null){if(!Number.isFinite(jointPosition)||values.length)throw TypeError('PDO joint target must be a finite scalar without a pose');}
 else if(values.length!==16||!values.every(v=>typeof v==='number'&&Number.isFinite(v)))throw TypeError('PDO target must contain 16 finite doubles');
 const seq=unsigned(sequence);if(seq===0n)throw RangeError('PDO target sequence must be positive');
 const builder=new Builder(256),token=builder.createString(text(editToken,'editToken')),scene=builder.createString(text(epoch,'epoch'));
 const matrix=MachineIKTarget.createTargetVector(builder,values);
 const root=MachineIKTarget.createMachineIKTarget(builder,1,token,scene,seq,matrix,jointPosition);
 MachineIKTarget.finishMachineIKTargetBuffer(builder,root);return builder.asUint8Array().slice();
}
export function decodeMachineIKResult(input){
 const bytes=bytesOf(input);verifyResult(bytes);const bb=new ByteBuffer(bytes);
 if(!MachineIKResult.bufferHasIdentifier(bb))bad();
 const r=MachineIKResult.getRootAsMachineIKResult(bb);if(r.schemaVersion()!==1)bad();
 const editToken=r.editToken(),epoch=r.epoch(),status=r.status();if(!editToken||!epoch||!status)bad();
 const readVector=(length,get)=>{const values=Array.from({length},(_,i)=>get(i));if(!values.every(Number.isFinite))bad();return values;};
 const positions=readVector(r.positionsLength(),i=>r.positions(i));
 const tipWorld=readVector(r.tipWorldLength(),i=>r.tipWorld(i));
 const worldMatrices=readVector(r.worldMatricesLength(),i=>r.worldMatrices(i));
 const unavailable=['Invalidated','Error'].includes(status)&&!positions.length&&!tipWorld.length&&!worldMatrices.length;
 if(!unavailable&&tipWorld.length!==16||worldMatrices.length%16)bad();
 const metrics=Object.fromEntries(['captureMs','prepareMs','solveMs','collisionMs','commitMs','totalMs','positionErrorMm','rotationErrorRadians'].map(name=>[name,r[name]()]));
 if(!Object.values(metrics).every(Number.isFinite))bad();
 const collider=value=>{if(!value)bad();const ref={ownerElementId:value.ownerElementId(),ownerElementName:value.ownerElementName(),boxId:value.boxId()||'',boxName:value.boxName(),boxIndex:value.boxIndex(),representation:value.representation(),editable:value.editable()};if(!ref.ownerElementId||!ref.ownerElementName||!ref.boxName||!['Box','OwnMesh'].includes(ref.representation)||ref.boxIndex < -1||ref.editable&&(ref.representation!=='Box'||ref.boxIndex<0))bad();return ref;};
 const collisionPairs=Array.from({length:r.collisionPairsLength()},(_,i)=>{const pair=r.collisionPairs(i);if(!pair)bad();return {first:collider(pair.first()),second:collider(pair.second())};});
 if(collisionPairs.length&&r.collisionStatus()!=='Blocked')bad();
 return {schemaVersion:1,editToken,epoch,resultSequence:r.resultSequence().toString(),processedSequence:r.processedSequence().toString(),appliedSequence:r.appliedSequence().toString(),mutationSequence:r.mutationSequence().toString(),status,collisionStatus:r.collisionStatus()||'',reason:r.reason()||'',positions,tipWorld,worldMatrices,collisionPairs,collisionDiagnosticsComplete:r.collisionDiagnosticsComplete(),cacheReused:r.cacheReused(),...metrics};
}
