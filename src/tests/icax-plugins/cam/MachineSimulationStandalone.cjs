// Run --prepare, restart the isolated desktop runtime, then run --reopen.
// PLAYWRIGHT_MODULE may point to the bundled Playwright package.
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../../..').replaceAll('\\','/');
const out=root+'/output/tests/machine-simulation';
const projectPath=out+'/工业机床碰撞验证.i5cam';
const reopen=process.argv.includes('--reopen');
(async()=>{
 const browser=await chromium.connectOverCDP(process.env.MACHINE_CDP||'http://127.0.0.1:9482');
 const page=browser.contexts()[0].pages()[0];await page.setViewportSize({width:1600,height:1000});
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 if(reopen){
  assert.notEqual(await page.evaluate(()=>__icaxAppShell.getState().activeProjectPath),projectPath,'Reopen acceptance requires a fresh application process');
  await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),projectPath);
 }else await page.evaluate(p=>__icaxAppShell.createProject({projectName:'工业机床展示',projectPath:p,productId:'icax.five-axis-cam'}),projectPath);
 await page.evaluate(async()=>{
  const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');
  window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);
  window.mi=(method,payload={})=>mv.viewportSceneProxy.invoke(method,payload,{timeoutMs:120000});
 });
 await page.waitForFunction(()=>mv.scene?.domain);
 if(!reopen){
  await page.evaluate(async root=>{
   const request=s=>({commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId});
   const wait=async(t,m)=>{while(t.status==='Running'){await new Promise(r=>setTimeout(r,100));t=await mi(m,{epoch:t.epoch,taskId:t.taskId});}if(t.status!=='Completed')throw Error(JSON.stringify(t));return t;};
   let s=await mi('CamScene.ReadState');await wait(await mi('CamWorkpiece.Import',{...request(s),sourcePath:root+'/TestData/automotive-stamping/mercedes-benz-t-node/Test-support-block.step'}),'CamWorkpiece.QueryTask');
   s=await mi('CamScene.ReadState');const placement=s.placements[0],setup=s.setups.find(v=>v.id===s.activeSetupId),digest='machine-acceptance';
   const sources=await mi('CamCut.QuerySources',{expected:s.expected,setupId:setup.id,placementId:placement.id});
   const extraction=await wait(await mi('CamCut.ExtractCandidates',{...request(s),placementId:placement.id,sourceGeometry:s.workpieces[0].geometryRef,extractionRuleRef:setup.planningProfileRef,selection:{mode:'Faces',faceUses:[sources.faceUses[0]]},geometryOnly:true,representation:'SkinSurface',workpieceFamily:'SheetSurface',side:{outwardSign:1},requestedKinds:['GeneralContour','Circle','Rectangle','Slot'],draftId:crypto.randomUUID(),draftRevision:'1',draftDigest:digest}),'CamPlan.QueryStatus');
   const candidates=await mi('CamCut.QueryCandidates',{epoch:extraction.epoch,candidateSet:extraction.result.candidateSet,pageSize:'100'});
   const {cutPolicyDraft,processDraftDefaults}=await import('../../../apps/five-axis-cam/webpage/processPolicyDraft.mjs');
   const policy=await mi('CamProcess.Prepare',{...request(s),placementId:placement.id,...cutPolicyDraft(processDraftDefaults)});
   await mi('CamCut.Create',{...request(s),placementId:placement.id,candidateSet:extraction.result.candidateSet,candidateIds:candidates.candidates.filter(c=>c.confirmationEligibility==='Ready').map(c=>c.candidateId),draftDigest:digest,decisions:[],process:policy.process,pose:policy.pose});
   s=await mi('CamScene.ReadState');await wait(await mi('CamPlan.Generate',request(s)),'CamPlan.QueryStatus');
  },root);
  await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));
 }
 await page.locator('[data-tab-id="simulation"]').click();
 if(!reopen){
  const id=await page.evaluate(()=>mv.scene.machineDefinitions.find(d=>d.name.includes('LC-3015')).id);
  await page.locator(`[data-five-command="five.machine.definition-select:${id}"]`).click();
  await page.locator('[data-command-id="five.machine-panel.instantiate"]').click();
  await page.waitForFunction(()=>!mv.fiveMachine.busy&&mv.scene.machines.length===1);
  await page.locator('[data-five-command="five.machine.post-save"]').click();await page.waitForFunction(()=>!mv.fiveMachine.busy);
  await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.tool.machine-simulation'));
  await page.locator('[data-five-machine-field="supportId"]').selectOption({label:'工作台'});
  await page.locator('[data-five-machine-field="toolId"]').selectOption({label:'A 激光切割头'});
  await page.locator('[data-five-machine-field="tcp2"]').fill('-220');
  await page.locator('[data-five-machine-field="position2"]').fill('30');
  await page.locator('[data-five-command="five.machine.apply"]').click();await page.waitForFunction(()=>!mv.fiveMachine.busy);
 }
 if(reopen)await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.tool.machine-simulation'));
 const config=await page.evaluate(()=>mv.scene.domain.setups.find(s=>s.id===mv.scene.domain.activeSetupId).machineSetup);
 assert.deepEqual(config.tcp,[0,0,-220]);assert.equal(config.placement[11],reopen?150:30);
 const motion=await page.evaluate(async()=>{
  const m=mv.scene.machines[0],j=m.joints.find(j=>j.jointName==='X');
  const before=await mi('CamSimulation.ReadMachine',{setupId:mv.scene.domain.activeSetupId});
  await mi('Machine.SetJointPosition',{entityId:j.entityId,position:50});
  const moved=(await mi('Machine.List')).machines[0].joints.find(x=>x.entityId===j.entityId).position;
  let limitRejected=false;try{await mi('Machine.SetJointPosition',{entityId:j.entityId,position:99999});}catch{limitRejected=true;}
  await mi('Machine.Home',{machineEntityId:m.entityId});const after=await mi('CamSimulation.ReadMachine',{setupId:mv.scene.domain.activeSetupId});return{moved,limitRejected,homeOriginBefore:before.nodes.find(n=>n.id===j.entityId).origin,homeOriginAfter:after.nodes.find(n=>n.id===j.entityId).origin};
 });assert.equal(motion.moved,50);assert.equal(motion.limitRejected,true);assert.deepEqual(motion.homeOriginAfter,motion.homeOriginBefore);assert.equal(motion.homeOriginAfter[3],-430);
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));
 const xID=await page.evaluate(()=>mv.scene.machines[0].joints.find(j=>j.jointName==='X').entityId);
 const xInput=page.locator(`[data-five-machine-field="joint:${xID}"]`);
 await xInput.fill('75');await xInput.press('Tab');await page.waitForFunction(()=>!mv.fiveMachine.busy);
 const jogged=await page.evaluate(async id=>(await mi('Machine.List')).machines[0].joints.find(j=>j.entityId===id).position,xID);
 assert.equal(jogged,75,'Manual axis input must survive the pending panel update');motion.uiMoved= jogged;
 await page.locator('[data-five-command="five.machine.home"]').click();await page.waitForFunction(()=>!mv.fiveMachine.busy);
 const place=async(x,z)=>{await page.locator('[data-five-machine-field="position0"]').fill(String(x));await page.locator('[data-five-machine-field="position2"]').fill(String(z));await page.locator('[data-five-command="five.machine.apply"]').click();await page.waitForFunction(()=>!mv.fiveMachine.busy);};
 const build=async()=>{await page.locator('[data-five-command="five.machine.build"]').click();await page.waitForFunction(()=>!mv.fiveMachine.busy,{},{timeout:120000});return page.evaluate(()=>({status:mv.fiveMachine.report?.status,collision:mv.fiveMachine.report?.collisionStatus,reason:mv.fiveMachine.report?.reason,samples:mv.fiveMachine.report?.frames.length,positionErrorMm:mv.fiveMachine.report?.positionErrorMm,error:mv.fiveMachine.error,objects:mv.viewport.sceneObjects.size}));};
 await place(0,30);const rejected=await build();assert.equal(rejected.status,'Blocked',rejected.error);assert.match(rejected.reason,/工作台/);assert.equal(await page.locator('[data-five-command="five.machine.play"]').isDisabled(),false);
 await page.locator('[data-five-machine-field="speed"]').selectOption('10');await page.locator('[data-five-command="five.machine.play"]').click();await page.waitForFunction(()=>mv.fiveMachine.alarm&&!mv.fiveMachine.playing,{},{timeout:30000});const alarm=await page.evaluate(()=>({cursor:mv.fiveMachine.cursor,target:mv.fiveMachine.report.alarmFrame,highlighted:mv.fiveMachine.highlighted.size}));assert.equal(alarm.cursor,alarm.target);assert.ok(alarm.highlighted>=2);await page.waitForTimeout(200);assert.equal(await page.evaluate(()=>mv.fiveMachine.cursor),alarm.cursor);
 await page.screenshot({path:out+(reopen?'/collision-reopened.png':'/collision-blocked.png')});
 await page.locator('[data-five-command="five.machine.reset"]').click();assert.equal(await page.evaluate(()=>mv.fiveMachine.inspect),false);
 await place(5000,150);const unreachable=await build();assert.equal(unreachable.status,'Unreachable');assert.equal(await page.locator('[data-five-command="five.machine.play"]').isDisabled(),false);
 await page.locator('[data-five-command="five.machine.play"]').click();await page.waitForFunction(()=>mv.fiveMachine.alarm&&!mv.fiveMachine.playing,{},{timeout:30000});assert.equal(await page.evaluate(()=>mv.fiveMachine.cursor),await page.evaluate(()=>mv.fiveMachine.report.alarmFrame));
 await place(500,150);
 if(!reopen){
  // Two real 30 x 30 x 60 mm imported support blocks fill the 120 mm
  // beneath the centered workpiece; both remain enabled collision obstacles.
  await page.evaluate(async root=>{
   const state=()=>mi('CamScene.ReadState'),request=s=>({commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId});
   const wait=async t=>{while(t.status==='Running'){await new Promise(r=>setTimeout(r,100));t=await mi('CamFixture.QueryTask',{epoch:t.epoch,taskId:t.taskId});}if(t.status!=='Completed')throw Error(JSON.stringify(t));return t;};
   for(const [i,z] of [-150,-90].entries()){let s=await state();const t=await wait(await mi('CamFixture.Import',{...request(s),sourcePath:root+'/TestData/automotive-stamping/mercedes-benz-t-node/Test-support-block.step'}));s=await state();await mi('CamFixture.Update',{...request(s),fixtureId:t.result.fixtureId,name:'支撑块 '+(i+1),transform:[1,0,0,-15,0,1,0,-15,0,0,1,z,0,0,0,1]});}
   const s=await state();let t=await mi('CamPlan.Generate',request(s));while(t.status==='Running'){await new Promise(r=>setTimeout(r,100));t=await mi('CamPlan.QueryStatus',{epoch:t.epoch,taskId:t.taskId});}if(t.status!=='Completed')throw Error(JSON.stringify(t));
  },root);await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));
 }
 const report=await build();assert.equal(report.status,'Ready',report.reason||report.error);assert.equal(report.collision,'Clear');assert.equal(report.samples,95);assert.equal(report.objects,13);assert.ok(report.positionErrorMm<=.01);
 const guarded=await page.evaluate(async()=>{const z=mv.scene.machines[0].joints.find(j=>j.jointName==='Z');const result=await mi('CamSimulation.CheckMachineMove',{expected:mv.scene.domain.expected,setupId:mv.scene.domain.activeSetupId,positions:{[z.entityId]:-450}});return {status:result.status,reason:result.reason};});assert.equal(guarded.status,'Blocked');
 const exported=await page.evaluate(async destination=>mi('CamSimulation.ExportMachineProgram',{expected:mv.scene.domain.expected,setupId:mv.scene.domain.activeSetupId,modelKey:mv.fiveMachine.report.modelKey,destination}),out+'/industrial-motion.json');assert.equal(exported.instructionCount,94);const jointProgram=JSON.parse(fs.readFileSync(out+'/industrial-motion.json','utf8'));assert.equal(jointProgram.instructions.length,95);assert.equal(jointProgram.machineReady,false);assert.deepEqual(jointProgram.instructions[0].joints,jointProgram.initialJoints);assert.equal(jointProgram.instructions[1].process,false);assert.ok(jointProgram.instructions.some(x=>x.process));
 await page.locator('[data-five-command="five.machine.play"]').click();await page.waitForFunction(()=>mv.fiveMachine.cursor>5);
 await page.locator('[data-five-command="five.machine.pause"]').click();
 const cursor=await page.evaluate(()=>mv.fiveMachine.cursor);await page.waitForTimeout(100);assert.equal(await page.evaluate(()=>mv.fiveMachine.cursor),cursor);
 await page.locator('[data-tab-id="workpiece"]').click();
 await page.waitForFunction(()=>mv.viewport.sceneObjects.size===3);
 const restored=await page.evaluate(()=>{const p=mv.scene.domain.placements[0],object=mv.viewport.sceneObjects.get(p.id);return{expected:p.transform,actual:[0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15].map(i=>object.matrix.elements[i])};});
 restored.actual.forEach((value,i)=>assert.ok(Math.abs(value-restored.expected[i])<1e-8,'Leaving simulation must restore the CAD placement'));
 await page.locator('[data-tab-id="simulation"]').click();
 await page.waitForFunction(()=>mv.viewport.sceneObjects.size===13);
 await page.setViewportSize({width:1600,height:800});
 // Real SDO data, delayed delivery only: continue scrolling while the response is in flight.
 const interaction=await page.evaluate(async()=>{
  const field=document.querySelector('[data-five-machine-field="tcp2"]'),canvas=mv.viewport.renderer.domElement;
  const left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane');
  let events=0;field.addEventListener('machine-test',()=>events++);field.focus({preventScroll:true});field.setSelectionRange(1,3);
  left.scrollTop=40;right.scrollTop=100;
  const original=mv.viewportSceneProxy.invoke;
  mv.viewportSceneProxy.invoke=async function(...args){const value=await original.apply(this,args);if(args[0]==='CamScene.ReadState')await new Promise(r=>setTimeout(r,250));return value;};
  try{
   const refresh=__icaxAppShell.executeRibbonCommand('five.refresh');await new Promise(r=>setTimeout(r,80));left.scrollTop=90;right.scrollTop=180;
   const expected=[left.scrollTop,right.scrollTop];await refresh;field.dispatchEvent(new Event('machine-test'));
   return{sameField:field===document.querySelector('[data-five-machine-field="tcp2"]'),sameCanvas:canvas===mv.viewport.renderer.domElement,focused:document.activeElement===field,selection:[field.selectionStart,field.selectionEnd],scroll:[left.scrollTop,right.scrollTop],expected,events};
  }finally{mv.viewportSceneProxy.invoke=original;}
 });console.log('interaction',interaction);assert.equal(interaction.sameField,true);assert.equal(interaction.sameCanvas,true);assert.equal(interaction.focused,true);assert.deepEqual(interaction.selection,[1,3]);assert.deepEqual(interaction.scroll,interaction.expected);assert.ok(interaction.expected[1]>0);assert.equal(interaction.events,1);
 await page.setViewportSize({width:1600,height:1000});
 await page.locator('[data-five-command="five.machine.fit"]').click();
 await page.screenshot({path:out+(reopen?'/industrial-reopened.png':'/industrial-verified.png')});
 const saved=await page.evaluate(p=>mi('Project.Save',{projectPath:p}),projectPath);assert.equal(saved.saved,true);
 fs.writeFileSync(out+(reopen?'/reopen-acceptance.json':'/prepare-acceptance.json'),JSON.stringify({stage:reopen?'fresh-process-reopen':'prepare',report,rejected,unreachable,guarded,motion,interaction,alarm,exportedInstructions:exported.instructionCount,saved:true},null,2));
 console.log(JSON.stringify({stage:reopen?'reopen':'prepare',report,rejected,unreachable,guarded,motion,interaction,alarm,exportedInstructions:exported.instructionCount,saved:true},null,2));process.exit(0);
})().catch(error=>{console.error(error);process.exit(1);});
