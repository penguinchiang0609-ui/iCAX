import {encodeMachineJogControl,decodeMachineJogResult} from '../protocol/machineJogPDOProtocol.mjs';

// A dead-man signal is state: each new PDO version renews the native lease.
// Native time, not renderer event count, determines the actual joint movement.
export class MachineJogPDOStream {
 constructor(pdo,descriptor,{onResult=()=>{},onError=()=>{},requestFrame=cb=>requestAnimationFrame(cb),cancelFrame=id=>cancelAnimationFrame(id),now=()=>performance.now()}={}){
  if(!pdo?.enabled)throw Error('当前场景未启用实时点动通道。');
  this.pdo=pdo;this.descriptor=descriptor;this.onResult=onResult;this.onError=onError;this.requestFrame=requestFrame;this.cancelFrame=cancelFrame;this.now=now;
  this.sequence=0n;this.writtenSequence=0n;this.processedSequence=0n;this.resultSequence=0n;this.lastPublishedAt=-Infinity;this.held=true;
  this.stats={writes:0,busyWrites:0,results:0};
 }
 start(){if(!this.stopped&&this.frame==null&&!this.pulsing)this.frame=this.requestFrame(()=>{this.frame=null;void this.pulse();});return this;}
 async write(held){
  const sequence=++this.sequence,bytes=encodeMachineJogControl({token:this.descriptor.token,epoch:this.descriptor.epoch,sequence,held});
  const written=await this.pdo.withWriteDescriptor({...this.descriptor.controlPDO,dataVersion:String(sequence)},buffer=>{
   if(bytes.byteLength>buffer.byteLength)throw Error('实时点动状态超出通道容量。');new Uint8Array(buffer).set(bytes);return bytes.byteLength;
  });
  if(written){this.writtenSequence=sequence;this.lastPublishedAt=this.now();this.stats.writes++;}else this.stats.busyWrites++;
 }
 async pulse(){
  if(this.stopped||this.error||this.pulsing)return this.pulsing;
  this.pulsing=(async()=>{try{
   await Promise.all([
    this.held&&this.now()-this.lastPublishedAt>=60?this.write(true):null,
    (async()=>{
     const result=await this.pdo.withReadDescriptor(this.descriptor.resultPDO,(buffer,meta)=>{
      if(!buffer?.byteLength||meta?.dataVersion&&BigInt(meta.dataVersion)<=this.resultSequence)return null;return decodeMachineJogResult(buffer);
     });
     if(this.stopped||!result||result.token!==this.descriptor.token||result.epoch!==this.descriptor.epoch)return;
     const version=BigInt(result.resultSequence),processed=BigInt(result.processedSequence);
     if(version<=this.resultSequence||processed<this.processedSequence)return;
     if(processed>this.sequence)throw Error('实时点动回执序号超出当前手势。');
     this.resultSequence=version;this.processedSequence=processed;this.lastResult=result;this.stats.results++;
     this.onResult(result);
    })(),
   ]);
  }catch(error){if(!this.stopped){this.error=error;this.onError(error);}}
  finally{this.pulsing=null;if(!this.stopped&&!this.error&&this.held)this.start();}})();return this.pulsing;
 }
 async stop({release=false}={}){
  this.held=false;if(this.frame!=null)this.cancelFrame(this.frame);this.frame=null;
  await this.pulsing;
  // Release is one nonblocking snapshot attempt. Reliable Stop is still the
  // owner's responsibility and never waits for a busy slot or another tick.
  if(release&&!this.stopped&&!this.error)try{await this.write(false);}catch(error){this.error=error;}
  this.stopped=true;
 }
}
