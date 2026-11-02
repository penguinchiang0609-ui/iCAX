import { cutPolicyDraft, processDraftDefaults } from '../../../apps/five-axis-cam/webpage/processPolicyDraft.mjs';
import { resolve } from 'node:path';

export async function prepareDiscreteScene(rpc,sourcePath,output) {
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const wait=async(task,method)=>{while(task.status==='Running'){await new Promise(r=>setTimeout(r,50));task=await invoke(method,{epoch:task.epoch,taskId:task.taskId});}if(task.status!=='Completed')throw Error(task.error||task.status);return task;};
  const request=s=>({commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId});
  let s=await invoke('CamScene.ReadState');
  await wait(await invoke('CamWorkpiece.Import',{...request(s),sourcePath:resolve(sourcePath)}),'CamWorkpiece.QueryTask');
  s=await invoke('CamScene.ReadState');const p=s.placements[0],setup=s.setups.find(v=>v.id===s.activeSetupId),digest='discrete-playback-test';
  const extracted=await wait(await invoke('CamCut.ExtractCandidates',{...request(s),placementId:p.id,sourceGeometry:s.workpieces[0].geometryRef,extractionRuleRef:setup.planningProfileRef,selection:{mode:'Automatic'},geometryOnly:true,representation:'SkinSurface',workpieceFamily:'SheetSurface',side:{outwardSign:1},requestedKinds:['GeneralContour','Circle','Rectangle','Slot'],draftId:crypto.randomUUID(),draftRevision:'1',draftDigest:digest}),'CamPlan.QueryStatus');
  const candidates=await invoke('CamCut.QueryCandidates',{epoch:extracted.epoch,candidateSet:extracted.result.candidateSet,pageSize:'100'});
  const policy=await invoke('CamProcess.Prepare',{...request(s),placementId:p.id,...cutPolicyDraft(processDraftDefaults)});
  await invoke('CamCut.Create',{...request(s),placementId:p.id,candidateSet:extracted.result.candidateSet,candidateIds:candidates.candidates.filter(c=>c.confirmationEligibility==='Ready').map(c=>c.candidateId),draftDigest:digest,decisions:[],process:policy.process,pose:policy.pose});
  s=await invoke('CamScene.ReadState');await wait(await invoke('CamPlan.Generate',request(s)),'CamPlan.QueryStatus');
  await rpc({action:'export-project',path:resolve(output,'part45-discrete-generated.i5cam')});
}
