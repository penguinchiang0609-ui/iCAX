const utf8=new TextDecoder('utf-8',{fatal:true});
const bad=()=>{throw Error('实时过程数据无效。');};
export function unsignedSequence(value){
 if(typeof value==='number'&&!Number.isSafeInteger(value))throw TypeError('PDO sequence must retain exact integer precision');
 if(!['number','bigint','string'].includes(typeof value)||typeof value==='string'&&!/^\d+$/.test(value))throw TypeError('PDO sequence must be an unsigned 64-bit integer');
 const n=BigInt(value);if(n<0n||n>=(1n<<64n))throw RangeError('PDO sequence is outside uint64');return n;
}
export function pdoText(value,name){if(typeof value!=='string'||!value||new TextEncoder().encode(value).length>1024)throw TypeError('PDO '+name+' is required');return value;}
// Generated JS accessors do not provide a verifier. Check their complete byte
// boundary and field types before reading, while the short lease is held.
export function verifyPDOFlatBuffer(input,identifier,types){
 const bytes=input instanceof ArrayBuffer?new Uint8Array(input):ArrayBuffer.isView(input)?new Uint8Array(input.buffer,input.byteOffset,input.byteLength):null;
 if(!bytes||bytes.length<8||bytes.length>1024*1024||String.fromCharCode(...bytes.subarray(4,8))!==identifier)bad();
 const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
 const range=(at,size)=>{if(!Number.isSafeInteger(at)||!Number.isSafeInteger(size)||at<0||size<0||at+size>bytes.length)bad();};
 const root=data.getUint32(0,true);range(root,4);if(root<8)bad();
 const vt=root-data.getInt32(root,true);range(vt,4);const vtSize=data.getUint16(vt,true),objectSize=data.getUint16(vt+2,true);
 if(vtSize<4||vtSize%2||objectSize<4||vtSize>4+types.length*2)bad();range(vt,vtSize);range(root,objectSize);
 types.forEach((kind,index)=>{
  const entry=4+index*2,relative=entry<vtSize?data.getUint16(vt+entry,true):0;if(!relative){if(kind==='required')bad();return;}
  const width=kind==='bool'?1:['u64','double'].includes(kind)?8:4;
  if(relative<4||relative+width>objectSize)bad();const field=root+relative;range(field,width);
  if(['required','string','vector'].includes(kind)){
   const offset=data.getUint32(field,true);if(offset<4)bad();const at=field+offset;range(at,4);const length=data.getUint32(at,true);
   if(kind==='vector'){range(at+4,length*8);return;}
   range(at+4,length+1);if(data.getUint8(at+4+length)!==0)bad();utf8.decode(bytes.subarray(at+4,at+4+length));
  }
 });return bytes;
}
