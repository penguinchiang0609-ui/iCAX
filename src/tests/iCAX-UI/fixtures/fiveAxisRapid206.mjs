import { cutPolicyDraft, processDraftDefaults } from '../../../apps/five-axis-cam/webpage/processPolicyDraft.mjs';
import { parseRenderGeometryResource } from '../../../iCAX-UI/SDK/Viewport/renderResource.mjs';
import { resolve } from 'node:path';

export async function prepareRapid206(rpc,output) {
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const wait=async(task,method)=>{while(task.status==='Running'){await new Promise(r=>setTimeout(r,30));task=await invoke(method,{epoch:task.epoch,taskId:task.taskId});}if(task.status!=='Completed')throw Error(task.error||task.status);return task;};
  const request=s=>({commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId});
  let s=await invoke('CamScene.ReadState');
  await wait(await invoke('CamWorkpiece.Import',{...request(s),sourcePath:resolve('TestData/FA/刀路/刀路/整理测试刀路/多种斜孔-多种边-206.STEP')}),'CamWorkpiece.QueryTask');
  s=await invoke('CamScene.ReadState');const p=s.placements[0],w=s.workpieces[0],setup=s.setups.find(v=>v.id===s.activeSetupId);
  const faceMap=await invoke('CamPlacement.ReadReferenceFaces',{expected:s.expected,setupId:s.activeSetupId,placementId:p.id});
  const resource=await rpc({action:'resource',url:w.previewRef.url,version:w.previewRef.version,head:false});
  const mesh=parseRenderGeometryResource(Uint8Array.from(resource.bytes).buffer),faces=[];
  const vertex=i=>mesh.positions.slice(i*3,i*3+3);
  for(const face of faceMap.faces){let area=0,z=0,nz=0;
    for(let i=Number(face.first);i<Number(face.first)+Number(face.count);++i){const a=vertex(mesh.indices[i*3]),b=vertex(mesh.indices[i*3+1]),c=vertex(mesh.indices[i*3+2]);
      const u=b.map((v,j)=>v-a[j]),v=c.map((x,j)=>x-a[j]),n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]],weight=Math.hypot(...n)*.5;
      area+=weight;z+=weight*(a[2]+b[2]+c[2])/3;nz+=n[2]*.5;
    }faces.push({...face,area,z:z/area,nz:nz/area});
  }
  const top=faces.filter(f=>f.nz>.95).sort((a,b)=>b.area-a.area||b.z-a.z)[0];if(!top)throw Error('No top face in 206 fixture');
  const sources=await invoke('CamCut.QuerySources',{expected:s.expected,setupId:s.activeSetupId,placementId:p.id});
  const extracted=await wait(await invoke('CamCut.ExtractCandidates',{...request(s),placementId:p.id,sourceGeometry:w.geometryRef,extractionRuleRef:setup.planningProfileRef,selection:{mode:'Faces',faceUses:sources.faceUses.filter(f=>f.localId===top.faceId)},geometryOnly:true,representation:'Solid',workpieceFamily:'GeneralSolid',side:{outwardSign:1},requestedKinds:['GeneralContour','Circle','Rectangle','Slot'],draftId:crypto.randomUUID(),draftRevision:'1',draftDigest:'rapid-206'}),'CamPlan.QueryStatus');
  const candidates=await invoke('CamCut.QueryCandidates',{epoch:extracted.epoch,candidateSet:extracted.result.candidateSet,pageSize:'100'});
  const policy=await invoke('CamProcess.Prepare',{...request(s),placementId:p.id,...cutPolicyDraft(processDraftDefaults)});
  await invoke('CamCut.Create',{...request(s),placementId:p.id,candidateSet:extracted.result.candidateSet,candidateIds:candidates.candidates.filter(c=>c.confirmationEligibility==='Ready'&&c.machiningKind==='Cut').map(c=>c.candidateId),draftDigest:'rapid-206',decisions:[],process:policy.process,pose:policy.pose});
  s=await invoke('CamScene.ReadState');await wait(await invoke('CamPlan.Generate',request(s)),'CamPlan.QueryStatus');
  await rpc({action:'export-project',path:resolve(output,'206-generated.i5cam')});
  return {topFace:top.faceId,candidates:candidates.candidates};
}
