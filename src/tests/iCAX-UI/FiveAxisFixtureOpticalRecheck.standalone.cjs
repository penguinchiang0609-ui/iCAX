'use strict';
// Standalone CEF acceptance. Only explicit staged methods may send UI input.
// A stopped tool call must not leave a background sequence of UI actions.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const assert=require('node:assert/strict');
const hash=x=>crypto.createHash('sha256').update(JSON.stringify(x)).digest('hex');
const same=(a,b,label)=>assert.equal(hash(a),hash(b),label);
const omit=(o,keys)=>Object.fromEntries(Object.entries(o||{}).filter(([k])=>!keys.includes(k)));
const setup=s=>s.setups.find(x=>x.id===s.activeSetupId);
const fileHash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
// Only derived verification metadata may differ. Every plate's complete
// geometry/outline/joinery/material record remains part of this exact oracle.
const assemblyAuthor=a=>a?omit(a,['cutClearance','validity','inputKey','geometryValidity','beamValidity','generationCutClearance']):a;
const designAuthor=d=>d?{...omit(d,['beamCheck']),assembly:assemblyAuthor(d.assembly),candidate:assemblyAuthor(d.candidate)}:d;
const beamTargets=g=>(g?.operations||[]).map(op=>({intentId:op.intentId,placementId:op.placementId,order:op.order,parts:op.parts.map(p=>({role:p.role,points:p.points}))}));
function authorFacts(state,policies,{generatedRefresh=false}={}){
 return {workpieces:state.workpieces,placements:state.placements,fixtures:state.fixtures,
  cuts:state.intentToolpaths,policies,
  units:state.fixtureUnits.map(u=>({...u,design:designAuthor(u.design)})),
  setups:state.setups.map(s=>({...omit(s,['fixtureBeamCheck']),fixtureDesign:designAuthor(s.fixtureDesign),
   generatedGeometry:generatedRefresh?beamTargets(s.generatedGeometry):omit(s.generatedGeometry,['sampleGeometryRef'])}))};
}
function assertTaskTransport(raw,startMethod,{queryMethod='CamSimulation.QueryTask'}={}){
 const starts=raw.calls.filter(c=>c.method===startMethod);assert.equal(starts.length,1,'One reliable Start');
 const receipt=starts[0].result;assert.ok(receipt?.taskId&&receipt.progressPDO,'Real progress PDO descriptor');
 const frames=raw.frames.filter(f=>f.progress.taskId===receipt.taskId);
 assert.ok(frames.length,'Actual decoded task PDO applications');
 const terminal=frames.find(f=>f.progress.status!=='Running');assert.ok(terminal,'Actual terminal PDO');
 assert.ok(frames.every(f=>f.dataVersion===f.progress.sequence),'PDO versions equal decoded sequence');
 assert.ok(frames.every(f=>!Object.hasOwn(f.progress,'result')&&!Object.hasOwn(f.progress,'frames')),'PDO carries progress, not complete results');
 if(startMethod==='CamSimulation.CheckBeamClearance')assert.ok(frames.some(f=>f.progress.collisionProgress?.unit==='SegmentPartPairs'),'Real segment × plate progress unit');
 const queries=raw.calls.filter(c=>c.method===queryMethod&&c.payload.taskId===receipt.taskId);
 assert.equal(queries.length,1,'Only one terminal reliable result query');
 assert.ok(queries[0].at>=terminal.at,'No QueryTask while Running');
 assert.equal(queries[0].result.status,'Completed');
 return{taskId:receipt.taskId,startCount:1,startReceiptMilliseconds:starts[0].elapsedMs,
  pdoApplications:frames.length,queryWhileRunning:0,terminalQueryCount:1,result:queries[0].result.result};
}
// Component references carry uint versions; the documented wire DTO uses text.
const ref=r=>r?{url:r.url,version:String(r.version)}:r;
const distance=(a,b)=>Math.hypot(...a.map((x,i)=>x-b[i]));
const unit=v=>{const length=Math.hypot(...v);assert.ok(Number.isFinite(length)&&length>0);return v.map(x=>x/length);};
const world=(m,p,direction=false)=>[0,1,2].map(i=>p.reduce((sum,x,j)=>sum+m[i*4+j]*x,direction?0:m[i*4+3]));
const axis=q=>[2*(q[0]*q[2]+q[1]*q[3]),2*(q[1]*q[2]-q[0]*q[3]),1-2*(q[0]*q[0]+q[1]*q[1])];
function expectedBeamSegments(state){
 const s=setup(state),follow=Number(s.rapidSettings.followHeightMm),out=[];
 for(const op of s.generatedGeometry.operations){const placement=state.placements.find(p=>p.id===op.placementId);assert.ok(placement);
  let last;
  for(const part of op.parts){assert.equal(part.points.length,part.directions.length);
   for(let i=0;i<part.points.length;i++){const target=world(placement.transform,part.points[i]),direction=unit(world(placement.transform,part.directions[i],true));
    const current={target,axis:direction,position:target.map((v,j)=>v+direction[j]*follow),role:part.role};
    if(last&&!(i===0&&distance(last.position,current.position)>1e-4))out.push({intentId:op.intentId,role:last.role,endRole:current.role,start:last,end:current});last=current;
   }
  }
 }
 return out;
}
function assertBeamProof(report,state,{mode,previous}={}){
 assert.equal(report.schema,'cam.fixture-beam-clearance');assert.equal(report.validity,'Current');
 assert.equal(report.scope,'CutAndLeadsOnly');assert.equal(report.headStatus,'NotChecked');assert.equal(report.rapidStatus,'Unchecked');
 assert.equal(report.representation,'ForwardCylinderMesh');assert.equal(report.meshClaim,'SuppliedTriangleMeshes');
 assert.equal(report.productionQualified,false);assert.ok(['Clear','Blocked','Unchecked'].includes(report.status));
 assert.equal(report.checkMode,mode);assert.ok(report.sourceSignature&&report.environmentSignature);
 const count=Number(report.totalSegmentCount),checked=Number(report.checkedSegmentCount),reused=Number(report.reusedSegmentCount);
 const fixtures=state.fixtures.filter(f=>f.enabled!==false&&f.collisionEnabled!==false),parts=report.parts;
 assert.equal(parts.length,fixtures.length,'Every enabled fixture own mesh is included');
 assert.equal(new Set(parts.map(p=>p.partId)).size,parts.length);assert.ok(parts.some(p=>p.role==='Support')&&parts.some(p=>p.role==='Base'));
 assert.equal(checked+reused,count);assert.equal(report.segmentProofs.length,count);assert.equal(Number(report.totalPairCount),count*parts.length);
 assert.equal(Number(report.checkedPairCount),checked*parts.length);assert.equal(Number(report.reusedPairCount),reused*parts.length);
 assert.equal(report.pairCertificates.length,count*parts.length);
 const indices=new Set(report.checkedSegmentIndices.map(Number));assert.equal(indices.size,checked);assert.ok([...indices].every(i=>i>=0&&i<count));
 if(mode==='Baseline'){assert.equal(checked,count,'First proof checks the whole real setup');assert.equal(reused,0);}
 else{assert.ok(previous,'Local proof requires saved previous certificate');assert.equal(previous.environmentSignature,report.environmentSignature,'Exact environment permits reuse');
  assert.ok(checked>0&&checked<count,'Actual local pose edit checks a strict subset');assert.ok(reused>0);
 }
 const expected=expectedBeamSegments(state);assert.equal(expected.length,count,'Report spans the actual generated cut/lead intervals');
 const rows=new Map(),proofs=report.segmentProofs;
 for(let i=0;i<count;i++){const p=proofs[i],e=expected[i];assert.equal(Number(p.index),i);assert.equal(p.intentId,e.intentId);assert.equal(p.role,e.role);assert.equal(p.endRole,e.endRole);
  assert.ok(distance(p.startTarget,e.start.target)<1e-6&&distance(p.endTarget,e.end.target)<1e-6,'Proof uses actual beam target geometry');
  assert.ok(distance(p.start.position,e.start.position)<1e-6&&distance(p.end.position,e.end.position)<1e-6,'Proof TCP follows actual IJK and follow height');
  assert.ok(distance(axis(p.start.rotation),e.start.axis)<1e-6&&distance(axis(p.end.rotation),e.end.axis)<1e-6,'Proof ray follows actual authored/generated directions');
  assert.ok(p.segmentKey&&p.poseSignature);
 }
 let blocked=0,unchecked=0;
 for(const row of report.pairCertificates){const i=Number(row.segmentIndex),key=row.partId+':'+i;
  assert.ok(i>=0&&i<count);assert.ok(parts.some(p=>p.partId===row.partId));assert.ok(!rows.has(key),'Unique actual segment/plate pair');rows.set(key,row);
  assert.ok(['Clear','Blocked','Unchecked'].includes(row.status));blocked+=row.status==='Blocked';unchecked+=row.status==='Unchecked';
 }
 for(const part of parts){const f=fixtures.find(f=>f.id===part.partId);assert.ok(f);same(ref(part.geometryRef),ref(f.geometryRef),'Own fixture mesh identity exact');same(part.transform,f.transform,'Own fixture world transform exact');
  const unitRecord=state.fixtureUnits.find(u=>u.id===part.unitId),original=unitRecord?.design?.assembly?.parts?.find(p=>p.partId===part.partId);assert.ok(original);assert.equal(part.role,original.role);
  assert.equal(Number(part.checkedSegmentCount),checked);assert.equal(Number(part.reusedSegmentCount),reused);
  let hits=0,unknown=0;for(let i=0;i<count;i++){const row=rows.get(part.partId+':'+i);assert.ok(row,'Each plate has every cut/lead interval covered');hits+=row.status==='Blocked';unknown+=row.status==='Unchecked';}
  assert.equal(Number(part.blockedSegmentCount),hits);assert.equal(Number(part.uncheckedSegmentCount),unknown);assert.equal(part.status,hits?'Blocked':unknown?'Unchecked':'Clear');
 }
 assert.equal(Number(report.blockedPairCount),blocked);assert.equal(Number(report.uncheckedPairCount),unchecked);assert.equal(report.status,blocked?'Blocked':unchecked?'Unchecked':'Clear');
 const issueKeys=report.issues.map(r=>r.partId+':'+r.segmentIndex);assert.equal(new Set(issueKeys).size,issueKeys.length);assert.equal(issueKeys.length,blocked+unchecked);
 for(const [key,row]of rows)assert.equal(issueKeys.includes(key),row.status!=='Clear','Every physical block/unknown remains visible');
 if(previous){
  const oldSegments=new Map(previous.segmentProofs.map(p=>[p.segmentKey,p])),oldRows=new Map(previous.pairCertificates.map(r=>[r.partId+':'+r.segmentIndex,r]));
  const dirty=new Set();for(let i=0;i<count;i++){const p=proofs[i],old=oldSegments.get(p.segmentKey);if(!old||old.poseSignature!==p.poseSignature||parts.some(part=>oldRows.get(part.partId+':'+old.index)?.status==='Unchecked'))dirty.add(i);}
  const expectedChecked=new Set(dirty);for(const i of dirty)for(const j of [Math.max(0,i-1),Math.min(count-1,i+1)])if(proofs[j].intentId===proofs[i].intentId)expectedChecked.add(j);
  same([...indices].sort((a,b)=>a-b),[...expectedChecked].sort((a,b)=>a-b),'Only actual changed/unverified intervals and immediate same-cut joins are checked');
  for(let i=0;i<count;i++)if(!indices.has(i)){const p=proofs[i],old=oldSegments.get(p.segmentKey);assert.ok(old);assert.equal(p.poseSignature,old.poseSignature);for(const part of parts){const row=rows.get(part.partId+':'+i),prior=oldRows.get(part.partId+':'+old.index);same(omit(row,['segmentIndex']),omit(prior,['segmentIndex']),'Unchanged pair certificate is reused exactly');}}
 }
 return{status:report.status,checkMode:report.checkMode,totalSegments:count,checkedSegments:checked,reusedSegments:reused,totalPairs:count*parts.length,
  supportParts:parts.filter(p=>p.role==='Support').length,baseParts:parts.filter(p=>p.role==='Base').length,blockedPairs:blocked,uncheckedPairs:unchecked,
  everyOwnMeshIdentityTransformAndPairCovered:true,actualGeneratedRayGeometryVerified:true};
}
async function attachAudit(page){
 await page.evaluate(async()=>{
  window.fixtureLightAudit?.restore?.();
  const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');
  const v=getProjectView(__icaxAppShell.getState().activeProjectId),client=v.viewportSceneProxy.project.product.sdoClient;
  const{PDOClient}=await import('../../../iCAX-UI/SDK/PDO/pdoClient.mjs');
  const a=window.fixtureLightAudit={client,invoke:client.invoke,PDOClient,read:PDOClient.prototype.withReadDescriptor,calls:[],frames:[],restored:false};
  client.invoke=function(channel,method,payload,...rest){const row={at:performance.now(),method,payload:structuredClone(payload)};a.calls.push(row);
   const promise=a.invoke.call(this,channel,method,payload,...rest);promise.then(result=>{row.elapsedMs=performance.now()-row.at;row.result=structuredClone(result);},error=>{row.error=error.message;});return promise;};
  PDOClient.prototype.withReadDescriptor=function(descriptor,read){return a.read.call(this,descriptor,(buffer,meta)=>{
   const result=read(buffer,meta);if(result?.taskId&&result.sequence)a.frames.push({at:performance.now(),dataVersion:String(meta?.dataVersion??''),progress:structuredClone(result)});return result;});};
  a.restore=()=>{if(a.restored)return;a.restored=true;client.invoke=a.invoke;PDOClient.prototype.withReadDescriptor=a.read;};
 });
}
async function create({page,project,output,candidateHash}){
 project=path.resolve(project).replaceAll('\\','/');output=path.resolve(output);assert.match(output,/^[cC]:[\\/]/);
 assert.ok(fs.existsSync(project));fs.mkdirSync(output,{recursive:true});
 const report={passed:false,project,output,candidateHash,startedAt:new Date().toISOString(),cases:[],coverage:{realStandaloneCEF:true,noDirectEngineeringMutation:true,fixtureRegeneration:false}};
 const write=()=>fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2));
 const session=await page.context().newCDPSession(page);await session.send('Emulation.setFocusEmulationEnabled',{enabled:false});
 const wait=(f,a,timeout=20000)=>page.waitForFunction(f,a,{timeout});let target;
 const bind=async()=>{await wait(async file=>{const a=__icaxAppShell.getState();if(a.activeProjectPath?.replaceAll('\\','/')!==file)return false;
  const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.fixtureLightView=getProjectView(a.activeProjectId);
  return !!fixtureLightView?.viewportSceneProxy&&!fixtureLightView.pending;},project);
  await wait(()=>!!fixtureLightView.scene?.domain&&!fixtureLightView.pending);
  target=await page.evaluate(()=>{window.fixtureLightProduct=fixtureLightView.viewportSceneProxy.project.product;return __icaxAppShell.getState().activeProjectId;});};
 const idle=()=>wait(()=>!fixtureLightView.pending&&!fixtureLightView.fiveHistoryBusy&&!fixtureLightView.fiveFixtureLight?.running&&!__icaxAppShell.getState().pendingCount);
 const read=()=>page.evaluate(async()=>{const p=fixtureLightView.viewportSceneProxy,{hydratePathSamples}=await import('../../../apps/five-axis-cam/webpage/pathSampleResource.mjs');return hydratePathSamples(await p.invoke('CamScene.ReadState',{}),p);});
 const policies=state=>page.evaluate(state=>fixtureLightView.viewportSceneProxy.invoke('CamProcess.Read',{expected:state.expected,intentIds:state.intentToolpaths.map(c=>c.id)}),state);
 const history=()=>page.evaluate(()=>fixtureLightView.viewportSceneProxy.getUndoRedoState());
 const readBeam=()=>page.evaluate(()=>fixtureLightView.viewportSceneProxy.invoke('CamSimulation.ReadBeamClearance',{setupId:fixtureLightView.scene.domain.activeSetupId}));
 const catalogue=()=>page.evaluate(async()=>{const p=fixtureLightProduct,s=await p.sdoClient.invoke(p.productChannelId,'Product.GetState',{});return[...new Set(s.catalogs.flatMap(c=>[c.mainProject,...c.projects]).filter(Boolean).map(p=>p.projectId))].sort();});
 const others=ids=>page.evaluate(async ids=>{const p=fixtureLightProduct,{hydratePathSamples}=await import('../../../apps/five-axis-cam/webpage/pathSampleResource.mjs'),out=[];
  for(const id of ids){const proxy=p.projects.get(id)?.getMainScene();if(!proxy)throw Error('Other project missing '+id);const state=await hydratePathSamples(await proxy.invoke('CamScene.ReadState',{}),proxy);
   for(const s of state.setups??[])if(s.generatedGeometry)delete s.generatedGeometry.sampleGeometryRef;out.push({id,state,machines:await proxy.invoke('Machine.List',{})});}return out;},ids);
 const mesh=async()=>{await wait(()=>{const v=fixtureLightView,a=v.areas[v.activeAreaId],s=a?.viewContent?.snapshot;return a?.viewContent?.viewportReceipt?.applied&&!a.viewApplyByRevision?.size&&v.appliedViewportSnapshotKey===`${s?.viewId}:${a.appliedViewRevision}`;});
  return page.evaluate(()=>{const v=fixtureLightView;v.viewport.scene.updateMatrixWorld(true);return[...v.viewport.sceneObjects].filter(([id,o])=>o.geometry?.attributes.position).map(([id,o])=>({id,visible:o.visible,positions:Array.from(o.geometry.attributes.position.array),index:o.geometry.index?Array.from(o.geometry.index.array):null,world:o.matrixWorld.toArray()})).sort((a,b)=>a.id.localeCompare(b.id));});};
 let before;
 return{report,write,bind,idle,read,policies,history,readBeam,catalogue,others,mesh,
  async prepare({opticalValidity='Stale'}={}){
   await page.evaluate(file=>__icaxAppShell.openProject(file,'icax.five-axis-cam'),project);await bind();
   await page.locator('[data-tab-id="toolpath"]').click();await idle();
   const state=await read(),policy=await policies(state),all=await catalogue(),otherIds=all.filter(id=>id!==target);
   before={state,policies:policy,history:await history(),mesh:await mesh(),otherProjectIds:otherIds,otherProjectFacts:await others(otherIds),fileHash:fileHash(project)};
   assert.equal(setup(state).simulationCheck.validity,'Current');assert.equal(setup(state).simulationCheck.status,'Clear');
   assert.equal(setup(state).fixtureDesign.assembly.cutClearance.validity,opticalValidity);
   const roles=setup(state).fixtureDesign.assembly.parts.map(p=>p.role);assert.ok(roles.includes('Support')&&roles.includes('Base'));
   fs.writeFileSync(path.join(output,'before-native.json'),JSON.stringify(before));
   report.initial={projectId:target,headStatus:'Clear',headValidity:'Current',opticalValidity,parts:setup(state).fixtureDesign.assembly.parts.map(p=>({partId:p.partId,role:p.role,geometryRef:p.geometryRef})),meshCount:before.mesh.length};write();return report.initial;
  },
  async openFromAlert(){const unit=before.state.fixtureUnits.find(u=>u.setupId===before.state.activeSetupId&&u.enabled!==false);assert.ok(unit);
   await page.locator(`[data-ribbon-alert-id="fixture-light:${unit.id}"]`).click();await idle();
   assert.equal(await page.locator('[data-five-fixture-light-dialog]').count(),1,'Dedicated light-clearance processing window');
   same(authorFacts(await read(),await policies(before.state)),authorFacts(before.state,before.policies),'Opening warning changes no author facts');
   return{unitId:unit.id};
  },
  async begin(){await attachAudit(page);await page.locator('[data-five-command="five.fixture.light-start"]').click();},
  async status(){return page.evaluate(()=>{const s=fixtureLightView.fiveFixtureLight;return{running:s?.running,phase:s?.phase,task:s?.task?.status,kind:s?.task?.kind,progress:s?.task?.collisionProgress,message:s?.message,pdoApplications:fixtureLightAudit?.frames?.length||0};});},
  async capture(){return page.evaluate(()=>({calls:fixtureLightAudit.calls,frames:fixtureLightAudit.frames}));},
  async snapshot(name){await idle();const state=await read(),policy=await policies(state);
   const value={state,policies:policy,beam:await readBeam(),history:await history(),mesh:await mesh(),fileHash:fileHash(project)};
   if(name){assert.match(name,/^[a-z0-9-]+$/);fs.writeFileSync(path.join(output,name+'.json'),JSON.stringify(value));}return value;},
  async unchanged(reference=before,{generatedRefresh=false}={}){const state=await read(),policy=await policies(state);same(authorFacts(state,policy,{generatedRefresh}),authorFacts(reference.state,reference.policies,{generatedRefresh}),'Verification changes no geometry, author parameters, cutting poses or body certificate');same(await history(),reference.history,'Derived verification adds no author-edit history');return{state,policies:policy};},
  async closeTarget(){await idle();await page.locator(`[data-action="close-open-project"][data-project-id="${target}"]`).click();},
  async directCloseChoice(){await page.locator('[data-project-close-choice="discard"]').click();
   await wait(id=>!fixtureLightProduct.projects.has(id),target);same(await catalogue(),before.otherProjectIds,'Only supplied test target closes');
   same(await others(before.otherProjectIds),before.otherProjectFacts,'Other project facts preserved while target is closed');},
  async reopen(){await page.evaluate(file=>__icaxAppShell.openProject(file,'icax.five-axis-cam'),project);await bind();await idle();},
  async showCam(){await page.locator('[data-tab-id="toolpath"]').click();await idle();},
  async verifyReopened(reference){const state=await read(),policy=await policies(state);
   same(authorFacts(state,policy),authorFacts(reference.state,reference.policies),'Saved author parameters, geometry, generated directions and body report exact');
   for(const original of reference.state.setups){const current=state.setups.find(s=>s.id===original.id);
    same(current.fixtureDesign,original.fixtureDesign,'Saved setup optical result and dependency proof exact');}
   same(state.fixtureUnits,reference.state.fixtureUnits,'Saved fixture-unit optical results exact');
   same(omit(await readBeam(),['expected']),omit(reference.beam,['expected']),'Saved detailed optical segment/pair certificates exact');
   same(await mesh(),reference.mesh,'Actual same-area mesh buffers/indices/world matrices exact');
   same((await catalogue()).filter(id=>id!==target),before.otherProjectIds,'Complete other project set remains exact');
   same(await others(before.otherProjectIds),before.otherProjectFacts,'Other project native facts remain exact after reopen');
   return{state,policies:policy};},
  async dispose(){await page.evaluate(()=>{window.fixtureLightAudit?.restore?.();delete window.fixtureLightAudit;});}
 };
}
module.exports={create,authorFacts,assertTaskTransport,assertBeamProof,expectedBeamSegments,same,hash,fileHash};
if(require.main===module){
 console.error('Use the explicit staged acceptance API with an observed private CEF target. No UI actions were run.');
 process.exitCode=2;
}
