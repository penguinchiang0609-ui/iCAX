const{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright'),fs=require('node:fs'),assert=require('node:assert/strict');
const root='D:/penguinchinage0609/iCAX',out=root+'/output/tests/machine-simulation',project=out+'/六轴机械臂SDF演示.i5cam';
(async()=>{
 const reopen=process.argv.includes('--reopen');
 if(!reopen&&!fs.existsSync(project))fs.copyFileSync(out+'/工业机床碰撞验证.i5cam',project);
 const b=await chromium.connectOverCDP('http://127.0.0.1:9482'),p=b.contexts()[0].pages()[0];await p.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await p.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 await p.waitForFunction(async path=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.mi=(m,p)=>mv.viewportSceneProxy.invoke(m,p,{timeoutMs:120000});return __icaxAppShell.getState().activeProjectPath===path&&mv.scene?.machines?.length&&!mv.pending;},project);
 await p.locator('[data-tab-id="simulation"]').click();await p.locator('[data-command-id="five.tool.machine-definitions"]').click();
 if(!reopen){
  const id=await p.evaluate(()=>mv.scene.machineDefinitions.find(d=>d.name.includes('IR-1600')).id);
  await p.locator(`[data-five-command="five.machine.definition-select:${id}"]`).click();
  const existing=await p.evaluate(()=>mv.scene.machines.find(m=>m.name.includes('IR-1600'))?.entityId);
  if(!existing){await p.locator('[data-command-id="five.machine-panel.instantiate"]').click();await p.waitForFunction(()=>!mv.fiveMachine.busy&&mv.scene.machines.some(m=>m.name.includes('IR-1600')));}
  else {await p.locator('[data-command-id="five.tool.machine-instances"]').click();await p.locator('[data-five-machine-instance]').selectOption(existing);}
 }else{
  await p.locator('[data-command-id="five.tool.machine-instances"]').click();const id=await p.evaluate(()=>mv.scene.machines.find(m=>m.name.includes('IR-1600')).entityId);await p.locator('[data-five-machine-instance]').selectOption(id);
 }
 const robot=await p.evaluate(()=>mv.scene.machines.find(m=>m.name.includes('IR-1600')));
 assert.equal(robot.joints.filter(j=>j.type==='revolute').length,6);assert.ok(robot.collisions.length>=9);
 if(!reopen){await p.locator('[data-five-command="five.machine.post-save"]').click();await p.waitForFunction(()=>!mv.fiveMachine.busy);}
 const joints=await p.evaluate(async id=>{
  const m=mv.scene.machines.find(m=>m.entityId===id),rows=[];
  for(const j of m.joints.filter(j=>j.type==='revolute')){
   const args={expected:mv.scene.domain.expected,machineId:id};const moved=await mi('CamSimulation.MoveMachine',{...args,positions:{[j.entityId]:Math.PI/180}});
   const q=(await mi('Machine.GetElement',{entityId:j.entityId})).machineElement.joint.position;
   const home=await mi('CamSimulation.MoveMachine',{...args,positions:{[j.entityId]:0}});rows.push({name:j.jointName,status:moved.collisionStatus,q,home:home.collisionStatus,reason:moved.reason});
  }return rows;
 },robot.entityId);for(const j of joints){assert.equal(j.status,'Clear',j.name+': '+j.reason);assert.ok(Math.abs(j.q-Math.PI/180)<1e-10);assert.equal(j.home,'Clear');}
 await p.locator('[data-command-id="five.tool.machine-simulation"]').click();
 if(!reopen){
  await p.locator('[data-five-machine-instance]').selectOption(robot.entityId);
  await p.locator('[data-five-machine-field="supportId"]').selectOption({label:'工件装夹台'});await p.locator('[data-five-machine-field="toolId"]').selectOption({label:'J6 激光加工头'});
  for(const[key,value]of Object.entries({tcp0:280,tcp1:0,tcp2:0,beam0:1,beam1:0,beam2:0,position0:30,position1:0,position2:60,rotation1:90}))await p.locator(`[data-five-machine-field="${key}"]`).fill(String(value));
  await p.locator('[data-five-command="five.machine.apply"]').click();await p.waitForFunction(()=>!mv.fiveMachine.busy);assert.equal(await p.evaluate(()=>mv.fiveMachine.error),'');
 }
 await p.locator('[data-five-command="five.machine.build"]').click();await p.waitForFunction(()=>!mv.fiveMachine.busy,{},{timeout:120000});
 const report=await p.evaluate(()=>mv.fiveMachine.report);fs.writeFileSync(out+'/sdf-last-report.json',JSON.stringify(report,null,2));
 assert.equal(report.status,'Ready',report.reason);assert.equal(report.collisionStatus,'Clear');assert.ok(report.frames.length>50);
 const exported=await p.evaluate(destination=>mi('CamSimulation.ExportMachineProgram',{expected:mv.scene.domain.expected,setupId:mv.scene.domain.activeSetupId,modelKey:mv.fiveMachine.report.modelKey,destination}),out+'/robot-motion.json');
 const program=JSON.parse(fs.readFileSync(out+'/robot-motion.json','utf8'));assert.equal(program.instructions.length,report.frames.length);
 await p.locator('[data-five-machine-field="speed"]').selectOption('10');await p.locator('[data-five-command="five.machine.play"]').click();await p.waitForFunction(()=>mv.fiveMachine.cursor>5);await p.locator('[data-five-command="five.machine.pause"]').click();
 await p.setViewportSize({width:1600,height:1000});await p.locator('[data-five-command="five.machine.fit"]').click();await p.waitForTimeout(400);await p.screenshot({path:out+'/industrial-arm-'+(reopen?'reopened':'verified')+'.png'});
 await p.evaluate(path=>mi('Project.Save',{projectPath:path}),project);
 fs.writeFileSync(out+'/sdf-'+(reopen?'reopen':'prepare')+'-acceptance.json',JSON.stringify({robot:robot.entityId,joints,frames:report.frames.length,positionErrorMm:report.positionErrorMm,status:report.status,collision:report.collisionStatus,exported},null,2));console.log(JSON.stringify({pass:true,reopen,joints,frames:report.frames.length,exportedInstructions:exported.instructionCount}));process.exit(0);
})().catch(e=>{console.error(e);process.exit(1)});
