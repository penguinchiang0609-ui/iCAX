import {encodeMachineIKTarget,decodeMachineIKResult} from '../protocol/machineIKPDOProtocol.mjs';

// A stream is a pair of overwritable snapshots, never a queue of pointer
// requests. The only reliable operations belong to its owner (Begin/End).
export class MachineIKPDOStream {
 constructor(pdo,descriptor,{onResult=()=>{},onError=()=>{},requestFrame=cb=>requestAnimationFrame(cb),cancelFrame=id=>cancelAnimationFrame(id),timeoutMs=5000}={}){
  if(!pdo?.enabled)throw Error('当前场景未启用实时姿态通道。');
  this.pdo=pdo;this.descriptor=descriptor;this.onResult=onResult;this.onError=onError;
  this.requestFrame=requestFrame;this.cancelFrame=cancelFrame;this.timeoutMs=timeoutMs;
  this.sequence=0n;this.writtenSequence=0n;this.processedSequence=0n;this.resultSequence=0n;
  this.waiters=new Set();this.stats={targetChanges:0,overwrittenTargets:0,writes:0,busyWrites:0,results:0};
 }
 start(){if(!this.stopped&&this.frame==null&&!this.pulsing)this.frame=this.requestFrame(()=>{this.frame=null;void this.pulse();});return this;}
 update(target){
  if(this.stopped||this.error)throw this.error||Error('实时姿态通道已结束。');
  const joint=this.descriptor.motionMode==='FK';
  if(joint?!Number.isFinite(target):target?.length!==16||!Array.from(target).every(Number.isFinite))throw Error(joint?'关节目标必须是有效数值。':'目标姿态必须是有效的四阶矩阵。');
  if(this.latest&&this.latest.sequence>this.writtenSequence)this.stats.overwrittenTargets++;
  this.latest={sequence:++this.sequence,...(joint?{jointPosition:target}:{target:Array.from(target)})};this.stats.targetChanges++;this.start();return this.sequence;
 }
 async pulse(){
  if(this.stopped||this.error||this.pulsing)return this.pulsing;
  this.pulsing=(async()=>{try{
   const latest=this.latest;
   await Promise.all([
    (async()=>{
     if(!latest||latest.sequence<=this.writtenSequence)return;
     const bytes=encodeMachineIKTarget({editToken:this.descriptor.editToken,epoch:this.descriptor.epoch,...latest});
     const written=await this.pdo.withWriteDescriptor({...this.descriptor.targetPDO,dataVersion:String(latest.sequence)},buffer=>{
      if(bytes.byteLength>buffer.byteLength)throw Error('实时目标超出通道容量。');new Uint8Array(buffer).set(bytes);return bytes.byteLength;
     });
     if(written){this.writtenSequence=latest.sequence;this.stats.writes++;}else this.stats.busyWrites++;
    })(),
    (async()=>{
     const result=await this.pdo.withReadDescriptor(this.descriptor.resultPDO,(buffer,meta)=>{
      if(!buffer?.byteLength||meta?.dataVersion&&BigInt(meta.dataVersion)<=this.resultSequence)return null;
      return decodeMachineIKResult(buffer);
     });
     if(this.stopped||!result)return;
     if(result.editToken!==this.descriptor.editToken||result.epoch!==this.descriptor.epoch)return;
     const version=BigInt(result.resultSequence),processed=BigInt(result.processedSequence);
     if(version<=this.resultSequence||processed<this.processedSequence)return;
     if(processed>this.sequence)throw Error('实时姿态回执序号超出本次拖动。');
     this.resultSequence=version;this.processedSequence=processed;this.lastResult=result;this.stats.results++;
     this.onResult(result);
     if(result.status==='Invalidated'||result.status==='Error')throw Error(result.reason||'机床配置已变化，请重新开始调整。');
    })(),
   ]);
   this.settle();
  }catch(error){if(!this.stopped){this.error=error;this.settle();this.onError(error);}}
  finally{this.pulsing=null;if(!this.stopped&&!this.error)this.start();}})();return this.pulsing;
 }
 settle(){for(const waiter of this.waiters)if(this.error||this.writtenSequence>=waiter.sequence&&this.processedSequence>=waiter.sequence){clearTimeout(waiter.timer);this.waiters.delete(waiter);if(this.error)waiter.reject(this.error);else waiter.resolve(this.lastResult);}}
 flush(){
  if(this.error)return Promise.reject(this.error);
  const sequence=this.sequence;if(this.writtenSequence>=sequence&&this.processedSequence>=sequence)return Promise.resolve(this.lastResult);
  if(this.stopped)return Promise.reject(Error('实时姿态通道已结束。'));
  this.start();return new Promise((resolve,reject)=>{const waiter={sequence,resolve,reject};waiter.timer=setTimeout(()=>{this.waiters.delete(waiter);reject(Error('末次拖动目标尚未得到机床回执，已取消本次调整。'));},this.timeoutMs);this.waiters.add(waiter);});
 }
 async stop(){
  this.stopped=true;if(this.frame!=null)this.cancelFrame(this.frame);this.frame=null;
  await this.pulsing;
  for(const waiter of this.waiters){clearTimeout(waiter.timer);waiter.reject(Error('实时姿态通道已结束。'));}this.waiters.clear();
 }
}
