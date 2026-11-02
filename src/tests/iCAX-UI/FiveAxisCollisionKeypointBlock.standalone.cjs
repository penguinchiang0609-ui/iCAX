'use strict';
// Explicit disposable project; only rendered controls write engineering data.
// Exports staged operations so a stopped Computer Use turn has no queued UI steps.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const assert=require('node:assert/strict'),{isDeepStrictEqual}=require('node:util');
const hash=x=>crypto.createHash('sha256').update(JSON.stringify(x)).digest('hex');
const same=(a,b,label)=>assert.equal(hash(a),hash(b),label);
const setup=s=>s.setups.find(x=>x.id===s.activeSetupId);
const omit=(value,keys)=>Object.fromEntries(Object.entries(value).filter(([k])=>!keys.includes(k)));
const source=s=>({workpieces:s.workpieces,placements:s.placements,fixtures:s.fixtures,fixtureUnits:s.fixtureUnits,
 cuts:s.intentToolpaths.map(c=>omit(c,['poseRef','inputRevision'])),
 setups:s.setups.map(x=>({id:x.id,headModel:x.headModel,rapidSettings:x.rapidSettings,definitionOrder:x.definitionOrder,cutGroups:x.cutGroups,machineSetup:x.machineSetup}))});
const editable=(state,policies)=>({source:source(state),policies,
 poses:state.intentToolpaths.map(c=>({id:c.id,poseRef:c.poseRef})),
 generated:state.setups.map(x=>({id:x.id,geometry:omit(x.generatedGeometry||{},['sampleGeometryRef'])}))});

function validateBlockPolicies(before,after,result){
 const controls=result.poseSearch?.collisionControls;
 assert.ok(Array.isArray(controls)&&controls.length,'Actual collision control metadata required');
 const changes=(result.adjustments||[]).flatMap(a=>(a.keypointEdits||[]).map(e=>({...e,intentId:a.intentId})));
 assert.ok(changes.length,'Accepted actual keypoint diffs required');
 const changed=[];
 assert.equal(after.items.length,before.items.length);
 for(const old of before.items){
  const current=after.items.find(x=>x.intentId===old.intentId);assert.ok(current);
  same(omit(current,['pose','posePolicy']),omit(old,['pose','posePolicy']),'All process assignments remain exact');
  same(omit(current.posePolicy,['keypoints']),omit(old.posePolicy,['keypoints']),'Pose mode and global constraints remain exact');
  const previous=old.posePolicy.keypoints||[],next=current.posePolicy.keypoints||[],edits=changes.filter(e=>e.intentId===old.intentId);
  assert.equal(new Set(next.map(p=>p.id)).size,next.length,'Unique actual point IDs');
  for(const point of previous){
   const value=next.find(p=>p.id===point.id);assert.ok(value,'Existing manual point ID cannot disappear');
   same(omit(value,['direction']),omit(point,['direction']),'Old anchor/transition/influence/ID remain exact');
   const edited=edits.find(e=>e.pointId===point.id);
   if(!isDeepStrictEqual(value.direction,point.direction)){
    assert.equal(edited?.action,'Updated','Only declared collision controls can change direction');
    assert.ok(controls.some(c=>c.existingPointId===point.id),'Updated point belongs to actual collision block');
    same(edited.sourceAnchor,point.anchor,'Updated control keeps its original source anchor');
    changed.push({intentId:old.intentId,id:point.id,action:'Updated',anchor:point.anchor});
   }else assert.ok(!edited,'Declared update must change the actual stored direction');
  }
  for(const point of next.filter(p=>!previous.some(x=>x.id===p.id))){
   const edit=edits.find(e=>e.pointId===point.id);assert.equal(edit?.action,'Inserted');
   same(edit.sourceAnchor,point.anchor,'Inserted override stays at its original automatic control');
   assert.ok(controls.some(c=>!c.existingPointId&&isDeepStrictEqual(c.sourceAnchor,point.anchor)),'New point only at an actual automatic collision control');
   assert.ok(!previous.some(p=>isDeepStrictEqual(p.anchor,point.anchor)),'Existing anchor must retain its old ID');
   changed.push({intentId:old.intentId,id:point.id,action:'Inserted',anchor:point.anchor});
  }
  assert.equal(next.length,previous.length+edits.filter(e=>e.action==='Inserted').length);
  if(!edits.length)same(current,old,'Unrelated cut policy and stored refs remain exact');
 }
 assert.equal(changed.length,changes.length,'Reported and actual keypoint edits agree exactly');
 return{controls,edits:changed,originalIdsAnchorsAndOtherFieldsExact:true,outsideControlDirectionsExact:true};
}

function taskEvidence(raw){
 const start=raw.calls.filter(c=>c.method==='CamSimulation.AutoRepairCollision');assert.equal(start.length,1);
 const receipt=start[0].result;assert.ok(receipt.progressPDO);
 const frames=raw.frames.filter(f=>f.progress.taskId===receipt.taskId),terminal=frames.find(f=>f.progress.status!=='Running');
 assert.ok(frames.length&&terminal,'Actual terminal PDO required');
 assert.ok(frames.every(f=>f.dataVersion===f.progress.sequence));
 assert.ok(frames.every(f=>!Object.hasOwn(f.progress,'result')&&!Object.hasOwn(f.progress,'frames')));
 const queries=raw.calls.filter(c=>c.method==='CamSimulation.QueryTask'&&c.payload.taskId===receipt.taskId);
 assert.equal(queries.length,1);assert.ok(queries[0].at>=terminal.at);assert.equal(queries[0].result.status,'Completed');
 return{startCount:1,startReceiptMilliseconds:start[0].elapsedMs,pdoApplications:frames.length,queryWhileRunning:0,terminalQueryCount:1,visibleStages:raw.visible};
}

async function create({page,project,output,maxDeviation=15,index=2661,candidateHash}){
 project=path.resolve(project).replaceAll('\\','/');output=path.resolve(output);assert.match(output,/^[cC]:[\\/]/);
 assert.ok(fs.existsSync(project));fs.mkdirSync(output,{recursive:true});page.setDefaultTimeout(30000);
 const report={passed:false,project,output,candidateHash,startedAt:new Date().toISOString(),cases:[],coverage:{realStandaloneCEF:true,noDirectEngineeringMutation:true,scope:'Configured head boxes versus actual workpiece/fixture meshes; no controller qualification'}};
 const write=()=>fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2));
 const fileHash=()=>crypto.createHash('sha256').update(fs.readFileSync(project)).digest('hex');
 const session=await page.context().newCDPSession(page);await session.send('Emulation.setFocusEmulationEnabled',{enabled:false});
 const wait=(f,a,ms=30000)=>page.waitForFunction(f,a,{timeout:ms});let target;
 const bind=async()=>{
  await wait(async file=>{const a=__icaxAppShell.getState();if(a.activeProjectPath?.replaceAll('\\','/')!==file)return false;
   const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.blockRepairView=getProjectView(a.activeProjectId);return !!blockRepairView?.viewportSceneProxy&&!blockRepairView.pending;},project);
  await wait(()=>!!blockRepairView.scene?.domain&&!blockRepairView.pending);
  target=await page.evaluate(()=>{window.blockStableProduct=blockRepairView.viewportSceneProxy.project.product;return __icaxAppShell.getState().activeProjectId;});
 };
 const idle=()=>wait(()=>{const v=blockRepairView,s=v.fiveSimulation;return !v.pending&&!v.fiveHistoryBusy&&!s?.checking&&!s?.repairing&&!__icaxAppShell.getState().pendingCount;});
 const area=async()=>{await page.locator('[data-tab-id="toolpath"]').click();await idle();if(!await page.locator('[data-five-key="collision-dialog"]').count())await page.locator('[data-command-id="five.tool.collision"]').click();await idle();await wait(()=>blockRepairView.fiveSimulation?.frames?.length>0);};
 const read=()=>page.evaluate(async()=>{const p=blockRepairView.viewportSceneProxy,{hydratePathSamples}=await import('../../../apps/five-axis-cam/webpage/pathSampleResource.mjs');return hydratePathSamples(await p.invoke('CamScene.ReadState',{}),p);});
 const policies=state=>page.evaluate(state=>blockRepairView.viewportSceneProxy.invoke('CamProcess.Read',{expected:state.expected,intentIds:state.intentToolpaths.map(c=>c.id)}),state);
 const history=()=>page.evaluate(()=>blockRepairView.viewportSceneProxy.getUndoRedoState());
 const ids=()=>page.evaluate(async()=>{const p=blockStableProduct,s=await p.sdoClient.invoke(p.productChannelId,'Product.GetState',{});return[...new Set(s.catalogs.flatMap(c=>[c.mainProject,...c.projects]).filter(Boolean).map(p=>p.projectId))].sort();});
 const otherFacts=ids=>page.evaluate(async ids=>{const p=blockStableProduct,{hydratePathSamples}=await import('../../../apps/five-axis-cam/webpage/pathSampleResource.mjs'),out=[];for(const id of ids){const proxy=p.projects.get(id)?.getMainScene();if(!proxy)throw Error('Other project missing '+id);const s=await hydratePathSamples(await proxy.invoke('CamScene.ReadState',{}),proxy);for(const x of s.setups||[])if(x.generatedGeometry)delete x.generatedGeometry.sampleGeometryRef;out.push({id,state:s,machines:await proxy.invoke('Machine.List',{})});}return out;},ids);
 const mesh=async()=>{await wait(()=>{const v=blockRepairView,a=v.areas[v.activeAreaId],s=a?.viewContent?.snapshot;return a?.viewContent?.viewportReceipt?.applied&&!a.viewApplyByRevision?.size&&v.appliedViewportSnapshotKey===`${s?.viewId}:${a.appliedViewRevision}`;});return page.evaluate(()=>{const v=blockRepairView;v.viewport.scene.updateMatrixWorld(true);return[...v.viewport.sceneObjects].filter(([id,o])=>o.geometry?.attributes.position).map(([id,o])=>({id,visible:o.visible,positions:Array.from(o.geometry.attributes.position.array),index:o.geometry.index?Array.from(o.geometry.index.array):null,world:o.matrixWorld.toArray()})).sort((a,b)=>a.id.localeCompare(b.id));});};
 const display=async state=>page.evaluate(async native=>{const v=blockRepairView,s=v.fiveSimulation,{Quaternion,Vector3}=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),frames=native.setups.find(x=>x.id===native.activeSetupId).simulationCheck.frames;
  if(s.frames.length!==frames.length)throw Error('Displayed playback count differs');
  const height=Number(native.setups.find(x=>x.id===native.activeSetupId).rapidSettings.followHeightMm);
  for(let i=0;i<frames.length;i++){const f=s.frames[i],n=frames[i],q=new Quaternion(...n.rotation);if(f.position.distanceTo(new Vector3(...n.position))>1e-6||f.rotation.angleTo(q)>1e-6)throw Error('Displayed native pose differs at '+i);if(f.target&&new Vector3(...n.position).distanceTo(f.target.clone().addScaledVector(new Vector3(0,0,1).applyQuaternion(q),height))>1e-6)throw Error('TCP does not follow the exact beam target and height at '+i);}
  const g=s.diagnosticOverlay?.visible?s.diagnosticOverlay:s.group;if(!g?.visible)throw Error('Actual cutting head must be visible');
  const cursor=s.cursor,i=Math.floor(cursor),a=frames[i],b=frames[Math.min(i+1,frames.length-1)],fraction=cursor-i,p=new Vector3(...a.position).lerp(new Vector3(...b.position),fraction),q=new Quaternion(...a.rotation).slerp(new Quaternion(...b.rotation),fraction);
  if(g.position.distanceTo(p)>1e-6||g.quaternion.angleTo(q)>1e-6)throw Error('Actual visible head differs from native pose');
  return{frameCount:frames.length,cursor,group:g.name,poseSource:g.userData?.poseSource||g.userData?.kind,actualHeadAndAllDisplayedFramesMatch:true};
 },state);
 await page.evaluate(file=>__icaxAppShell.openProject(file,'icax.five-axis-cam'),project);await bind();await area();
 const before=await read(),beforePolicies=await policies(before),beforeHistory=await history(),beforeMesh=await mesh(),initialHash=fileHash(),others=(await ids()).filter(id=>id!==target),othersBefore=await otherFacts(others),baseline=setup(before).simulationCheck;
 assert.equal(baseline.validity,'Current','Reuse saved Current report without full Check');assert.equal(baseline.status,'Collision');assert.equal(baseline.frames[Number(index)].role,'Cut');assert.equal(baseline.segments.find(s=>Number(s.index)===Number(index)).status,'Collision');
 fs.writeFileSync(path.join(output,'before-native.json'),JSON.stringify({state:before,policies:beforePolicies,history:beforeHistory,mesh:beforeMesh,others:othersBefore}));
 report.initial={frames:baseline.frames.length,collisions:baseline.collisionCount,unchecked:baseline.uncheckedCount,selectedIndex:index,otherProjectIds:others};write();
 await page.locator(`[data-five-command="five.simulation.collision-result:${index}"]`).first().click();await idle();
 await page.evaluate(async()=>{const client=blockStableProduct.sdoClient,{PDOClient}=await import('../../../iCAX-UI/SDK/PDO/pdoClient.mjs');const a=window.blockTaskAudit={client,invoke:client.invoke,PDOClient,read:PDOClient.prototype.withReadDescriptor,calls:[],frames:[],visible:[],running:true};
  client.invoke=function(channel,method,payload,...rest){const c={at:performance.now(),method,payload:structuredClone(payload)};a.calls.push(c);const p=a.invoke.call(this,channel,method,payload,...rest);p.then(r=>{c.elapsedMs=performance.now()-c.at;c.result=structuredClone(r)},e=>c.error=e.message);return p;};
  PDOClient.prototype.withReadDescriptor=function(d,reader){return a.read.call(this,d,(buffer,meta)=>{const r=reader(buffer,meta);if(r?.taskId&&r.sequence)a.frames.push({at:performance.now(),dataVersion:String(meta?.dataVersion??''),progress:structuredClone(r)});return r;});};
  const frame=()=>{if(!a.running)return;const e=document.querySelector('[data-five-repair-stage]');if(e?.getBoundingClientRect().width&&!a.visible.some(x=>x.text===e.textContent))a.visible.push({at:performance.now(),text:e.textContent});requestAnimationFrame(frame);};requestAnimationFrame(frame);
  a.restore=()=>{a.running=false;client.invoke=a.invoke;PDOClient.prototype.withReadDescriptor=a.read;};
 });
 let started=false;
 return{report,
  async start(){assert.equal(started,false);started=true;await page.locator('[data-five-simulation-field="repair-deviation"]').fill(String(maxDeviation));await page.locator('[data-five-key="collision-dialog"] [data-five-command="five.simulation.collision-repair"]').click();return this.status();},
  status(){return page.evaluate(()=>{const s=blockRepairView.fiveSimulation;return{repairing:s.repairing,task:s.repairTask?.status,progress:s.repairTask?.repairProgress,pdoApplications:blockTaskAudit.frames.length,result:s.repairResult};});},
  async finish({undoRedo=true,roundtrip=true}={}){
   await idle();const result=await page.evaluate(()=>({status:blockRepairView.fiveSimulation.repairTask?.status,result:blockRepairView.fiveSimulation.repairResult}));assert.equal(result.status,'Completed');assert.ok(result.result);
   const raw=await page.evaluate(()=>{const a=blockTaskAudit;a.restore();return{calls:a.calls,frames:a.frames,visible:a.visible};});fs.writeFileSync(path.join(output,'progress-raw.json'),JSON.stringify(raw));report.transport=taskEvidence(raw);report.result=result.result;
   const after=await read(),afterPolicies=await policies(after);same(source(after),source(before),'All cutting geometry and unrelated author inputs remain exact');
   if(!result.result.changed){
    assert.equal(result.result.status,'Unresolved');same(editable(after,afterPolicies),editable(before,beforePolicies),'Finite unresolved search changes no engineering facts');same(await history(),beforeHistory,'No unresolved history');assert.equal(fileHash(),initialHash);report.successfulRepair=false;report.finiteSearchOnly=true;report.coverageLimit='Finite candidate search returned Unresolved; this does not prove all physically possible orientations impossible.';write();return report;
   }
   assert.equal(result.result.status,'Repaired');report.policyDiff=validateBlockPolicies(beforePolicies,afterPolicies,result.result);
   const search=result.result.poseSearch,entry=Number(search.fixedEntryFrameIndex),exit=Number(search.fixedExitFrameIndex),current=setup(after).simulationCheck;assert.ok(entry<exit);assert.equal(current.frames.length,baseline.frames.length);
   const targets=s=>setup(s).generatedGeometry.operations.map(o=>({intentId:o.intentId,placementId:o.placementId,parts:o.parts.map(p=>({role:p.role,points:p.points}))}));
   same(targets(after),targets(before),'Every actual beam target remains exact; TCP can move with changed axis and follow height');
   for(let i=0;i<current.frames.length;i++)if(i<=entry||i>=exit)same(current.frames[i],baseline.frames[i],'Clear boundary and outside full poses stay exact at '+i);
   const checked=Number(result.result.localCheckedSegmentCount),reused=Number(result.result.reusedSegmentCount);assert.ok(checked>0&&checked<current.segments.length);assert.ok(reused>0);assert.equal(current.validity,'Current');assert.equal(current.uncheckedCount,baseline.uncheckedCount);assert.ok(Number(current.collisionCount)<Number(baseline.collisionCount));
   const checkedIndices=new Set(current.localCheckedSegmentIndices.map(Number));assert.equal(checkedIndices.size,checked);for(let i=entry;i<exit;i++)assert.ok(checkedIndices.has(i),'Every interval inside the control block is truly checked');
   for(const row of current.segments)if(checkedIndices.has(Number(row.index)))assert.equal(row.status,'Clear','Every checked block interval and join is Clear');
   for(const row of baseline.segments)if(!checkedIndices.has(Number(row.index)))same(current.segments.find(x=>x.index===row.index),row,'Every reused collision certificate stays exact');
   assert.equal((await history()).undoSteps.length,beforeHistory.undoSteps.length+1);same(await mesh(),beforeMesh,'Actual source meshes/world transforms remain exact');report.display=await display(after);
   const saves=raw.calls.filter(c=>c.method==='Project.AutoSave'&&c.result?.saved);assert.ok(saves.length,'Accepted edit must auto-persist');report.persistence=saves.map(c=>({persistence:c.result.persistence,elapsedMs:c.elapsedMs}));assert.ok(saves.every(c=>['Snapshot','Journal'].includes(c.result.persistence)));assert.notEqual(fileHash(),initialHash,'New pose/generation/report resources must be durably saved');
   fs.writeFileSync(path.join(output,'after-native.json'),JSON.stringify({state:after,policies:afterPolicies}));
   report.localValidation={entry,exit,checked,reused,remainingCollisions:current.collisionCount};report.successfulRepair=true;write();
   if(undoRedo)for(const[action,state,policy]of[['undo-project',before,beforePolicies],['redo-project',after,afterPolicies]]){await page.locator(`[data-action="${action}"]`).click();await idle();const value=await read();same(editable(value,await policies(value)),editable(state,policy),'Native '+action+' restores complete author policies and generated data');same(setup(value).simulationCheck,setup(state).simulationCheck,'Native '+action+' restores complete report');same(await mesh(),beforeMesh,'Actual source 3D remains exact through '+action);}
   if(!roundtrip){report.passed=true;report.finishedAt=new Date().toISOString();report.roundtripDeferred=true;write();return report;}
   const final=await read(),finalPolicies=await policies(final);await page.locator(`[data-action="close-open-project"][data-project-id="${target}"]`).click();await page.locator('[data-project-close-choice="discard"]').click();await wait(id=>!blockStableProduct.projects.has(id),target);same(await ids(),others,'Only supplied target closes');same(await otherFacts(others),othersBefore,'Other native project facts preserved');
   await page.evaluate(file=>__icaxAppShell.openProject(file,'icax.five-axis-cam'),project);await bind();await idle();const reopened=await read();same(editable(reopened,await policies(reopened)),editable(final,finalPolicies),'Automatic persistence restores complete parameters and actual generated arrays');same(setup(reopened).simulationCheck,setup(final).simulationCheck,'Saved Current report exact');await area();same(await mesh(),beforeMesh,'Reopen restores exact actual mesh buffers/world transforms');same((await ids()).filter(id=>id!==target),others);same(await otherFacts(others),othersBefore);
   report.reopenedDisplay=await display(await read());report.localValidation={entry,exit,checked,reused,remainingCollisions:current.collisionCount};report.successfulRepair=true;report.passed=true;report.finishedAt=new Date().toISOString();await page.screenshot({path:path.join(output,'final-visible.png')});write();return report;
  },
  async dispose(){await page.evaluate(()=>window.blockTaskAudit?.restore?.());}
 };
}
module.exports={create,validateBlockPolicies,taskEvidence,source,editable};
if(require.main===module){
 const args=process.argv.slice(2),opt=n=>{const i=args.indexOf(n);assert.ok(i>=0&&args[i+1]&&!args[i+1].startsWith('--'),'Explicit '+n+' required');return args[i+1];};
 (async()=>{const{chromium}=require('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'),browser=await chromium.connectOverCDP(opt('--cdp'),{noDefaults:true}),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('AppShell/index.html'));assert.ok(page);const run=await create({page,project:opt('--project'),output:opt('--output')});await run.start();const deadline=Date.now()+600000;while((await run.status()).repairing){if(Date.now()>deadline)throw Error('Repair task timeout');await new Promise(r=>setTimeout(r,500));}const report=await run.finish();await run.dispose();console.log(JSON.stringify(report,null,2));process.exit(report.passed?0:1);})().catch(e=>{console.error(e.stack);process.exit(1);});
}
