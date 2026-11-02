import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {randomUUID} from 'node:crypto';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {cutPolicyDraft,processDraftDefaults} from '../../apps/five-axis-cam/webpage/processPolicyDraft.mjs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY);
mkdirSync(output,{recursive:true});
const native=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'src/x64/Debug');
const child=spawn(resolve(native,'FiveAxisCAMTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:native,windowsHide:true,env:{...process.env,ICAX_FIVE_AXIS_BRIDGE_BOX:'1'},stdio:['pipe','pipe','pipe']});
const waiting=new Map();let serial=0,log='';
child.stderr.on('data',b=>log+=String(b));
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__'))return;const response=JSON.parse(line.slice(12)),pending=waiting.get(response.id);if(!pending)return;waiting.delete(response.id);clearTimeout(pending.timer);response.ok?pending.resolve(response.result):pending.reject(Error(response.error));});
child.on('exit',code=>{for(const p of waiting.values()){clearTimeout(p.timer);p.reject(Error('Native bridge exited '+code));}waiting.clear();});
const rpc=message=>new Promise((resolve,reject)=>{const id=++serial;const timer=setTimeout(()=>{waiting.delete(id);reject(Error('Native request timed out: '+message.action));},120000);waiting.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(message.payload||{})}:{})})+'\n');});
const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState'});
const invoke=async(method,payload)=>{const d=await read();return rpc({action:'invoke',scope:'scene',method,payload:{commandId:randomUUID(),expected:d.expected,...payload}});};
try{
  const info=await rpc({action:'info'});await rpc({action:'prepare-axis-test'});
  let d=await read();const workpieceOne=d.workpieces[0].id,poseOne=d.activePlacementId;
  const policy=cutPolicyDraft({...processDraftDefaults,cutFeed:'20',cutPower:'700'});
  const prepared=await invoke('CamProcess.Prepare',{setupId:d.activeSetupId,placementId:poseOne,...policy});
  await invoke('CamProcess.Assign',{intentIds:d.intentToolpaths.map(c=>c.id),libraryData:prepared.process.value.libraryData,overrides:[]});
  await invoke('CamSetup.Manage',{placementId:poseOne,action:'create',name:'侧面位姿'});
  d=await read();const poseTwo=d.activePlacementId;
  let task=await invoke('CamWorkpiece.Import',{sourcePath:info.fixturePath});
  while(task.status==='Running'){await new Promise(r=>setTimeout(r,25));task=await rpc({action:'invoke',scope:'scene',method:'CamWorkpiece.QueryTask',payload:{epoch:task.epoch,taskId:task.taskId}});}
  if(task.status!=='Completed')throw Error(task.error||'Import did not complete');
  d=await read();const placementTwo=d.activePlacementId;
  await invoke('CamSetup.Manage',{placementId:placementTwo,action:'create',name:'第二工件位姿'});
  d=await read();const secondWorkpiece=d.workpieces.find(w=>w.id!==workpieceOne).id,secondPose=d.activePlacementId;
  await invoke('CamWorkpiece.Activate',{placementId:poseOne});
  const project=resolve(output,'CAM布局示例.i5cam');await rpc({action:'export-project',path:project});
  d=await read();writeFileSync(resolve(output,'fixture.json'),JSON.stringify({project,workpieceOne,poseOne,poseTwo,secondWorkpiece,secondPose,cutIds:d.intentToolpaths.map(c=>c.id),workpieces:d.workpieces.map(w=>({id:w.id,geometryRef:w.geometryRef})),placements:d.placements},null,2));
  console.log(JSON.stringify({prepared:true,project,cuts:d.intentToolpaths.length,workpieces:d.workpieces.length,poses:d.placements.length}));
}finally{try{await rpc({action:'exit'});}catch{}child.stdin.end();writeFileSync(resolve(output,'prepare-native.log'),log);}
